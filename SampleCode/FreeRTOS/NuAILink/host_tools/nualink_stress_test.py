#!/usr/bin/env python3
"""NuAILink USB CDC stress test with logical multi-client simulation.

Features:
- Single serial port dispatcher with request/response id matching.
- Simulates N logical clients via Python worker threads.
- Mixed workload: tools/list + tools/call (system.info / ping / led.auto status / eadc.auto status).
- Collects latency, timeout/error counts, and final system.info telemetry.
"""

from __future__ import annotations

import argparse
import json
import statistics
import threading
import time
from dataclasses import dataclass, field
from queue import Queue
from typing import Any


def _load_serial_module() -> Any:
    try:
        import serial  # type: ignore[import-not-found]
    except ImportError as exc:
        raise SystemExit(
            "pyserial is required: python -m pip install pyserial") from exc
    return serial


def _percentile(values: list[float], q: float) -> float:
    if not values:
        return 0.0
    sorted_values = sorted(values)
    idx = int((len(sorted_values) - 1) * q)
    return sorted_values[idx]


@dataclass
class PendingRequest:
    sent_at: float
    done: threading.Event
    response: dict[str, Any] | None = None
    error: Exception | None = None


class SerialRpcDispatcher:
    def __init__(self, port: Any, default_timeout: float) -> None:
        self._port = port
        self._default_timeout = default_timeout
        self._write_lock = threading.Lock()
        self._lock = threading.Lock()
        self._pending: dict[int, PendingRequest] = {}
        self._next_id = 1
        self._stop_event = threading.Event()
        self._reader = threading.Thread(
            target=self._reader_loop, name="serial-rpc-reader", daemon=True)

        self.notifications: Queue[dict[str, Any]] = Queue()
        self.orphan_frames = 0
        self.invalid_json_frames = 0
        self.decode_error_frames = 0

    def start(self) -> None:
        self._reader.start()

    def stop(self) -> None:
        self._stop_event.set()
        self._reader.join(timeout=1.0)
        with self._lock:
            for req in self._pending.values():
                req.error = TimeoutError("dispatcher stopped")
                req.done.set()
            self._pending.clear()

    def _alloc_id(self) -> int:
        with self._lock:
            request_id = self._next_id
            self._next_id += 1
        return request_id

    def _reader_loop(self) -> None:
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
                    self.decode_error_frames += 1
                    continue

                if not text:
                    continue

                try:
                    frame = json.loads(text)
                except json.JSONDecodeError:
                    self.invalid_json_frames += 1
                    continue

                if not isinstance(frame, dict):
                    self.invalid_json_frames += 1
                    continue

                if frame.get("jsonrpc") != "2.0":
                    self.invalid_json_frames += 1
                    continue

                if "id" not in frame and isinstance(frame.get("method"), str):
                    self.notifications.put(frame)
                    continue

                req_id = frame.get("id")
                if not isinstance(req_id, int):
                    self.orphan_frames += 1
                    continue

                with self._lock:
                    pending = self._pending.pop(req_id, None)

                if pending is None:
                    self.orphan_frames += 1
                    continue

                pending.response = frame
                pending.done.set()

    def call(self,
             method: str,
             params: dict[str, Any] | None = None,
             timeout_s: float | None = None) -> tuple[dict[str, Any], float]:
        request_id = self._alloc_id()
        timeout = timeout_s if timeout_s is not None else self._default_timeout

        request: dict[str, Any] = {
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
        }
        if params is not None:
            request["params"] = params

        payload = json.dumps(request, separators=(
            ",", ":")).encode("utf-8") + b"\n"
        pending = PendingRequest(
            sent_at=time.monotonic(), done=threading.Event())

        with self._lock:
            self._pending[request_id] = pending

        with self._write_lock:
            self._port.write(payload)
            self._port.flush()

        if not pending.done.wait(timeout):
            with self._lock:
                self._pending.pop(request_id, None)
            raise TimeoutError(
                f"timeout waiting for id={request_id} method={method}")

        if pending.error is not None:
            raise pending.error
        if pending.response is None:
            raise RuntimeError("missing response")

        return pending.response, (time.monotonic() - pending.sent_at)


@dataclass
class WorkerStats:
    total: int = 0
    ok: int = 0
    fail: int = 0
    timeouts: int = 0
    latencies_ms: list[float] = field(default_factory=list)


def _build_workload(op_index: int) -> tuple[str, dict[str, Any] | None]:
    # Keep request mix deterministic and low-risk for board state.
    slot = op_index % 6
    if slot == 0:
        return "tools/list", None
    if slot == 1:
        return "ping", None
    if slot == 2:
        return "tools/call", {"name": "system.info", "arguments": {}}
    if slot == 3:
        return "tools/call", {"name": "led.auto", "arguments": {"action": "status"}}
    if slot == 4:
        return "tools/call", {"name": "eadc.auto", "arguments": {"action": "status"}}
    return "tools/call", {"name": "gpio.auto", "arguments": {"action": "status"}}


def _worker_thread(worker_id: int,
                   ops_per_worker: int,
                   dispatcher: SerialRpcDispatcher,
                   timeout_s: float,
                   stats: WorkerStats) -> None:
    for i in range(ops_per_worker):
        method, params = _build_workload(worker_id * ops_per_worker + i)
        stats.total += 1

        try:
            response, elapsed = dispatcher.call(
                method, params=params, timeout_s=timeout_s)
            if response.get("jsonrpc") != "2.0":
                raise AssertionError("jsonrpc mismatch")
            if "error" in response:
                raise AssertionError(f"rpc error: {response['error']}")
            stats.ok += 1
            stats.latencies_ms.append(elapsed * 1000.0)
        except TimeoutError:
            stats.fail += 1
            stats.timeouts += 1
        except Exception:
            stats.fail += 1


