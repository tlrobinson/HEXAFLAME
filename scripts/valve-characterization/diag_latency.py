#!/usr/bin/env python3
"""Measure how long the SERVO42C takes to act on enable / run / stop, and
reproduce the post-rearm overspeed seen in the survey (move 20), mid-window.

Every move is bounded by a travel guard: if the shaft goes further than
allowed, the script sends stop + disable and aborts. Run with the valve well
away from both ends (>= 200 deg of room on each side).
"""
import json, sys, time
from mks42c import Servo42C, COUNTS_PER_REV
PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM2"
SEAT = json.load(open("data/state.json"))["seat_enc"]
def deg(c): return c * 360 / COUNTS_PER_REV
log = open("data/diag_latency.jsonl", "a")
def rec(kind, **k): log.write(json.dumps({"type": kind, "t": time.time(), **k}) + "\n"); log.flush()
def ev(msg, **k): print(f"  [{time.strftime('%H:%M:%S')}] {msg}", flush=True); rec("event", msg=msg, **k)

d = Servo42C(PORT, timeout=0.1)
class Guard(Exception): pass

def watch(seconds, label, start=None, limit_deg=None, stop_when_still=False, stop_cmd=True):
    """Poll encoder/err/enabled every ~10 ms. Returns list of (t, enc, err, en)."""
    t0 = time.time(); rows = []; last_enc = None; last_change = t0
    while time.time() - t0 < seconds:
        e = d.encoder(); er = d.error(); en = d.enabled()
        t = time.time() - t0
        rows.append((t, e, er, en)); rec("diag", label=label, dt=round(t, 4), enc=e, err=er, enabled=en)
        if e is not None:
            if start is not None and limit_deg is not None and abs(deg(e - start)) > limit_deg:
                if stop_cmd: d.stop()
                d.enable(False)
                ev(f"GUARD: {label} travelled {deg(e - start):+.1f} deg (> {limit_deg}); stopped and disabled", label=label)
                raise Guard(label)
            if last_enc is None or e != last_enc: last_enc, last_change = e, time.time()
            elif stop_when_still and time.time() - last_change > 0.3 and t > 0.3: break
    return rows

def speed_profile(rows):
    """Average speed (rpm) over successive encoder updates."""
    out = []; prev = None
    for t, e, er, en in rows:
        if e is None: continue
        if prev and e != prev[1]:
            dt = t - prev[0]
            if dt > 0: out.append((round(t, 3), round(deg(e - prev[1]) / dt / 6, 1)))
            prev = (t, e)
        elif prev is None: prev = (t, e)
    return out

def pos(): e = d.encoder(); return e, deg(e - SEAT)

try:
    e, p = pos(); ev(f"start: enc {e}, {p:.1f} deg open, enabled {d.enabled()}, err {d.error()}, blocked {d.blocked()}")
    if not (150 <= p <= 300): ev("valve is not in the 200..300 deg window; refusing"); sys.exit(2)
    d.set_maxt(500)

    # E1: enable latency from the disabled state (no motion expected)
    ev("E1: enable(True) from disabled; watching 2 s")
    start = e; d.enable(True)
    rows = watch(2.0, "E1 enable", start=start, limit_deg=20, stop_cmd=True)
    t_en = next((t for t, _, _, en in rows if en), None)
    t_err = next((t for t, _, er, _ in rows if er is not None and abs(deg(er)) < 1), None)
    ev(f"E1: enabled reported after {t_en} s; err < 1 deg after {t_err} s; shaft moved {deg(d.encoder() - start):+.2f} deg",
       t_enabled=t_en, t_err_ok=t_err)

    # E2: slow closing move, 30 deg, then stop latency
    ev("E2: F6 gear 1 closing, 30 deg bounded; then measure stop latency")
    start = d.encoder(); d.run(True, 1)
    rows = watch(1.0, "E2 run", start=start, limit_deg=60)
    d.stop(); t_stop = time.time()
    rows2 = watch(1.5, "E2 stop", start=start, limit_deg=60, stop_when_still=True)
    ev(f"E2: ran {deg(d.encoder() - start):+.1f} deg; speed profile {speed_profile(rows)[-4:]}; after stop {speed_profile(rows2)}")
    time.sleep(0.5)

    # E3: fast opening move (gear 26 ~ 122 rpm) for ~100 deg, then stop; then gear 4 closing WITHOUT any enable cycle
    ev("E3: F6 gear 26 opening ~60 deg, stop; then gear 4 closing 60 deg (no enable cycle)")
    start = d.encoder(); d.run(False, 26)
    rows = []
    while True:
        rows += watch(0.03, "E3 fast", start=start, limit_deg=150)
        if deg(d.encoder() - start) >= 60: break
    d.stop()
    rows2 = watch(1.5, "E3 stop", start=start, limit_deg=200, stop_when_still=True)
    ev(f"E3: fast move {deg(d.encoder() - start):+.1f} deg; profile {speed_profile(rows)[-3:]}; after stop {speed_profile(rows2)}; err {d.error()} enabled {d.enabled()}")
    time.sleep(0.5)
    start = d.encoder(); d.run(True, 4)
    rows = watch(0.4, "E3 slow-after-fast", start=start, limit_deg=80)
    d.stop(); watch(0.8, "E3 slow stop", start=start, limit_deg=90, stop_when_still=True)
    ev(f"E3: gear-4 closing after fast move: {deg(d.encoder() - start):+.1f} deg, profile {speed_profile(rows)}")
    time.sleep(0.5)

    # E4: reproduce move 20: fast opening, stop, enable(False)/enable(True), gear 4 closing
    ev("E4: MaxT 300, F6 gear 26 opening ~60 deg, stop, enable off/on (0.2 s each), then gear 4 closing (MaxT 500)")
    d.set_maxt(300)
    start = d.encoder(); d.run(False, 26)
    rows = []
    while True:
        rows += watch(0.03, "E4 fast", start=start, limit_deg=150)
        if deg(d.encoder() - start) >= 60: break
    d.stop()
    watch(1.0, "E4 stop", start=start, limit_deg=200, stop_when_still=True)
    ev(f"E4: fast move {deg(d.encoder() - start):+.1f} deg; err {d.error()}")
    d.enable(False); time.sleep(0.2)
    s = d.encoder(); ev(f"E4: after enable(False)+0.2 s: enabled {d.enabled()} err {d.error()}")
    d.enable(True); time.sleep(0.2)
    ev(f"E4: after enable(True)+0.2 s: enabled {d.enabled()} err {d.error()}")
    d.set_maxt(500)
    start = d.encoder(); d.run(True, 4)
    rows = watch(0.4, "E4 slow-after-cycle", start=start, limit_deg=80)
    d.stop(); rows2 = watch(1.0, "E4 slow stop", start=start, limit_deg=120, stop_when_still=True)
    ev(f"E4: gear-4 closing after enable cycle: {deg(d.encoder() - start):+.1f} deg, profile {speed_profile(rows)}, after stop {speed_profile(rows2)}")

    # E5: disable latency
    ev("E5: enable(False); watching 2 s")
    d.enable(False)
    rows = watch(2.0, "E5 disable")
    t_dis = next((t for t, _, _, en in rows if en is False), None)
    ev(f"E5: disabled reported after {t_dis} s; err while disabled {d.error()}", t_disabled=t_dis)
except Guard:
    pass
finally:
    d.stop(); d.set_maxt(500); d.enable(False); time.sleep(0.3)
    e, p = pos(); ev(f"end: enc {e}, {p:.1f} deg open, enabled {d.enabled()}, blocked {d.blocked()}")
    d.close(); log.close()
