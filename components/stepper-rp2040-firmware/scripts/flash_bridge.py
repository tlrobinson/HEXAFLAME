#!/usr/bin/env python3
"""Firmware flash and serial bridge for Docker or remote-host workflows.

Run this on whichever machine can see the RP2040 serial/BOOTSEL device. A
Docker container or another machine can then POST to /serial, /serial-stream, or
/flash to control and flash the board through this bridge.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import py_compile
import queue
import select
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


DEFAULT_SERIAL_PORT = "/dev/cu.usbmodem2101"
DEFAULT_BOOTSEL_VOLUME_NAME = "RPI-RP2"
MAX_JSON_BODY_BYTES = 4096
MAX_UF2_BODY_BYTES = 8 * 1024 * 1024
MAX_OUTPUT_BYTES = 60_000
MAX_LOG_TEXT_BYTES = 60_000
COLOR_RESET = "\033[0m"
COLOR_KEY = "\033[36m"
COLOR_STRING = "\033[32m"
COLOR_NUMBER = "\033[33m"
COLOR_LITERAL = "\033[35m"
COLOR_PUNCT = "\033[2m"
SERIAL_GLOBS = (
    "/dev/serial/by-id/*Raspberry*",
    "/dev/serial/by-id/*Pico*",
    "/dev/serial/by-id/*RP2040*",
    "/dev/serial/by-id/*usbmodem*",
    "/dev/serial/by-id/*",
    "/dev/ttyACM*",
    "/dev/ttyUSB*",
    "/dev/cu.usbmodem*",
    "/dev/cu.usbserial*",
    "/dev/tty.usbmodem*",
    "/dev/tty.usbserial*",
)


class FlashState:
    def __init__(self, token: str | None):
        self.token = token
        self.lock = threading.Lock()
        self.serial_manager = SerialManager()


class SerialCommandRequest:
    def __init__(self, command: str, read_timeout_s: float, append_newline: bool):
        self.command = command
        self.read_timeout_s = read_timeout_s
        self.append_newline = append_newline
        self.events: queue.Queue[dict[str, Any]] = queue.Queue()
        self.done = threading.Event()
        self.output: list[str] = []
        self.exit_code = 0

        try:
            parsed = json.loads(command)
        except json.JSONDecodeError:
            parsed = None
        self.request_id = parsed.get("id") if isinstance(parsed, dict) else None

    def emit_output(self, line: str) -> None:
        data = line + "\n"
        self.output.append(data)
        self.events.put({"type": "output", "data": data})

    def emit_error(self, message: str) -> None:
        self.output.append(message)
        self.events.put({"type": "output", "data": message})
        self.exit_code = 1

    def finish(self) -> None:
        self.events.put({"type": "end", "ok": self.exit_code == 0, "exit_code": self.exit_code})
        self.done.set()


class SerialWorker:
    def __init__(self, serial_port: str, baud: int):
        self.serial_port = serial_port
        self.baud = baud
        self.requests: queue.Queue[SerialCommandRequest | None] = queue.Queue()
        self.stop_event = threading.Event()
        self.fd: int | None = None
        self.receive_buffer = ""
        self.thread = threading.Thread(target=self._run, name=f"serial-worker-{serial_port}", daemon=True)
        self.thread.start()

    def submit(self, request: SerialCommandRequest) -> SerialCommandRequest:
        self.requests.put(request)
        return request

    def close(self) -> None:
        self.stop_event.set()
        self.requests.put(None)
        self.thread.join(timeout=2.0)
        self._close_fd()

    def _close_fd(self) -> None:
        if self.fd is None:
            return
        try:
            os.close(self.fd)
        except OSError:
            pass
        self.fd = None

    def _open_fd(self) -> str:
        if self.fd is not None:
            return ""

        code, output = prepare_serial_port(self.serial_port, self.baud)
        if code != 0:
            return output
        fd, open_error = open_serial_fd(self.serial_port)
        if fd is None:
            return open_error
        self.fd = fd
        self.receive_buffer = ""
        return ""

    def _read_available_lines(self) -> list[str]:
        if self.fd is None:
            return []
        try:
            readable, _, _ = select.select([self.fd], [], [], 0)
        except OSError as exc:
            self._close_fd()
            raise OSError(f"Serial device disconnected while waiting for reply: {exc}") from exc
        if not readable:
            return []
        try:
            chunk = os.read(self.fd, 4096)
        except BlockingIOError:
            return []
        except OSError as exc:
            self._close_fd()
            raise OSError(f"Serial device disconnected while reading reply: {exc}") from exc
        if not chunk:
            return []

        self.receive_buffer += chunk.decode("utf-8", errors="replace")
        lines: list[str] = []
        while "\n" in self.receive_buffer:
            line, self.receive_buffer = self.receive_buffer.split("\n", 1)
            line = line.rstrip("\r")
            if line:
                lines.append(line)
        return lines

    def _read_idle_lines(self, duration_s: float = 0.2) -> None:
        deadline = time.monotonic() + duration_s
        while time.monotonic() < deadline and not self.stop_event.is_set():
            try:
                lines = self._read_available_lines()
            except OSError as exc:
                log_bridge_event(str(exc))
                return
            for line in lines:
                log_json_traffic("<-", line)
            time.sleep(0.02)

    def _line_matches_request(self, request: SerialCommandRequest, line: str) -> bool:
        if request.request_id is None:
            return False
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            return False
        return isinstance(message, dict) and message.get("id") == request.request_id

    def _run_request(self, request: SerialCommandRequest) -> None:
        open_error = self._open_fd()
        if open_error:
            request.emit_error(open_error)
            request.finish()
            return

        assert self.fd is not None
        self._read_idle_lines()
        if self.receive_buffer.strip():
            log_bridge_event(f"Dropped stale partial serial line before command: {self.receive_buffer.strip()}")
            self.receive_buffer = ""
        try:
            os.write(self.fd, request.command.encode("utf-8"))
            if request.append_newline and not request.command.endswith("\n"):
                os.write(self.fd, b"\n")
        except OSError as exc:
            self._close_fd()
            request.emit_error(f"Failed to write to serial port: {exc}\n")
            request.finish()
            return

        log_json_traffic("->", request.command)
        deadline = time.monotonic() + request.read_timeout_s
        while time.monotonic() < deadline and not self.stop_event.is_set():
            try:
                lines = self._read_available_lines()
            except OSError as exc:
                request.emit_error(str(exc) + "\n")
                break
            for line in lines:
                log_json_traffic("<-", line)
                request.emit_output(line)
                if self._line_matches_request(request, line):
                    request.finish()
                    return
            time.sleep(0.02)

        if self.receive_buffer.strip():
            line = self.receive_buffer.strip()
            self.receive_buffer = ""
            log_json_traffic("<-", line)
            request.emit_output(line)
        request.finish()

    def _run(self) -> None:
        while not self.stop_event.is_set():
            request = self.requests.get()
            if request is None:
                break
            self._run_request(request)


class SerialManager:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.workers: dict[tuple[str, int], SerialWorker] = {}

    def submit(self, serial_port: str, baud: int, request: SerialCommandRequest) -> SerialCommandRequest:
        key = (serial_port, baud)
        with self.lock:
            worker = self.workers.get(key)
            if worker is None or not worker.thread.is_alive():
                worker = SerialWorker(serial_port, baud)
                self.workers[key] = worker
            worker.submit(request)
        return request

    def close_all(self) -> None:
        with self.lock:
            workers = list(self.workers.values())
            self.workers.clear()
        for worker in workers:
            worker.close()


def discover_serial_ports() -> list[str]:
    ports: list[str] = []
    for pattern in SERIAL_GLOBS:
        for path in sorted(Path("/").glob(pattern.lstrip("/"))):
            port = str(path)
            if port not in ports:
                ports.append(port)
    return ports


def default_serial_port() -> str:
    env_port = os.environ.get("HEXAFLAME_FLASH_SERIAL")
    if env_port:
        return env_port
    ports = discover_serial_ports()
    return ports[0] if ports else DEFAULT_SERIAL_PORT


def stty_command(serial_port: str, baud: int | str, *extra: str) -> list[str]:
    flag = "-f" if sys.platform == "darwin" else "-F"
    return ["stty", flag, serial_port, str(baud), *extra]


def bootsel_volume_candidates(volume: str | None = None) -> list[Path]:
    if volume:
        return [Path(volume)]
    env_volume = os.environ.get("HEXAFLAME_BOOTSEL_VOLUME")
    if env_volume:
        return [Path(env_volume)]

    name = os.environ.get("HEXAFLAME_BOOTSEL_VOLUME_NAME", DEFAULT_BOOTSEL_VOLUME_NAME)
    candidates = [Path("/Volumes") / name]
    for base_pattern in ("/media/*", "/run/media/*", "/mnt"):
        for base in sorted(Path("/").glob(base_pattern.lstrip("/"))):
            candidates.append(base / name)
    return candidates


def find_bootsel_volume(volume: str | None = None) -> Path | None:
    for candidate in bootsel_volume_candidates(volume):
        if candidate.is_dir():
            return candidate
    return None


def mounted_volume_summary() -> str:
    volumes: list[str] = []
    for base in (Path("/Volumes"), Path("/media"), Path("/run/media"), Path("/mnt")):
        if not base.is_dir():
            continue
        for path in sorted(base.glob("*")):
            volumes.append(str(path))
        for path in sorted(base.glob("*/*")):
            volumes.append(str(path))
    return ", ".join(volumes) if volumes else "[none]"


def json_response(handler: BaseHTTPRequestHandler, status: int, payload: dict[str, Any]) -> None:
    body = json.dumps(payload, indent=2).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


def read_json_body(handler: BaseHTTPRequestHandler) -> dict[str, Any]:
    length = int(handler.headers.get("Content-Length", "0"))
    if length > MAX_JSON_BODY_BYTES:
        raise ValueError("request body is too large")
    if length == 0:
        return {}
    raw = handler.rfile.read(length)
    if not raw:
        return {}
    data = json.loads(raw.decode("utf-8"))
    if not isinstance(data, dict):
        raise ValueError("request body must be a JSON object")
    return data


def read_binary_body(handler: BaseHTTPRequestHandler, max_bytes: int) -> bytes:
    length = int(handler.headers.get("Content-Length", "0"))
    if length <= 0:
        raise ValueError("request body is empty")
    if length > max_bytes:
        raise ValueError(f"request body is too large: {length} > {max_bytes}")
    body = handler.rfile.read(length)
    if len(body) != length:
        raise ValueError("request body was truncated")
    return body


def request_is_authorized(handler: BaseHTTPRequestHandler, token: str | None) -> bool:
    if not token:
        return True
    auth = handler.headers.get("Authorization", "")
    header_token = handler.headers.get("X-Flash-Token", "")
    return auth == f"Bearer {token}" or header_token == token


def log_bridge_event(message: str) -> None:
    timestamp = dt.datetime.now(dt.UTC).isoformat(timespec="seconds")
    print(f"[{timestamp}] {message}", flush=True)


def colorize_json_text(text: str) -> str:
    try:
        parsed = json.loads(text)
    except json.JSONDecodeError:
        return text

    if isinstance(parsed, dict) and parsed.get("jsonrpc") == "2.0":
        parsed = {key: value for key, value in parsed.items() if key != "jsonrpc"}

    compact = json.dumps(parsed, separators=(",", ":"), sort_keys=True)
    output: list[str] = []
    index = 0
    length = len(compact)
    while index < length:
        char = compact[index]
        if char == '"':
            start = index
            index += 1
            escaped = False
            while index < length:
                current = compact[index]
                index += 1
                if escaped:
                    escaped = False
                elif current == "\\":
                    escaped = True
                elif current == '"':
                    break
            token = compact[start:index]
            lookahead = index
            while lookahead < length and compact[lookahead].isspace():
                lookahead += 1
            color = COLOR_KEY if lookahead < length and compact[lookahead] == ":" else COLOR_STRING
            output.append(f"{color}{token}{COLOR_RESET}")
            continue
        if char in "{}[]:,":
            output.append(f"{COLOR_PUNCT}{char}{COLOR_RESET}")
            index += 1
            continue
        if char.isdigit() or char == "-":
            start = index
            index += 1
            while index < length and compact[index] in "0123456789.eE+-":
                index += 1
            output.append(f"{COLOR_NUMBER}{compact[start:index]}{COLOR_RESET}")
            continue
        if compact.startswith("true", index) or compact.startswith("null", index):
            token = compact[index:index + 4]
            output.append(f"{COLOR_LITERAL}{token}{COLOR_RESET}")
            index += 4
            continue
        if compact.startswith("false", index):
            output.append(f"{COLOR_LITERAL}false{COLOR_RESET}")
            index += 5
            continue
        output.append(char)
        index += 1

    return "".join(output)


def log_json_traffic(direction: str, text: str) -> None:
    for raw_line in text.splitlines():
        line = raw_line.strip()
        if line:
            log_bridge_event(f"{direction} {colorize_json_text(line)}")


def restart_bridge_after_response(delay_s: float = 0.25) -> None:
    def restart() -> None:
        time.sleep(delay_s)
        log_bridge_event("RESTART")
        os.execv(sys.executable, [sys.executable, *sys.argv])

    thread = threading.Thread(target=restart, daemon=True)
    thread.start()


def format_log_block(text: str) -> str:
    if not text:
        return "[empty]"

    encoded = text.encode("utf-8", errors="replace")
    if len(encoded) > MAX_LOG_TEXT_BYTES:
        encoded = encoded[-MAX_LOG_TEXT_BYTES:]
        text = "[truncated to last " + str(MAX_LOG_TEXT_BYTES) + " bytes]\n" + encoded.decode("utf-8", errors="replace")

    return text.rstrip("\n") or "[empty]"


def send_bootsel_json_rpc(serial_port: str) -> str:
    stty = subprocess.run(
        stty_command(serial_port, 115200, "raw", "-echo"),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    output = ""
    if stty.returncode != 0:
        output += f"stty failed ({stty.returncode}): {stty.stdout}\n"

    request_id = "bridge-bootloader"
    command = json.dumps(
        {"jsonrpc": "2.0", "id": request_id, "method": "bootloader", "params": {}},
        separators=(",", ":"),
    )

    try:
        fd = os.open(serial_port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    except OSError as exc:
        return output + f"Failed to open {serial_port} for JSON-RPC bootloader request: {exc}\n"

    saw_response = False
    try:
        drain_serial_fd(fd, 0.25)
        write_error = write_serial_command(fd, command, True)
        output += f"Sent JSON-RPC bootloader request to {serial_port}\n"
        if write_error:
            return output + write_error

        def on_line(line: str) -> None:
            nonlocal output, saw_response
            output += line + "\n"
            try:
                message = json.loads(line)
            except json.JSONDecodeError:
                return
            if isinstance(message, dict) and message.get("id") == request_id:
                saw_response = True

        def on_error(message: str) -> None:
            nonlocal output
            output += message

        read_serial_lines(fd, 1.5, on_line, on_error)
    finally:
        os.close(fd)

    if saw_response:
        output += "Firmware acknowledged JSON-RPC bootloader request\n"
    else:
        output += "No JSON-RPC bootloader acknowledgement before disconnect/timeout\n"
    return output


def touch_serial_bootloader(serial_port: str) -> str:
    stty = subprocess.run(
        stty_command(serial_port, 1200),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if stty.returncode != 0:
        return f"1200-baud touch failed ({stty.returncode}): {stty.stdout}\n"

    try:
        with open(serial_port, "wb"):
            pass
    except OSError as exc:
        return f"1200-baud touch set baud but open/close failed: {exc}\n"

    return f"Touched {serial_port} at 1200 baud to request bootloader\n"


def wait_for_bootsel_volume(volume_path: Path, timeout_s: float = 15.0) -> bool:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        if volume_path.is_dir():
            return True
        time.sleep(0.25)
    return volume_path.is_dir()


def wait_for_any_bootsel_volume(volume: str | None = None, timeout_s: float = 15.0) -> Path | None:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        found = find_bootsel_volume(volume)
        if found is not None:
            return found
        time.sleep(0.25)
    return find_bootsel_volume(volume)


def run_serial_command(
    serial_port: str,
    command: str,
    read_timeout_s: float,
    baud: int = 115200,
    append_newline: bool = True,
) -> tuple[int, str, list[str]]:
    stty = subprocess.run(
        stty_command(serial_port, baud, "raw", "-echo"),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    output = ""
    if stty.returncode != 0:
        output += f"stty failed ({stty.returncode}): {stty.stdout}\n"

    try:
        fd = os.open(serial_port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    except OSError as exc:
        return 1, output + f"Failed to open {serial_port}: {exc}\n", ["serial", serial_port, command.rstrip("\n")]

    try:
        drain_serial_fd(fd, 0.35)
        try:
            os.write(fd, command.encode("utf-8"))
            if append_newline and not command.endswith("\n"):
                os.write(fd, b"\n")
        except OSError as exc:
            return 1, output + f"Failed to write to {serial_port}: {exc}\n", ["serial", serial_port, command.rstrip("\n")]

        def on_line(line: str) -> None:
            nonlocal output
            output += line + "\n"

        def on_error(message: str) -> None:
            nonlocal output
            output += message

        _, read_output = read_serial_lines(fd, read_timeout_s, on_line, on_error)
        if not output:
            output = read_output
        command_summary = ["serial", serial_port, command.rstrip("\n")]
        if not append_newline:
            command_summary.append("--no-newline")
        return 0, output[-MAX_OUTPUT_BYTES:], command_summary
    finally:
        os.close(fd)


def prepare_serial_port(serial_port: str, baud: int) -> tuple[int, str]:
    stty = subprocess.run(
        stty_command(serial_port, baud, "raw", "-echo"),
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    if stty.returncode != 0:
        return stty.returncode, f"stty failed ({stty.returncode}): {stty.stdout}\n"
    return 0, ""


def open_serial_fd(serial_port: str) -> tuple[int | None, str]:
    try:
        return os.open(serial_port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK), ""
    except OSError as exc:
        return None, f"Failed to open {serial_port}: {exc}\n"


def drain_serial_fd(fd: int, duration_s: float = 0.25) -> None:
    drain_deadline = time.monotonic() + duration_s
    while time.monotonic() < drain_deadline:
        readable, _, _ = select.select([fd], [], [], 0.02)
        if not readable:
            continue
        os.read(fd, 4096)


def read_serial_lines(
    fd: int,
    read_timeout_s: float,
    on_line: Any,
    on_error: Any,
) -> tuple[int, str]:
    chunks: list[str] = []
    receive_buffer = ""
    exit_code = 0
    deadline = time.monotonic() + read_timeout_s
    while time.monotonic() < deadline:
        try:
            readable, _, _ = select.select([fd], [], [], 0.1)
        except OSError as exc:
            message = f"Serial device disconnected while waiting for reply: {exc}\n"
            chunks.append(message)
            on_error(message)
            break
        if not readable:
            continue
        try:
            chunk = os.read(fd, 4096)
        except BlockingIOError:
            continue
        except OSError as exc:
            message = f"Serial device disconnected while reading reply: {exc}\n"
            chunks.append(message)
            on_error(message)
            break
        if not chunk:
            continue

        text = chunk.decode("utf-8", errors="replace")
        chunks.append(text)
        receive_buffer += text
        while "\n" in receive_buffer:
            line, receive_buffer = receive_buffer.split("\n", 1)
            line = line.rstrip("\r")
            if line:
                on_line(line)

    if receive_buffer.strip():
        line = receive_buffer.strip()
        chunks.append("\n")
        on_line(line)

    return exit_code, "".join(chunks)[-MAX_OUTPUT_BYTES:]


def write_serial_command(fd: int, command: str, append_newline: bool) -> str:
    try:
        os.write(fd, command.encode("utf-8"))
        if append_newline and not command.endswith("\n"):
            os.write(fd, b"\n")
    except OSError as exc:
        return f"Failed to write to serial port: {exc}\n"
    return ""


def serial_command_summary(serial_port: str, command_text: str, append_newline: bool) -> list[str]:
    command_summary = ["serial", serial_port, command_text.rstrip("\n")]
    if not append_newline:
        command_summary.append("--no-newline")
    return command_summary


def flash_uploaded_uf2(
    uf2_data: bytes,
    *,
    serial_port: str,
    volume: str | None,
    filename: str = "firmware.uf2",
) -> tuple[int, str, list[str]]:
    output = f"Received UF2 upload: {len(uf2_data)} bytes\n"
    if not uf2_data.startswith(b"UF2\n"):
        output += "Warning: UF2 file does not start with literal UF2 header; continuing anyway\n"

    volume_path = find_bootsel_volume(volume)
    if volume_path is None and serial_port:
        try:
            output += "\n" + send_bootsel_json_rpc(serial_port)
        except OSError as exc:
            output += f"\nFailed to send JSON-RPC bootloader request to {serial_port}: {exc}\n"
        volume_path = wait_for_any_bootsel_volume(volume, 15.0)

    if volume_path is None and serial_port:
        output += touch_serial_bootloader(serial_port)
        volume_path = wait_for_any_bootsel_volume(volume, 15.0)

    if volume_path is None:
        expected = ", ".join(str(item) for item in bootsel_volume_candidates(volume))
        return (
            1,
            output
            + f"\nTimed out waiting for BOOTSEL volume. Expected one of: {expected}\n"
            + f"Mounted volumes: {mounted_volume_summary()}\n",
            ["upload-uf2", filename],
        )

    safe_name = Path(filename).name or "firmware.uf2"
    if not safe_name.lower().endswith(".uf2"):
        safe_name += ".uf2"
    destination = volume_path / safe_name
    destination.write_bytes(uf2_data)
    return (
        0,
        output + f"\nCopied uploaded UF2 to {destination}\n",
        ["upload-uf2", str(destination)],
    )


class FlashHandler(BaseHTTPRequestHandler):
    server_version = "HexaflameFlashBridge/1.0"

    def do_GET(self) -> None:
        if self.path != "/health":
            json_response(self, 404, {"ok": False, "error": "not found"})
            return
        state: FlashState = self.server.flash_state  # type: ignore[attr-defined]
        json_response(
            self,
            200,
            {
                "ok": True,
                "platform": sys.platform,
                "default_serial_port": default_serial_port(),
                "serial_ports": discover_serial_ports(),
                "bootsel_volume": str(find_bootsel_volume()) if find_bootsel_volume() else None,
                "bootsel_volume_candidates": [str(item) for item in bootsel_volume_candidates()],
                "flash_modes": ["uploaded-uf2"],
            },
        )

    def do_POST(self) -> None:
        route = self.path.split("?", 1)[0]
        if route not in {"/flash-uf2", "/serial", "/serial-stream", "/restart"}:
            json_response(self, 404, {"ok": False, "error": "not found"})
            return

        state: FlashState = self.server.flash_state  # type: ignore[attr-defined]
        if not request_is_authorized(self, state.token):
            json_response(self, 401, {"ok": False, "error": "unauthorized"})
            return

        if route == "/flash-uf2":
            serial_port = self.headers.get("X-Serial-Port") or default_serial_port()
            volume = self.headers.get("X-Bootsel-Volume")
            filename = self.headers.get("X-Filename") or "firmware.uf2"

            acquired = state.lock.acquire(blocking=False)
            if not acquired:
                json_response(self, 409, {"ok": False, "error": "flash already in progress"})
                return

            try:
                uf2_data = read_binary_body(self, MAX_UF2_BODY_BYTES)
                state.serial_manager.close_all()
                log_bridge_event("FLASH-UF2")
                exit_code, output, command = flash_uploaded_uf2(
                    uf2_data,
                    serial_port=serial_port,
                    volume=volume,
                    filename=filename,
                )
            except ValueError as exc:
                json_response(self, 400, {"ok": False, "error": str(exc)})
                return
            finally:
                state.lock.release()

            json_response(
                self,
                200 if exit_code == 0 else 500,
                {
                    "ok": exit_code == 0,
                    "exit_code": exit_code,
                    "command": command,
                    "output": output,
                },
            )
            return

        try:
            payload = read_json_body(self)
        except (json.JSONDecodeError, ValueError) as exc:
            json_response(self, 400, {"ok": False, "error": str(exc)})
            return

        if route == "/restart":
            acquired = state.lock.acquire(blocking=False)
            if not acquired:
                json_response(self, 409, {"ok": False, "error": "flash already in progress"})
                return
            try:
                try:
                    py_compile.compile(__file__, doraise=True)
                except py_compile.PyCompileError as exc:
                    json_response(self, 500, {"ok": False, "error": "restart compile check failed", "details": str(exc)})
                    return
            finally:
                state.lock.release()

            state.serial_manager.close_all()
            json_response(self, 200, {"ok": True, "restarting": True})
            restart_bridge_after_response()
            return

        if route in {"/serial", "/serial-stream"}:
            serial_port = str(
                payload.get("serial_port")
                or default_serial_port()
            )
            command_text = str(payload.get("command") or "")
            if not command_text:
                json_response(self, 400, {"ok": False, "error": "missing serial command"})
                return
            read_timeout_s = float(payload.get("read_timeout_s") or 5.0)
            baud = int(payload.get("baud") or 115200)
            append_newline = bool(payload.get("append_newline", True))
            if route == "/serial-stream":
                self.handle_serial_stream(serial_port, command_text, read_timeout_s, baud, append_newline)
                return

            request = state.serial_manager.submit(
                serial_port,
                baud,
                SerialCommandRequest(command_text, read_timeout_s, append_newline),
            )
            request.done.wait(timeout=read_timeout_s + 10.0)
            if not request.done.is_set():
                request.emit_error("Timed out waiting for queued serial request\n")
                request.finish()
            command = serial_command_summary(serial_port, command_text, append_newline)
            output = "".join(request.output)[-MAX_OUTPUT_BYTES:]
            exit_code = request.exit_code
            json_response(
                self,
                200 if exit_code == 0 else 500,
                {
                    "ok": exit_code == 0,
                    "exit_code": exit_code,
                    "command": command,
                    "output": output,
                },
            )
            return

        json_response(self, 404, {"ok": False, "error": "unsupported route"})

    def write_stream_event(self, payload: dict[str, Any]) -> None:
        self.wfile.write(json.dumps(payload).encode("utf-8") + b"\n")
        self.wfile.flush()

    def handle_serial_stream(
        self,
        serial_port: str,
        command_text: str,
        read_timeout_s: float,
        baud: int,
        append_newline: bool,
    ) -> None:
        state: FlashState = self.server.flash_state  # type: ignore[attr-defined]
        command_summary = serial_command_summary(serial_port, command_text, append_newline)

        self.send_response(200)
        self.send_header("Content-Type", "application/x-ndjson")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()

        self.write_stream_event({"type": "start", "command": command_summary})
        request = state.serial_manager.submit(
            serial_port,
            baud,
            SerialCommandRequest(command_text, read_timeout_s, append_newline),
        )
        try:
            while True:
                try:
                    event = request.events.get(timeout=read_timeout_s + 10.0)
                except queue.Empty:
                    request.emit_error("Timed out waiting for queued serial request\n")
                    request.finish()
                    continue
                if event.get("type") == "end":
                    self.write_stream_event(
                        {
                            "type": "end",
                            "ok": event.get("ok"),
                            "exit_code": event.get("exit_code"),
                            "command": command_summary,
                        }
                    )
                    return
                self.write_stream_event(event)
        except BrokenPipeError:
            return

    def log_message(self, format: str, *args: Any) -> None:
        log_bridge_event(format % args)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run a HEXAFLAME firmware flash and serial bridge.")
    parser.add_argument("--host", default=os.environ.get("HEXAFLAME_FLASH_HOST", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("HEXAFLAME_FLASH_PORT", "8765")))
    parser.add_argument("--token", default=os.environ.get("HEXAFLAME_FLASH_TOKEN"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    state = FlashState(args.token)
    server = ThreadingHTTPServer((args.host, args.port), FlashHandler)
    server.flash_state = state  # type: ignore[attr-defined]

    log_bridge_event(f"Flash bridge listening on http://{args.host}:{args.port}")
    log_bridge_event("Flash mode: uploaded UF2 only")
    log_bridge_event("Token auth: enabled" if state.token else "Token auth: disabled")
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
