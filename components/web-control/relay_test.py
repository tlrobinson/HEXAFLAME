#!/usr/bin/env python3

import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("pyserial is required: pip install pyserial", file=sys.stderr)
    raise


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def build_write_single_coil(slave: int, channel: int, enabled: bool) -> bytes:
    if not 1 <= slave <= 255:
        raise ValueError("slave must be in 1..255")
    if not 1 <= channel <= 32:
        raise ValueError("channel must be in 1..32")

    coil_address = channel - 1
    payload = bytes(
        [
            slave,
            0x05,
            (coil_address >> 8) & 0xFF,
            coil_address & 0xFF,
            0xFF if enabled else 0x00,
            0x00,
        ]
    )
    crc = crc16_modbus(payload)
    return payload + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def write_and_read(ser: serial.Serial, frame: bytes) -> bytes:
    ser.reset_input_buffer()
    ser.write(frame)
    ser.flush()
    return ser.read(8)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Toggle a Waveshare Modbus RTU Relay 32CH channel."
    )
    parser.add_argument("--port", required=True, help="Serial port, e.g. /dev/tty.usbserial-0001")
    parser.add_argument("--slave", type=int, default=1, help="Modbus slave address (default: 1)")
    parser.add_argument("--baudrate", type=int, default=115200, help="Serial baudrate (default: 115200)")
    parser.add_argument("--channel", type=int, required=True, help="Relay channel 1-32")
    parser.add_argument(
        "--state",
        choices=("on", "off", "pulse"),
        required=True,
        help="Desired relay state",
    )
    parser.add_argument(
        "--pulse-ms",
        type=int,
        default=500,
        help="Pulse duration in ms when --state pulse (default: 500)",
    )
    args = parser.parse_args()

    try:
        ser = serial.Serial(
            port=args.port,
            baudrate=args.baudrate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.5,
        )
    except Exception as exc:
        print(f"failed to open serial port: {exc}", file=sys.stderr)
        return 1

    with ser:
        try:
            if args.state in ("on", "off"):
                frame = build_write_single_coil(
                    args.slave, args.channel, args.state == "on"
                )
                response = write_and_read(ser, frame)
                print(f"tx: {frame.hex(' ')}")
                print(f"rx: {response.hex(' ') if response else '(no response)'}")
            else:
                on_frame = build_write_single_coil(args.slave, args.channel, True)
                off_frame = build_write_single_coil(args.slave, args.channel, False)
                on_response = write_and_read(ser, on_frame)
                time.sleep(args.pulse_ms / 1000)
                off_response = write_and_read(ser, off_frame)
                print(f"tx on: {on_frame.hex(' ')}")
                print(f"rx on: {on_response.hex(' ') if on_response else '(no response)'}")
                print(f"tx off: {off_frame.hex(' ')}")
                print(f"rx off: {off_response.hex(' ') if off_response else '(no response)'}")
        except Exception as exc:
            print(f"serial write failed: {exc}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
