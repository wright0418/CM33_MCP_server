# NuAILink Code Review 與交接注意事項

> 本文件整理本次深度閱讀 `SampleCode/FreeRTOS/NuAILink` 後的發現。  
> 目標是讓接手者知道「目前做得好的地方」、「哪些地方是刻意設計」、「哪些地方是限制或風險」。

## 1. Review 範圍

本次閱讀重點：

| 類別 | 檔案 |
| :--- | :--- |
| App / RTOS | `main.c`, `FreeRTOSConfig.h`, `nualink/src/platform/nualink_tasks.c` |
| Board HAL | `nualink/src/platform/nualink_board.c`, `nualink/include/nualink_board.h` |
| USB CDC | `nualink/src/transport/nualink_usb_cdc.c`, `nualink/src/transport/nualink_usb_descriptors.c` |
| MCP core | `mcp_jsonrpc.c`, `mcp_response.c`, `mcp_registry.c`, related headers |
| Plugins | `plugin_system.c`, `plugin_led_gpio.c`, `plugin_gpio.c`, `plugin_eadc.c`, `plugin_llsi.c` |
| Host tools | `host_tools/*.py`, `run_regression.ps1` |
| CMSIS project | `VSCode/NuAILink.csolution.yml`, `VSCode/NuAILink.cproject.yml` |

## 2. 整體評價

目前 NuAILink 已經不是只有概念雛形，而是具備：

- 可枚舉的 USB CDC ACM device。
- newline JSON-RPC transport。
- MCP-like `initialize` / `tools/list` / `tools/call`。
- 13 個 hardware tools。
- 按鈕與 auto mode notification。
- Python smoke / regression / stress / stdio bridge。
- FreeRTOS heap / stack / assert / fault hooks。
- 多封包 USB TX 的穩定化處理。

整體分層清楚，適合作為 M3334 hardware MCP SDK prototype。後續最大的架構改善點是把目前 inline tool execution 從 USB task 拆出去，並讓 queue/drop/error telemetry 更完整。

## 3. 做得好的地方

### 3.1 分層明確

`transport`、`core`、`plugins`、`platform` 分層清楚。`mcp_jsonrpc.c` 不直接碰硬體；hardware access 集中在 board HAL 和 plugin callback。

### 3.2 cJSON allocator hook 正確

`NuAILink_CJSONInitHooks()` 把 cJSON malloc/free 接到 FreeRTOS allocator：

```text
cJSON malloc -> pvPortMalloc
cJSON free   -> vPortFree
```

搭配 `heap_4.c`，比早期 `heap_2.c` 更適合 JSON parse / free 的生命週期。

### 3.3 Response buffer 已考慮 tools/list 長度

`NUALINK_JSON_RESPONSE_SIZE = 6144`，足以容納目前 `tools/list` 多工具 schema。這也逼出了並修掉 USB TX 多封包問題。

### 3.4 USB TX 多封包修正很關鍵

`NuAILink_TransportWrite()` 目前採用穩定策略：

- chunk size = HS 512 / FS 64。
- 以 `TXPKIF/TXPKIEN` 等待 packet transmitted。
- 每個 chunk 前 flush EPA FIFO。
- 清 stale `TXPKIF | INTKIF`。
- 只在最後 chunk 設 `SHORTTXEN`。

這些細節不可隨意簡化，否則 `tools/list` 或其他長 response 會出現截斷、錯位、leading/trailing newline drift。

### 3.5 Host bridge stdout/stderr 分離正確

`nualink_stdio_bridge.py` stdout 保留給 JSON-RPC frame，diagnostics 走 stderr。這是接標準 MCP client 的必要條件。

### 3.6 Stress test 架構正確

`nualink_stress_test.py` 用單 serial dispatcher 模擬多 logical clients，而不是多程序同時開同一個 COM port。這符合 serial transport 實際限制，也能測 response id matching。

## 4. 重要現況：Parser task 目前未接入資料流

`NuAILink_TasksCreate()` 建立了：

- `s_request_queue`
- `s_response_queue`
- `USB_Comm` task
- `MCP_Parse` task
- `Heartbeat` task

但目前 `USB_Comm` 收到完整 frame 後會直接呼叫：

```text
MCP_JSONRPC_Handle(request->data, response.data, sizeof(response.data))
```

也就是 inline parse / dispatch / tool callback。

`MCP_Parse` task 仍在等待 `s_request_queue`，但目前沒有任何地方 `xQueueSend(s_request_queue, ...)`。

### 影響

| 影響 | 說明 |
| :--- | :--- |
| 架構文件需如實描述 | 不要誤以為 parser task 正在處理 request |
| 長 callback 會擋 USB task | tool callback 若 blocking，USB RX/TX/auto mode 都受影響 |
| request queue storage 目前是 RAM 成本 | 可視為未來拆 task 的預留成本 |

