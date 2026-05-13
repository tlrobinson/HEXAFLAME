#!/usr/bin/env python3
"""Trigger a HEXAFLAME flash/serial bridge."""

from __future__ import annotations

import argparse
import json
import os
import http.client
import subprocess
import sys
import socket
import urllib.error
import urllib.request
import shutil


DEFAULT_BASE_URL = "http://host.docker.internal:8765"
DEFAULT_FLASH_UF2_URL = f"{DEFAULT_BASE_URL}/flash-uf2"
DEFAULT_HEALTH_URL = f"{DEFAULT_BASE_URL}/health"
DEFAULT_RESTART_URL = f"{DEFAULT_BASE_URL}/restart"
DEFAULT_SERIAL_URL = f"{DEFAULT_BASE_URL}/serial"
DEFAULT_SERIAL_STREAM_URL = f"{DEFAULT_BASE_URL}/serial-stream"
URL_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))
FIRMWARE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ROOT_DIR = os.path.dirname(os.path.dirname(FIRMWARE_DIR))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Request a firmware flash from the host bridge.")
    parser.add_argument(
        "--base-url",
        default=os.environ.get("HEXAFLAME_FLASH_BASE_URL", DEFAULT_BASE_URL),
        help="Base URL for the bridge. Defaults to host.docker.internal:8765.",
    )
    parser.add_argument("--health", action="store_true", help="Only verify bridge reachability.")
    parser.add_argument("--restart-bridge", action="store_true", help="Ask the bridge to restart itself with current code.")
    parser.add_argument("--serial-command", help="Send a command to the board serial port through the bridge.")
    parser.add_argument("--stream", action="store_true", help="Stream serial command output as it arrives.")
    parser.add_argument("--uf2-url", default=os.environ.get("HEXAFLAME_FLASH_UF2_URL"))
    parser.add_argument(
        "--serial-url",
        default=os.environ.get("HEXAFLAME_FLASH_SERIAL_URL", DEFAULT_SERIAL_URL),
    )
    parser.add_argument(
        "--serial-stream-url",
        default=os.environ.get("HEXAFLAME_FLASH_SERIAL_STREAM_URL", DEFAULT_SERIAL_STREAM_URL),
    )
    parser.add_argument(
        "--health-url",
        default=os.environ.get("HEXAFLAME_FLASH_HEALTH_URL", DEFAULT_HEALTH_URL),
    )
    parser.add_argument(
        "--restart-url",
        default=os.environ.get("HEXAFLAME_FLASH_RESTART_URL", DEFAULT_RESTART_URL),
    )
    parser.add_argument("--serial-port", default=os.environ.get("HEXAFLAME_FLASH_SERIAL"))
    parser.add_argument("--serial-baud", type=int, default=115200, help="Baud rate for --serial-command.")
    parser.add_argument("--no-newline", action="store_true", help="Do not append a newline to --serial-command.")
    parser.add_argument("--uf2-file", help="Upload an already-built UF2 file to the bridge and flash it.")
    parser.add_argument("--build-uf2", action="store_true", help="Build firmware.uf2 locally before uploading it to the bridge.")
    parser.add_argument("--pio", default=os.environ.get("HEXAFLAME_LOCAL_PIO"), help="PlatformIO executable for --build-uf2.")
    parser.add_argument("--env", default=os.environ.get("HEXAFLAME_FLASH_ENV", "rp2040_v3_codex"), help="PlatformIO environment for --build-uf2.")
    parser.add_argument("--volume", default=os.environ.get("HEXAFLAME_BOOTSEL_VOLUME"))
    parser.add_argument("--token", default=os.environ.get("HEXAFLAME_FLASH_TOKEN"))
    parser.add_argument("--read-timeout", type=float, default=5.0)
    parser.add_argument("--timeout", type=float, default=10.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    base_url = args.base_url.rstrip("/")
    if args.uf2_url is None:
        args.uf2_url = f"{base_url}/flash-uf2"
    if args.serial_url == DEFAULT_SERIAL_URL and os.environ.get("HEXAFLAME_FLASH_SERIAL_URL") is None:
        args.serial_url = f"{base_url}/serial"
    if args.serial_stream_url == DEFAULT_SERIAL_STREAM_URL and os.environ.get("HEXAFLAME_FLASH_SERIAL_STREAM_URL") is None:
        args.serial_stream_url = f"{base_url}/serial-stream"
    if args.health_url == DEFAULT_HEALTH_URL and os.environ.get("HEXAFLAME_FLASH_HEALTH_URL") is None:
        args.health_url = f"{base_url}/health"
    if args.restart_url == DEFAULT_RESTART_URL and os.environ.get("HEXAFLAME_FLASH_RESTART_URL") is None:
        args.restart_url = f"{base_url}/restart"

    if args.health:
        try:
            with URL_OPENER.open(args.health_url, timeout=args.timeout) as response:
                print(response.read().decode("utf-8", errors="replace"))
                return 0
        except urllib.error.HTTPError as exc:
            print(exc.read().decode("utf-8", errors="replace"), file=sys.stderr)
            return 1
        except (TimeoutError, socket.timeout, http.client.RemoteDisconnected, urllib.error.URLError) as exc:
            print(f"flash bridge health check failed: {exc}", file=sys.stderr)
            return 1

    if args.restart_bridge:
        request = urllib.request.Request(
            args.restart_url,
            data=b"{}",
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        if args.token:
            request.add_header("Authorization", f"Bearer {args.token}")

        try:
            with URL_OPENER.open(request, timeout=args.timeout) as response:
                response_body = response.read().decode("utf-8", errors="replace")
                print(response_body)
                data = json.loads(response_body)
                return 0 if data.get("ok") else 1
        except urllib.error.HTTPError as exc:
            print(exc.read().decode("utf-8", errors="replace"), file=sys.stderr)
            return 1
        except (TimeoutError, socket.timeout, http.client.RemoteDisconnected, urllib.error.URLError) as exc:
            print(f"flash bridge restart request failed: {exc}", file=sys.stderr)
            return 1

    if args.serial_command:
        payload: dict[str, str | float | int | bool] = {
            "command": args.serial_command,
            "read_timeout_s": args.read_timeout,
            "baud": args.serial_baud,
            "append_newline": not args.no_newline,
        }
        if args.serial_port:
            payload["serial_port"] = args.serial_port

        request = urllib.request.Request(
            args.serial_stream_url if args.stream else args.serial_url,
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        if args.token:
            request.add_header("Authorization", f"Bearer {args.token}")

        try:
            with URL_OPENER.open(request, timeout=max(args.timeout, args.read_timeout + 5.0)) as response:
                if args.stream:
                    ok = False
                    output_buffer = ""
                    for raw_line in response:
                        if not raw_line:
                            continue
                        try:
                            event = json.loads(raw_line.decode("utf-8", errors="replace"))
                        except json.JSONDecodeError:
                            print(raw_line.decode("utf-8", errors="replace"), end="")
                            continue
                        event_type = event.get("type")
                        if event_type == "output":
                            output_buffer += str(event.get("data", ""))
                            while "\n" in output_buffer:
                                line, output_buffer = output_buffer.split("\n", 1)
                                print(line, flush=True)
                        elif event_type == "end":
                            ok = bool(event.get("ok"))
                    if output_buffer.strip():
                        print(output_buffer.strip(), flush=True)
                    return 0 if ok else 1

                response_body = response.read().decode("utf-8", errors="replace")
                print(response_body)
                data = json.loads(response_body)
                return 0 if data.get("ok") else 1
        except urllib.error.HTTPError as exc:
            print(exc.read().decode("utf-8", errors="replace"), file=sys.stderr)
            return 1
        except (TimeoutError, socket.timeout, http.client.RemoteDisconnected, urllib.error.URLError) as exc:
            print(f"flash bridge serial request failed: {exc}", file=sys.stderr)
            return 1

    if args.build_uf2:
        pio = args.pio or shutil.which("pio")
        if pio is None:
            candidate = os.path.join(ROOT_DIR, ".venv-platformio", "bin", "pio")
            if os.path.isfile(candidate):
                pio = candidate
        if pio is None:
            print("PlatformIO executable not found. Install it or pass --pio.", file=sys.stderr)
            return 1
        proc = subprocess.run(
            [pio, "run", "-e", args.env],
            cwd=FIRMWARE_DIR,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
        print(proc.stdout, end="")
        if proc.returncode != 0:
            return proc.returncode
        args.uf2_file = os.path.join(
            FIRMWARE_DIR,
            ".pio",
            "build",
            args.env,
            "firmware.uf2",
        )

    if args.uf2_file:
        try:
            with open(args.uf2_file, "rb") as handle:
                body = handle.read()
        except OSError as exc:
            print(f"failed to read UF2 file: {exc}", file=sys.stderr)
            return 1

        headers = {
            "Content-Type": "application/octet-stream",
            "X-Filename": os.path.basename(args.uf2_file),
        }
        if args.serial_port:
            headers["X-Serial-Port"] = args.serial_port
        if args.volume:
            headers["X-Bootsel-Volume"] = args.volume
        request = urllib.request.Request(args.uf2_url, data=body, headers=headers, method="POST")
        if args.token:
            request.add_header("Authorization", f"Bearer {args.token}")
        try:
            with URL_OPENER.open(request, timeout=max(args.timeout, 120.0)) as response:
                response_body = response.read().decode("utf-8", errors="replace")
                print(response_body)
                data = json.loads(response_body)
                return 0 if data.get("ok") else 1
        except urllib.error.HTTPError as exc:
            print(exc.read().decode("utf-8", errors="replace"), file=sys.stderr)
            return 1
        except (TimeoutError, socket.timeout, http.client.RemoteDisconnected, urllib.error.URLError) as exc:
            print(f"flash UF2 upload failed: {exc}", file=sys.stderr)
            return 1

    print("no action requested; use --serial-command, --uf2-file, --build-uf2, --health, or --restart-bridge", file=sys.stderr)
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
