# NuAILink Host Tools 與測試流程

> Host tools 位置：`SampleCode/FreeRTOS/NuAILink/host_tools`  
> 主要依賴：Python 3 + `pyserial`。

## 1. Host tools 設計原則

NuAILink MCU 端提供 USB CDC serial newline JSON-RPC。Host tools 負責：

- 打開 COM / ttyACM serial port。
- 送出 compact JSON + `\n`。
- 讀取 newline-delimited response。
- 過濾 notification 或依 id match response。
- 提供 regression / stress / feature demo。

現有工具大多會：

1. `serial.Serial(port, 115200, timeout=0.1, write_timeout=...)`
2. `setDTR(True)` / `setRTS(True)`
3. 等 0.2~0.3 秒
4. `reset_input_buffer()` / `reset_output_buffer()`
5. 送 JSON-RPC request
6. 等待 response 或 event

## 2. 工具總覽

| Tool | 用途 |
| :--- | :--- |
| `com4_probe.py` | 最小 COM4 initialize probe；硬編 COM4，主要供快速 bring-up |
| `nualink_serial_smoke.py` | initialize / ping / tools/list / system.info / led.set smoke test |
| `nualink_regression.py` | 一鍵 regression，含 handshake、tools、optional hardware checks、ping stress |
| `run_regression.ps1` | PowerShell wrapper，自動優先使用 `.venv` Python |
| `nualink_stdio_bridge.py` | stdio JSON-RPC <-> USB CDC bridge，給標準 MCP client 使用 |
| `nualink_stress_test.py` | 單 serial dispatcher + 多 logical clients 壓力測試 |
| `nualink_led.py` | LED on/off demo |
| `nualink_led_bpwm.py` | LED BPWM brightness demo |
| `nualink_eadc_read.py` | EADC CH8/CH9 read demo |
| `nualink_button_listen.py` | 監聽 PB14 `button.event` notification |
| `nualink_llsi_fill.py` | LLSI fill demo |
| `nualink_llsi_pattern.py` | LLSI pattern frame demo |
| `nualink_llsi_autoplay.py` | LLSI autoplay start/update/status/stop demo |

## 3. Python environment

若已有專案 venv，建議使用：

```powershell
.\.venv\Scripts\python.exe
```

若沒有 `pyserial`，安裝：

```powershell
python -m pip install pyserial
```

> 注意：工具內部會在 import serial 失敗時提示 `pyserial is required`。

## 4. Smoke test

### `nualink_serial_smoke.py`

用途：基本連線驗證。

涵蓋：

- `initialize`
- `ping`
- `tools/list`
- `system.info`
- `led.set on/off`（除非 `--skip-led`）

範例：

```powershell
python .\nualink_serial_smoke.py COM4
```

若不想動 PC14 LED：

```powershell
python .\nualink_serial_smoke.py COM4 --skip-led
```

## 5. 一鍵 regression

### `nualink_regression.py`

用途：功能 + ping stress 回歸。

基本範例：

```powershell
python .\nualink_regression.py COM4 --ping-count 80
```

常用 options：

| Option | 用途 |
| :--- | :--- |
| `--baud 115200` | CDC line coding metadata |
| `--timeout 2.0` | 每筆 request response timeout |
| `--ping-count N` | ping stress 次數 |
| `--skip-led` | 跳過 LED on/off |
| `--check-led-bpwm` | 驗證 `led.bpwm.set` |
| `--check-eadc` | 驗證 CH8/CH9 `eadc.read` |
| `--check-llsi` | 驗證 LLSI fill/pattern/autoplay |
| `--check-auto` | 驗證 LED/GPIO/EADC auto modes 與 notification |

### `run_regression.ps1`

PowerShell wrapper：

```powershell
.\run_regression.ps1 -Port COM4
```

可指定：

```powershell
.\run_regression.ps1 -Port COM4 -PingCount 200 -Timeout 2.0
```

Wrapper 會優先使用：

```text
host_tools/.venv/Scripts/python.exe
```

不存在時 fallback 到 `python`。

## 6. Stress test

### `nualink_stress_test.py`

用途：用單一 serial port dispatcher 模擬多個 logical clients。

特性：

- Reader thread 持續讀 newline frame。
- 每筆 request 分配 unique integer id。
- Response 依 id match 到 pending request。
- Notification 放入 queue。
- 統計 latency、timeout、invalid JSON、orphan frame。
- 前後讀 `system.info`，觀察 heap 與 `usbRxDropCount`。

範例：

```powershell
python .\nualink_stress_test.py COM4 --clients 10 --ops-per-client 60
```

Workload mix：

| Slot | Request |
| :--- | :--- |
| 0 | `tools/list` |
| 1 | `ping` |
| 2 | `system.info` |
| 3 | `led.auto status` |
| 4 | `eadc.auto status` |
| 5 | `gpio.auto status` |

輸出重點：

| Metric | 期待 |
| :--- | :--- |
| `fail` | 0 |
| `timeouts` | 0 |
| `invalid_json_frames` | 0 |
| `decode_error_frames` | 0 |
| `post_system_info.usbRxDropCount` | 0 under normal workload |
| p95 / p99 latency | 用於比較修改前後，不要憑空訂固定值 |