### 建議

若要產品化，建議改成：

```text
USB_Comm task
  - 只做 USB RX/TX + newline framing
  - 完整 request 丟 s_request_queue

MCP_Parse task
  - parse JSON-RPC
  - 短 callback 可直接執行
  - 長 callback 丟 plugin/action queue

USB_Comm task
  - 從 s_response_queue 送回 host
```

## 5. USB RX backpressure 限制

目前 EPB OUT ISR 只有一個 pending application packet buffer：

```text
s_usb_rx_buffer[512]
s_bulk_out_ready
s_rx_size
```

如果上一包還沒被 `NuAILink_TransportReadPacket()` 取走，又收到下一包，ISR 會 drain FIFO 並 `s_rx_drop_count++`。

### 影響

- Host 無限制 burst request 可能掉包。
- Inline tool callback 太久也可能讓 USB task 來不及取 packet。
- `usbRxDropCount` 是目前唯一外部可觀測指標。

### 建議

1. Host 端保留 dispatcher / in-flight limit。
2. Firmware 端可改成 stream buffer / ring buffer。
3. 將 parser/action 從 USB task 拆出去。
4. 增加 `usbRxDropCount` regression assertion。

## 6. Response / notification queue 限制

`NUALINK_RESPONSE_QUEUE_LENGTH = 2`。Response 與 unsolicited notification 共用 queue。

`NuAILink_TasksPushNotification()` 和 `NuAILink_TasksPushNotificationFromISR()` 使用 0 timeout enqueue；queue 滿時會失敗，但目前多數 caller 沒有統計 notification drop。

### 影響

- Button bounce 雖有 30 ms debounce，但若 host 不讀或 response 太慢，notification 可能 drop。
- Auto notify 開太快時也可能 drop。

### 建議

- 增加 `notificationDropCount`。
- `system.info` 回報該 counter。
- 對 auto mode notify 做 rate limit 或 queue depth check。
- 若 notification reliability 很重要，改成 event ring buffer。

## 7. Board HAL 參數防禦性不足：LLSI fill count

`plugin_llsi.c` 會把 `count` 限制在 1..10，因此從 MCP tool call 進來是安全的。

但 `NuAILink_BoardLlsiFill()` 本身：

```text
uint8_t tx_bytes[NUALINK_LLSI_MAX_PIXELS * 3U];
for pixel_index < pixel_count:
    write tx_bytes[pixel_index * 3 + ...]
return prvLlsiTransmitPixels(tx_bytes, pixel_count);
```

`pixel_count` 如果由其他 C code 直接傳入 > 10，會在呼叫 `prvLlsiTransmitPixels()` 驗證前先寫爆 stack array。

### 建議修正

在 `NuAILink_BoardLlsiFill()` 一開始加入：

```text
if ((pixel_count == 0U) || (pixel_count > NUALINK_LLSI_MAX_PIXELS)) return false;
```

雖然目前 MCP path 安全，board HAL 最好仍自我保護。

## 8. GPIO ownership 風險

`gpio.write` 禁止 PB12/PB13/PB14，但其他 pin 仍可任意設成 output。

### 風險

- Host 可能誤動已接外設的 pin。
- PC14 被 LED / BPWM / heartbeat / gpio.auto 多方共享。
- 若未來加入 I2C/SPI/UART/Modbus plugin，需要更完整 pin ownership table。

### 建議

- 建立 central pin policy / ownership map。
- `gpio.write` 應依 board profile 限制可寫 pins。
- `system.info` 或新增 `board.pins` tool 回報目前 pin ownership。

## 9. Tool schema 與 validation 是兩套機制

目前 `input_schema_json` 是給 host / MCP client 探索用。Firmware 沒有通用 JSON Schema validator，而是每個 callback 手動檢查。

### 風險

- 修改 schema 卻忘記改 callback validation。
- 修改 callback range 卻忘記改 schema。

### 建議

- 新增 regression 測 invalid params。
- 文件/測試生成可由單一 tool metadata source 產生。
- 至少在 PR checklist 加「schema 與 validation 同步」。

## 10. Auto mode timing 與 USB task 耦合

Auto modes 由 `USB_Comm` loop 每 1 ms 呼叫 process function。

### 影響

- USB task 若忙於 TX 長 response 或 callback，auto mode jitter 會增加。
- LLSI autoplay 或 EADC auto 若頻率高，也會影響 USB task loop。

### 建議

- 短期：限制 auto interval 最小值，目前已有 10/20 ms 下限。
- 中期：建立 dedicated `Plugin_Auto` task。
- 長期：用 timer/task notification 驅動，不要依賴 USB loop polling。

## 11. Interrupt priority 注意事項

目前設定：

