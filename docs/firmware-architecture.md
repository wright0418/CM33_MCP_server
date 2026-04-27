# NuAILink M3334 韌體架構

> 本文件從目前程式碼反推實際架構，而不是只描述早期規劃。  
> 主要程式位置：`SampleCode/FreeRTOS/NuAILink`。

## 1. 系統定位

NuAILink 韌體是一個「MCP hardware bridge」：

- MCU：Nuvoton M3334KIGAE / M3331 series BSP。
- RTOS：FreeRTOS on Cortex-M33。
- Host transport：USB High-Speed Device CDC ACM。
- Application protocol：newline-delimited JSON-RPC 2.0。
- MCP surface：`initialize`、`ping`、`tools/list`、`tools/call`。
- Hardware tools：system info、PC14 LED/GPIO/BPWM、PB14 button、GPIO read/write/auto、EADC CH8/CH9、LLSI0 PB15 WS2812 patterns。

整體精神是讓 host 或 AI agent 不必知道暫存器細節，而是用語義化 tool name 和 JSON arguments 控制硬體。

## 2. 目錄與分層

```text
SampleCode/FreeRTOS/NuAILink/
  main.c
  FreeRTOSConfig.h
  host_tools/
  nualink/
    include/
      mcp_types.h
      mcp_registry.h
      mcp_jsonrpc.h
      mcp_response.h
      nualink_board.h
      nualink_config.h
      nualink_log.h
      nualink_tasks.h
      nualink_transport.h
    src/
      core/
        mcp_jsonrpc.c
        mcp_registry.c
        mcp_response.c
      platform/
        nualink_board.c
        nualink_tasks.c
      plugins/
        plugin_system.c
        plugin_led_gpio.c
        plugin_gpio.c
        plugin_eadc.c
        plugin_llsi.c
      transport/
        nualink_usb_cdc.c
        nualink_usb_descriptors.c
  VSCode/
    NuAILink.csolution.yml
    NuAILink.cproject.yml
```

| Layer | 主要檔案 | 職責 |
| :--- | :--- | :--- |
| App bootstrap | `main.c` | 開機、BoardInit、cJSON hooks、建立 tasks、啟動 scheduler、fault/assert hooks |
| Config | `FreeRTOSConfig.h`, `nualink_config.h` | RTOS、buffer、版本、debug log 開關 |
| Platform / Board HAL | `nualink_board.c` | clock、UART0、USB PHY、PC14 LED、PB14 button、GPIO、EADC、LLSI |
| RTOS orchestration | `nualink_tasks.c` | queue、task、USB RX framing、response queue、auto-mode pump、notification queueing |
| Transport | `nualink_usb_cdc.c`, `nualink_usb_descriptors.c` | CDC ACM descriptor、HSUSBD endpoint、IRQ、packet RX/TX |
| MCP core | `mcp_jsonrpc.c`, `mcp_response.c`, `mcp_registry.c` | JSON-RPC dispatch、tool registry、response/error serialization |
| Plugins | `plugin_*.c` | 每個 hardware tool 的 schema、argument validation、callback、auto mode |
| Host tools | `host_tools/*.py` | serial smoke、regression、stdio bridge、stress test、feature demos |

## 3. 啟動流程

`main.c` 的實際流程：

```text
Reset_Handler / SystemInit
        |
        v
main()
  SYS_UnlockReg()
  optional boot probe LED init
  SYS_LockReg()
  optional 3 blinks
        |
        v
NuAILink_BoardInit()
  PC14 GPIO heartbeat LED
  early UART0 init on HIRC
  enable HIRC + HXT
  set HCLK = 180 MHz
  reopen UART0 at new clock
  enable GPIO + TMR0 clocks
  power up HS USB PHY
  enable HSUSBD module clock
        |
        v
NuAILink_CJSONInitHooks()
  cJSON malloc/free -> FreeRTOS pvPortMalloc/vPortFree
        |
        v
NuAILink_TasksCreate()
  create static queues
  create USB_Comm task
  create MCP_Parse task (currently legacy/future path)
  create Heartbeat task
  init PB14 button interrupt
        |
        v
vTaskStartScheduler()
```

### Boot diagnostics

`NUALINK_ENABLE_BOOT_DIAGNOSTICS` 預設為 `0`。打開後，PC14 active-low LED 會提供 early boot checkpoint：

