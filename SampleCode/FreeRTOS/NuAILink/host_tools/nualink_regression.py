#!/usr/bin/env python3
"""NuAILink USB CDC one-click regression test.

Coverage:
- JSON-RPC handshake: initialize
- Core method: ping
- Tool discovery: tools/list
- Tool execution: system.info, led.set on/off (optional)
- Stress: ping loop with latency stats
"""

from __future__ import annotations

import argparse
import json
import statistics
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


def _send_request(port: Any, request: dict[str, Any], timeout_s: float) -> tuple[dict[str, Any], float]:
    payload = json.dumps(request, separators=(
        ",", ":")).encode("utf-8") + b"\n"
    t0 = time.monotonic()
    deadline = t0 + timeout_s

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
        return response, (time.monotonic() - t0)

    raise TimeoutError(f"No response for method {request.get('method')}")


def _expect_ok(response: dict[str, Any], expected_id: int, stage: str) -> None:
    if not isinstance(response, dict):
        raise AssertionError(f"[{stage}] response is not a JSON object")
    if response.get("jsonrpc") != "2.0":
        raise AssertionError(f"[{stage}] jsonrpc mismatch: {response!r}")
    if response.get("id") != expected_id:
        raise AssertionError(
            f"[{stage}] id mismatch: expected {expected_id}, got {response.get('id')}")
    if "error" in response:
        raise AssertionError(f"[{stage}] returned error: {response['error']}")


def _p95(values: list[float]) -> float:
    if not values:
        return 0.0
    sorted_values = sorted(values)
    index = int(0.95 * (len(sorted_values) - 1))
    return sorted_values[index]


