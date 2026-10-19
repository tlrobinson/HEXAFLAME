# valve-node: TMC2209 + magnetic encoder + RS-485 stepper node

A per-valve driver board for the HEXAFLAME needle valves: one NEMA 17 each,
up to 30 nodes daisy-chained on one RS-485 bus with power on the same cable.
Costed with the same JLCPCB model as `hardware/hexaservo42` (copied here).

    python3 cost_model.py --qty 30 --scenario economic   # encoder hand-soldered
    python3 cost_model.py --qty 30 --scenario standard   # fully assembled, double-sided
    python3 cost_model.py --refresh                      # re-fetch prices before ordering

## Architecture

| Block | Part | Why |
|---|---|---|
| Driver | TMC2209-LA-T | 2 A RMS, UART-configured (IRUN/IHOLD/microsteps/StallGuard), no trim pot |
| Encoder | MT6701 (SOP-8, bottom side) | 14-bit absolute single-turn, SSI; faces a magnet on the motor's rear shaft |
| MCU | STM32G030F6P6 | enough for Modbus RTU + TMC2209 UART + SSI + step timing; same family as nothing else here, so swap freely |
| Bus | SP3485 + Micro-Fit 2x2 in/out | half-duplex RS-485, 115200, address by DIP or assigned over the bus; 120 R jumper on the last node |
| Power | 60 V buck to 5 V, LDO to 3.3 V, TVS, fuse | 12-24 V on the bus cable |

The host protocol is the same Modbus RTU map the solenoid firmware speaks
(`components/esp32-solenoid-firmware/lib/hexaflame_link`), extended with
position/velocity registers, so HEXAFLAME drives valves and solenoids through
one link library.

## Open loop vs closed loop

The hardware is identical either way; it is a firmware choice. The TMC2209 is
a step/dir chopper driver, so "closed loop" here means step-correction with
the encoder (re-issue missed steps, track true position), not field-oriented
control. For the needle valves the parts that stop them sticking are:

- torque capped by the run current (IRUN) so a seat strike cannot jam;
- encoder lag/stall detection at any speed, including the slow seat approach
  where StallGuard is unreliable;
- a ramped approach near the seat and a small back-off after it.

All three work open-loop. Step-correction can be added later for free.

## Known gaps in this BOM

- Sense resistor line uses the 20 mR part's price; the 110 mR 1206 part is
  the same family and price band but needs its own LCSC number.
- TMC2209, STM32G030 and the DIP switch are price estimates (not in the
  cached JLCPCB price list); re-run with `--refresh` and real LCSC ids.
- PCB priced as 4-layer like hexaservo42; 2-layer with a 2 oz pour is likely
  adequate for a 1.2 A valve drive and ~40 % cheaper on the bare board.
