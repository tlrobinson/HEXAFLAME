"""Minimal MKS SERVO42C (firmware V1.1) UART driver for bench characterization.

Frame: addr, fn, params, checksum (low byte of the sum). Replies to set and
control commands are addr, 01/00, checksum.

Hard-won facts about this driver (see docs in the HEXAFLAME memory notes):
  * Motion commands (F3/F6/F7/FD) only work in work mode 02 (CR_UART).
  * Read encoder (0x30) returns int32 carry + uint16 value at 65536 counts
    per revolution; it is absolute and does not re-base on enable.
  * Shaft angle (0x36) and pulses (0x33) reset to 0 on every enable.
  * After a stall the driver sets the blocked flag AND disables the motor.
    Both 0x3D (release) and F3 01 (enable) are needed before it drives again.
  * MaxT (0xA5, 0..0x4B0) caps closed-loop torque; use it as the "current".
"""
from __future__ import annotations

import struct
import time
from dataclasses import dataclass

import serial

COUNTS_PER_REV = 65536
MAXT_MAX = 0x4B0  # 1200


def checksum(payload: bytes) -> int:
    return sum(payload) & 0xFF


def rpm_for_gear(gear: int, mstep: int, full_steps_per_rev: int = 200) -> float:
    # From the MKS wiki: RPM = gear * 30000 / (Mstep * 200) for a 1.8 deg motor.
    return gear * 30000.0 / (mstep * full_steps_per_rev)


def gear_for_rpm(rpm: float, mstep: int) -> int:
    return max(1, min(127, round(rpm * mstep * 200 / 30000.0)))


@dataclass
class Sample:
    t: float
    enc: int | None        # absolute encoder counts (65536/rev)
    err: int | None        # position error, counts (65536/rev), target - actual
    blocked: bool | None
    enabled: bool | None

    def err_deg(self) -> float | None:
        return None if self.err is None else self.err * 360.0 / COUNTS_PER_REV


class Servo42C:
    def __init__(self, port: str, baud: int = 115200, address: int = 0xE0, timeout: float = 0.25):
        self.ser = serial.Serial(port, baud, timeout=timeout)
        self.addr = address
        time.sleep(0.05)
        self.ser.reset_input_buffer()
        self.mstep = 32
        self.bad_replies = 0        # link-quality counters
        self.retried_cmds = 0
        self.failed_cmds = 0

    def close(self) -> None:
        self.ser.close()

    # --- low level ---------------------------------------------------------
    def xfer(self, fn: int, params: bytes = b"", nbytes: int = 3) -> bytes | None:
        frame = bytes([self.addr, fn]) + params
        frame += bytes([checksum(frame)])
        self.ser.reset_input_buffer()
        self.ser.write(frame)
        self.ser.flush()
        rx = self.ser.read(nbytes)
        if len(rx) != nbytes or rx[0] != self.addr or checksum(rx[:-1]) != rx[-1]:
            # Bad or missing reply: let any late bytes arrive, then drop them so
            # the next transaction starts aligned.
            self.bad_replies += 1
            time.sleep(0.02)
            self.ser.reset_input_buffer()
            return None
        return rx[1:-1]

    def cmd(self, fn: int, params: bytes = b"", retries: int = 3) -> bool:
        """Send a command and require its ack; resend up to `retries` times.
        A frame lost on the USB link otherwise fails silently, and a lost
        stop/disable is exactly what must not happen."""
        for attempt in range(retries + 1):
            data = self.xfer(fn, params)
            if data is not None and data[0] == 1:
                if attempt:
                    self.retried_cmds += 1
                return True
            if attempt < retries:
                time.sleep(0.03)
        self.failed_cmds += 1
        return False

    # --- reads ---------------------------------------------------------------
    def encoder(self) -> int | None:
        d = self.xfer(0x30, nbytes=8)
        if d is None:
            return None
        return struct.unpack(">i", d[:4])[0] * COUNTS_PER_REV + struct.unpack(">H", d[4:])[0]

    def error(self) -> int | None:
        d = self.xfer(0x39, nbytes=4)
        return None if d is None else struct.unpack(">h", d)[0]

    def blocked(self) -> bool | None:
        d = self.xfer(0x3E)
        return None if d is None else d[0] == 1

    def enabled(self) -> bool | None:
        d = self.xfer(0x3A)
        return None if d is None else d[0] == 1

    def pulses(self) -> int | None:
        d = self.xfer(0x33, nbytes=6)
        return None if d is None else struct.unpack(">i", d)[0]

    def sample(self) -> Sample:
        return Sample(time.time(), self.encoder(), self.error(), self.blocked(), self.enabled())

    # --- settings ------------------------------------------------------------
    def set_work_mode(self, mode: int) -> bool:          # 0 open, 1 vFOC, 2 UART
        return self.cmd(0x82, bytes([mode]))

    def set_mstep(self, mstep: int) -> bool:
        ok = self.cmd(0x84, bytes([mstep]))
        if ok:
            self.mstep = mstep
        return ok

    def set_protect(self, on: bool) -> bool:
        return self.cmd(0x88, b"\x01" if on else b"\x00")

    def set_maxt(self, value: int) -> bool:
        value = max(0, min(MAXT_MAX, int(value)))
        return self.cmd(0xA5, struct.pack(">H", value))

    # --- control -------------------------------------------------------------
    def enable(self, on: bool = True) -> bool:
        return self.cmd(0xF3, b"\x01" if on else b"\x00")

    def release(self) -> bool:
        return self.cmd(0x3D)

    def rearm(self) -> None:
        """Clear a stall: release the lock, then re-enable (both are required)."""
        if self.blocked():
            self.release()
            time.sleep(0.15)
        self.enable(True)
        time.sleep(0.15)

    def wait_enabled(self, want: bool, timeout: float = 1.0) -> bool:
        """Send enable/disable and poll until the driver reports it (resending once)."""
        self.enable(want)
        t0 = time.time()
        while time.time() - t0 < timeout:
            if self.enabled() is want:
                return True
            if time.time() - t0 > timeout / 2:
                self.enable(want)
            time.sleep(0.01)
        return self.enabled() is want

    def stop_verified(self, timeout: float = 1.0) -> bool:
        """Stop and poll the encoder until the shaft is still; resend the stop
        if it is still moving after 0.4 s. Returns True when stationary."""
        self.stop()
        t0 = time.time(); last = self.encoder(); last_change = t0; resent = False
        while time.time() - t0 < timeout:
            e = self.encoder()
            if e is not None and last is not None and abs(e - last) > 40:
                last, last_change = e, time.time()
                if not resent and time.time() - t0 > 0.4:
                    self.stop(); resent = True
            elif time.time() - last_change > 0.2 and time.time() - t0 > 0.15:
                return True
            time.sleep(0.01)
        return False

    def run(self, closing: bool, gear: int) -> bool:
        """Constant-speed run. Closing = F6 forward on this valve."""
        return self.cmd(0xF6, bytes([(0x00 if closing else 0x80) | (max(1, min(127, gear)) & 0x7F)]))

    def stop(self) -> bool:
        return self.cmd(0xF7)
