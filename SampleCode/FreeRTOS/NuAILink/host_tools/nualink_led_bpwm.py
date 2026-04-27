#!/usr/bin/env python3
"""Set NuAILink PC14 LED brightness via led.bpwm.set over USB CDC JSON-RPC."""

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
        raise SystemExit(
            "pyserial is required: python -m pip install pyserial") from exc
    return serial


def _build_request(duty: int) -> dict[str, Any]:
    return {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "tools/call",
        "params": {
            "name": "led.bpwm.set",
            "arguments": {"duty": duty},
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Set NuAILink PC14 LED brightness using BPWM duty")
    parser.add_argument(
        "port", help="Serial device, for example COM4 or /dev/ttyACM0")
    parser.add_argument(
        "duty", type=int, help="Brightness duty percent, 0..100")
    parser.add_argument("--baud", type=int, default=115200,
                        help="CDC baud metadata")
    parser.add_argument("--timeout", type=float, default=2.0,
                        help="Response timeout (seconds)")
    args = parser.parse_args()

    if args.duty < 0 or args.duty > 100:
        print("duty must be in range 0..100", file=sys.stderr)
        return 2

    serial = _load_serial_module()
    request = _build_request(args.duty)
    payload = json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n"

    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as port:
        port.setDTR(True)
        port.setRTS(True)
        time.sleep(0.2)
        port.reset_input_buffer()
        port.reset_output_buffer()

        port.write(payload)
        port.flush()

        deadline = time.monotonic() + args.timeout
        while time.monotonic() < deadline:
            line = port.readline()
            if not line:
                continue

            text = line.decode("utf-8", errors="replace").strip()
            if not text:
                continue

            response = json.loads(text)
            print(json.dumps(response, indent=2, sort_keys=True))
            return 1 if "error" in response else 0

    print(
        f"No response on {args.port} within {args.timeout:.1f}s", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