| LED pattern | 意義 |
| :--- | :--- |
| 3 quick blinks | `main()` reached |
| 6 quick blinks | `NuAILink_BoardInit()` returned |
| 9 quick blinks | `NuAILink_TasksCreate()` returned `pdPASS` |
| solid ON | task create failed 或 fatal halt |
| heartbeat | scheduler 正常運作 |

## 4. Clock、pin 與板級資源

`NuAILink_BoardInit()` 目前做了以下板級初始化：

| 資源 | 設定 |
| :--- | :--- |
| HCLK | `CLK_SetCoreClock(FREQ_180MHZ)`，`SystemCoreClockUpdate()` |
| PCLK | APB0/APB1 divide by 2 |
| UART0 | PB12 RXD / PB13 TXD，115200，作為 `printf` debug console |
| PC14 | active-low LED，預設 heartbeat GPIO output；也可切到 BPWM2_CH0 |
| USB PHY | HS USB device role，`HSUSBEN + HSUSBACT`，enable `HSUSBD_MODULE` |
| PB14 | active-low button，GPIO input + pull-up + both-edge interrupt |
| PB8/PB9 | EADC0 CH8/CH9，使用時才 lazy init，disable digital path |
| PB15 | LLSI0 OUT，控制 WS2812 / RGB LED chain |

### Pin ownership 注意事項

| Pin | 目前用途 | 注意 |
| :--- | :--- | :--- |
| PB12/PB13 | UART0 debug console | `gpio.write` 明確禁止寫入 |
| PB14 | button input + interrupt | `gpio.write` 明確禁止寫入 |
| PC14 | LED / GPIO / BPWM2_CH0 | `led.set` 會切回 GPIO；`led.bpwm.set` 會切到 BPWM；`gpio.write C14` 會停止 BPWM |
| PB8/PB9 | EADC analog input | 使用 EADC 後 digital path disabled |
| PB15 | LLSI0 output | 給 WS2812-like LED 使用 |

## 5. FreeRTOS 設定

重要設定在 `FreeRTOSConfig.h`：

| Setting | Value | 影響 |
| :--- | :--- | :--- |
| `configCPU_CLOCK_HZ` | `180000000` | kernel tick 以 180 MHz 為基準 |
| `configTICK_RATE_HZ` | `100` | 10 ms tick |
| `configMAX_PRIORITIES` | `7` | USB / parser / heartbeat 分層 |
| `configUSE_TIME_SLICING` | `0` | 同優先權不因 tick 自動輪轉，排程較可預測 |
| `configTOTAL_HEAP_SIZE` | `64 * 1024` | cJSON + dynamic allocation 空間 |
| Heap source | `heap_4.c` | 支援 free block coalescing，較適合 JSON alloc/free |
| `configCHECK_FOR_STACK_OVERFLOW` | `2` | stack overflow hook 啟用 |
| `configUSE_MALLOC_FAILED_HOOK` | `1` | malloc failed hook 啟用 |
| `configCHECK_HANDLER_INSTALLATION` | `0` | 避免間接 vector routing 造成 FreeRTOS assert |
| `configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY` | `5` | FromISR API 的 IRQ priority boundary |

## 6. Task 與 Queue 架構

### Static queues

`nualink_tasks.c` 定義兩種 frame 物件：

| Type | Payload size | Queue length | 用途 |
| :--- | :--- | :--- | :--- |
| `nualink_request_t` | `NUALINK_JSON_REQUEST_SIZE = 1024` | `2` | 預留 request queue；目前未實際使用 |
| `nualink_response_t` | `NUALINK_JSON_RESPONSE_SIZE = 6144` | `2` | response / notification TX queue |

> 現況重點：`s_request_queue` 與 `MCP_Parse` task 已建立，但 USB task 目前在收到完整 newline frame 後直接呼叫 `MCP_JSONRPC_Handle()` inline 處理，沒有把 request 丟進 `s_request_queue`。

### Tasks

| Task name | Function | Priority | Stack words | 現況職責 |
| :--- | :--- | :--- | :--- | :--- |
| `USB_Comm` | `prvUsbCommTask` | `configMAX_PRIORITIES - 2` | 2048 | init transport、poll USB、TX response queue、RX packet、newline framing、inline JSON-RPC、auto modes |
| `MCP_Parse` | `prvParserTask` | `configMAX_PRIORITIES - 3` | 4096 | 預留 parser task；目前等待 request queue，沒有資料來源 |
| `Heartbeat` | `prvHeartbeatTask` | `1` | 1024 | 每 `NUALINK_HEARTBEAT_PERIOD_MS` toggle PC14 heartbeat |

