#!/usr/bin/env python3
"""Find out whether the driver's zero-return is what moves the shaft to in-turn
~180.8 deg. Run 30 deg away from that angle; every watch has a travel guard."""
import json, struct, sys, time
from mks42c import Servo42C, COUNTS_PER_REV
d = Servo42C('/dev/ttyACM2', timeout=0.1)
def deg(c): return c*360/COUNTS_PER_REV
log = open("data/diag_zero.jsonl", "a")
def rec(**k): log.write(json.dumps({"type": "zerodiag", "t": time.time(), **k}) + "\n"); log.flush()
def pulses():
    r = d.xfer(0x33, nbytes=6); return struct.unpack(">i", r)[0] if r else None
def watch(secs, label, limit=60):
    e0 = d.encoder(); t0 = time.time(); peak = 0; path = []
    while time.time() - t0 < secs:
        e = d.encoder(); er = d.error(); en = d.enabled(); p = pulses()
        rec(label=label, dt=round(time.time()-t0, 3), enc=e, err=er, enabled=en, pulses=p)
        if e is not None:
            mv = deg(e - e0); peak = max(peak, abs(mv))
            if not path or abs(e - path[-1]) > 60: path.append(e)
            if abs(mv) > limit:
                d.stop(); d.enable(False); print(f"  GUARD {label}: moved {mv:+.1f} deg; stopped+disabled", flush=True); return mv
        time.sleep(0.005)
    e = d.encoder()
    print(f"  {label:<44} moved {deg(e-e0):+6.1f} deg -> in-turn {deg(e%COUNTS_PER_REV):6.1f}  err {d.error()}  enabled {d.enabled()}  pulses {pulses()}", flush=True)
    return deg(e - e0)
def goto_offset(target_inturn):
    """Slow gear-1 move to a given in-turn angle (short way), guarded."""
    e = d.encoder(); cur = deg(e % COUNTS_PER_REV); delta = (target_inturn - cur + 180) % 360 - 180
    closing = delta < 0
    d.wait_enabled(True); d.set_maxt(500)
    start = d.encoder(); d.run(closing, 1)
    while abs(deg(d.encoder() - start)) < abs(delta) - 1.5:
        if abs(deg(d.encoder() - start)) > abs(delta) + 20: break
        time.sleep(0.005)
    d.stop_verified()
    e = d.encoder(); print(f"  moved to in-turn {deg(e%COUNTS_PER_REV):.1f} (wanted {target_inturn})", flush=True)

ZERO = 180.8
e = d.encoder(); print(f"start enc {e} in-turn {deg(e%COUNTS_PER_REV):.1f} enabled {d.enabled()}", flush=True)
goto_offset(ZERO - 30)
print("A: work mode 2", flush=True); d.set_work_mode(2); watch(2.0, "after 82 02")
print("B: disable/enable cycle", flush=True); d.wait_enabled(False); time.sleep(0.2); d.wait_enabled(True); watch(2.0, "after F3 00 / F3 01")
print("C: go-to-zero with mode presumably off", flush=True); r = d.xfer(0x94, b"\x00"); print("  94 reply", r.hex() if r else None, flush=True); watch(2.0, "after 94 (mode off?)")
print("D: NearMode on, then go-to-zero", flush=True); print("  90 02 ack", d.cmd(0x90, b"\x02"), flush=True)
r = d.xfer(0x94, b"\x00"); print("  94 reply", r.hex() if r else None, flush=True); mv = watch(3.0, "after 94 (NearMode)")
print("E: zero mode off again", flush=True); print("  90 00 ack", d.cmd(0x90, b"\x00"), flush=True)
goto_offset(ZERO - 30)
r = d.xfer(0x94, b"\x00"); print("  94 reply", r.hex() if r else None, flush=True); watch(2.0, "after 94 (mode off)")
print("F: DirMode on, then go-to-zero", flush=True); print("  90 01 ack", d.cmd(0x90, b"\x01"), flush=True)
r = d.xfer(0x94, b"\x00"); print("  94 reply", r.hex() if r else None, flush=True); watch(3.0, "after 94 (DirMode)")
print("G: zero mode off, verify once more", flush=True); print("  90 00 ack", d.cmd(0x90, b"\x00"), flush=True)
goto_offset(ZERO - 30)
r = d.xfer(0x94, b"\x00"); print("  94 reply", r.hex() if r else None, flush=True); watch(2.0, "after 94 (mode off #2)")
d.wait_enabled(False); e = d.encoder(); print(f"end enc {e} in-turn {deg(e%COUNTS_PER_REV):.1f} enabled {d.enabled()}", flush=True)
d.close(); log.close()
