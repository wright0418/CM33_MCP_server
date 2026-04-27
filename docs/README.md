# NuAILink M3334 Firmware 文件入口

> 文件日期：2026-04-28  
> 適用程式碼：`SampleCode/FreeRTOS/NuAILink` 目前實作  
> Target：`M3334KIGAE`（使用 M3331 series CMSIS BSP）  
> 主要用途：讓接手 M3334 FW / NuAILink MCP USB CDC 專案的人，能快速理解架構、通訊格式、工具資料模型、建置測試與後續擴充方向。

## 這份文件集怎麼讀

建議依照下列順序閱讀：

1. [`firmware-architecture.md`](firmware-architecture.md)  
   韌體整體架構、啟動流程、FreeRTOS task、USB transport、board HAL、plugin 分層。
2. [`mcp-usb-cdc-protocol.md`](mcp-usb-cdc-protocol.md)  
   **獨立的 MCP over USB CDC 通訊格式文件**，包含 framing、JSON-RPC method、error、notification 與 Python 範例。
3. [`data-model-and-tools.md`](data-model-and-tools.md)  
   MCP result 格式、`structuredContent` 資料模型，以及目前 13 個 tools 的參數與回傳資料。
4. [`build-flash-debug.md`](build-flash-debug.md)  
   建置、燒錄、基本 bring-up、UART/LED 診斷與常見問題。
5. [`host-tools-and-testing.md`](host-tools-and-testing.md)  
   `host_tools` 下 Python / PowerShell 工具說明、回歸測試與壓力測試流程。
6. [`code-review-handoff-notes.md`](code-review-handoff-notes.md)  
   深度 code review 後整理出的穩定點、限制、風險與建議修正項。
7. [`usb-mcp-use-cases.md`](usb-mcp-use-cases.md)  
   USB MCP hardware bridge 可以發揮巨大效益的應用想像與產品方向。

## 專案位置速查

| 類別 | 路徑 |
| :--- | :--- |
| NuAILink FW root | `SampleCode/FreeRTOS/NuAILink` |
| FW entry point | `SampleCode/FreeRTOS/NuAILink/main.c` |
| NuAILink SDK headers | `SampleCode/FreeRTOS/NuAILink/nualink/include` |
| NuAILink SDK sources | `SampleCode/FreeRTOS/NuAILink/nualink/src` |
| USB CDC transport | `nualink/src/transport/nualink_usb_cdc.c` |
| USB descriptor | `nualink/src/transport/nualink_usb_descriptors.c` |
| JSON-RPC core | `nualink/src/core/mcp_jsonrpc.c` |
| Tool registry | `nualink/src/core/mcp_registry.c` |
| Board HAL | `nualink/src/platform/nualink_board.c` |
| RTOS tasks | `nualink/src/platform/nualink_tasks.c` |
| Plugins | `nualink/src/plugins` |
| Host tools | `SampleCode/FreeRTOS/NuAILink/host_tools` |
| CMSIS solution | `SampleCode/FreeRTOS/NuAILink/VSCode/NuAILink.csolution.yml` |
| CMSIS project | `SampleCode/FreeRTOS/NuAILink/VSCode/NuAILink.cproject.yml` |

## 系統一句話總結

NuAILink 是跑在 M3334KIGAE / Cortex-M33 上的 FreeRTOS 韌體，透過 High-Speed USB CDC ACM 暴露 newline-delimited JSON-RPC 2.0 介面；host 可以把它當成 MCP server，使用 `tools/list` 探索工具，再用 `tools/call` 控制 GPIO、LED、EADC、LLSI/WS2812，並接收按鈕與自動模式事件 notification。

## 目前實作重點

| 面向 | 現況 |
| :--- | :--- |
| USB class | CDC ACM，product string `NuAILink MCP Bridge` |
| USB VID/PID | `0x0416:0xB002` |
| Bulk packet | HS 512 bytes，FS fallback 64 bytes |
| Framing | UTF-8 JSON，一行一 frame，以 `\n` 結尾；`\r` 會被忽略 |
| JSON-RPC | `initialize`、`ping`、`tools/list`、`tools/call` |
| MCP tools | `system.info`、LED、GPIO、button、EADC、LLSI、auto modes，共 13 個 |
| Notification | `button.event`、`led.auto.event`、`gpio.auto.event`、`eadc.auto.event` |
| RX request buffer | `NUALINK_JSON_REQUEST_SIZE = 1024` bytes |
| TX response buffer | `NUALINK_JSON_RESPONSE_SIZE = 6144` bytes |
| FreeRTOS heap | `heap_4.c`，`configTOTAL_HEAP_SIZE = 64 * 1024` |
| Core clock | 180 MHz |
| Debug UART | UART0 PB12/PB13, 115200 |
| Heartbeat LED | PC14 active-low |

## 最短上手流程

1. 從 `SampleCode/FreeRTOS/NuAILink/VSCode` 建置 CMSIS project。
2. 使用 pyOCD / Nu-Link 將 `out/NuAILink+FreeRTOS.cbuild-run.yml` 對應 image 燒錄到板子。
3. Host 端確認出現 CDC serial port，例如 Windows `COM4`。
4. 執行 `host_tools/nualink_regression.py` 或 `run_regression.ps1` 驗證 `initialize`、`ping`、`tools/list`、`system.info`、LED 與壓力 ping。
5. 若要接標準 MCP client，使用 `host_tools/nualink_stdio_bridge.py` 將 stdio JSON-RPC bridge 到 CDC serial。

詳情請看 [`build-flash-debug.md`](build-flash-debug.md) 與 [`host-tools-and-testing.md`](host-tools-and-testing.md)。

## 接手者最需要先知道的 5 件事

1. **目前 request 解析實際上在 `USB_Comm` task inline 執行。**  
   `MCP_Parse` task 與 `s_request_queue` 已建立，但目前沒有被餵資料；這是未來要把長時間 plugin action 從 USB task 拆出去的預留架構。
2. **USB TX 多封包回應已修過關鍵 bug。**  
   `NuAILink_TransportWrite()` 必須用 `TXPKIF/TXPKIEN` 等待封包真正傳完，而不是用 `INTKIF`；並且每個 chunk 前 flush EPA FIFO。
3. **CDC serial 的 baud rate 是 metadata。**  
   Host 仍可用 `115200` 打開 port，但實際資料透過 USB bulk 傳輸，不是 UART baud 速度。
4. **工具 schema 是給 host / MCP client 探索用，韌體仍靠 callback 手動驗證參數。**  
   新增 plugin 時，schema 與 callback validation 必須同步更新。
5. **監測 `system.info.structuredContent.usbRxDropCount` 很重要。**  
   USB OUT 目前只有一個 pending packet buffer；host burst 太猛或 plugin callback 太久都可能造成 drop。

## 文件維護原則

- 若新增 tool，請同步更新：
  - `mcp_registry.c`
  - plugin 的 `input_schema_json`
  - [`data-model-and-tools.md`](data-model-and-tools.md)
  - Python host example / regression coverage
- 若修改 USB framing、buffer size、error code，請同步更新：
  - [`mcp-usb-cdc-protocol.md`](mcp-usb-cdc-protocol.md)
  - [`host-tools-and-testing.md`](host-tools-and-testing.md)
- 若修改 task / queue / IRQ 優先權，請同步更新：
  - [`firmware-architecture.md`](firmware-architecture.md)
  - [`code-review-handoff-notes.md`](code-review-handoff-notes.md)
