#!/usr/bin/env python3
"""Needle-valve characterization with an MKS SERVO42C as the instrument.

The driver runs closed-loop with its torque limit (MaxT, 0..1200) used as
the "current" knob; the position error (target - actual) is the skip/stall
signal, and the supply current comes from the Siglent SPD3303X-E.

Subcommands (each appends to one JSONL run file in data/):
  home     open a little if at the seat, then find the seat at low torque
  travel   open at low torque until the open end is found (sets the window)
  survey   torque x speed grid across the window, both directions
  stick    closing-torque -> opening-torque-needed sweep at the seat, with
           approach-speed and ramp-down variants
  all      home, travel, survey, stick

Safety: every stall is handled with release + re-enable; the valve is only
ever driven into the seat in the stick test; if it cannot be opened at full
torque the run aborts with the motor disabled so it can be freed by hand.
"""
from __future__ import annotations

import argparse
import json
import sys
import threading
import time
from pathlib import Path

from mks42c import COUNTS_PER_REV, MAXT_MAX, Servo42C, gear_for_rpm, rpm_for_gear
from spd3303x import Spd3303x

DATA_DIR = Path(__file__).parent / "data"
STATE_FILE = DATA_DIR / "state.json"      # seat / open-end references between runs
MSTEP = 32
LOW_MAXT = 500
STALL_ERR_DEG = 8.0
SEAT_MARGIN_DEG = 12.0                    # never command closing below seat + this, except in stick/home
WINDOW_LO_DEG = 60.0                      # survey never closes below this (room for one encoder update at speed)
WINDOW_HI_CAP_DEG = 330.0                 # ...nor opens above this: a driver zero-return (NearMode) from here lands mid-window
SILENT_SAMPLES_ABORT = 6                  # consecutive unanswered polls => driver rebooting/hung => stop, disable, abort
ZERO_DEG_DEFAULT = 229.9                  # the driver's stored zero angle, in deg open (data/state.json: zero_deg)
# The 0xA5 (MaxT) command makes this driver lunge to its zero angle about 1 time in 27 (measured: 11/300,
# none in 480 sends of F3/F6/F7/reads). So MaxT is only ever changed while parked AT the zero angle, where the
# lunge is a zero-length move, or while stuck at the seat, where a lunge can only open the valve.
ENC_UPDATE_S = 0.06                       # the driver refreshes its encoder reply about every 60 ms


def deg(counts: float) -> float:
    return counts * 360.0 / COUNTS_PER_REV


def counts(degrees: float) -> int:
    return int(degrees / 360.0 * COUNTS_PER_REV)


class Abort(Exception):
    pass


