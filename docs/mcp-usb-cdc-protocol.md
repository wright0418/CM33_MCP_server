# MCP over USB CDC 通訊格式

> 這是獨立的通訊協定文件，描述 host 如何透過 USB CDC serial 與 NuAILink M3334 firmware 溝通。  
> 對應 firmware：`nualink/src/transport/nualink_usb_cdc.c`、`nualink/src/core/mcp_jsonrpc.c`、`nualink/src/core/mcp_response.c`。

## 1. USB 裝置層

NuAILink 在 USB 上宣告為 CDC ACM device，因此在 host 端通常會出現：

- Windows：`COMx`，例如 `COM4`
- Linux：`/dev/ttyACM0` 或 `/dev/ttyACM1`
- macOS：`/dev/tty.usbmodem*`

| 欄位 | 值 |
| :--- | :--- |
| USB class | CDC ACM |
| VID | `0x0416` |
| PID | `0xB002` |
| Manufacturer string | `Nuvoton` |
| Product string | `NuAILink MCP Bridge` |
| Control endpoint | CEP0, max packet 64 |
| Bulk IN | EPA, endpoint 1, device -> host |
| Bulk OUT | EPB, endpoint 2, host -> device |
| Interrupt IN | EPC, endpoint 3, CDC notification endpoint |

> CDC open 時使用的 baud rate（預設 115200）只是 CDC line coding metadata；資料實際走 USB bulk endpoint，不受 UART baud rate 限制。

## 2. Serial port 開啟建議

Host 建議設定：

| Setting | 建議值 |
| :--- | :--- |
| Baud | `115200` |
| Data bits | 8 |
| Parity | none |
| Stop bits | 1 |
| Read timeout | 0.1 秒左右，應用層另設 request timeout |
| Write timeout | 2 秒左右 |
| DTR / RTS | 開啟；現有 Python tools 會 `setDTR(True)` / `setRTS(True)` |
| Startup delay | 開 port 後等 0.2~0.3 秒，再 reset input/output buffer |

## 3. Frame 格式

NuAILink 使用 newline-delimited UTF-8 JSON：

```text
<one JSON object encoded as UTF-8><LF>
```

規則：

| 規則 | 說明 |
| :--- | :--- |
| Frame delimiter | `\n` |
| Carriage return | `\r` 會被 firmware 忽略，因此 `\r\n` 也可用 |
| Encoding | UTF-8 |
| JSON top-level | 必須是 object |
| Empty line | 忽略 |
| Request max length | 目前 `NUALINK_JSON_REQUEST_SIZE = 1024`，實際 JSON payload 最多約 1023 bytes，不含結尾 `\0` |
| Response max length | 目前 `NUALINK_JSON_RESPONSE_SIZE = 6144`，包含 JSON 與尾端 `\n` |
| Response compactness | firmware 使用 compact JSON，沒有 pretty print |

如果一筆 request 超過 firmware request buffer，firmware 會丟棄直到下一個 newline，然後回：

```json
{"jsonrpc":"2.0","error":{"code":-32600,"message":"message_too_large"},"id":null}
```

## 4. JSON-RPC 2.0 基本格式

### Request

```json
{"jsonrpc":"2.0","id":1,"method":"ping"}
```

或：

```json
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"system.info","arguments":{}}}
```

### Success response

```json
{"jsonrpc":"2.0","result":{"status":"ok","tick":1234},"id":1}
```

Tool response 通常是 MCP-style result：

```json
{
  "jsonrpc": "2.0",
  "result": {
    "content": [
      {"type": "text", "text": "PC14 LED is on"}
    ],
    "structuredContent": {
      "pin": "PC14",
      "mode": "gpio",
      "on": true
    },
    "isError": false
  },
  "id": 10
}
```

### Error response

```json
{"jsonrpc":"2.0","error":{"code":-32602,"message":"invalid_params"},"id":10}
```

## 5. 支援的 JSON-RPC methods

