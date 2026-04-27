#!/usr/bin/env python3
"""NuAILink CDC serial JSON-RPC smoke test."""

from __future__ import annotations

import argparse
import json
import sys
import time
from typing import Any


def _load_serial_module() -> Any:
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError as exc:
        raise SystemExit("pyserial is required: python -m pip install pyserial") from exc
    return serial


def _send_request(port: Any, request: dict[str, Any], timeout_s: float) -> dict[str, Any]:
    payload = json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n"
    deadline = time.monotonic() + timeout_s

    port.write(payload)
    port.flush()

    while time.monotonic() < deadline:
        line = port.readline()
        if not line:
            continue
        text = line.decode("utf-8", errors="replace").strip()
        if not text:
            continue
        return json.loads(text)

    raise TimeoutError(f"No response for method {request.get('method')}")


def _print_response(name: str, response: dict[str, Any]) -> None:
    print(f"## {name}")
    print(json.dumps(response, indent=2, sort_keys=True))


def main() -> int:
    parser = argparse.ArgumentParser(description="Smoke-test NuAILink over USB CDC serial.")
    parser.add_argument("port", help="Serial device, for example /dev/ttyACM0 or COM7")
    parser.add_argument("--baud", type=int, default=115200, help="CDC baud rate metadata")
    parser.add_argument("--timeout", type=float, default=2.0, help="Response timeout in seconds")
    parser.add_argument("--skip-led", action="store_true", help="Do not send led.set requests")
    args = parser.parse_args()

    serial = _load_serial_module()

    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as port:
        port.reset_input_buffer()
        port.reset_output_buffer()

        requests = [
            ("initialize", {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}),
            ("ping", {"jsonrpc": "2.0", "id": 2, "method": "ping"}),
            ("tools/list", {"jsonrpc": "2.0", "id": 3, "method": "tools/list"}),
            ("system.info", {"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "system.info", "arguments": {}}}),
        ]

        if not args.skip_led:
            requests.extend(
                [
                    ("led.set on", {"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {"name": "led.set", "arguments": {"on": True}}}),
                    ("led.set off", {"jsonrpc": "2.0", "id": 6, "method": "tools/call", "params": {"name": "led.set", "arguments": {"on": False}}}),
                ]
            )

        for name, request in requests:
            response = _send_request(port, request, args.timeout)
            _print_response(name, response)

    return 0


if __name__ == "__main__":
    sys.exit(main())