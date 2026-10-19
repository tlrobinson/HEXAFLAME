#!/usr/bin/env python3
"""Exercise the HEXAFLAME solenoid firmware over its Modbus RTU link.

The frames are the same ones HEXAFLAME's web app and the WaveShare Modbus
RTU Relay 32CH use, so this doubles as a check that a host written for the
WaveShare module works against this firmware.

Note: by default the firmware switches every output off when the host
closes the port, so state does not survive between invocations of this
script. `selftest` runs a whole sequence over one connection.

Examples:
  solenoid_test.py --port /dev/ttyACM0 status
  solenoid_test.py --port /dev/ttyACM0 selftest
  solenoid_test.py --port /dev/ttyACM0 on 0
  solenoid_test.py --port /dev/ttyACM0 pulse 3 --ms 500
  solenoid_test.py --port /dev/ttyACM0 mask 0x0f
  solenoid_test.py --port /dev/ttyACM0 walk --ms 150
  solenoid_test.py --port /dev/ttyACM0 raw "01 05 00 00 FF 00"
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("pyserial is required: pip install pyserial", file=sys.stderr)
    raise

CHANNELS = 64
DIAG = [
    ("boards online mask", 0x9000),
    ("boards offline mask", 0x9001),
    ("i2c error count", 0x9002),
    ("uptime s", 0x9003),
    ("firmware version", 0x9004),
    ("channel count", 0x9005),
    ("host connected", 0x9006),
    ("frames handled", 0x9007),
    ("frames rejected", 0x9008),
    ("exceptions sent", 0x9009),
    ("i2c base address", 0x900A),
    ("board 0 output latch", 0x9010),
]


def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def frame(payload: bytes) -> bytes:
    crc = crc16(payload)
    return payload + bytes([crc & 0xFF, crc >> 8])


class Device:
    def __init__(self, port: str, baud: int, address: int, timeout: float):
        self.ser = serial.Serial(port, baud, timeout=timeout)
        self.address = address
        time.sleep(0.05)
        self.ser.reset_input_buffer()

    def close(self):
        self.ser.close()

    def transact(self, payload: bytes, expect: int = None) -> bytes:
        tx = frame(payload)
        self.ser.reset_input_buffer()
        self.ser.write(tx)
        self.ser.flush()
        if expect is None:
            expect = len(tx)
        rx = self.ser.read(expect)
        # Exceptions are 5 bytes; read the remainder of a longer reply if needed.
        if len(rx) >= 2 and rx[1] & 0x80 and len(rx) < 5:
            rx += self.ser.read(5 - len(rx))
        return rx

    def check(self, label: str, payload: bytes, expect: int = None) -> bytes:
        rx = self.transact(payload, expect)
        tx = frame(payload)
        status = "ok"
        if not rx:
            status = "NO RESPONSE"
        elif len(rx) >= 2 and rx[1] & 0x80:
            status = f"EXCEPTION {rx[2]:#04x}" if len(rx) >= 3 else "EXCEPTION"
        elif len(rx) >= 2 and crc16(rx[:-2]) != rx[-2] | (rx[-1] << 8):
            status = "BAD CRC"
        print(f"{label:<24} tx {tx.hex(' ')}")
        print(f"{'':<24} rx {rx.hex(' ') if rx else '-'}  [{status}]")
        return rx

    # --- WaveShare-compatible operations -----------------------------------

    def write_coil(self, channel: int, value: int, label: str):
        return self.check(label, bytes([self.address, 0x05, channel >> 8, channel & 0xFF, value >> 8, value & 0xFF]))

    def read_coils(self, start: int, count: int):
        payload = bytes([self.address, 0x01, start >> 8, start & 0xFF, count >> 8, count & 0xFF])
        rx = self.check(f"read coils {start}+{count}", payload, 5 + (count + 7) // 8)
        if len(rx) >= 3 and rx[1] == 0x01:
            data = rx[3:3 + rx[2]]
            return [(data[i // 8] >> (i % 8)) & 1 for i in range(count)]
        return None

    def read_register(self, reg: int, label: str):
        payload = bytes([self.address, 0x03, reg >> 8, reg & 0xFF, 0x00, 0x01])
        rx = self.check(label, payload, 7)
        if len(rx) == 7 and rx[1] == 0x03:
            return (rx[3] << 8) | rx[4]
        return None

    def write_mask(self, mask: int, count: int = 32):
        nbytes = (count + 7) // 8
        bits = bytes((mask >> (8 * i)) & 0xFF for i in range(nbytes))
        payload = bytes([self.address, 0x0F, 0x00, 0x00, count >> 8, count & 0xFF, nbytes]) + bits
        return self.check(f"write mask {mask:#x}", payload, 8)


def cmd_status(dev: Device, args):
    version = dev.read_register(0x8000, "software version")
    if version is not None:
        print(f"{'':<24} -> V{version / 100:.2f}")
    dev.read_register(0x4000, "device address")
    uart = dev.read_register(0x2000, "uart params")
    if uart is not None:
        print(f"{'':<24} -> parity {uart >> 8}, baud code {uart & 0xFF}")
    for label, reg in DIAG:
        value = dev.read_register(reg, label)
        if value is not None:
            print(f"{'':<24} -> {value} ({value:#06x})")
    states = dev.read_coils(0, CHANNELS)
    if states:
        on = [i for i, s in enumerate(states) if s]
        print(f"{'':<24} -> on: {on if on else 'none'}")


def cmd_selftest(dev: Device, args):
    """Drive the device through a scripted sequence over one connection and
    check both the Modbus replies and the MCP23017 output latch."""
    failures = []

    def expect(label, condition):
        print(f"{'':<24} {'PASS' if condition else 'FAIL'}: {label}")
        if not condition:
            failures.append(label)

    online = dev.read_register(0x9000, "boards online mask") or 0
    expect("at least one solenoid board online", online != 0)
    board = next((i for i in range(8) if online >> i & 1), None)
    base = board * 8 if board is not None else 0

    def latch():
        if board is None:
            return None
        return dev.read_register(0x9010 + board, f"board {board} output latch")

    dev.write_coil(0x00FF, 0x0000, "all off")
    expect("latch clear after all off", latch() == 0)

    dev.write_coil(base + 0, 0xFF00, f"channel {base} on")
    states = dev.read_coils(base, 8)
    expect("coil read shows channel 0 on", states == [1, 0, 0, 0, 0, 0, 0, 0])
    expect("latch bit 0 set", latch() == 0x01)

    dev.write_coil(base + 3, 0x5500, f"channel {base + 3} toggle")
    expect("latch bits 0 and 3 set", latch() == 0x09)

    dev.write_mask(0xA5 << base, 32)
    states = dev.read_coils(base, 8)
    expect("mask 0xA5 read back", states == [1, 0, 1, 0, 0, 1, 0, 1])
    expect("latch 0xA5", latch() == 0xA5)

    dev.write_coil(0x00FF, 0x5500, "all toggle")
    expect("latch 0x5A after toggle", latch() == 0x5A)

    dev.write_coil(0x00FF, 0x0000, "all off")
    dev.write_coil(0x0200 + base + 1, 3, f"channel {base + 1} flash on 300 ms")
    expect("flash on: latch 0x02 immediately", latch() == 0x02)
    time.sleep(0.4)
    expect("flash on: latch clear after 400 ms", latch() == 0x00)

    dev.write_coil(base + 2, 0xFF00, f"channel {base + 2} on")
    dev.write_coil(0x0400 + base + 2, 2, f"channel {base + 2} flash off 200 ms")
    expect("flash off: latch clear immediately", latch() == 0x00)
    time.sleep(0.3)
    expect("flash off: latch 0x04 after 300 ms", latch() == 0x04)

    rx = dev.check("illegal address", bytes([dev.address, 0x05, 0x05, 0x00, 0xFF, 0x00]), 5)
    expect("illegal address -> exception 02", rx == frame(bytes([dev.address, 0x85, 0x02])))
    rx = dev.check("illegal value", bytes([dev.address, 0x05, 0x00, 0x00, 0x12, 0x34]), 5)
    expect("illegal value -> exception 03", rx == frame(bytes([dev.address, 0x85, 0x03])))

    dev.write_coil(base + 5, 0xFF00, f"channel {base + 5} on (left on)")
    expect("latch 0x24 before disconnect", latch() == 0x24)
    port, baud, address, timeout = dev.ser.port, dev.ser.baudrate, dev.address, dev.ser.timeout
    dev.close()
    time.sleep(0.3)
    dev.ser = serial.Serial(port, baud, timeout=timeout)
    time.sleep(0.1)
    dev.ser.reset_input_buffer()
    expect("outputs cleared when the port was closed", latch() == 0x00)
    states = dev.read_coils(base, 8)
    expect("coils read clear after reconnect", states == [0] * 8)

    errors = dev.read_register(0x9002, "i2c error count")
    expect("no I2C errors", errors == 0)

    print()
    if failures:
        print(f"SELFTEST FAILED: {len(failures)} check(s): {failures}")
        sys.exit(1)
    print("SELFTEST PASSED")


def cmd_on(dev, args):
    dev.write_coil(args.channel, 0xFF00, f"channel {args.channel} on")


def cmd_off(dev, args):
    dev.write_coil(args.channel, 0x0000, f"channel {args.channel} off")


def cmd_toggle(dev, args):
    dev.write_coil(args.channel, 0x5500, f"channel {args.channel} toggle")


def cmd_pulse(dev, args):
    units = max(1, min(0x7FFF, round(args.ms / 100)))
    dev.write_coil(0x0200 + args.channel, units, f"channel {args.channel} flash on {units * 100} ms")


def cmd_all(dev, args):
    value = {"on": 0xFF00, "off": 0x0000, "toggle": 0x5500}[args.state]
    dev.write_coil(0x00FF, value, f"all {args.state}")


def cmd_mask(dev, args):
    dev.write_mask(int(args.mask, 0), args.count)


def cmd_walk(dev, args):
    try:
        for channel in range(args.start, args.start + args.count):
            dev.write_mask(1 << channel, 32)
            time.sleep(args.ms / 1000)
    finally:
        dev.write_mask(0, 32)


def cmd_raw(dev, args):
    payload = bytes(int(tok, 16) for tok in args.payload.replace(",", " ").split())
    dev.check("raw", payload, 64)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--address", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=0.3)
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("status").set_defaults(fn=cmd_status)
    sub.add_parser("selftest").set_defaults(fn=cmd_selftest)
    for name, fn in (("on", cmd_on), ("off", cmd_off), ("toggle", cmd_toggle)):
        p = sub.add_parser(name)
        p.add_argument("channel", type=int)
        p.set_defaults(fn=fn)
    p = sub.add_parser("pulse")
    p.add_argument("channel", type=int)
    p.add_argument("--ms", type=int, default=500)
    p.set_defaults(fn=cmd_pulse)
    p = sub.add_parser("all")
    p.add_argument("state", choices=("on", "off", "toggle"))
    p.set_defaults(fn=cmd_all)
    p = sub.add_parser("mask")
    p.add_argument("mask")
    p.add_argument("--count", type=int, default=32)
    p.set_defaults(fn=cmd_mask)
    p = sub.add_parser("walk")
    p.add_argument("--start", type=int, default=0)
    p.add_argument("--count", type=int, default=8)
    p.add_argument("--ms", type=int, default=200)
    p.set_defaults(fn=cmd_walk)
    p = sub.add_parser("raw")
    p.add_argument("payload", help="hex bytes without CRC, e.g. '01 05 00 00 FF 00'")
    p.set_defaults(fn=cmd_raw)

    args = parser.parse_args()
    dev = Device(args.port, args.baud, args.address, args.timeout)
    try:
        args.fn(dev, args)
    finally:
        dev.close()


if __name__ == "__main__":
    main()
