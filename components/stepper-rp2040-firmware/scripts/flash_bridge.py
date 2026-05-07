#!/usr/bin/env python3
"""Host-side firmware flash bridge for Docker-based workflows.

Run this on macOS, where the RP2040 serial device is visible. A Docker
container can then POST to /flash via host.docker.internal to make the host run
the PlatformIO upload command.
"""

from __future__ import annotations

import argparse
import json
import os
import select
import shutil
import subprocess
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


def run_serial_command(serial_port: str, command: str, read_timeout_s: float, baud: int = 115200) -> tuple[int, str, list[str]]:
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
        drain_deadline = time.monotonic() + 0.25
        try:
            while time.monotonic() < drain_deadline:
                readable, _, _ = select.select([fd], [], [], 0.02)
                if not readable:
                    continue
                os.read(fd, 4096)
        except BlockingIOError:
            pass

        try:
            os.write(fd, command.encode("utf-8"))
            if not command.endswith("\n"):
                os.write(fd, b"\n")
        except OSError as exc:
            return 1, output + f"Failed to write to {serial_port}: {exc}\n", ["serial", serial_port, command.rstrip("\n")]

        chunks: list[bytes] = []
        deadline = time.monotonic() + read_timeout_s
        while time.monotonic() < deadline:
            readable, _, _ = select.select([fd], [], [], 0.1)
            if not readable:
                continue
            try:
                chunk = os.read(fd, 4096)
            except BlockingIOError:
                continue
            if chunk:
                chunks.append(chunk)

        output += b"".join(chunks).decode("utf-8", errors="replace")
        return 0, output[-MAX_OUTPUT_BYTES:], ["serial", serial_port, command.rstrip("\n")]
    finally:
        os.close(fd)


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
        if self.path not in {"/flash", "/serial"}:
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

        if self.path == "/serial":
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
            exit_code, output, command = run_serial_command(serial_port, command_text, read_timeout_s)
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

    def log_message(self, format: str, *args: Any) -> None:
        print(f"{self.address_string()} - {format % args}")


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

    print(f"Flash bridge listening on http://{args.host}:{args.port}")
    print(f"Project: {state.project_dir}")
    print(f"Environment: {state.env_name}")
    print("Token auth: enabled" if state.token else "Token auth: disabled")
    server.serve_forever()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
