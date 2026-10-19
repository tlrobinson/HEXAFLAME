#!/usr/bin/env python3
"""Apply motor power (Siglent CH1) with the driver already parked, and watch.

The driver logic is now fed from a separate 3.3 V supply, so it stays alive
while V+ is off. This script therefore parks the driver FIRST (motor disabled,
torque limit 300, zero-return mode off), then switches CH1 on, keeps
re-sending "disable" for the first two seconds in case the logic reboots on
the V+ edge, and samples the encoder for 15 s to show whether the shaft moved
on its own. The motor is left disabled.
"""
import json, sys, time
from mks42c import Servo42C, COUNTS_PER_REV
from spd3303x import Spd3303x

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyACM2"
VOLTS, AMPS = 16.0, 1.5                      # modest current limit for the boot test
def deg(c): return c * 360 / COUNTS_PER_REV
log = open("data/poweron.jsonl", "a")
def rec(**k): log.write(json.dumps({"t": time.time(), **k}) + "\n"); log.flush()

psu = Spd3303x()
print("PSU:", psu.identity(), flush=True)
d = Servo42C(PORT, timeout=0.25)
e_before = d.encoder()
if e_before is None:
    print("driver not answering on logic power; aborting before applying V+", flush=True); sys.exit(1)
ok = d.enable(False) and d.set_maxt(300) and d.cmd(0x90, b"\x00")
print(f"parked: enc {e_before} ({deg(e_before % COUNTS_PER_REV):.1f} deg in-turn), enabled {d.enabled()}, "
      f"blocked {d.blocked()}, park acks {ok}", flush=True)
rec(event="parked", enc=e_before, enabled=d.enabled())
if d.enabled() is not False:
    print("driver still reports enabled; not applying V+", flush=True); sys.exit(1)

psu.write(f"CH1:VOLT {VOLTS:.2f}"); psu.write(f"CH1:CURR {AMPS:.2f}")
t_on = time.time(); psu.write("OUTP CH1,ON"); rec(event="output on")
print(f"CH1 on at {VOLTS} V / {AMPS} A limit; holding disable for 2 s", flush=True)
d.ser.timeout = 0.05
naks = 0
while time.time() - t_on < 2.0:
    if not d.cmd(0xF3, b"\x00"):
        naks += 1
    time.sleep(0.02)
d.ser.timeout = 0.25
e0 = d.encoder()
rebooted = e0 is not None and e_before is not None and abs(e0 - e_before) > COUNTS_PER_REV // 2
print(f"after power: enc {e0}, enabled {d.enabled()}, blocked {d.blocked()}, missed acks {naks}"
      + ("  ** turn counter changed: logic rebooted **" if rebooted else ""), flush=True)
rec(event="powered", enc=e0, enabled=d.enabled(), naks=naks, rebooted=rebooted)
print("watching for 15 s with the motor disabled", flush=True)
t0 = time.time(); peak = 0.0; i = v = None
while time.time() - t0 < 15:
    e = d.encoder(); v, i = psu.measure()
    if e is not None and e0 is not None:
        peak = max(peak, abs(deg(e - e0)))
    rec(enc=e, v=v, i=i)
    time.sleep(0.2)
print(f"shaft moved at most {peak:.2f} deg; supply {v} V {i} A; enabled {d.enabled()}, blocked {d.blocked()}", flush=True)
d.close(); psu.close(); log.close()