| IRQ | Priority | 是否呼叫 FreeRTOS API |
| :--- | ---: | :--- |
| `USBD20_IRQn` | `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` = 5 | 目前不直接呼叫 FreeRTOS API |
| `GPB_IRQn` | 6 | 呼叫 `xQueueSendFromISR` via notification push |

FreeRTOS Cortex-M 規則：呼叫 FromISR API 的 IRQ priority 數值必須 >= `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY`。

PB14 使用 priority 6 是安全的。

## 12. Request / response 大小限制

| Item | Value | 影響 |
| :--- | ---: | :--- |
| USB RX packet | 512 bytes | HS OUT 單 packet buffer |
| JSON request | 1024 bytes | 一筆 request line 超過會 `message_too_large` |
| JSON response | 6144 bytes | `tools/list` 可跨多 USB packet，但仍不能超過此 buffer |
| Response queue length | 2 | host 不讀或 notification 過多會塞住 |
| TX timeout | 250 ms | 大 response 或 host 不收可能 timeout |

建議新增 tool 時注意 schema 長度，避免 `tools/list` 逐漸逼近 6144 bytes。

## 13. MCP / JSON-RPC 行為細節

### Notification request

如果 request 沒有 `id` 且成功處理，`MCP_JSONRPC_Handle()` 會不產生 response。

### Invalid notification

若沒有 `id` 但 request 無效，firmware 仍可能回 error with `id:null`。這對 debug 有幫助，但 host 應能容忍。

### Unknown tool

`tools/call` 指定未知 tool 時目前回 `-32601 method_not_found`。

## 14. Host tooling review

### `nualink_stdio_bridge.py`

優點：

- byte-stream buffer + newline framing，耐 partial reads。
- stdout only JSON，stderr diagnostics。
- `--strict-json` 可提升 MCP client 相容性。
- `--drain-after-eof` 適合 one-shot pipeline。

注意：

- 開 port 後會 reset input/output buffer，可能丟掉剛開 port 時已經在 queue 的 notification。
- Long-running MCP session 應保持 bridge process 存活。

### `nualink_stress_test.py`

優點：

- 單 serial dispatcher 正確。
- id matching 正確。
- 統計 invalid/orphan/decode errors。
- 前後讀 `system.info`。

建議：

- 增加 threshold：若 post `usbRxDropCount` > pre，測試可選擇 fail。
- 增加 CSV/JSON output，方便 CI trend。

## 15. 建議優先修正清單

| Priority | 建議 | 理由 |
| :--- | :--- | :--- |
| P0 | 在 `NuAILink_BoardLlsiFill()` 補 `pixel_count` range check | Board HAL 自保，避免未來 C caller 造成 stack overflow |
| P1 | 將 request path 改為 queue + parser task | 避免 USB task 被 callback 阻塞 |
| P1 | 增加 notification drop counter | 讓 event reliability 可觀測 |
| P1 | stress test assert `usbRxDropCount` 不增加 | 防止 RX backpressure regression |
| P2 | 建立 pin ownership policy | 避免 GPIO tool 誤動保留腳位 |
| P2 | 將 tool metadata 單一來源化 | 避免 schema / validation / docs drift |
| P2 | Auto mode 拆出 dedicated task | 降低 USB loop jitter |
| P3 | 增加 host-side mock transport test | 無實板也能測 JSON-RPC parser / bridge |

## 16. 修改時不要踩的地雷

1. 不要把 `NuAILink_TransportWrite()` 的 `TXPKIF` pacing 改回 `INTKIF`。
2. 不要移除 EPA FIFO flush；長 response 會出現殘留 byte 問題。
3. 不要在 ISR stack 宣告大型 `nualink_response_t`；目前 ISR notification 使用 static buffer 是刻意設計。
4. 不要讓 `USBD20_IRQHandler()` 做 cJSON parse 或硬體 plugin action。
5. 不要讓 stdout 混入 bridge diagnostics；MCP stdio client 會把它當 JSON-RPC frame。
6. 不要假設 `tools/list` 一次 `read()` 就完整；host 必須讀到 newline。
7. 不要忘記 PC14 active-low；BPWM duty 需要反相。
8. 不要把 `NUALINK_ENABLE_DEBUG_LOG` 開著做效能測試。

## 17. 接手者第一天建議工作

1. Build + flash 現有 firmware。
2. 跑 `nualink_regression.py COM4 --ping-count 80`。
3. 跑 `nualink_stress_test.py COM4 --clients 10 --ops-per-client 60`。
4. 用 `system.info` 記錄 baseline：heap、minimumEverFreeHeap、usbRxDropCount。
5. 手動驗證：LED on/off、button event、EADC read、LLSI off。
6. 閱讀 `NuAILink_TransportWrite()`，理解長 response TX 細節。
7. 若要改架構，先處理 request queue/parser task 拆分。
