"""Siglent SPD3303X-E over SCPI/TCP with one persistent socket, for sampling
voltage and current while a test runs. Falls back to reconnecting per call
if the instrument drops the connection."""
from __future__ import annotations

import socket
import time


class Spd3303x:
    def __init__(self, host: str = "10.0.0.82", port: int = 5025, timeout: float = 1.5, channel: str = "CH1"):
        self.host, self.port, self.timeout, self.channel = host, port, timeout, channel
        self.sock: socket.socket | None = None

    def _connect(self) -> None:
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)

    def _io(self, command: str, reply: bool) -> str | None:
        for attempt in range(2):
            try:
                if self.sock is None:
                    self._connect()
                assert self.sock is not None
                self.sock.sendall(command.encode("ascii") + b"\n")
                if not reply:
                    return None
                buf = b""
                while not buf.endswith(b"\n"):
                    chunk = self.sock.recv(4096)
                    if not chunk:
                        raise ConnectionError("closed")
                    buf += chunk
                return buf.decode("ascii", errors="replace").strip()
            except (OSError, ConnectionError):
                self.close()
                if attempt == 1:
                    raise
        return None

    def query(self, command: str) -> str:
        return self._io(command, True) or ""

    def write(self, command: str) -> None:
        self._io(command, False)

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def identity(self) -> str:
        return self.query("*IDN?")

    def measure(self) -> tuple[float | None, float | None]:
        def f(s: str) -> float | None:
            try:
                return float(s)
            except ValueError:
                return None
        return f(self.query(f"MEAS:VOLT? {self.channel}")), f(self.query(f"MEAS:CURR? {self.channel}"))

    def _measure(self, what: str) -> float | None:
        try:
            return float(self.query(f"MEAS:{what}? {self.channel}"))
        except ValueError:
            return None

    def measure_voltage(self) -> float | None:
        return self._measure("VOLT")

    def measure_current(self) -> float | None:
        return self._measure("CURR")

    def set_voltage(self, volts: float, max_volts: float = 18.0) -> None:
        if not 0 < volts <= max_volts:
            raise ValueError(f"voltage must be in (0, {max_volts}] V")
        self.write(f"{self.channel}:VOLT {volts:.2f}")
        time.sleep(0.2)

    def set_current_limit(self, amps: float) -> None:
        self.write(f"{self.channel}:CURR {amps:.3f}")
        time.sleep(0.2)