## 7. MCP stdio bridge

### `nualink_stdio_bridge.py`

用途：把標準 MCP client 的 stdin/stdout JSON-RPC stream 轉到 NuAILink USB CDC serial。

資料流：

```text
MCP client stdin/stdout
        |
        v
nualink_stdio_bridge.py
        |
        v
COM4 / ttyACM0
        |
        v
NuAILink firmware
```

執行：

```powershell
python .\nualink_stdio_bridge.py COM4
```

常用 options：

| Option | 說明 |
| :--- | :--- |
| `--baud` | CDC baud metadata，預設 115200 |
| `--timeout` | serial write timeout |
| `--startup-delay` | open port 後等待時間 |
| `--strict-json` | stdin / serial 非 JSON object frame 直接丟棄 |
| `--verbose` | diagnostics 印到 stderr |
| `--drain-after-eof` | stdin EOF 後繼續讀 serial 一小段時間，適合 one-shot pipeline |

重要設計：

- stdout 只輸出 JSON-RPC frame。
- log / summary / error 全部走 stderr。
- serial reader 使用 byte buffer 搜尋 `\n`，不是單純依賴 `readline()`；可避免 load 下 partial frame parse fail。

## 8. Feature demo tools

### 8.1 LED on/off

```powershell
python .\nualink_led.py COM4 on
```

```powershell
python .\nualink_led.py COM4 off
```

對應 tool：`led.set`。

### 8.2 LED BPWM brightness

```powershell
python .\nualink_led_bpwm.py COM4 35
```

對應 tool：`led.bpwm.set`。

`duty` 是語義亮度 0..100；firmware 內部會處理 PC14 active-low。

### 8.3 EADC read

```powershell
python .\nualink_eadc_read.py COM4 8 --count 10 --interval 0.2
```

```powershell
python .\nualink_eadc_read.py COM4 9 --count 10 --interval 0.2
```

對應 tool：`eadc.read`。

### 8.4 Button notification

```powershell
python .\nualink_button_listen.py COM4 --seconds 30
```

流程：

1. 開 port。
2. 送一次 `button.read`。
3. 持續讀 frame。
4. 如果 `method == button.event`，印出 pressed 狀態。

### 8.5 LLSI fill

```powershell
python .\nualink_llsi_fill.py COM4 255 0 0 --count 10
```

關閉：

```powershell
python .\nualink_llsi_fill.py COM4 0 0 0 --count 10
```

對應 tool：`llsi.fill`。

### 8.6 LLSI pattern frames

```powershell
python .\nualink_llsi_pattern.py COM4 --pattern rainbow --frames 20 --step 3 --interval 0.05
```

對應 tool：`llsi.pattern`。

### 8.7 LLSI autoplay

Start rainbow autoplay，跑 3 秒後查 status 並 stop：

```powershell
python .\nualink_llsi_autoplay.py COM4 --action start --pattern rainbow --count 10 --step 3 --interval-ms 60 --run-seconds 3 --stop-after
```

查 status：

```powershell
python .\nualink_llsi_autoplay.py COM4 --action status
```

停止：

```powershell
python .\nualink_llsi_autoplay.py COM4 --action stop
```

## 9. Host 程式撰寫建議

### 9.1 永遠用 newline framing

送出：

```python
payload = json.dumps(request, separators=(",", ":")).encode("utf-8") + b"\n"
port.write(payload)
port.flush()
```

接收：累積到 `\n` 再 parse；不要假設一個 USB packet 就是一筆 JSON。

### 9.2 用 id match response

如果 host 可能同時處理 notification 或多 logical clients，必須用 id match。

```python
if "id" not in frame and isinstance(frame.get("method"), str):
    notifications.put(frame)
elif frame.get("id") in pending:
    pending[frame["id"]].set_result(frame)
else:
    orphan_frames += 1
```

### 9.3 監控 `usbRxDropCount`

每輪 regression / stress 後讀 `system.info`：

```python
info = response["result"]["structuredContent"]
assert info["usbRxDropCount"] == 0
```

如果 drop count 增加，代表 host 發送太快或 firmware 被阻塞。

### 9.4 stdout / stderr 分離

如果工具要接 MCP client，stdout 必須只放 JSON-RPC frame；diagnostics 應全部放 stderr。`nualink_stdio_bridge.py` 已遵守此原則。

## 10. 建議測試矩陣

| 階段 | 測試 |
| :--- | :--- |
| USB transport 修改 | smoke + tools/list + stress |
| 新 tool | valid call + invalid params + tools/list schema |
| Notification 修改 | `button_listen` 或 regression `--check-auto` |
| EADC 修改 | CH8/CH9 多次讀，觀察 mV range |
| LLSI 修改 | fill red/off、pattern rainbow/off、autoplay status/stop |
| Queue / buffer 修改 | stress，觀察 timeout、invalid JSON、usbRxDropCount |
| Release | full regression + stress + manual LED/button sanity |