### Runtime data flow（目前實作）

```text
Host CDC serial
   |
   | USB OUT bulk packet
   v
USBD20_IRQHandler
   | copy EPB FIFO -> s_usb_rx_buffer
   | set s_bulk_out_ready
   v
USB_Comm task
   | NuAILink_TransportReadPacket()
   | append bytes until '\n'
   | ignore '\r'
   | reject oversized frame
   v
MCP_JSONRPC_Handle()  <-- inline inside USB_Comm
   | cJSON parse
   | method dispatch
   | tool callback
   v
response queue
   |
   v
USB_Comm task
   | NuAILink_TransportWrite()
   v
USB IN bulk packet(s)
   |
   v
Host CDC serial
```

### Auto modes

`USB_Comm` task 每輪 loop 呼叫：

```text
NuAILink_LedAutoProcess()
NuAILink_GpioAutoProcess()
NuAILink_EadcAutoProcess()
NuAILink_LlsiAutoplayProcess()
```

這代表 auto mode 的 timing 由 `USB_Comm` 1 ms loop 近似驅動；若 tool callback 或 USB TX/RX 卡住，auto mode timing 也會被影響。

## 7. USB CDC transport 架構

`nualink_usb_descriptors.c` 宣告 CDC ACM device：

| 欄位 | 值 |
| :--- | :--- |
| VID | `0x0416` |
| PID | `0xB002` |
| Manufacturer | `Nuvoton` |
| Product | `NuAILink MCP Bridge` |
| Class | CDC ACM：communication interface + data interface |

Endpoint 配置在 `nualink_usb_cdc.c`：

| Endpoint | Direction | Type | EP num | HS max packet | FS max packet | 用途 |
| :--- | :--- | :--- | :--- | ---: | ---: | :--- |
| CEP | control | control | 0 | 64 | 64 | setup/class requests |
| EPA | IN | bulk | 1 | 512 | 64 | device -> host JSON response |
| EPB | OUT | bulk | 2 | 512 | 64 | host -> device JSON request |
| EPC | IN | interrupt | 3 | 64 | 64 | CDC notification endpoint（目前未傳 app event） |

### RX path

1. EPB interrupt 發生。
2. ISR 讀 `EPDATCNT`。
3. 若 `s_bulk_out_ready == 0` 且長度不超過 `s_usb_rx_buffer`：
   - 從 EPB FIFO copy 到 `s_usb_rx_buffer`。
   - `s_bulk_out_ready = 1`。
   - 暫停 EPB RX interrupt，等待 task 消化。
4. 若上一包尚未消化：
   - drain FIFO。
   - `s_rx_drop_count++`。
5. `USB_Comm` task 呼叫 `NuAILink_TransportReadPacket()`，在 critical section 取走 packet，重新 enable EPB RX interrupt。

`system.info` 會回報 `usbRxDropCount`，可用來監控 host burst 或 firmware blocking 是否造成掉包。

### TX path

`NuAILink_TransportWrite()` 會：

1. 依目前速度使用 `s_bulk_in_max_packet` 切 chunk（HS 512 / FS 64）。
2. 等待上一個 EPA TX 完成（`s_tx_busy == 0`）。
3. 每個 chunk 前 flush EPA FIFO，避免殘留 bytes 造成 frame shift。
4. 寫入 EPA FIFO。
5. 清除 stale `TXPKIF | INTKIF`。
6. 只有最後一個 chunk 設 `SHORTTXEN`。
7. 設 `EPTXCNT`，enable `TXPKIEN`。
8. 等 EPA interrupt 以 `TXPKIF` 清掉 `s_tx_busy`。

> Code review 重點：長回應（例如 `tools/list`）跨多個 512-byte packet。這裡必須用 `TXPKIF/TXPKIEN` 作為 pacing，不能用 `INTKIF`，否則 IN token 可能早於實際 packet 完成，造成 FIFO 被過早覆寫。

## 8. MCP core 架構

### Tool 型別

`mcp_types.h`：

