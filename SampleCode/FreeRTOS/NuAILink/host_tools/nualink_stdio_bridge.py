#!/usr/bin/env python3
"""Bridge stdio JSON-RPC and NuAILink USB CDC serial.

Design goals:
- stdin -> serial: forward each JSON line to NuAILink over CDC.
- serial -> stdout: forward each JSON line from NuAILink to stdio client.
- never print diagnostics to stdout (stdout is reserved for JSON-RPC frames).

Typical usage (Linux):
    python nualink_stdio_bridge.py /dev/ttyACM0

Typical usage (Windows):
    python nualink_stdio_bridge.py COM4
"""

from __future__ import annotations

import argparse
import json
import sys
import threading
import time
from dataclasses import dataclass
from typing import Any


def _load_serial_module() -> Any:
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError as exc:
        raise SystemExit(
            "pyserial is required: python -m pip install pyserial") from exc
    return serial


def _json_object_or_none(text: str) -> dict[str, Any] | None:
    try:
        value = json.loads(text)
    except json.JSONDecodeError:
        return None

    if not isinstance(value, dict):
        return None
    return value


@dataclass
class BridgeStats:
    stdin_frames: int = 0
    stdout_frames: int = 0
    stdin_invalid_json: int = 0
    serial_invalid_json: int = 0
    serial_decode_errors: int = 0


class StdioSerialBridge:
    def __init__(self,
                 port: Any,
                 strict_json: bool,
                 verbose: bool,
                 drain_after_eof_s: float) -> None:
        self._port = port
        self._strict_json = strict_json
        self._verbose = verbose
        self._drain_after_eof_s = max(drain_after_eof_s, 0.0)
        self._stop_event = threading.Event()
        self._serial_reader = threading.Thread(target=self._serial_to_stdout_loop,
                                               name="serial-to-stdout",
                                               daemon=True)
        self._write_lock = threading.Lock()
        self.stats = BridgeStats()

    def _log(self, message: str) -> None:
        if self._verbose:
            print(f"[bridge] {message}", file=sys.stderr, flush=True)

    def _serial_to_stdout_loop(self) -> None:
        rx_buffer = bytearray()

        while not self._stop_event.is_set():
            chunk_size = int(getattr(self._port, "in_waiting", 0) or 0)
            if chunk_size <= 0:
                chunk_size = 64

            chunk = self._port.read(chunk_size)
            if not chunk:
                continue

            rx_buffer.extend(chunk)

            while True:
                newline_index = rx_buffer.find(b"\n")
                if newline_index < 0:
                    break

                line = bytes(rx_buffer[:newline_index])
                del rx_buffer[: newline_index + 1]

                try:
                    text = line.decode("utf-8", errors="strict").strip()
                except UnicodeDecodeError:
                    self.stats.serial_decode_errors += 1
                    self._log("dropped serial frame due to UTF-8 decode error")
                    continue

                if not text:
                    continue

                if self._strict_json and (_json_object_or_none(text) is None):
                    self.stats.serial_invalid_json += 1
                    self._log(f"dropped non-JSON frame from serial: {text}")
                    continue

                sys.stdout.write(text + "\n")
                sys.stdout.flush()
                self.stats.stdout_frames += 1

    def _stdin_to_serial_loop(self) -> None:
        for raw_line in sys.stdin:
            if self._stop_event.is_set():
                break

            text = raw_line.strip()
            if not text:
                continue

            if self._strict_json and (_json_object_or_none(text) is None):
                self.stats.stdin_invalid_json += 1
                self._log(f"dropped non-JSON frame from stdin: {text}")
                continue

            payload = text.encode("utf-8") + b"\n"
            with self._write_lock:
                self._port.write(payload)
                self._port.flush()
            self.stats.stdin_frames += 1

    def run(self) -> int:
        self._serial_reader.start()

        exit_code = 0
        try:
            self._stdin_to_serial_loop()
            if self._drain_after_eof_s > 0.0:
                self._log(
                    f"stdin closed, draining serial for {self._drain_after_eof_s:.3f}s")
                time.sleep(self._drain_after_eof_s)
        except KeyboardInterrupt:
            self._log("interrupted by user")
        except Exception as exc:  # pragma: no cover - hardware/runtime path
            self._log(f"fatal error: {exc}")
            exit_code = 1
        finally:
            self._stop_event.set()
            self._serial_reader.join(timeout=1.0)

        self._log(
            "summary: "
            f"stdin_frames={self.stats.stdin_frames} "
            f"stdout_frames={self.stats.stdout_frames} "
            f"stdin_invalid_json={self.stats.stdin_invalid_json} "
            f"serial_invalid_json={self.stats.serial_invalid_json} "
            f"serial_decode_errors={self.stats.serial_decode_errors}"
        )
        return exit_code


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "port", help="Serial device, e.g. COM4 or /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200,
                        help="CDC baud metadata")
    parser.add_argument("--timeout", type=float, default=2.0,
                        help="Serial write timeout (seconds)")
    parser.add_argument("--startup-delay", type=float, default=0.25,
                        help="Delay after opening port before forwarding (seconds)")
    parser.add_argument("--strict-json", action="store_true",
                        help="Drop frames that are not valid JSON objects")
    parser.add_argument("--verbose", action="store_true",
                        help="Print diagnostics to stderr")
    parser.add_argument("--drain-after-eof", type=float, default=0.25,
                        help="Seconds to keep bridge alive after stdin EOF for one-shot pipelines")
    args = parser.parse_args()

    serial = _load_serial_module()

    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as port:
        port.setDTR(True)
        port.setRTS(True)
        time.sleep(max(args.startup_delay, 0.0))
        port.reset_input_buffer()
        port.reset_output_buffer()

        bridge = StdioSerialBridge(port=port,
                                   strict_json=args.strict_json,
                                   verbose=args.verbose,
                                   drain_after_eof_s=args.drain_after_eof)
        return bridge.run()


if __name__ == "__main__":
    sys.exit(main())