class Rig:
    def __init__(self, port: str, psu_host: str | None, run_name: str):
        DATA_DIR.mkdir(exist_ok=True)
        self.drv = Servo42C(port)
        self.psu = Spd3303x(psu_host) if psu_host else None
        self.log_path = DATA_DIR / f"{time.strftime('%Y%m%d-%H%M%S')}_{run_name}.jsonl"
        self.log = self.log_path.open("w")
        self.state = json.loads(STATE_FILE.read_text()) if STATE_FILE.exists() else {}
        self.move_id = 0
        self.ctx: dict = {}
        self._stop = threading.Event()
        self._psu_thread: threading.Thread | None = None
        self.last_psu: tuple[float | None, float | None] = (None, None)
        self.maxt = None

    # --- bookkeeping ------------------------------------------------------
    def record(self, kind: str, **fields) -> None:
        rec = {"type": kind, "t": time.time(), **fields}
        self.log.write(json.dumps(rec) + "\n")
        self.log.flush()

    def event(self, msg: str, **fields) -> None:
        print(f"  [{time.strftime('%H:%M:%S')}] {msg}", flush=True)
        self.record("event", msg=msg, **fields)

    def save_state(self) -> None:
        STATE_FILE.write_text(json.dumps(self.state, indent=2))

    def deg_open(self, enc: int | None) -> float | None:
        seat = self.state.get("seat_enc")
        return None if enc is None or seat is None else deg(enc - seat)

    # --- PSU sampling thread ---------------------------------------------
    def start_psu(self) -> None:
        if not self.psu:
            return
        self.event("psu " + self.psu.identity())
        def loop():
            n = 0
            v = None
            while not self._stop.is_set():
                try:
                    if n % 8 == 0:
                        v = self.psu.measure_voltage()
                    i = self.psu.measure_current()
                    self.last_psu = (v, i)
                    self.record("psu", v=v, i=i, move_id=self.move_id, **self.ctx)
                except Exception as exc:  # keep sampling through hiccups
                    self.record("psu_error", error=str(exc))
                    time.sleep(0.5)
                n += 1
        self._psu_thread = threading.Thread(target=loop, daemon=True)
        self._psu_thread.start()

    # --- driver helpers ---------------------------------------------------
    @property
    def zero_deg(self) -> float:
        return float(self.state.get("zero_deg") or ZERO_DEG_DEFAULT)

    def set_maxt(self, value: int, in_place: bool = False) -> None:
        """Change the torque limit. Unless `in_place`, first travel to the zero
        angle so the A5-triggered lunge cannot move anything. Never re-sent when
        unchanged (every send is a lunge lottery)."""
        if value == self.maxt:
            return
        d = self.drv
        here = self.deg_open(d.encoder())
        if not in_place and "seat_enc" in self.state and here is not None and abs(here - self.zero_deg) > 5.0:
            self.move(closing=self.zero_deg < here, rpm=20.0, maxt=None, target_enc=self.state["seat_enc"] + counts(self.zero_deg),
                      label="park at zero for MaxT", ramp_deg=15.0)
        e0 = d.encoder()
        if not d.set_maxt(value):
            raise Abort(f"MaxT {value} not acknowledged")
        self.record("maxt", maxt=value, at_deg_open=self.deg_open(e0), in_place=in_place)
        self.maxt = value
        t0 = time.time(); moved = 0.0
        while time.time() - t0 < 0.4:                 # a lunge shows within ~100 ms
            e = d.encoder()
            if e is not None and e0 is not None:
                moved = deg(e - e0)
                if abs(moved) > 3.0:
                    d.stop(); d.wait_enabled(False)
                    self.event(f"lunge after MaxT {value} at {self.deg_open(e0)} deg open: {moved:+.1f} deg", lunge_after_maxt=moved)
                    self.recover_from_lunge(f"MaxT {value}")
                    break
            time.sleep(0.01)

    def sample(self, **extra):
        s = self.drv.sample() if extra.get("full") else None
        enc = self.drv.encoder(); err = self.drv.error(); blk = self.drv.blocked()
        rec = {"enc": enc, "deg_open": self.deg_open(enc), "err_deg": None if err is None else deg(err),
               "blocked": blk, "move_id": self.move_id, "psu_v": self.last_psu[0], "psu_i": self.last_psu[1], **self.ctx}
        self.record("sample", **rec)
        return enc, (None if err is None else deg(err)), blk

    def setup(self) -> None:
        d = self.drv
        d.set_work_mode(2); time.sleep(0.2)
        d.set_mstep(MSTEP); d.set_protect(True)
        # Zero-return mode OFF (RAM only; the OLED menu value is what survives a reboot).
        if not d.cmd(0x90, b"\x00"):
            raise Abort("could not switch the zero-return mode off")
        d.rearm()
        # The driver's limit is whatever the last run left (always <= LOW_MAXT); travel to the zero angle on it, then set ours.
        self.maxt = None
        if "seat_enc" in self.state:
            here = self.deg_open(d.encoder())
            if here is not None and abs(here - self.zero_deg) > 5.0:
                self.move(closing=self.zero_deg < here, rpm=20.0, maxt=None, target_enc=self.state["seat_enc"] + counts(self.zero_deg),
                          label="park at zero (setup)", ramp_deg=15.0)
        self.set_maxt(LOW_MAXT, in_place=True)
        enc, err, blk = self.sample()
        self.event(f"setup: enc {enc}, {self.deg_open(enc)} deg open, err {err}, blocked {blk}")

    # --- core motion ------------------------------------------------------
    def move(self, *, closing: bool, rpm: float, maxt: int | None = None, target_enc: int | None = None,
             max_travel_deg: float | None = None, timeout: float = 60.0, ramp_deg: float = 0.0,
             stall_err_deg: float = STALL_ERR_DEG, label: str = "", probe_after_stall: bool = True, _retry: bool = False) -> dict:
        """Run at `rpm` (closing or opening) until target/travel/stall/timeout.
        Returns a summary dict. Stops the motor before returning; on stall the
        driver is rearmed (release + enable)."""
        d = self.drv
        if maxt is not None and maxt != self.maxt:
            self.set_maxt(maxt)
        maxt = self.maxt if self.maxt is not None else -1      # -1: driver's current limit, unknown (setup only)
        self.move_id += 1
        gear = gear_for_rpm(rpm, d.mstep)
        rpm_actual = rpm_for_gear(gear, d.mstep)
        self.ctx = {"label": label, "closing": closing, "gear": gear, "rpm": rpm_actual, "maxt": maxt}
        start_enc = d.encoder()
        if start_enc is None:
            raise Abort("no encoder reading")
        if label != "post-stall probe":
            self.precheck(label)
        last = self.state.get("_last_end_enc")
        if last is not None and abs(deg(start_enc - last)) > 5.0:
            self.event(f"shaft moved {deg(start_enc - last):+.1f} deg between moves (uncommanded)", uncommanded_deg=deg(start_enc - last))
        if target_enc is not None:
            max_travel_deg = abs(deg(target_enc - start_enc))
        sign = -1 if closing else 1
        # Stop lead: distance covered between the last poll and the motor actually stopping.
        lead_deg = rpm_actual * 6.0 * 0.06
        self.record("move_start", move_id=self.move_id, start_enc=start_enc, start_deg_open=self.deg_open(start_enc),
                    target_enc=target_enc, max_travel_deg=max_travel_deg, ramp_deg=ramp_deg, **self.ctx)
        if not d.run(closing, gear):
            d.stop(); d.enable(False)
            raise Abort(f"run command not acknowledged ({label}); motor disabled")
        t0 = time.time(); peak_err = 0.0; last_enc = start_enc; last_move_t = t0; reason = "target"
        cur_gear = gear; samples = 0; peak_rpm = 0.0; silent = 0
        # Overspeed guard: the shaft must never move much faster than commanded.
        # (Seen once: ~100 rpm on an 18.8 rpm command after a stall + rearm.)
        rpm_limit = max(2.2 * rpm_actual, rpm_actual + 15.0, 20.0)
        while True:
            enc, err_deg, blk = self.sample()
            samples += 1
            now = time.time()
            if enc is None and err_deg is None and blk is None:
                silent += 1
                if silent >= SILENT_SAMPLES_ABORT:
                    for _ in range(20):                       # keep trying until it listens
                        if d.stop() and d.enable(False):
                            break
                        time.sleep(0.05)
                    self.event(f"DRIVER SILENT for {silent} polls during {label} (reboot?); stop+disable sent", silent=silent)
                    raise Abort(f"driver went silent during {label}")
                continue
            silent = 0
            if err_deg is not None:
                peak_err = max(peak_err, abs(err_deg))
            if blk:
                reason = "blocked"; break
            if err_deg is not None and abs(err_deg) > stall_err_deg:
                reason = "stall"; break
            if enc is not None:
                travelled = sign * (enc - start_enc)
                if abs(enc - last_enc) > 60:
                    dt = max(now - last_move_t, ENC_UPDATE_S * 0.5)
                    obs_rpm = abs(deg(enc - last_enc)) / dt / 6.0
                    peak_rpm = max(peak_rpm, obs_rpm)
                    if obs_rpm > rpm_limit:
                        d.stop(); d.wait_enabled(False)
                        self.record("overspeed", move_id=self.move_id, obs_rpm=obs_rpm, rpm_limit=rpm_limit, enc=enc, **self.ctx)
                        self.event(f"OVERSPEED: {obs_rpm:.0f} rpm observed on a {rpm_actual:.1f} rpm command ({label}); stopped and disabled",
                                   obs_rpm=obs_rpm)
                        self.ctx = {}
                        self.record("move_end", move_id=self.move_id, reason="overspeed", elapsed=now - t0, samples=samples,
                                    start_enc=start_enc, end_enc=enc, travel_deg=None, end_deg_open=self.deg_open(enc),
                                    peak_err_deg=peak_err, end_err_deg=None, blocked=False, peak_rpm=obs_rpm, label=label,
                                    closing=closing, gear=gear, rpm=rpm_actual, maxt=maxt)
                        if _retry:
                            raise Abort(f"overspeed twice on {label}: {obs_rpm:.0f} rpm vs {rpm_actual:.1f} commanded")
                        # The driver's zero-return heads for a mid-window angle; let it stop, re-sync, and retry once.
                        self.recover_from_lunge(label)
                        return self.move(closing=closing, rpm=rpm, maxt=maxt, target_enc=target_enc, max_travel_deg=max_travel_deg,
                                         timeout=timeout, ramp_deg=ramp_deg, stall_err_deg=stall_err_deg, label=label,
                                         probe_after_stall=probe_after_stall, _retry=True)
                    last_enc, last_move_t = enc, now
                elif now - last_move_t > 0.8 and now - t0 > 0.5:
                    reason = "no_motion"; break
                if max_travel_deg is not None:
                    remaining = max_travel_deg - deg(travelled)
                    if ramp_deg > 0 and remaining < ramp_deg and cur_gear > 1:
                        # Step the speed down through the ramp zone.
                        frac = max(0.0, remaining / ramp_deg)
                        g = max(1, int(round(gear * frac)))
                        if g < cur_gear:
                            cur_gear = g; d.run(closing, g)
                            self.ctx["gear"] = g; self.ctx["rpm"] = rpm_for_gear(g, d.mstep)
                    if remaining <= (lead_deg if cur_gear == gear else rpm_for_gear(cur_gear, d.mstep) * 6.0 * 0.06):
                        reason = "target"; break
            if now - t0 > timeout:
                reason = "timeout"; break
        if not d.stop_verified():
            d.enable(False)
            raise Abort(f"shaft still moving after stop ({label}); motor disabled")
        end_enc = d.encoder(); end_err = d.error(); end_blk = d.blocked()
        stalled = reason in ("blocked", "stall", "no_motion") or bool(end_blk)
        if stalled:
            d.rearm()
            err_after = d.error()
            if err_after is not None and abs(deg(err_after)) > 1.0:
                # Residual error at a low torque limit is normal (stiction); re-sync the
                # target to the shaft with a verified disable/enable cycle.
                self.event(f"error {deg(err_after):+.1f} deg after rearm; re-syncing", err_after_rearm_deg=deg(err_after))
                d.wait_enabled(False); time.sleep(0.1); d.wait_enabled(True); time.sleep(0.1)
                self.record("resync", move_id=self.move_id, err_before=deg(err_after), err_after=deg(d.error() or 0))
        self.state["_last_end_enc"] = end_enc
        summary = {"move_id": self.move_id, "reason": reason, "elapsed": time.time() - t0, "samples": samples,
                   "start_enc": start_enc, "end_enc": end_enc, "travel_deg": None if end_enc is None else deg(sign * (end_enc - start_enc)),
                   "end_deg_open": self.deg_open(end_enc), "peak_err_deg": peak_err,
                   "end_err_deg": None if end_err is None else deg(end_err), "blocked": end_blk, "peak_rpm": peak_rpm,
                   "bad_replies": d.bad_replies, "retried_cmds": d.retried_cmds, "failed_cmds": d.failed_cmds, **self.ctx}
        self.record("move_end", **summary)
        print(f"  move {self.move_id:>3} {label:<28} {'close' if closing else 'open '} {rpm_actual:6.1f} rpm MaxT {maxt:>4}: "
              f"{reason:<9} {summary['travel_deg'] or 0:7.1f} deg in {summary['elapsed']:5.2f} s, peak err {peak_err:5.2f} deg"
              + (f", I {self.last_psu[1]} A" if self.last_psu[1] is not None else ""), flush=True)
        self.ctx = {}
        if stalled and probe_after_stall and label != "post-stall probe":
            self.post_stall_probe(end_enc)
        return summary

    def recover_from_lunge(self, label: str) -> None:
        """After an uncommanded fast move: wait until the shaft is still with the
        motor disabled, then re-enable with the target re-synced to the shaft."""
        d = self.drv
        t0 = time.time(); last = d.encoder()
        while time.time() - t0 < 3.0:
            time.sleep(0.15)
            e = d.encoder()
            if e is not None and last is not None and abs(e - last) < 40:
                break
            last = e
        d.wait_enabled(True); time.sleep(0.2)
        err = d.error()
        self.event(f"recovered after lunge during {label}: enc {d.encoder()} ({self.deg_open(d.encoder())} deg open), err {None if err is None else deg(err):+.2f} deg",
                   recovered=True)
        self.state["_last_end_enc"] = d.encoder()

    def precheck(self, label: str) -> None:
        """Before any commanded move: shaft still, enabled, not blocked, target on the shaft."""
        d = self.drv
        e1 = d.encoder(); time.sleep(0.12); e2 = d.encoder()
        if e1 is not None and e2 is not None and abs(e2 - e1) > 60:
            self.event(f"shaft moving while idle before {label} ({deg(e2 - e1):+.1f} deg in 0.12 s); stopping", idle_motion_deg=deg(e2 - e1))
            d.stop(); d.wait_enabled(False)
            self.recover_from_lunge(label)
        if d.blocked():
            d.rearm()
        if d.enabled() is not True:
            if not d.wait_enabled(True):
                raise Abort(f"driver will not enable before {label}")
        err = d.error()
        if err is not None and abs(deg(err)) > 1.5:
            d.wait_enabled(False); time.sleep(0.1); d.wait_enabled(True); time.sleep(0.1)
            err2 = d.error()
            self.record("resync", label=label, err_before=deg(err), err_after=deg(err2 or 0))
            if err2 is not None and abs(deg(err2)) > 1.5:
                d.enable(False)
                raise Abort(f"driver target {deg(err2):+.1f} deg off the shaft before {label}; motor disabled")

    def post_stall_probe(self, enc: int | None) -> None:
        """After a stall: one slow, short, guarded move AWAY from the nearer end
        before anything else is commanded. If the driver is in the state that
        once produced a ~100 rpm lunge, the guard catches it here, with margin."""
        if enc is None or "seat_enc" not in self.state:
            return
        pos = self.deg_open(enc) or 0.0
        end = self.state.get("open_end_deg") or self.state.get("probed_open_deg") or 400.0
        closing = (end - pos) < pos          # nearer the open end -> probe closing
        self.move(closing=closing, rpm=5.0, max_travel_deg=6.0, timeout=2.0, label="post-stall probe")

    def goto(self, deg_open: float, rpm: float = 20.0, maxt: int | None = None, label: str = "goto", ramp_deg: float = 15.0) -> dict:
        seat = self.state["seat_enc"]
        target = seat + counts(deg_open)
        here = self.drv.encoder()
        if here is None:
            raise Abort("no encoder reading")
        if abs(deg(target - here)) < 1.0:
            return {"reason": "already", "end_deg_open": self.deg_open(here)}
        closing = target < here
        if closing and deg_open < SEAT_MARGIN_DEG:
            raise Abort(f"refusing to close to {deg_open} deg (margin {SEAT_MARGIN_DEG})")
        return self.move(closing=closing, rpm=rpm, maxt=maxt, target_enc=target, label=label, ramp_deg=ramp_deg)

    def unstick(self, torques: list[int], label: str) -> int | None:
        """After a stall at the seat: try opening at the current torque first (no
        MaxT command), then at ascending torque set in place (a lunge here can
        only open the valve). Returns the first torque that moved it >= 2 deg."""
        ladder = [self.maxt] + [t for t in torques if t > (self.maxt or 0)]
        for t in ladder:
            self.drv.rearm()
            if t != self.maxt:
                self.set_maxt(t, in_place=True)
            s = self.move(closing=False, rpm=5.0, max_travel_deg=6.0, timeout=1.5, label=f"{label} unstick@{t}", probe_after_stall=False)
            if (s["travel_deg"] or 0) >= 2.0:
                self.event(f"freed at opening MaxT {t}", unstick_maxt=t, label=label)
                return t
        return None

    def shutdown(self, disable: bool = True) -> None:
        """Always leave the driver stopped, at low torque and DISABLED. A run once
        ended with the motor enabled at MaxT 1200 and the driver later drove the
        valve into the seat on its own; a disabled motor cannot do that."""
        self._stop.set()
        try:
            still = self.drv.stop_verified(timeout=1.5)
            here = self.deg_open(self.drv.encoder())
            if (self.maxt or 0) > LOW_MAXT and here is not None and abs(here - self.zero_deg) <= 5.0:
                self.set_maxt(LOW_MAXT, in_place=True)
            elif (self.maxt or 0) > LOW_MAXT:
                self.event(f"NOTE: driver left at MaxT {self.maxt} (not at the zero angle, so MaxT was not changed)")
            self.drv.set_protect(True)
            off = self.drv.wait_enabled(False, timeout=1.5)
            if not off:
                self.drv.enable(False)
            self.event(f"shutdown: motor {'disabled' if off else 'DISABLE NOT CONFIRMED'}, shaft {'still' if still else 'STILL MOVING'}, "
                       f"MaxT {self.maxt}, enc {self.drv.encoder()}, enabled {self.drv.enabled()}, "
                       f"link: {self.drv.bad_replies} bad replies, {self.drv.retried_cmds} retried, {self.drv.failed_cmds} failed cmds")
        finally:
            self.drv.close()
            if self.psu:
                self.psu.close()
            self.log.close()