```c
typedef int32_t (*mcp_tool_callback_t)(const cJSON *arguments,
                                       cJSON *result,
                                       void *context);

typedef struct
{
    const char *name;
    const char *description;
    const char *input_schema_json;
    mcp_tool_callback_t callback;
    void *context;
} mcp_tool_t;
```

### Registry

`mcp_registry.c` 以 static table 註冊所有 tool：

```text
system.info
led.set
led.bpwm.set
led.auto
gpio.read
gpio.write
button.read
gpio.auto
eadc.read
eadc.auto
llsi.fill
llsi.pattern
llsi.autoplay
```

新增 tool 的基本步驟：

1. 在 plugin `.c` 檔宣告 `const mcp_tool_t gNuAILinkXxxTool`。
2. 在 `mcp_registry.c` 加 `extern`。
3. 把 tool pointer 加進 `s_tools[]`。
4. 在 `VSCode/NuAILink.cproject.yml` 加入新的 source file。
5. 更新 host regression 與本文件。

### JSON-RPC dispatch

`MCP_JSONRPC_Handle()` 支援：

| Method | Handler |
| :--- | :--- |
| `initialize` | 回 protocol version、capabilities、serverInfo |
| `ping` | 回 `status=ok` 與 FreeRTOS tick |
| `tools/list` | 從 registry 產生 tool array 與 input schema |
| `tools/call` | 找 tool、驗證 `params.name` / `params.arguments`、呼叫 callback |

Response 由 `mcp_response.c` 統一序列化，使用 compact JSON 並在尾端加 `\n`。

## 9. Plugin 架構

每個 plugin 通常包含：

1. `static const char s_xxx_schema[]`：給 MCP client 的 JSON Schema。
2. `static int32_t prvXxxCallback(...)`：參數驗證與硬體動作。
3. result builder：產生 `content` + `structuredContent` + `isError`。
4. 若有 autonomous mode：維護 static state，並提供 `NuAILink_XxxAutoProcess()`。
5. `const mcp_tool_t gNuAILinkXxxTool`：註冊用 tool object。

目前 plugin：

| Plugin | Tools | Hardware |
| :--- | :--- | :--- |
| `plugin_system.c` | `system.info` | clock、heap、USB drop counter、LED state |
| `plugin_led_gpio.c` | `led.set`, `led.bpwm.set`, `led.auto` | PC14 GPIO / BPWM2_CH0 |
| `plugin_gpio.c` | `gpio.read`, `gpio.write`, `button.read`, `gpio.auto` | GPIO A-H、PB14 button |
| `plugin_eadc.c` | `eadc.read`, `eadc.auto` | EADC0 CH8/PB8, CH9/PB9 |
| `plugin_llsi.c` | `llsi.fill`, `llsi.pattern`, `llsi.autoplay` | LLSI0 PB15 WS2812 |

## 10. Error / fault handling

`main.c` 提供：

| Hook / Handler | 行為 |
| :--- | :--- |
| `vAssertCalled()` | 印 file/line、VTOR、SVC/PendSV vector，進 fatal halt |
| `HardFault_Handler()` | 印 HFSR/CFSR/MMFAR/BFAR/ICSR，進 fatal halt |
| `MemManage_Handler()` | 同上 |
| `BusFault_Handler()` | 同上 |
| `UsageFault_Handler()` | 同上 |
| `vApplicationStackOverflowHook()` | 印 task name，PC14 solid on，停 IRQ |
| `vApplicationMallocFailedHook()` | 印 malloc failed，PC14 solid on，停 IRQ |

`NUALINK_ERR()` 不受 `NUALINK_ENABLE_DEBUG_LOG` 限制，會用 `printf` 輸出到 UART0。

## 11. 架構現況與下一步方向

目前架構已能穩定完成 USB CDC JSON-RPC 與多個硬體 tool，但如果要走向較完整 SDK，建議下一步是：

1. 將 `USB_Comm` 的 inline `MCP_JSONRPC_Handle()` 改成 request queue + parser/action task，避免長 callback 擋 USB RX。
2. 將 notification queue 做成可觀測：記錄 notification drop count。
3. 對 board HAL API 增加防禦式參數檢查，讓它不只依賴 plugin 層 validation。
4. 對 auto mode timing 改用 dedicated task 或 timer-driven schedule，降低 USB traffic 對 animation/sampling jitter 的影響。
5. 把 host protocol test 擴成 CI-friendly mock / loopback 測試，避免每次都需要實板。
