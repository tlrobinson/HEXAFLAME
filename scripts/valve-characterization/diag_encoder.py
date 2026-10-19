#!/usr/bin/env python3
"""Motor disabled: read the absolute encoder (0x30 carry+value), the relative
shaft angle (0x36), the pulse count (0x33) and the error (0x39) as fast as
possible and report every jump. Nothing is commanded."""
import json, struct, sys, time
from mks42c import Servo42C, COUNTS_PER_REV
PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM2"
SECS = float(sys.argv[2]) if len(sys.argv) > 2 else 60
d = Servo42C(PORT, timeout=0.1)
def deg(c): return c * 360 / COUNTS_PER_REV
log = open("data/diag_encoder.jsonl", "a")
print("enabled", d.enabled(), "blocked", d.blocked(), flush=True)
assert d.enabled() is False, "motor must be disabled for this test"
t0 = time.time(); n = 0; prev = None; jumps = 0
raw_prev = None
while time.time() - t0 < SECS:
    r30 = d.xfer(0x30, nbytes=8)
    r36 = d.xfer(0x36, nbytes=6)
    r33 = d.xfer(0x33, nbytes=6)
    err = d.error()
    if r30 is None: continue
    carry = struct.unpack(">i", r30[:4])[0]; val = struct.unpack(">H", r30[4:])[0]
    enc = carry * COUNTS_PER_REV + val
    ang36 = struct.unpack(">i", r36)[0] if r36 else None
    pulses = struct.unpack(">i", r33)[0] if r33 else None
    rec = {"type": "encdiag", "t": time.time(), "carry": carry, "val": val, "enc": enc, "ang36": ang36, "pulses": pulses, "err": err}
    log.write(json.dumps(rec) + "\n"); n += 1
    if prev is not None and (abs(enc - prev["enc"]) > 20 or (ang36 is not None and prev["ang36"] is not None and abs(ang36 - prev["ang36"]) > 20)):
        jumps += 1
        print(f"{time.time()-t0:7.2f}s JUMP enc {prev['enc']} -> {enc} ({deg(enc-prev['enc']):+.2f} deg)  carry {prev['carry']}->{carry} val {prev['val']}->{val}  "
              f"ang36 {prev['ang36']}->{ang36}  pulses {pulses}  err {err}", flush=True)
    prev = rec
print(f"{n} reads in {time.time()-t0:.1f} s, {jumps} jumps; last enc {enc} ({deg(enc % COUNTS_PER_REV):.1f} deg in-turn), ang36 {ang36}, pulses {pulses}, err {err}", flush=True)
d.close(); log.close()