# --- tests ---------------------------------------------------------------------

def test_home(rig: Rig) -> None:
    rig.event("home: find the seat at low torque")
    d = rig.drv
    d.rearm()
    # If we might be at the seat, open a little first (opening is always safe).
    pre = rig.move(closing=False, rpm=5.0, maxt=LOW_MAXT, max_travel_deg=20.0, timeout=8.0, label="home pre-open")
    if pre["reason"] != "target" and (pre["travel_deg"] or 0) < 5.0:
        raise Abort("valve will not open at low torque: it is stuck at the seat (run `free`, or free it by hand)")
    s = rig.move(closing=True, rpm=5.0, maxt=LOW_MAXT, max_travel_deg=720.0, timeout=120.0, label="home approach", probe_after_stall=False)
    if s["reason"] not in ("stall", "blocked", "no_motion"):
        raise Abort(f"home: no seat found ({s['reason']})")
    rig.state["seat_enc"] = s["end_enc"]
    rig.state["seat_found_at"] = time.time()
    rig.save_state()
    rig.event(f"seat at enc {s['end_enc']}", seat_enc=s["end_enc"])
    rig.move(closing=False, rpm=5.0, maxt=LOW_MAXT, max_travel_deg=15.0, timeout=8.0, label="home back-off")


def test_travel(rig: Rig, max_deg: float) -> None:
    rig.event(f"travel: open at low torque until the open end (max {max_deg} deg)")
    s = rig.move(closing=False, rpm=5.0, maxt=LOW_MAXT, max_travel_deg=max_deg, timeout=240.0, label="travel probe", probe_after_stall=False)
    if s["reason"] in ("stall", "blocked", "no_motion"):
        rig.state["open_end_deg"] = s["end_deg_open"]
        rig.event(f"open end at {s['end_deg_open']:.1f} deg open", open_end_deg=s["end_deg_open"])
        rig.move(closing=True, rpm=5.0, maxt=LOW_MAXT, max_travel_deg=15.0, timeout=8.0, label="travel back-off")
    else:
        rig.state["open_end_deg"] = None
        rig.state["probed_open_deg"] = s["end_deg_open"]
        rig.event(f"no open end within {s['end_deg_open']:.1f} deg", probed_open_deg=s["end_deg_open"])
    rig.save_state()


