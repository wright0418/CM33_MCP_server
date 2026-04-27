#!/usr/bin/env python3
"""Listen for NuAILink button.event JSON-RPC notifications on USB CDC.

Usage:
    python nualink_button_listen.py COM4 [--seconds 30]

Press the PB14 button on the board; each press/release is reported as a
"button.event" notification (no JSON-RPC id field).  Also runs button.read
once at startup to show the current state.
"""

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


def _send_request(port: Any, request: dict[str, Any]) -> None:
    payload = json.dumps(request, separators=(
        ",", ":")).encode("utf-8") + b"\n"
    port.write(payload)
    port.flush()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="Serial device, e.g. COM4")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=float, default=30.0,
                        help="How long to listen (seconds)")
    args = parser.parse_args()

    serial = _load_serial_module()

    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=2.0) as port:
        port.setDTR(True)
        port.setRTS(True)
        time.sleep(0.2)
        port.reset_input_buffer()
        port.reset_output_buffer()

        # Probe current state.
        _send_request(port, {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "tools/call",
            "params": {"name": "button.read", "arguments": {}},
        })

        deadline = time.monotonic() + args.seconds
        print(f"Listening for button.event on {args.port} "
              f"for {args.seconds:.1f}s (Ctrl+C to stop)...")

        events = 0
        try:
            while time.monotonic() < deadline:
                line = port.readline()
                if not line:
                    continue
                text = line.decode("utf-8", errors="replace").strip()
                if not text:
                    continue
                try:
                    msg = json.loads(text)
                except json.JSONDecodeError:
                    print(f"<non-json> {text}")
                    continue

                if msg.get("method") == "button.event":
                    events += 1
                    pressed = msg.get("params", {}).get("pressed")
                    print(f"[event #{events}] pressed={pressed}")
                elif "result" in msg:
                    sc = msg.get("result", {}).get("structuredContent", {})
                    print(f"[reply] button.read -> {sc}")
                else:
                    print(f"[other] {msg}")
        except KeyboardInterrupt:
            print("interrupted by user")

        print(f"done, captured {events} button events")
    return 0


if __name__ == "__main__":
    sys.exit(main())
