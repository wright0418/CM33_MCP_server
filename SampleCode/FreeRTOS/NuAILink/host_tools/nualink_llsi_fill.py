#!/usr/bin/env python3
"""Fill NuAILink LLSI WS2812 pixels over USB CDC JSON-RPC."""

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


def _build_request(req_id: int, r: int, g: int, b: int, count: int) -> dict[str, Any]:
    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "method": "tools/call",
        "params": {
            "name": "llsi.fill",
            "arguments": {"r": r, "g": g, "b": b, "count": count},
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fill first N WS2812 pixels on NuAILink LLSI(PB15)")
    parser.add_argument("port", help="Serial device, for example COM4")
    parser.add_argument("r", type=int, help="Red (0..255)")
    parser.add_argument("g", type=int, help="Green (0..255)")
    parser.add_argument("b", type=int, help="Blue (0..255)")
    parser.add_argument("--count", type=int, default=10,
                        help="Pixel count (1..10)")
    parser.add_argument("--repeat", type=int, default=1,
                        help="Repeat count")
    parser.add_argument("--interval", type=float, default=0.2,
                        help="Interval between requests (seconds)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="CDC baud metadata")
    parser.add_argument("--timeout", type=float, default=2.0,
                        help="Response timeout (seconds)")
    args = parser.parse_args()

    for name, value, lo, hi in (
        ("r", args.r, 0, 255),
        ("g", args.g, 0, 255),
        ("b", args.b, 0, 255),
        ("count", args.count, 1, 10),
    ):
        if value < lo or value > hi:
            print(f"{name} must be in range [{lo}, {hi}]", file=sys.stderr)
            return 2

    if args.repeat < 1:
        print("repeat must be >= 1", file=sys.stderr)
        return 2

    serial = _load_serial_module()

    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as port:
        port.setDTR(True)
        port.setRTS(True)
        time.sleep(0.2)
        port.reset_input_buffer()
        port.reset_output_buffer()

        for i in range(args.repeat):
            req_id = i + 1
            request = _build_request(req_id, args.r, args.g, args.b, args.count)
            payload = json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n"

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
                if response.get("id") != req_id:
                    continue

                if "error" in response:
                    print(json.dumps(response, indent=2, sort_keys=True))
                    return 1

                result = response.get("result", {})
                structured = result.get("structuredContent", {}) if isinstance(result, dict) else {}
                if isinstance(structured, dict):
                    print(f"[{i + 1}] pin={structured.get('pin')} count={structured.get('count')} "
                          f"rgb=({structured.get('r')},{structured.get('g')},{structured.get('b')})")
                else:
                    print(json.dumps(response, indent=2, sort_keys=True))
                break
            else:
                print(
                    f"No response on {args.port} within {args.timeout:.1f}s", file=sys.stderr)
                return 2

            if (i + 1) < args.repeat:
                time.sleep(args.interval)

    return 0


if __name__ == "__main__":
    sys.exit(main())