def test_free(rig: Rig, ladder: list[int]) -> None:
    """Valve stuck at the seat: open with ascending torque until it moves, then
    re-home at low torque."""
    rig.event(f"free: opening torque ladder {ladder}")
    needed = rig.unstick(ladder, label="free")
    rig.record("stick_result", closing_maxt=None, approach_rpm=None, ramp=None, unstick_maxt=needed, note="stuck on arrival")
    if needed is None:
        raise Abort("could not free the valve even at full torque")
    rig.move(closing=False, rpm=5.0, maxt=LOW_MAXT, max_travel_deg=15.0, timeout=8.0, label="free back-off")
    test_home(rig)


def window(rig: Rig) -> tuple[float, float]:
    lo = WINDOW_LO_DEG
    end = rig.state.get("open_end_deg") or rig.state.get("probed_open_deg") or 200.0
    return lo, max(lo + 60.0, min(end - 30.0, WINDOW_HI_CAP_DEG))


def test_survey(rig: Rig, torques: list[int], rpms: list[float]) -> None:
    lo, hi = window(rig)
    rig.event(f"survey: window {lo:.0f}..{hi:.0f} deg open, torques {torques}, rpms {rpms}")
    for maxt in torques:
        rig.set_maxt(maxt)                       # travels to the zero angle first
        rig.goto(lo, label="survey start")
        stalls = 0
        for rpm in rpms:
            if stalls >= 2:
                rig.event(f"skip rpm {rpm} at MaxT {maxt}: two stalls already")
                continue
            pos = rig.deg_open(rig.drv.encoder()) or lo
            if abs(pos - lo) > 10:
                rig.goto(lo, label="survey reposition")
            so = rig.move(closing=False, rpm=rpm, maxt=maxt, target_enc=rig.state["seat_enc"] + counts(hi), label=f"survey T{maxt} R{rpm}")
            if so["reason"] in ("stall", "blocked", "no_motion"):
                stalls += 1
                continue
            sc = rig.move(closing=True, rpm=rpm, maxt=maxt, target_enc=rig.state["seat_enc"] + counts(lo), label=f"survey T{maxt} R{rpm}")
            if sc["reason"] in ("stall", "blocked", "no_motion"):
                stalls += 1
    rig.goto(rig.zero_deg, label="survey end (park at zero)")
    rig.set_maxt(LOW_MAXT)