| Method | Params | Result |
| :--- | :--- | :--- |
| `initialize` | `{}` 可省略 | protocol version、capabilities、serverInfo |
| `ping` | 無 | `{status:"ok", tick:<FreeRTOS tick>}` |
| `tools/list` | 無 | `{tools:[{name,description,inputSchema}, ...]}` |
| `tools/call` | `{name:string, arguments:object}` | tool-specific MCP result |

### `initialize`

Request：

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}
```

Response 內容：

```json
{
  "jsonrpc": "2.0",
  "result": {
    "protocolVersion": "2024-11-05",
    "capabilities": {
      "tools": {}
    },
    "serverInfo": {
      "name": "NuAILink",
      "version": "0.1.0"
    }
  },
  "id": 1
}
```

### `ping`

Request：

```json
{"jsonrpc":"2.0","id":2,"method":"ping"}
```

Response：

```json
{"jsonrpc":"2.0","result":{"status":"ok","tick":12345},"id":2}
```

### `tools/list`

Request：

```json
{"jsonrpc":"2.0","id":3,"method":"tools/list"}
```

Response 會列出目前 registry 內所有 tools。這筆 response 可能超過單一 HS bulk packet，因此會由 firmware 自動切成多個 USB IN packets；host 仍然只需讀到 newline。

### `tools/call`

Request：

```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "tools/call",
  "params": {
    "name": "led.set",
    "arguments": {"on": true}
  }
}
```

Response：

```json
{
  "jsonrpc": "2.0",
  "result": {
    "content": [{"type":"text","text":"PC14 LED is on"}],
    "structuredContent": {"on":true,"mode":"gpio","pin":"PC14"},
    "isError": false
  },
  "id": 4
}
```

## 6. Notification 格式

Notification 是 JSON-RPC object，但沒有 `id`，host 不需要回覆。

```json
{"jsonrpc":"2.0","method":"button.event","params":{"pin":"PB14","pressed":true}}
```

目前 firmware 可能主動送出的 notifications：

| Method | 來源 | Params |
| :--- | :--- | :--- |
| `button.event` | PB14 GPIO interrupt | `pin`, `pressed` |
| `led.auto.event` | `led.auto` notify mode | `pin`, `on`, `event_count`, `tick` |
| `gpio.auto.event` | `gpio.auto` notify mode | `port`, `pin`, `value`, `event_count`, `tick` |
| `eadc.auto.event` | `eadc.auto` notify / threshold mode | `event`, `channel`, `pin`, `raw`, `mV`, `event_count`, `tick` |

Host 如果同時等待 request response 與 notification，應用 `id` 做 response matching，並把沒有 `id` 的 frame 視為 event。

## 7. Error code

| Code | Message | 來源 |
| ---: | :--- | :--- |
| `-32700` | `parse_error` | JSON parse 失敗或 top-level 不是 object |
| `-32600` | `invalid_request` | method 欄位缺失/型別錯誤 |
| `-32600` | `message_too_large` | line 超過 request buffer |
| `-32601` | `method_not_found` | method 不支援，或 `tools/call` 指定未知 tool |
| `-32602` | `invalid_params` | params / arguments 不合法 |
| `-32603` | `internal_error` | cJSON allocation、硬體操作或 response builder 失敗 |
| `-32603` | `response_too_large` | result 超過 response buffer |
| `-32000` | `server_busy` | inline handling / response enqueue 失敗時的 application error |

## 8. Python 最小範例

以下範例使用 `pyserial`，送出一筆 request 並讀回同 id response，同時忽略 notification。

```python
import json
import time
import serial


def call(port, request, timeout_s=2.0):
    expected_id = request.get("id")
    payload = json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n"
    deadline = time.monotonic() + timeout_s

    port.write(payload)
    port.flush()

    while time.monotonic() < deadline:
        line = port.readline()
        if not line:
            continue

        frame = json.loads(line.decode("utf-8", errors="replace").strip())

        # Notification: no id, method present.
        if "id" not in frame and isinstance(frame.get("method"), str):
            print("event:", frame)
            continue

        if frame.get("id") == expected_id:
            return frame

    raise TimeoutError(f"no response for id={expected_id}")


