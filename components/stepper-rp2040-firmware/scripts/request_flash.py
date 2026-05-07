#!/usr/bin/env python3
"""Trigger the macOS flash bridge from inside Docker."""

from __future__ import annotations

import argparse
import json
import os
import http.client
import sys
import socket
import urllib.error
import urllib.request


DEFAULT_BASE_URL = "http://host.docker.internal:8765"
DEFAULT_FLASH_URL = f"{DEFAULT_BASE_URL}/flash"
DEFAULT_HEALTH_URL = f"{DEFAULT_BASE_URL}/health"
DEFAULT_SERIAL_URL = f"{DEFAULT_BASE_URL}/serial"
URL_OPENER = urllib.request.build_opener(urllib.request.ProxyHandler({}))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Request a firmware flash from the host bridge.")
    parser.add_argument("--health", action="store_true", help="Only verify bridge reachability.")
    parser.add_argument("--serial-command", help="Send a command to the board serial port through the bridge.")
    parser.add_argument("--url", default=os.environ.get("HEXAFLAME_FLASH_URL"))
    parser.add_argument(
        "--serial-url",
        default=os.environ.get("HEXAFLAME_FLASH_SERIAL_URL", DEFAULT_SERIAL_URL),
    )
    parser.add_argument(
        "--health-url",
        default=os.environ.get("HEXAFLAME_FLASH_HEALTH_URL", DEFAULT_HEALTH_URL),
    )
    parser.add_argument("--serial-port", default=os.environ.get("HEXAFLAME_FLASH_SERIAL"))
    parser.add_argument("--serial-baud", type=int, default=115200, help="Baud rate for --serial-command.")
    parser.add_argument("--no-newline", action="store_true", help="Do not append a newline to --serial-command.")
    parser.add_argument("--bootsel", action="store_true", help="Flash a board already in BOOTSEL mode via UF2 copy.")
    parser.add_argument("--uf2", action="store_true", help="Build and copy firmware.uf2 to a mounted BOOTSEL volume.")
    parser.add_argument("--auto", action="store_true", help="Ask running firmware to enter BOOTSEL, then copy UF2.")
    parser.add_argument("--volume", default=os.environ.get("HEXAFLAME_BOOTSEL_VOLUME"))
    parser.add_argument("--token", default=os.environ.get("HEXAFLAME_FLASH_TOKEN"))
    parser.add_argument("--read-timeout", type=float, default=5.0)
    parser.add_argument("--timeout", type=float, default=10.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
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
            args.serial_url,
            data=json.dumps(payload).encode("utf-8"),
            headers={"Content-Type": "application/json"},
            method="POST",
        )
        if args.token:
            request.add_header("Authorization", f"Bearer {args.token}")

        try:
            with URL_OPENER.open(request, timeout=max(args.timeout, args.read_timeout + 5.0)) as response:
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

    payload: dict[str, str | bool] = {}
    if args.bootsel:
        payload["bootsel"] = True
    if args.uf2:
        payload["uf2"] = True
    if args.auto:
        payload["auto"] = True
    if args.volume:
        payload["volume"] = args.volume
    if args.serial_port:
        payload["serial_port"] = args.serial_port

    body = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        args.url or DEFAULT_FLASH_URL,
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    if args.token:
        request.add_header("Authorization", f"Bearer {args.token}")

    try:
        with URL_OPENER.open(request, timeout=max(args.timeout, 240.0)) as response:
            response_body = response.read().decode("utf-8", errors="replace")
            print(response_body)
            data = json.loads(response_body)
            return 0 if data.get("ok") else 1
    except urllib.error.HTTPError as exc:
        print(exc.read().decode("utf-8", errors="replace"), file=sys.stderr)
        return 1
    except (TimeoutError, socket.timeout, http.client.RemoteDisconnected, urllib.error.URLError) as exc:
        print(f"flash bridge request failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