def main() -> int:
    parser = argparse.ArgumentParser(
        description="NuAILink USB CDC one-click regression")
    parser.add_argument("port", help="Serial device, for example COM4")
    parser.add_argument("--baud", type=int, default=115200,
                        help="CDC baud metadata")
    parser.add_argument("--timeout", type=float, default=2.0,
                        help="Response timeout (seconds)")
    parser.add_argument("--ping-count", type=int,
                        default=80, help="Ping stress count")
    parser.add_argument("--skip-led", action="store_true",
                        help="Skip led.set on/off checks")
    parser.add_argument("--check-led-bpwm", action="store_true",
                        help="Also verify led.bpwm.set duty command")
    parser.add_argument("--check-eadc", action="store_true",
                        help="Also verify eadc.read for channels 8 and 9")
    parser.add_argument("--check-llsi", action="store_true",
                        help="Also verify llsi.fill on PB15 WS2812 output")
    parser.add_argument("--check-auto", action="store_true",
                        help="Also verify autonomous modes: led.auto, gpio.auto, eadc.auto")
    args = parser.parse_args()

    serial = _load_serial_module()

    print(
        f"[REG] port={args.port} baud={args.baud} timeout={args.timeout}s ping_count={args.ping_count}")

    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as port:
        port.setDTR(True)
        port.setRTS(True)
        time.sleep(0.30)
        port.reset_input_buffer()
        port.reset_output_buffer()

        smoke_requests: list[tuple[str, dict[str, Any], int]] = [
            ("initialize", {"jsonrpc": "2.0", "id": 1,
             "method": "initialize", "params": {}}, 1),
            ("ping", {"jsonrpc": "2.0", "id": 2, "method": "ping"}, 2),
            ("tools/list", {"jsonrpc": "2.0",
             "id": 3, "method": "tools/list"}, 3),
            (
                "system.info",
                {
                    "jsonrpc": "2.0",
                    "id": 4,
                    "method": "tools/call",
                    "params": {"name": "system.info", "arguments": {}},
                },
                4,
            ),
        ]

        if not args.skip_led:
            smoke_requests.extend(
                [
                    (
                        "led.set on",
                        {
                            "jsonrpc": "2.0",
                            "id": 5,
                            "method": "tools/call",
                            "params": {"name": "led.set", "arguments": {"on": True}},
                        },
                        5,
                    ),
                    (
                        "led.set off",
                        {
                            "jsonrpc": "2.0",
                            "id": 6,
                            "method": "tools/call",
                            "params": {"name": "led.set", "arguments": {"on": False}},
                        },
                        6,
                    ),
                ]
            )

        if args.check_led_bpwm:
            smoke_requests.extend(
                [
                    (
                        "led.bpwm.set 35",
                        {
                            "jsonrpc": "2.0",
                            "id": 7,
                            "method": "tools/call",
                            "params": {"name": "led.bpwm.set", "arguments": {"duty": 35}},
                        },
                        7,
                    ),
                    (
                        "led.set off (gpio restore)",
                        {
                            "jsonrpc": "2.0",
                            "id": 8,
                            "method": "tools/call",
                            "params": {"name": "led.set", "arguments": {"on": False}},
                        },
                        8,
                    ),
                ]
            )

        if args.check_eadc:
            smoke_requests.extend(
                [
                    (
                        "eadc.read ch8",
                        {
                            "jsonrpc": "2.0",
                            "id": 9,
                            "method": "tools/call",
                            "params": {"name": "eadc.read", "arguments": {"channel": 8}},
                        },
                        9,
                    ),
                    (
                        "eadc.read ch9",
                        {
                            "jsonrpc": "2.0",
                            "id": 10,
                            "method": "tools/call",
                            "params": {"name": "eadc.read", "arguments": {"channel": 9}},
                        },
                        10,
                    ),
                ]
            )

        if args.check_auto:
            smoke_requests.extend(
                [
                    (
                        "led.auto start",
                        {
                            "jsonrpc": "2.0",
                            "id": 19,
                            "method": "tools/call",
                            "params": {
                                "name": "led.auto",
                                "arguments": {"action": "start", "interval_ms": 80, "initial_on": True},
                            },
                        },
                        19,
                    ),
                    (
                        "led.auto update",
                        {
                            "jsonrpc": "2.0",
                            "id": 20,
                            "method": "tools/call",
                            "params": {
                                "name": "led.auto",
                                "arguments": {"action": "update", "interval_ms": 60},
                            },
                        },
                        20,
                    ),
                    (
                        "led.auto status",
                        {
                            "jsonrpc": "2.0",
                            "id": 21,
                            "method": "tools/call",
                            "params": {
                                "name": "led.auto",
                                "arguments": {"action": "status"},
                            },
                        },
                        21,
                    ),
                    (
                        "led.auto stop",
                        {
                            "jsonrpc": "2.0",
                            "id": 22,
                            "method": "tools/call",
                            "params": {
                                "name": "led.auto",
                                "arguments": {"action": "stop"},
                            },
                        },
                        22,
                    ),
                    (
                        "gpio.auto start",
                        {
                            "jsonrpc": "2.0",
                            "id": 23,
                            "method": "tools/call",
                            "params": {
                                "name": "gpio.auto",
                                "arguments": {
                                    "action": "start",
                                    "port": "C",
                                    "pin": 14,
                                    "initial_value": 0,
                                    "interval_ms": 80,
                                },
                            },
                        },
                        23,
                    ),
                    (
                        "gpio.auto update",
                        {
                            "jsonrpc": "2.0",
                            "id": 24,
                            "method": "tools/call",
                            "params": {
                                "name": "gpio.auto",
                                "arguments": {"action": "update", "interval_ms": 60},
                            },
                        },
                        24,
                    ),
                    (
                        "gpio.auto status",
                        {
                            "jsonrpc": "2.0",
                            "id": 25,
                            "method": "tools/call",
                            "params": {
                                "name": "gpio.auto",
                                "arguments": {"action": "status"},
                            },
                        },
                        25,
                    ),
                    (
                        "gpio.auto stop",
                        {
                            "jsonrpc": "2.0",
                            "id": 26,
                            "method": "tools/call",
                            "params": {
                                "name": "gpio.auto",
                                "arguments": {"action": "stop"},
                            },
                        },
                        26,
                    ),
                    (
                        "eadc.auto start",
                        {
                            "jsonrpc": "2.0",
                            "id": 27,
                            "method": "tools/call",
                            "params": {
                                "name": "eadc.auto",
                                "arguments": {"action": "start", "channel": 8, "interval_ms": 100},
                            },
                        },
                        27,
                    ),
                    (
                        "eadc.auto update",
                        {
                            "jsonrpc": "2.0",
                            "id": 28,
                            "method": "tools/call",
                            "params": {
                                "name": "eadc.auto",
                                "arguments": {"action": "update", "channel": 9, "interval_ms": 80},
                            },
                        },
                        28,
                    ),
                    (
                        "eadc.auto status",
                        {
                            "jsonrpc": "2.0",
                            "id": 29,
                            "method": "tools/call",
                            "params": {
                                "name": "eadc.auto",
                                "arguments": {"action": "status"},
                            },
                        },
                        29,
                    ),
                    (
                        "eadc.auto stop",
                        {
                            "jsonrpc": "2.0",
                            "id": 30,
                            "method": "tools/call",
                            "params": {
                                "name": "eadc.auto",
                                "arguments": {"action": "stop"},
                            },
                        },
                        30,
                    ),
                ]
            )

        if args.check_llsi:
            smoke_requests.extend(
                [
                    (
                        "llsi.fill red",
                        {
                            "jsonrpc": "2.0",
                            "id": 11,
                            "method": "tools/call",
                            "params": {
                                "name": "llsi.fill",
                                "arguments": {"r": 255, "g": 0, "b": 0, "count": 10},
                            },
                        },
                        11,
                    ),
                    (
                        "llsi.fill off",
                        {
                            "jsonrpc": "2.0",
                            "id": 12,
                            "method": "tools/call",
                            "params": {
                                "name": "llsi.fill",
                                "arguments": {"r": 0, "g": 0, "b": 0, "count": 10},
                            },
                        },
                        12,
                    ),
                    (
                        "llsi.pattern rainbow",
                        {
                            "jsonrpc": "2.0",
                            "id": 13,
                            "method": "tools/call",
                            "params": {
                                "name": "llsi.pattern",
                                "arguments": {"pattern": "rainbow", "count": 10, "phase": 7},
                            },
                        },
                        13,
                    ),
                    (
                        "llsi.pattern off",
                        {
                            "jsonrpc": "2.0",
                            "id": 14,
                            "method": "tools/call",
                            "params": {
                                "name": "llsi.pattern",
                                "arguments": {"pattern": "off", "count": 10},
                            },
                        },
                        14,
                    ),
                    (
                        "llsi.autoplay start",
                        {
                            "jsonrpc": "2.0",
                            "id": 15,
                            "method": "tools/call",
                            "params": {
                                "name": "llsi.autoplay",
                                "arguments": {
                                    "action": "start",
                                    "pattern": "rainbow",
                                    "count": 10,
                                    "phase": 0,
                                    "step": 3,
                                    "interval_ms": 60,
                                },
                            },
                        },
                        15,
                    ),
                    (
                        "llsi.autoplay update",
                        {
                            "jsonrpc": "2.0",
                            "id": 16,
                            "method": "tools/call",
                            "params": {
                                "name": "llsi.autoplay",
                                "arguments": {
                                    "action": "update",
                                    "pattern": "chase",
                                    "step": 1,
                                    "interval_ms": 30,
                                    "r": 0,
                                    "g": 255,
                                    "b": 0,
                                },
                            },
                        },
                        16,
                    ),
                    (
                        "llsi.autoplay status",
                        {
                            "jsonrpc": "2.0",
                            "id": 17,
                            "method": "tools/call",
                            "params": {
                                "name": "llsi.autoplay",
                                "arguments": {
                                    "action": "status",
                                },
                            },
                        },
                        17,
                    ),
                    (
                        "llsi.autoplay stop",
                        {
                            "jsonrpc": "2.0",
                            "id": 18,
                            "method": "tools/call",
                            "params": {
                                "name": "llsi.autoplay",
                                "arguments": {
                                    "action": "stop",
                                },
                            },
                        },
                        18,
                    ),
                ]
            )

        for stage, request, req_id in smoke_requests:
            response, elapsed = _send_request(port, request, args.timeout)
            _expect_ok(response, req_id, stage)

            if stage == "llsi.autoplay status":
                result = response.get("result", {}) if isinstance(
                    response, dict) else {}
                structured = result.get("structuredContent", {}) if isinstance(
                    result, dict) else {}
                if not isinstance(structured, dict):
                    raise AssertionError(
                        "[llsi.autoplay status] missing structuredContent")
                if not bool(structured.get("running")):
                    raise AssertionError(
                        "[llsi.autoplay status] expected running=true")

            if stage in ("led.auto status", "gpio.auto status", "eadc.auto status"):
                result = response.get("result", {}) if isinstance(
                    response, dict) else {}
                structured = result.get("structuredContent", {}) if isinstance(
                    result, dict) else {}
                if not isinstance(structured, dict):
                    raise AssertionError(
                        f"[{stage}] missing structuredContent")
                if not bool(structured.get("running")):
                    raise AssertionError(f"[{stage}] expected running=true")
                if stage == "eadc.auto status" and not bool(structured.get("sample_valid")):
                    raise AssertionError(
                        "[eadc.auto status] expected sample_valid=true")

            if stage == "llsi.autoplay stop":
                result = response.get("result", {}) if isinstance(
                    response, dict) else {}
                structured = result.get("structuredContent", {}) if isinstance(
                    result, dict) else {}
                if not isinstance(structured, dict):
                    raise AssertionError(
                        "[llsi.autoplay stop] missing structuredContent")
                if bool(structured.get("running")):
                    raise AssertionError(
                        "[llsi.autoplay stop] expected running=false")

            if stage in ("led.auto stop", "gpio.auto stop", "eadc.auto stop"):
                result = response.get("result", {}) if isinstance(
                    response, dict) else {}
                structured = result.get("structuredContent", {}) if isinstance(
                    result, dict) else {}
                if not isinstance(structured, dict):
                    raise AssertionError(
                        f"[{stage}] missing structuredContent")
                if bool(structured.get("running")):
                    raise AssertionError(f"[{stage}] expected running=false")

            print(f"[REG][OK] {stage:<12} {elapsed * 1000.0:7.2f} ms")

        lat_ms: list[float] = []
        failures: list[str] = []

        for i in range(args.ping_count):
            req_id = 1000 + i
            request = {"jsonrpc": "2.0", "id": req_id, "method": "ping"}
            try:
                response, elapsed = _send_request(port, request, args.timeout)
                _expect_ok(response, req_id, f"ping-stress-{i}")
                lat_ms.append(elapsed * 1000.0)
            except Exception as exc:  # pragma: no cover - exercised in hardware runs
                failures.append(f"#{i} {exc}")
                if len(failures) <= 5:
                    print(f"[REG][FAIL] ping[{i}] {exc}")

        print("[REG] ---- stress summary ----")
        print(f"[REG] ping_total={args.ping_count}")
        print(f"[REG] ping_ok={len(lat_ms)}")
        print(f"[REG] ping_fail={len(failures)}")

        if lat_ms:
            print(f"[REG] lat_ms_min={min(lat_ms):.2f}")
            print(f"[REG] lat_ms_avg={statistics.mean(lat_ms):.2f}")
            print(f"[REG] lat_ms_p95={_p95(lat_ms):.2f}")
            print(f"[REG] lat_ms_max={max(lat_ms):.2f}")

        if failures:
            print("[REG] regression FAILED")
            return 1

    print("[REG] regression PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