def test_stick(rig: Rig, closing_torques: list[int], approach_rpms: list[float], stop_at_unstick: int) -> None:
    rig.event(f"stick: closing torques {closing_torques}, stop when unstick needs >= {stop_at_unstick}")
    unstick_ladder = [300, 400, 500, 600, 700, 800, 900, 1000, 1100, 1200]
    results = []
    for tc in closing_torques:
        rig.set_maxt(tc)                         # at the zero angle
        rig.goto(WINDOW_LO_DEG, label="stick reposition")
        s = rig.move(closing=True, rpm=5.0, max_travel_deg=WINDOW_LO_DEG + 30.0, timeout=40.0, label=f"stick close@{tc}", probe_after_stall=False)
        if s["reason"] not in ("stall", "blocked", "no_motion"):
            rig.event(f"no seat reached at MaxT {tc}?", closing_maxt=tc); continue
        seat_err = (s["end_enc"] or 0) - rig.state["seat_enc"]
        needed = rig.unstick(unstick_ladder, label=f"close@{tc}")
        results.append({"closing_maxt": tc, "approach_rpm": 5.0, "ramp": False, "unstick_maxt": needed, "seat_offset_deg": deg(seat_err)})
        rig.record("stick_result", **results[-1])
        if needed is None:
            raise Abort(f"valve stuck after closing at MaxT {tc}: could not open even at {MAXT_MAX}")
        if needed > tc + 100:
            rig.event(f"stopping torque sweep: closing at {tc} already needed {needed} to open", closing_maxt=tc, unstick_maxt=needed)
            break
        if needed >= stop_at_unstick:
            rig.event(f"stopping torque sweep: closing at {tc} needed {needed} to open", closing_maxt=tc, unstick_maxt=needed)
            break
    # Approach-speed effect at a moderate torque, with and without a ramp-down.
    safe = [r["closing_maxt"] for r in results if r["unstick_maxt"] is not None and r["unstick_maxt"] <= 600]
    tc = max(safe) if safe else closing_torques[0]
    rig.set_maxt(tc)
    for rpm in approach_rpms:
        for ramp in (False, True):
            if rpm <= 5.0 and ramp:
                continue
            if rig.maxt != tc:
                rig.set_maxt(tc)
            rig.goto(WINDOW_LO_DEG + 30.0, label="stick reposition")
            s = rig.move(closing=True, rpm=rpm, max_travel_deg=WINDOW_LO_DEG + 60.0, timeout=30.0, ramp_deg=25.0 if ramp else 0.0,
                         label=f"stick close@{tc} {rpm}rpm{' ramp' if ramp else ''}", probe_after_stall=False)
            if s["reason"] not in ("stall", "blocked", "no_motion"):
                rig.event("no seat reached", closing_maxt=tc, rpm=rpm); continue
            needed = rig.unstick(unstick_ladder, label=f"close@{tc} {rpm}rpm{' ramp' if ramp else ''}")
            r = {"closing_maxt": tc, "approach_rpm": rpm, "ramp": ramp, "unstick_maxt": needed,
                 "seat_offset_deg": deg((s["end_enc"] or 0) - rig.state["seat_enc"])}
            rig.record("stick_result", **r); results.append(r)
            if needed is None:
                raise Abort(f"valve stuck after closing at {rpm} rpm, MaxT {tc}")
    rig.goto(rig.zero_deg, label="stick end (park at zero)")
    rig.set_maxt(LOW_MAXT)
    print("\n  closing MaxT  rpm   ramp  -> opening MaxT needed   seat offset")
    for r in results:
        print(f"  {r['closing_maxt']:>11} {r['approach_rpm']:5.1f}  {str(r['ramp']):<5} -> {str(r['unstick_maxt']):>8}            {r['seat_offset_deg']:+.2f} deg")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="/dev/ttyACM2")
    ap.add_argument("--psu", default="10.0.0.82", help="SPD3303X-E host, or '' to skip")
    ap.add_argument("--torques", default="300,400,500,700,900,1200")
    ap.add_argument("--rpms", default="5,10,20,40,60,90,120,180")
    ap.add_argument("--closing-torques", default="300,400,500,600,700,800")
    ap.add_argument("--approach-rpms", default="5,15,30,60")
    ap.add_argument("--stop-at-unstick", type=int, default=800)
    ap.add_argument("--travel-max", type=float, default=720.0)
    ap.add_argument("--free-ladder", default="500,600,700,800,900,1000,1100,1200")
    ap.add_argument("test", choices=["free", "home", "travel", "survey", "stick", "all"])
    a = ap.parse_args()

    rig = Rig(a.port, a.psu or None, a.test)
    print(f"logging to {rig.log_path}")
    try:
        rig.start_psu()
        rig.setup()
        if a.test == "free":
            test_free(rig, [int(x) for x in a.free_ladder.split(",")])
        if a.test in ("home", "all") or "seat_enc" not in rig.state:
            test_home(rig)
        if a.test in ("travel", "all"):
            test_travel(rig, a.travel_max)
        if a.test in ("survey", "all"):
            test_survey(rig, [int(x) for x in a.torques.split(",")], [float(x) for x in a.rpms.split(",")])
        if a.test in ("stick", "all"):
            test_stick(rig, [int(x) for x in a.closing_torques.split(",")], [float(x) for x in a.approach_rpms.split(",")], a.stop_at_unstick)
        rig.event("done")
        return 0
    except Abort as exc:
        rig.event(f"ABORT: {exc}")
        print(f"\nABORT: {exc}\nMotor disabled; the valve may need to be freed by hand.", file=sys.stderr)
        return 3
    except KeyboardInterrupt:
        rig.event("interrupted")
        return 130
    finally:
        rig.shutdown()


if __name__ == "__main__":
    raise SystemExit(main())