def _extract_system_info_metrics(response: dict[str, Any]) -> dict[str, Any]:
    result = response.get("result", {}) if isinstance(response, dict) else {}
    structured = result.get("structuredContent", {}
                            ) if isinstance(result, dict) else {}
    if not isinstance(structured, dict):
        return {}

    keys = [
        "tick",
        "freeHeapBytes",
        "minimumEverFreeHeapBytes",
        "usbRxDropCount",
        "usbAttached",
    ]
    return {k: structured.get(k) for k in keys}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "port", help="Serial device, e.g. COM4 or /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--clients", type=int, default=10,
                        help="Logical client count")
    parser.add_argument("--ops-per-client", type=int,
                        default=60, help="RPC operations per logical client")
    parser.add_argument("--timeout", type=float, default=2.0,
                        help="Per-request timeout in seconds")
    parser.add_argument("--startup-delay", type=float, default=0.25)
    args = parser.parse_args()

    if args.clients <= 0 or args.ops_per_client <= 0:
        raise SystemExit("clients and ops-per-client must be > 0")

    serial = _load_serial_module()

    with serial.Serial(args.port, args.baud, timeout=0.1, write_timeout=args.timeout) as port:
        port.setDTR(True)
        port.setRTS(True)
        time.sleep(max(args.startup_delay, 0.0))
        port.reset_input_buffer()
        port.reset_output_buffer()

        dispatcher = SerialRpcDispatcher(
            port=port, default_timeout=args.timeout)
        dispatcher.start()

        # Initialize once.
        init_response, _ = dispatcher.call("initialize", params={})
        if "error" in init_response:
            raise SystemExit(f"initialize failed: {init_response['error']}")

        # Pre-snapshot.
        pre_info, _ = dispatcher.call(
            "tools/call", params={"name": "system.info", "arguments": {}})
        pre_metrics = _extract_system_info_metrics(pre_info)

        workers: list[threading.Thread] = []
        worker_stats: list[WorkerStats] = []

        t0 = time.monotonic()
        for worker_id in range(args.clients):
            s = WorkerStats()
            worker_stats.append(s)
            t = threading.Thread(
                target=_worker_thread,
                args=(worker_id, args.ops_per_client,
                      dispatcher, args.timeout, s),
                name=f"stress-worker-{worker_id}",
                daemon=True,
            )
            workers.append(t)
            t.start()

        for t in workers:
            t.join()
        elapsed_s = time.monotonic() - t0

        # Post-snapshot.
        post_info, _ = dispatcher.call(
            "tools/call", params={"name": "system.info", "arguments": {}})
        post_metrics = _extract_system_info_metrics(post_info)

        # Drain notifications quickly (non-blocking-ish by queue size checks).
        notification_count = 0
        while not dispatcher.notifications.empty():
            _ = dispatcher.notifications.get_nowait()
            notification_count += 1

        dispatcher.stop()

    all_latencies: list[float] = []
    total = 0
    ok = 0
    fail = 0
    timeouts = 0
    for s in worker_stats:
        total += s.total
        ok += s.ok
        fail += s.fail
        timeouts += s.timeouts
        all_latencies.extend(s.latencies_ms)

    qps = (ok / elapsed_s) if elapsed_s > 0 else 0.0

    print("[STRESS] ---- configuration ----")
    print(f"[STRESS] port={args.port} baud={args.baud}")
    print(
        f"[STRESS] logical_clients={args.clients} ops_per_client={args.ops_per_client}")
    print(f"[STRESS] timeout_s={args.timeout}")

    print("[STRESS] ---- summary ----")
    print(f"[STRESS] elapsed_s={elapsed_s:.2f}")
    print(f"[STRESS] total={total} ok={ok} fail={fail} timeouts={timeouts}")
    print(f"[STRESS] throughput_qps={qps:.2f}")

    if all_latencies:
        print(f"[STRESS] lat_ms_min={min(all_latencies):.2f}")
        print(f"[STRESS] lat_ms_avg={statistics.mean(all_latencies):.2f}")
        print(f"[STRESS] lat_ms_p95={_percentile(all_latencies, 0.95):.2f}")
        print(f"[STRESS] lat_ms_p99={_percentile(all_latencies, 0.99):.2f}")
        print(f"[STRESS] lat_ms_max={max(all_latencies):.2f}")

    print("[STRESS] ---- telemetry ----")
    print(f"[STRESS] notifications_seen={notification_count}")
    print(f"[STRESS] orphan_frames={dispatcher.orphan_frames}")
    print(f"[STRESS] invalid_json_frames={dispatcher.invalid_json_frames}")
    print(f"[STRESS] decode_error_frames={dispatcher.decode_error_frames}")

    print(
        f"[STRESS] pre_system_info={json.dumps(pre_metrics, separators=(',', ':'))}")
    print(
        f"[STRESS] post_system_info={json.dumps(post_metrics, separators=(',', ':'))}")

    if fail > 0:
        print("[STRESS] RESULT=FAILED")
        return 1

    print("[STRESS] RESULT=PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
