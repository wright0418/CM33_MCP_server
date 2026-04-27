#!/usr/bin/env python3
"""Render LLSI pattern frames (including animation stepping) over USB CDC JSON-RPC."""

from __future__ import annotations

import argparse
import json
import sys
import time
from typing import Any


PATTERNS = ("off", "solid", "chase", "gradient", "rainbow")


def _load_serial_module() -> Any:
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError as exc:
        raise SystemExit(
            "pyserial is required: python -m pip install pyserial") from exc
    return serial


def _build_request(req_id: int,
                   pattern: str,
                   count: int,
                   phase: int,
                   r: int,
                   g: int,
                   b: int) -> dict[str, Any]:
    arguments: dict[str, Any] = {
        "pattern": pattern,
        "count": count,
        "phase": phase,
    }

    # RGB is accepted for all patterns; it is used by solid/chase/gradient.
    arguments["r"] = r
    arguments["g"] = g
    arguments["b"] = b

    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "method": "tools/call",
        "params": {
            "name": "llsi.pattern",
            "arguments": arguments,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Render LLSI pattern frames on NuAILink LLSI0(PB15)")
    parser.add_argument("port", help="Serial device, for example COM4")
    parser.add_argument("--pattern", choices=PATTERNS, default="rainbow",
                        help="Pattern name")
    parser.add_argument("--count", type=int, default=10,
                        help="Pixel count (1..10)")
    parser.add_argument("--phase", type=int, default=0,
                        help="Initial phase (0..4095)")
    parser.add_argument("--frames", type=int, default=1,
                        help="Number of frames to send")
    parser.add_argument("--step", type=int, default=1,
                        help="Phase increment per frame")
    parser.add_argument("--interval", type=float, default=0.05,
                        help="Delay between frames (seconds)")
    parser.add_argument("--r", type=int, default=255,
                        help="Base red (0..255)")
    parser.add_argument("--g", type=int, default=0,
                        help="Base green (0..255)")
    parser.add_argument("--b", type=int, default=0,
                        help="Base blue (0..255)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="CDC baud metadata")
    parser.add_argument("--timeout", type=float, default=2.0,
                        help="Response timeout (seconds)")
    args = parser.parse_args()

    for name, value, lo, hi in (
        ("count", args.count, 1, 10),
        ("phase", args.phase, 0, 4095),
        ("frames", args.frames, 1, 5000),
        ("r", args.r, 0, 255),
        ("g", args.g, 0, 255),
        ("b", args.b, 0, 255),
    ):
        if value < lo or value > hi:
            print(f"{name} must be in range [{lo}, {hi}]", file=sys.stderr)
            return 2

    serial = _load_serial_module()

    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as port:
        port.setDTR(True)
        port.setRTS(True)
        time.sleep(0.2)
        port.reset_input_buffer()
        port.reset_output_buffer()

        for i in range(args.frames):
            req_id = i + 1
            phase = (args.phase + i * args.step) % 4096
            request = _build_request(req_id,
                                     args.pattern,
                                     args.count,
                                     phase,
                                     args.r,
                                     args.g,
                                     args.b)
            payload = json.dumps(request, separators=(
                ",", ":")).encode("utf-8") + b"\n"

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
                structured = result.get("structuredContent", {}) if isinstance(
                    result, dict) else {}
                if isinstance(structured, dict):
                    print(f"[{i + 1}] pattern={structured.get('pattern')} count={structured.get('count')} "
                          f"phase={structured.get('phase')} rgb=({structured.get('r')},"
                          f"{structured.get('g')},{structured.get('b')})")
                else:
                    print(json.dumps(response, indent=2, sort_keys=True))
                break
            else:
                print(
                    f"No response on {args.port} within {args.timeout:.1f}s", file=sys.stderr)
                return 2

            if (i + 1) < args.frames:
                time.sleep(args.interval)

    return 0


if __name__ == "__main__":
    sys.exit(main())
