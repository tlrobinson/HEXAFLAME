#!/usr/bin/env python3
"""Read state from a Siglent SPD3303X-E power supply over SCPI/TCP."""

from __future__ import annotations

import argparse
import json
import socket
import sys
import time
from typing import Any


DEFAULT_HOST = "10.0.0.82"
DEFAULT_PORT = 5025
PROGRAMMABLE_CHANNELS = ("CH1", "CH2")
MAX_SET_VOLTAGE_V = 18.0


class ScpiClient:
    def __init__(self, host: str, port: int, timeout: float):
        self.host = host
        self.port = port
        self.timeout = timeout

    def query(self, command: str) -> str:
        with socket.create_connection((self.host, self.port), timeout=self.timeout) as sock:
            sock.settimeout(self.timeout)
            sock.sendall(command.encode("ascii") + b"\n")
            return self._read_reply(sock)

    def write(self, command: str) -> None:
        with socket.create_connection((self.host, self.port), timeout=self.timeout) as sock:
            sock.settimeout(self.timeout)
            sock.sendall(command.encode("ascii") + b"\n")

    def _read_reply(self, sock: socket.socket) -> str:
        chunks: list[bytes] = []
        deadline = time.monotonic() + self.timeout

        while time.monotonic() < deadline:
            try:
                chunk = sock.recv(4096)
            except TimeoutError:
                break
            except socket.timeout:
                break

            if not chunk:
                break

            chunks.append(chunk)
            if b"\n" in chunk:
                break

        if not chunks:
            raise TimeoutError("no SCPI reply received")

        return b"".join(chunks).decode("ascii", errors="replace").strip()


def maybe_float(value: str | None) -> float | None:
    if value is None:
        return None
    try:
        return float(value)
    except ValueError:
        return None


def maybe_output_enabled(value: str | None) -> bool | None:
    if value is None:
        return None
    normalized = value.strip().upper()
    if normalized in {"1", "ON"}:
        return True
    if normalized in {"0", "OFF"}:
        return False
    return None


def decode_system_status(value: str | None) -> dict[str, Any] | None:
    if value is None:
        return None

    try:
        status = int(value, 16)
    except ValueError:
        return {"raw": value}

    tracking_bits = (status >> 2) & 0b11
    tracking_modes = {
        0b01: "independent",
        0b10: "parallel",
        0b11: "series",
    }

    return {
        "raw": value,
        "value": status,
        "tracking_mode": tracking_modes.get(tracking_bits, "unknown"),
        "channels": {
            "CH1": {
                "regulation_mode": "CC" if (status & (1 << 0)) else "CV",
                "output_enabled": bool(status & (1 << 4)),
                "timer_enabled": bool(status & (1 << 6)),
                "display_mode": "waveform" if (status & (1 << 8)) else "digital",
            },
            "CH2": {
                "regulation_mode": "CC" if (status & (1 << 1)) else "CV",
                "output_enabled": bool(status & (1 << 5)),
                "timer_enabled": bool(status & (1 << 7)),
                "display_mode": "waveform" if (status & (1 << 9)) else "digital",
            },
        },
    }


def safe_query(client: ScpiClient, command: str, errors: dict[str, str]) -> str | None:
    try:
        return client.query(command)
    except OSError as exc:
        errors[command] = str(exc)
    except TimeoutError as exc:
        errors[command] = str(exc)
    return None


def read_channel(
    client: ScpiClient,
    channel: str,
    errors: dict[str, str],
    system_channel_status: dict[str, Any] | None,
) -> dict[str, Any]:
    set_voltage_raw = safe_query(client, f"{channel}:VOLT?", errors)
    set_current_raw = safe_query(client, f"{channel}:CURR?", errors)
    measured_voltage_raw = safe_query(client, f"MEAS:VOLT? {channel}", errors)
    measured_current_raw = safe_query(client, f"MEAS:CURR? {channel}", errors)

    channel_state: dict[str, Any] = {
        "set_voltage_v": maybe_float(set_voltage_raw),
        "set_current_a": maybe_float(set_current_raw),
        "measured_voltage_v": maybe_float(measured_voltage_raw),
        "measured_current_a": maybe_float(measured_current_raw),
        "raw": {
            "set_voltage": set_voltage_raw,
            "set_current": set_current_raw,
            "measured_voltage": measured_voltage_raw,
            "measured_current": measured_current_raw,
        },
    }

    if system_channel_status:
        channel_state.update(system_channel_status)

    return channel_state


