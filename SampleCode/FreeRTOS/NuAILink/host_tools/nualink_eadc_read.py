#!/usr/bin/env python3
"""Read NuAILink EADC channel 8/9 over USB CDC JSON-RPC."""

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


def _build_request(req_id: int, channel: int) -> dict[str, Any]:
    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "method": "tools/call",
        "params": {
            "name": "eadc.read",
            "arguments": {"channel": channel},
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Read EADC channel 8(PB8) or 9(PB9) via NuAILink")
    parser.add_argument("port", help="Serial device, for example COM4")
    parser.add_argument("channel", type=int, choices=[8, 9],
                        help="EADC channel (8 or 9)")
    parser.add_argument("--count", type=int, default=1,
                        help="Number of reads")
    parser.add_argument("--interval", type=float, default=0.2,
                        help="Interval between reads in seconds")
    parser.add_argument("--baud", type=int, default=115200,
                        help="CDC baud metadata")
    parser.add_argument("--timeout", type=float, default=2.0,
                        help="Response timeout (seconds)")
    args = parser.parse_args()

    if args.count < 1:
        print("count must be >= 1", file=sys.stderr)
        return 2

    serial = _load_serial_module()

    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as port:
        port.setDTR(True)
        port.setRTS(True)
        time.sleep(0.2)
        port.reset_input_buffer()
        port.reset_output_buffer()

        for i in range(args.count):
            req_id = i + 1
            request = _build_request(req_id, args.channel)
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
                result = response.get("result", {})
                structured = result.get("structuredContent", {}) if isinstance(
                    result, dict) else {}

                if isinstance(structured, dict):
                    ch = structured.get("channel")
                    raw = structured.get("raw")
                    mv = structured.get("mV")
                    pin = structured.get("pin")
                    if ch is not None and raw is not None and mv is not None:
                        print(f"[{i + 1}] CH{ch} ({pin}): raw={raw}, {mv} mV")
                    else:
                        print(json.dumps(response, indent=2, sort_keys=True))
                else:
                    print(json.dumps(response, indent=2, sort_keys=True))

                if "error" in response:
                    return 1
                break
            else:
                print(
                    f"No response on {args.port} within {args.timeout:.1f}s", file=sys.stderr)
                return 2

            if (i + 1) < args.count:
                time.sleep(args.interval)

    return 0


if __name__ == "__main__":
    sys.exit(main())
