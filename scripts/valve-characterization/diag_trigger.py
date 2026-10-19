#!/usr/bin/env python3
"""Which serial command makes the driver lunge? Sit mid-window with the motor
enabled and send one command type many times, watching the shaft after each.
Guarded: motion beyond 4 deg after a command = lunge (stop, disable, log,
re-enable, re-centre); leaving [110, 330] deg open aborts."""
import json, sys, time
from mks42c import Servo42C, COUNTS_PER_REV
seat = json.load(open("data/state.json"))["seat_enc"]
d = Servo42C('/dev/ttyACM2', timeout=0.1)
def deg(c): return c * 360 / COUNTS_PER_REV
def pos(): e = d.encoder(); return e, (None if e is None else deg(e - seat))
log = open("data/diag_trigger.jsonl", "a")
def rec(**k): log.write(json.dumps({"type": "trigger", "t": time.time(), **k}) + "\n"); log.flush()
class Stop(Exception): pass
CENTRE = 200.0
def recentre():
    e, p = pos()
    if abs(p - CENTRE) < 8: return
    d.wait_enabled(True); d.set_maxt(500)
    closing = p > CENTRE; start = d.encoder(); d.run(closing, 2)
    while True:
        e, p = pos()
        if abs(p - CENTRE) < 4 or (closing and p < CENTRE) or (not closing and p > CENTRE): break
        if p < 100 or p > 340: raise Stop("left the safe band while recentring")
        time.sleep(0.005)
    d.stop_verified(); e, p = pos(); print(f"  recentred to {p:.1f} deg", flush=True)
def still(secs=0.4):
    t0 = time.time(); last = d.encoder()
    while time.time() - t0 < secs:
        time.sleep(0.05); e = d.encoder()
        if e is not None and last is not None and abs(e - last) > 40: t0 = time.time()
        last = e
lunges = []
def trial(name, send, n, watch=0.25, tol=4.0):
    print(f"{name}: {n} sends", flush=True); cnt = 0
    for i in range(n):
        e0, p0 = pos()
        if p0 is None or p0 < 110 or p0 > 330: raise Stop(f"out of band at {p0}")
        send(i)
        t0 = time.time(); peak = 0.0; path = e0
        while time.time() - t0 < watch:
            e = d.encoder()
            if e is not None:
                mv = deg(e - e0); peak = mv if abs(mv) > abs(peak) else peak
                if abs(mv) > tol:
                    d.stop(); d.wait_enabled(False); time.sleep(0.4); still()
                    e1, p1 = pos()
                    print(f"  LUNGE after {name} #{i}: {deg(e1 - e0):+.1f} deg ({p0:.1f} -> {p1:.1f} deg open)", flush=True)
                    rec(cmd=name, i=i, lunge=True, from_deg=p0, to_deg=p1, moved=deg(e1 - e0)); lunges.append((name, i, p0, p1)); cnt += 1
                    d.wait_enabled(True); time.sleep(0.2); recentre(); break
            time.sleep(0.008)
        else:
            rec(cmd=name, i=i, lunge=False, peak=peak)
    print(f"  {name}: {cnt} lunges in {n} sends", flush=True)
try:
    e, p = pos(); print(f"start {p:.1f} deg open, enabled {d.enabled()}", flush=True)
    d.wait_enabled(True); d.set_maxt(500); d.cmd(0x90, b"\x00"); recentre(); still()
    trial("reads only", lambda i: None, 120)
    trial("A5 MaxT 500", lambda i: d.set_maxt(500), 120)
    trial("A5 MaxT 400", lambda i: d.set_maxt(400), 60)
    trial("F3 01 enable", lambda i: d.enable(True), 120)
    trial("F6 g1 / F7", lambda i: (d.run(i % 2 == 0, 1), time.sleep(0.05), d.stop()), 120, watch=0.35, tol=6.0)
    trial("39/3E/3A reads", lambda i: (d.error(), d.blocked(), d.enabled()), 120)
    trial("A5 MaxT 500 #2", lambda i: d.set_maxt(500), 120)
except Stop as ex:
    print("STOP:", ex, flush=True)
finally:
    d.stop(); d.set_maxt(500); off = d.wait_enabled(False)
    e, p = pos(); print(f"parked: disabled {off}, {p:.1f} deg open; lunges: {lunges}", flush=True)
    rec(summary=lunges); d.close(); log.close()