def read_state(host: str, port: int, timeout: float) -> dict[str, Any]:
    client = ScpiClient(host, port, timeout)
    errors: dict[str, str] = {}

    state: dict[str, Any] = {
        "host": host,
        "port": port,
        "identity": safe_query(client, "*IDN?", errors),
        "channels": {},
    }

    system_status_raw = safe_query(client, "SYST:STATUS?", errors)
    system_status = decode_system_status(system_status_raw)
    if system_status is not None:
        state["system_status"] = system_status

    system_channels = (system_status or {}).get("channels", {})
    for channel in PROGRAMMABLE_CHANNELS:
        state["channels"][channel] = read_channel(client, channel, errors, system_channels.get(channel))

    state["channels"]["CH3"] = {
        "note": "CH3 is manually selectable on the SPD3303X-E and is not covered by the standard CH1/CH2 SCPI readback commands.",
    }

    if errors:
        state["query_errors"] = errors

    return state


def set_outputs(host: str, port: int, timeout: float, channels: list[str], enabled: bool) -> None:
    client = ScpiClient(host, port, timeout)
    state = "ON" if enabled else "OFF"
    for channel in channels:
        client.write(f"OUTP {channel},{state}")
        time.sleep(0.05)


def set_voltage(host: str, port: int, timeout: float, channel: str, voltage: float) -> None:
    if channel not in PROGRAMMABLE_CHANNELS:
        raise ValueError(f"voltage can only be set for {', '.join(PROGRAMMABLE_CHANNELS)}")
    if voltage <= 0 or voltage > MAX_SET_VOLTAGE_V:
        raise ValueError(f"voltage must be >0 and <= {MAX_SET_VOLTAGE_V:.1f}V")

    client = ScpiClient(host, port, timeout)
    client.write(f"{channel}:VOLT {voltage:.2f}")


def parse_channels(raw_channels: list[str]) -> list[str]:
    if not raw_channels:
        return ["CH1"]

    channels: list[str] = []
    for raw_channel in raw_channels:
        for part in raw_channel.split(","):
            channel = part.strip().upper()
            if not channel:
                continue
            if channel == "ALL":
                channels.extend(["CH1", "CH2", "CH3"])
            elif channel in {"CH1", "CH2", "CH3"}:
                channels.append(channel)
            else:
                raise ValueError(f"unsupported channel: {part}")

    deduped: list[str] = []
    for channel in channels:
        if channel not in deduped:
            deduped.append(channel)
    return deduped


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Read Siglent SPD3303X-E state over SCPI/TCP.")
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"Power supply IP address. Default: {DEFAULT_HOST}")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"SCPI TCP port. Default: {DEFAULT_PORT}")
    parser.add_argument("--timeout", type=float, default=1.5, help="Per-query socket timeout in seconds.")
    parser.add_argument("--compact", action="store_true", help="Print compact JSON.")
    parser.add_argument("--channel", action="append", default=[], help="Output channel to control: CH1, CH2, CH3, or ALL. Default: CH1.")
    parser.add_argument(
        "--set-voltage",
        type=float,
        help=f"Set the selected programmable channel voltage before reading state. Safety clamp: {MAX_SET_VOLTAGE_V:.1f}V.",
    )
    output_group = parser.add_mutually_exclusive_group()
    output_group.add_argument("--on", action="store_true", help="Turn the selected output channel(s) on before reading state.")
    output_group.add_argument("--off", action="store_true", help="Turn the selected output channel(s) off before reading state.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.on or args.off:
            channels = parse_channels(args.channel)
            set_outputs(args.host, args.port, args.timeout, channels, args.on)
            time.sleep(0.25)
        if args.set_voltage is not None:
            channels = parse_channels(args.channel)
            if len(channels) != 1:
                raise ValueError("--set-voltage requires exactly one channel")
            set_voltage(args.host, args.port, args.timeout, channels[0], args.set_voltage)
            time.sleep(0.25)

        state = read_state(args.host, args.port, args.timeout)
    except OSError as exc:
        print(f"failed to connect to {args.host}:{args.port}: {exc}", file=sys.stderr)
        return 1
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 1

    if args.compact:
        print(json.dumps(state, sort_keys=True))
    else:
        print(json.dumps(state, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
