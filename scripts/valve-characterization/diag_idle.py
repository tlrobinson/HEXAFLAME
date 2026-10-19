#!/usr/bin/env python3
"""Find which end-of-run command makes the driver move on its own.
Replays the sequence the last verify run ended with, one command at a time,
watching the encoder for 8 s after each. A watchdog disables the motor the
moment the shaft moves more than 4 deg without being commanded."""
import json, sys, time
from mks42c import Servo42C, COUNTS_PER_REV

SEAT = -74489          # low-torque seat from the verified home
def deg(c): return c * 360 / COUNTS_PER_REV
d = Servo42C("/dev/ttyACM2")
log = open("data/diag_idle.jsonl", "a")
def rec(**k): log.write(json.dumps({"t": time.time(), **k}) + "\n"); log.flush()

def watch(label, secs, allow_deg=4.0):
    e0 = d.encoder(); t0 = time.time(); peak = 0.0; tripped = False
    while time.time() - t0 < secs:
        e = d.encoder(); err = d.error(); blk = d.blocked()
        if e is not None:
            peak = max(peak, abs(deg(e - e0)))
            rec(label=label, enc=e, err=err, blocked=blk)
            if abs(deg(e - e0)) > allow_deg:
                d.enable(False); tripped = True
                print(f"  !! {label}: moved {deg(e - e0):+.1f} deg uncommanded -> motor DISABLED", flush=True); break
        time.sleep(0.03)
    e1 = d.encoder()
    print(f"  {label:<40} drift {deg(e1 - e0):+.2f} deg, peak {peak:.2f}, err {d.error()}, blocked {d.blocked()}, enabled {d.enabled()}", flush=True)
    return tripped

try:
    d.set_work_mode(2); time.sleep(0.2); d.set_mstep(32); d.set_protect(True); d.set_maxt(500)
    d.rearm()
    e = d.encoder(); print(f"start: enc {e}, {deg(e - SEAT):.1f} deg open from the old seat, err {d.error()}, blocked {d.blocked()}", flush=True)
    if deg(e - SEAT) < 70:
        print("opening to ~90 deg from the seat at low torque", flush=True)
        d.run(False, 1); t0 = time.time()
        while deg((d.encoder() or e) - SEAT) < 90 and time.time() - t0 < 40: time.sleep(0.03)
        d.stop(); time.sleep(0.3); d.rearm()
        e = d.encoder(); print(f"now {deg(e - SEAT):.1f} deg open", flush=True)
    if watch("baseline idle (enabled, MaxT 500)", 6): sys.exit(1)
    # Replay the verify run's ending, one step at a time.
    print("replay: F6 open 1 s then F7", flush=True)
    d.run(False, 1); time.sleep(1.0); d.stop(); time.sleep(0.3)
    if watch("after F6/F7", 8): sys.exit(1)
    d.enable(True); time.sleep(0.15)
    if watch("after F3 01 (re-enable)", 8): sys.exit(1)
    d.stop()
    if watch("after F7 while stopped", 8): sys.exit(1)
    d.set_maxt(1200)
    if watch("after MaxT -> 1200", 10): sys.exit(1)
    d.set_protect(True)
    if watch("after protect ON", 10): sys.exit(1)
    d.close(); time.sleep(0.5); d = Servo42C("/dev/ttyACM2")
    if watch("after serial close/reopen", 10): sys.exit(1)
    d.set_maxt(500)
    if watch("after MaxT -> 500", 6): sys.exit(1)
    if watch("long idle, enabled, MaxT 500", 45): sys.exit(1)
    print("no uncommanded motion reproduced", flush=True)
finally:
    try:
        d.stop(); d.set_maxt(500); d.enable(False)
        print(f"end: enc {d.encoder()}, enabled {d.enabled()}, motor left DISABLED", flush=True)
    finally:
        d.close(); log.close()
