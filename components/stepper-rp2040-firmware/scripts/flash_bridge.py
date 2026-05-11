#!/usr/bin/env python3
"""Host-side firmware flash bridge for Docker-based workflows.

Run this on macOS, where the RP2040 serial device is visible. A Docker
container can then POST to /flash via host.docker.internal to make the host run
the PlatformIO upload command.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import py_compile
import select
import shutil
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any


SCRIPT_DIR = Path(__file__).resolve().parent
FIRMWARE_DIR = SCRIPT_DIR.parent
DEFAULT_ENV = "rp2040_v3_codex"
DEFAULT_SERIAL_PORT = "/dev/cu.usbmodem2101"
DEFAULT_BOOTSEL_VOLUME = "/Volumes/RPI-RP2"
DEFAULT_BOOTSEL_COMMAND = "bootsel\n"
MAX_BODY_BYTES = 4096
MAX_OUTPUT_BYTES = 60_000
MAX_LOG_TEXT_BYTES = 60_000
COLOR_RESET = "\033[0m"
COLOR_KEY = "\033[36m"
COLOR_STRING = "\033[32m"
COLOR_NUMBER = "\033[33m"
COLOR_LITERAL = "\033[35m"
COLOR_PUNCT = "\033[2m"


class FlashState:
    def __init__(self, project_dir: Path, env_name: str, pio: str, token: str | None):
        self.project_dir = project_dir
        self.env_name = env_name
        self.pio = pio
        self.token = token
        self.lock = threading.Lock()


def json_response(handler: BaseHTTPRequestHandler, status: int, payload: dict[str, Any]) -> None:
    body = json.dumps(payload, indent=2).encode("utf-8")
    handler.send_response(status)
    handler.send_header("Content-Type", "application/json")
    handler.send_header("Content-Length", str(len(body)))
    handler.end_headers()
    handler.wfile.write(body)


def read_json_body(handler: BaseHTTPRequestHandler) -> dict[str, Any]:
    length = int(handler.headers.get("Content-Length", "0"))
    if length > MAX_BODY_BYTES:
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


def run_flash(state: FlashState, serial_port: str | None) -> tuple[int, str, list[str]]:
    command = [
        state.pio,
        "run",
        "-e",
        state.env_name,
        "-t",
        "upload",
    ]
    if serial_port:
        command.extend(["--upload-port", serial_port])
    proc = subprocess.run(
        command,
        cwd=state.project_dir,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=180,
        check=False,
    )
    output = proc.stdout[-MAX_OUTPUT_BYTES:]
    return proc.returncode, output, command


def run_uf2_flash(state: FlashState, volume: str | None) -> tuple[int, str, list[str]]:
    build_command = [state.pio, "run", "-e", state.env_name]
    proc = subprocess.run(
        build_command,
        cwd=state.project_dir,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=180,
        check=False,
    )
    output = proc.stdout
    if proc.returncode != 0:
        return proc.returncode, output[-MAX_OUTPUT_BYTES:], build_command

    uf2_path = state.project_dir / ".pio" / "build" / state.env_name / "firmware.uf2"
    if not uf2_path.is_file():
        return 1, output + f"\nFirmware UF2 not found: {uf2_path}\n", build_command

    volume_path = Path(volume or os.environ.get("HEXAFLAME_BOOTSEL_VOLUME") or DEFAULT_BOOTSEL_VOLUME)
    if not volume_path.is_dir():
        volumes = sorted(str(path) for path in Path("/Volumes").glob("*")) if Path("/Volumes").is_dir() else []
        return (
            1,
            output
            + f"\nBOOTSEL volume not found: {volume_path}\n"
            + f"Mounted volumes: {', '.join(volumes) if volumes else '[none]'}\n",
            build_command,
        )

    destination = volume_path / uf2_path.name
    shutil.copyfile(uf2_path, destination)
    return 0, output + f"\nCopied {uf2_path} to {destination}\n", build_command + ["&&", "cp", str(uf2_path), str(destination)]


def send_bootsel_command(serial_port: str) -> str:
    stty_command = ["stty", "-f", serial_port, "115200", "raw", "-echo"]
    stty = subprocess.run(
        stty_command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        check=False,
    )
    output = ""
    if stty.returncode != 0:
        output += f"stty failed ({stty.returncode}): {stty.stdout}\n"

    with open(serial_port, "wb", buffering=0) as serial:
        serial.write(DEFAULT_BOOTSEL_COMMAND.encode("ascii"))
    return output + f"Sent BOOTSEL command to {serial_port}\n"


def touch_serial_bootloader(serial_port: str) -> str:
    stty_command = ["stty", "-f", serial_port, "1200"]
    stty = subprocess.run(
        stty_command,
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


def run_serial_command(
    serial_port: str,
    command: str,
    read_timeout_s: float,
    baud: int = 115200,
    append_newline: bool = True,
) -> tuple[int, str, list[str]]:
    stty_command = ["stty", "-f", serial_port, str(baud), "raw", "-echo"]
    stty = subprocess.run(
        stty_command,
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
    stty_command = ["stty", "-f", serial_port, str(baud), "raw", "-echo"]
    stty = subprocess.run(
        stty_command,
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


def run_auto_flash(state: FlashState, serial_port: str, volume: str | None) -> tuple[int, str, list[str]]:
    build_command = [state.pio, "run", "-e", state.env_name]
    proc = subprocess.run(
        build_command,
        cwd=state.project_dir,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=180,
        check=False,
    )
    output = proc.stdout
    if proc.returncode != 0:
        return proc.returncode, output[-MAX_OUTPUT_BYTES:], build_command

    uf2_path = state.project_dir / ".pio" / "build" / state.env_name / "firmware.uf2"
    if not uf2_path.is_file():
        return 1, output + f"\nFirmware UF2 not found: {uf2_path}\n", build_command

    volume_path = Path(volume or os.environ.get("HEXAFLAME_BOOTSEL_VOLUME") or DEFAULT_BOOTSEL_VOLUME)
    try:
      output += "\n" + send_bootsel_command(serial_port)
    except OSError as exc:
      output += f"\nFailed to send BOOTSEL command to {serial_port}: {exc}\n"

    if not wait_for_bootsel_volume(volume_path):
        output += touch_serial_bootloader(serial_port)

    if not wait_for_bootsel_volume(volume_path):
        volumes = sorted(str(path) for path in Path("/Volumes").glob("*")) if Path("/Volumes").is_dir() else []
        return (
            1,
            output
            + f"\nTimed out waiting for BOOTSEL volume: {volume_path}\n"
            + f"Mounted volumes: {', '.join(volumes) if volumes else '[none]'}\n",
            build_command,
        )

    destination = volume_path / uf2_path.name
    shutil.copyfile(uf2_path, destination)
    return 0, output + f"\nCopied {uf2_path} to {destination}\n", build_command + ["&&", "bootsel", "&&", "cp", str(uf2_path), str(destination)]


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
                "project_dir": str(state.project_dir),
                "env": state.env_name,
            },
        )

    def do_POST(self) -> None:
        if self.path not in {"/flash", "/serial", "/serial-stream", "/restart"}:
            json_response(self, 404, {"ok": False, "error": "not found"})
            return

        state: FlashState = self.server.flash_state  # type: ignore[attr-defined]
        if not request_is_authorized(self, state.token):
            json_response(self, 401, {"ok": False, "error": "unauthorized"})
            return

        try:
            payload = read_json_body(self)
        except (json.JSONDecodeError, ValueError) as exc:
            json_response(self, 400, {"ok": False, "error": str(exc)})
            return

        if self.path == "/restart":
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

            json_response(self, 200, {"ok": True, "restarting": True})
            restart_bridge_after_response()
            return

        if self.path in {"/serial", "/serial-stream"}:
            serial_port = str(
                payload.get("serial_port")
                or os.environ.get("HEXAFLAME_FLASH_SERIAL")
                or DEFAULT_SERIAL_PORT
            )
            command_text = str(payload.get("command") or "")
            if not command_text:
                json_response(self, 400, {"ok": False, "error": "missing serial command"})
                return
            read_timeout_s = float(payload.get("read_timeout_s") or 5.0)
            baud = int(payload.get("baud") or 115200)
            append_newline = bool(payload.get("append_newline", True))
            if self.path == "/serial-stream":
                self.handle_serial_stream(serial_port, command_text, read_timeout_s, baud, append_newline)
                return

            log_json_traffic("->", command_text)
            exit_code, output, command = run_serial_command(serial_port, command_text, read_timeout_s, baud, append_newline)
            log_json_traffic("<-", output)
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

        if payload.get("bootsel") is True:
            serial_port = None
        else:
            serial_port = str(
                payload.get("serial_port")
                or os.environ.get("HEXAFLAME_FLASH_SERIAL")
                or DEFAULT_SERIAL_PORT
            )

        acquired = state.lock.acquire(blocking=False)
        if not acquired:
            json_response(self, 409, {"ok": False, "error": "flash already in progress"})
            return

        try:
            log_bridge_event("FLASH")
            if payload.get("auto") is True:
                volume = payload.get("volume")
                exit_code, output, command = run_auto_flash(state, serial_port or DEFAULT_SERIAL_PORT, str(volume) if volume else None)
            elif payload.get("uf2") is True or payload.get("bootsel") is True:
                volume = payload.get("volume")
                exit_code, output, command = run_uf2_flash(state, str(volume) if volume else None)
            else:
                exit_code, output, command = run_flash(state, serial_port)
        except subprocess.TimeoutExpired as exc:
            output = (exc.stdout or "") + (exc.stderr or "")
            json_response(
                self,
                504,
                {
                    "ok": False,
                    "error": "flash timed out",
                    "output": output[-MAX_OUTPUT_BYTES:],
                },
            )
            return
        except FileNotFoundError:
            json_response(
                self,
                500,
                {
                    "ok": False,
                    "error": f"PlatformIO executable not found: {state.pio}",
                },
            )
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
        command_summary = ["serial", serial_port, command_text.rstrip("\n")]
        if not append_newline:
            command_summary.append("--no-newline")

        self.send_response(200)
        self.send_header("Content-Type", "application/x-ndjson")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()

        log_json_traffic("->", command_text)
        self.write_stream_event({"type": "start", "command": command_summary})

        stty_code, stty_output = prepare_serial_port(serial_port, baud)
        if stty_output:
            self.write_stream_event({"type": "output", "data": stty_output})
        if stty_code != 0:
            self.write_stream_event({"type": "end", "ok": False, "exit_code": 1, "command": command_summary})
            return

        fd, open_error = open_serial_fd(serial_port)
        if fd is None:
            self.write_stream_event({"type": "output", "data": open_error})
            self.write_stream_event({"type": "end", "ok": False, "exit_code": 1, "command": command_summary})
            return

        exit_code = 0
        try:
            write_error = write_serial_command(fd, command_text, append_newline)
            if write_error:
                self.write_stream_event({"type": "output", "data": write_error})
                exit_code = 1
            else:
                def on_line(line: str) -> None:
                    log_json_traffic("<-", line)
                    self.write_stream_event({"type": "output", "data": line + "\n"})

                def on_error(message: str) -> None:
                    self.write_stream_event({"type": "output", "data": message})

                exit_code, _ = read_serial_lines(fd, read_timeout_s, on_line, on_error)
        except BrokenPipeError:
            exit_code = 1
            return
        finally:
            os.close(fd)

        self.write_stream_event(
            {
                "type": "end",
                "ok": exit_code == 0,
                "exit_code": exit_code,
                "command": command_summary,
            }
        )

    def log_message(self, format: str, *args: Any) -> None:
        log_bridge_event(format % args)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run a macOS host firmware flash bridge.")
    parser.add_argument("--host", default=os.environ.get("HEXAFLAME_FLASH_HOST", "0.0.0.0"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("HEXAFLAME_FLASH_PORT", "8765")))
    parser.add_argument("--project-dir", type=Path, default=FIRMWARE_DIR)
    parser.add_argument("--env", default=os.environ.get("HEXAFLAME_FLASH_ENV", DEFAULT_ENV))
    parser.add_argument("--pio", default=os.environ.get("HEXAFLAME_FLASH_PIO", "pio"))
    parser.add_argument("--token", default=os.environ.get("HEXAFLAME_FLASH_TOKEN"))
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    state = FlashState(args.project_dir.resolve(), args.env, args.pio, args.token)
    server = ThreadingHTTPServer((args.host, args.port), FlashHandler)
    server.flash_state = state  # type: ignore[attr-defined]

    log_bridge_event(f"Flash bridge listening on http://{args.host}:{args.port}")
    log_bridge_event(f"Project: {state.project_dir}")
    log_bridge_event(f"Environment: {state.env_name}")
    log_bridge_event("Token auth: enabled" if state.token else "Token auth: disabled")
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
