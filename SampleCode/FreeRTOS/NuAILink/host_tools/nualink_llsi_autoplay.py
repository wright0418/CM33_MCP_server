#!/usr/bin/env python3
"""Control MCU-side LLSI autoplay mode over USB CDC JSON-RPC."""

from __future__ import annotations

import argparse
import json
import sys
import time
from typing import Any


PATTERNS = ("off", "solid", "chase", "gradient", "rainbow")
ACTIONS = ("start", "stop", "status")


def _load_serial_module() -> Any:
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError as exc:
        raise SystemExit(
            "pyserial is required: python -m pip install pyserial") from exc
    return serial


def _build_request(req_id: int,
                   action: str,
                   pattern: str,
                   count: int,
                   phase: int,
                   step: int,
                   interval_ms: int,
                   r: int,
                   g: int,
                   b: int) -> dict[str, Any]:
    arguments: dict[str, Any] = {
        "action": action,
    }

    if action == "start":
        arguments.update(
            {
                "pattern": pattern,
                "count": count,
                "phase": phase,
                "step": step,
                "interval_ms": interval_ms,
                "r": r,
                "g": g,
                "b": b,
            }
        )

    return {
        "jsonrpc": "2.0",
        "id": req_id,
        "method": "tools/call",
        "params": {
            "name": "llsi.autoplay",
            "arguments": arguments,
        },
    }


def _send_request(port: Any,
                  request: dict[str, Any],
                  timeout_s: float) -> dict[str, Any]:
    payload = json.dumps(request, separators=(
        ",", ":")).encode("utf-8") + b"\n"
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

        response = json.loads(text)
        if response.get("id") != request.get("id"):
            continue
        return response

    raise TimeoutError(
        f"No response for request id={request.get('id')} within {timeout_s:.1f}s")


def _print_structured_response(prefix: str, response: dict[str, Any]) -> int:
    if "error" in response:
        print(
            f"{prefix} ERROR: {json.dumps(response['error'], ensure_ascii=False)}")
        return 1

    result = response.get("result", {})
    structured = result.get("structuredContent", {}
                            ) if isinstance(result, dict) else {}
    if isinstance(structured, dict):
        print(f"{prefix} running={structured.get('running')} pattern={structured.get('pattern')} "
              f"count={structured.get('count')} phase={structured.get('phase')} "
              f"step={structured.get('step')} interval_ms={structured.get('interval_ms')} "
              f"rgb=({structured.get('r')},{structured.get('g')},{structured.get('b')})")
        return 0

    print(f"{prefix} {json.dumps(response, ensure_ascii=False, indent=2, sort_keys=True)}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Control MCU-side LLSI autoplay on NuAILink LLSI0(PB15)")
    parser.add_argument("port", help="Serial device, for example COM4")
    parser.add_argument("--action", choices=ACTIONS, default="status",
                        help="Autoplay action")
    parser.add_argument("--pattern", choices=PATTERNS, default="rainbow",
                        help="Pattern for start action")
    parser.add_argument("--count", type=int, default=10,
                        help="Pixel count (1..10)")
    parser.add_argument("--phase", type=int, default=0,
                        help="Initial phase (0..4095)")
    parser.add_argument("--step", type=int, default=1,
                        help="Phase increment per autoplay frame (1..4095)")
    parser.add_argument("--interval-ms", type=int, default=50,
                        help="Autoplay frame interval in milliseconds (10..5000)")
    parser.add_argument("--r", type=int, default=255,
                        help="Base red (0..255)")
    parser.add_argument("--g", type=int, default=0,
                        help="Base green (0..255)")
    parser.add_argument("--b", type=int, default=0,
                        help="Base blue (0..255)")
    parser.add_argument("--run-seconds", type=float, default=0.0,
                        help="After start, wait N seconds before querying status")
    parser.add_argument("--stop-after", action="store_true",
                        help="After run-seconds, send stop action")
    parser.add_argument("--baud", type=int, default=115200,
                        help="CDC baud metadata")
    parser.add_argument("--timeout", type=float, default=2.0,
                        help="Response timeout (seconds)")
    args = parser.parse_args()

    for name, value, lo, hi in (
        ("count", args.count, 1, 10),
        ("phase", args.phase, 0, 4095),
        ("step", args.step, 1, 4095),
        ("interval_ms", args.interval_ms, 10, 5000),
        ("r", args.r, 0, 255),
        ("g", args.g, 0, 255),
        ("b", args.b, 0, 255),
    ):
        if value < lo or value > hi:
            print(f"{name} must be in range [{lo}, {hi}]", file=sys.stderr)
            return 2

    if args.run_seconds < 0.0:
        print("run-seconds must be >= 0", file=sys.stderr)
        return 2

    serial = _load_serial_module()

    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as port:
        port.setDTR(True)
        port.setRTS(True)
        time.sleep(0.2)
        port.reset_input_buffer()
        port.reset_output_buffer()

        request = _build_request(1,
                                 args.action,
                                 args.pattern,
                                 args.count,
                                 args.phase,
                                 args.step,
                                 args.interval_ms,
                                 args.r,
                                 args.g,
                                 args.b)
        response = _send_request(port, request, args.timeout)
        rc = _print_structured_response(f"[{args.action}]", response)
        if rc != 0:
            return rc

        if (args.action == "start") and (args.run_seconds > 0.0):
            time.sleep(args.run_seconds)

            status_request = _build_request(2,
                                            "status",
                                            args.pattern,
                                            args.count,
                                            args.phase,
                                            args.step,
                                            args.interval_ms,
                                            args.r,
                                            args.g,
                                            args.b)
            status_response = _send_request(port, status_request, args.timeout)
            rc = _print_structured_response("[status]", status_response)
            if rc != 0:
                return rc

            if args.stop_after:
                stop_request = _build_request(3,
                                              "stop",
                                              args.pattern,
                                              args.count,
                                              args.phase,
                                              args.step,
                                              args.interval_ms,
                                              args.r,
                                              args.g,
                                              args.b)
                stop_response = _send_request(port, stop_request, args.timeout)
                rc = _print_structured_response("[stop]", stop_response)
                if rc != 0:
                    return rc

    return 0


if __name__ == "__main__":
    sys.exit(main())