with serial.Serial("COM4", 115200, timeout=0.1, write_timeout=2.0) as ser:
    ser.setDTR(True)
    ser.setRTS(True)
    time.sleep(0.25)
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    print(call(ser, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}))
    print(call(ser, {"jsonrpc": "2.0", "id": 2, "method": "ping"}))
    print(call(ser, {
        "jsonrpc": "2.0",
        "id": 3,
        "method": "tools/call",
        "params": {"name": "led.set", "arguments": {"on": True}},
    }))
```

## 9. Python：列出 tools

```python
request = {"jsonrpc": "2.0", "id": 10, "method": "tools/list"}
response = call(ser, request)
for tool in response["result"]["tools"]:
    print(tool["name"], "-", tool["description"])
```

## 10. Python：讀取 system.info

```python
request = {
    "jsonrpc": "2.0",
    "id": 11,
    "method": "tools/call",
    "params": {"name": "system.info", "arguments": {}},
}
response = call(ser, request)
info = response["result"]["structuredContent"]
print("firmware:", info["firmwareVersion"])
print("hclk:", info["hclkHz"])
print("free heap:", info["freeHeapBytes"])
print("usb drops:", info["usbRxDropCount"])
```

## 11. Python：監聽 button event

```python
import json
import time
import serial

with serial.Serial("COM4", 115200, timeout=0.1, write_timeout=2.0) as ser:
    ser.setDTR(True)
    ser.setRTS(True)
    time.sleep(0.25)
    ser.reset_input_buffer()
    ser.reset_output_buffer()

    while True:
        line = ser.readline()
        if not line:
            continue
        frame = json.loads(line.decode("utf-8", errors="replace").strip())
        if frame.get("method") == "button.event":
            print("PB14 pressed:", frame.get("params", {}).get("pressed"))
```

## 12. Host concurrency 建議

Firmware 目前一次按 newline frame 串流處理 request。Host 若有多個 logical clients，建議：

1. 實際 serial port 只開一次。
2. 所有 requests 經過同一個 dispatcher 寫入 serial。
3. 每筆 request 使用唯一 integer `id`。
4. reader thread 持續讀 frame：
   - 有 `id`：喚醒對應 pending request。
   - 無 `id` 且有 `method`：放入 notification queue。
5. 監控 timeout、invalid JSON、orphan frame、`usbRxDropCount`。

此模式可參考 `host_tools/nualink_stress_test.py` 的 `SerialRpcDispatcher`。

## 13. 與標準 MCP client 的橋接

標準 MCP client 常用 stdio transport；NuAILink MCU 端使用 CDC serial。專案提供：

```text
host_tools/nualink_stdio_bridge.py
```

橋接方式：

```text
MCP client stdin/stdout
        |
        | JSON-RPC line
        v
nualink_stdio_bridge.py
        |
        | USB CDC serial line
        v
NuAILink firmware
```

橋接程式設計原則：

- stdout 只輸出 JSON-RPC frame。
- diagnostics 全部寫 stderr。
- 可用 `--strict-json` 丟棄非 JSON object frame。
- 支援 `--drain-after-eof`，方便 one-shot pipeline 讀完最後 response。

## 14. 相容性與限制

| 項目 | 現況 |
| :--- | :--- |
| Binary payload | 不支援；請用 JSON number/string/array 表示 |
| Request streaming | 不支援 chunked JSON；一筆 request 必須在單一 newline frame 內完整送出 |
| Large response | 支援 USB 多封包 TX，但 JSON response 必須小於 6144 bytes |
| Request queueing | Firmware 目前 inline 處理，host 不應無限制 burst |
| Notification reliability | 走 response queue，queue 滿時可能 drop；目前未回報 notification drop count |
| Schema validation | `inputSchema` 給 host 參考；firmware callback 以手動 validation 為準 |
