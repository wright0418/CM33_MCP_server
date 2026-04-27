# NuAILink RTOS 專案移植計畫

> 最新決策：本資料夾已是由 `SampleCode/FreeRTOS/Blinky` 重新複製而來的獨立 NuAILink FreeRTOS project。第三方 cJSON 放在 `ThirdParty/cJSON`，並由 `VSCode/NuAILink.cproject.yml` 直接引用 `cJSON.c` 與 `cJSON.h`。

本文件規劃如何在目前的 `SampleCode/FreeRTOS/NuAILink` 獨立專案底下，逐步建立 NuAILink SDK。此專案由 Blinky 複製而來，因此保留已驗證的 FreeRTOS 啟動流程、M3331 clock/pin 初始化與 PC14 heartbeat，再把 MCP/JSON-RPC、HS USB CDC transport、BSP driver plugin framework 分層導入。

> 註解：這份文件是「實作前藍圖」。它先定義架構、檔案落點、driver/library 依賴與驗證順序，避免一開始就把 USB、RTOS、JSON、各 peripheral plugin 混在同一個 `main.c` 裡。

## 1. NuAILink 初始基準

目前 NuAILink 專案沿用 Blinky 的最小 RTOS 骨架：

| 項目 | 現況 | NuAILink 用法 |
| :--- | :--- | :--- |
| 入口點 | `main.c` 先呼叫 `prvSetupHardware()`，再建立 static task | 保留此模式，將 `main.c` 改成只負責 board init 與 task bootstrap |
| RTOS 物件 | `xTaskCreateStatic()`、static TCB/stack | NuAILink task、queue、message buffer 優先使用 static allocation |
| Clock | `CLK_SetCoreClock(FREQ_180MHZ)` | 沿用 180MHz 作為 USB、JSON parsing、plugin scheduling 的效能基準 |
| GPIO LED | PC14 output，低電位點亮 | 作為 Phase 1 `led.set`、heartbeat、錯誤狀態指示 |
| UART0 | PB12/PB13，115200 debug console | 保留為 debug log 與 bring-up 診斷通道 |
| FreeRTOSConfig | heap 目前 4096 bytes，`heap_2.c` | NuAILink 需調整為較大 heap，並建議改用 `heap_4.c` |
| VSCode CMSIS project | 目前只加入 `clk/sys/uart/gpio/retarget` | 需加入 HSUSBD、PDMA、Timer、EADC、PWM、I2C、LLSI/ELLSI 等相關 BSP driver |

> 註解：NuAILink 的核心功能會比 Blinky 大很多，尤其 cJSON parsing、USB message buffer、tool schema、plugin callback 都會吃 RAM。Blinky 目前的 4KB heap 只夠 demo，不足以支撐 MCP flow。

## 2. 建議檔案佈局

NuAILink 已是 `SampleCode/FreeRTOS` 下的獨立 project；後續功能檔案建議集中在 `nualink/` 子目錄，避免核心 SDK、transport、plugin 程式碼散落在專案根目錄。

```text
SampleCode/FreeRTOS/NuAILink/
  main.c                         # 保留為 RTOS bootstrap 與 board init 入口
  FreeRTOSConfig.h               # 調整 heap、priority、diagnostics
  NuAILink_RTOS_Blinky_Plan.md   # 本規劃文件；名稱保留 Blinky 字樣以標示移植來源
  nualink/
    include/
      nualink_config.h           # 專案層設定：buffer size、tool 數量、feature switch
      nualink_board.h            # M3331 board init、pin map、clock init 對外介面
      nualink_tasks.h            # RTOS task/queue 建立入口
      nualink_transport.h        # USB CDC transport 抽象介面
      mcp_types.h                # mcp_tool_t、request/result/error 型別
      mcp_registry.h             # tool registry API
      mcp_jsonrpc.h              # JSON-RPC parser/response API
    src/
      platform/
        nualink_board.c          # 從 Blinky + HSUSBD sample 整合 clock/MFP/IRQ
        nualink_tasks.c          # 建立 USB、MCP、Plugin、Notify task
      transport/
        nualink_usb_cdc.c        # 從 HSUSBD_VCOM_SerialEmulator 改造
        nualink_usb_descriptors.c# NuAILink CDC descriptor/product string
      core/
        mcp_registry.c           # 註冊工具清單與查找
        mcp_jsonrpc.c            # initialize/tools/list/tools/call/ping
        mcp_response.c           # 統一 JSON response/error builder
      plugins/
        plugin_system.c          # system.info/system.heap/system.latency
        plugin_led_gpio.c        # led.set/gpio.read/gpio.write/button notification
        plugin_pwm.c             # Phase 2：EPWM/BPWM 調光
        plugin_eadc.c            # Phase 2：EADC 讀取
        plugin_i2c.c             # Phase 2：I2C sensor template
        plugin_modbus.c          # Phase 2：UART/RS485 Modbus RTU
        plugin_llsi.c            # Phase 2：LLSI/ELLSI LED matrix
  host_tools/
    nualink_serial_smoke.py      # Linux /dev/ttyACM* JSON-RPC smoke test
```

> 註解：`nualink_transport.h` 的存在很重要。Phase 1 可用 CDC serial，未來若改成 vendor bulk、HID 或其他 USB class，MCP core 不需要重寫。

## 3. BSP Driver 與 Library 使用規劃

### 3.1 必要核心依賴

| 類別 | BSP 路徑 | 用途 |
| :--- | :--- | :--- |
| CMSIS Core | `Library/CMSIS/Core/Include` | Cortex-M33 core definitions、NVIC、CMSIS intrinsic |
| Device header | `Library/Device/Nuvoton/m3331/Include` | M3331 register definitions、IRQn、module macros |
| Startup/System | `Library/Device/Nuvoton/m3331/Source/startup_m3331.c`, `system_m3331.c` | Reset handler、SystemInit、clock 基礎 |
| Linker/Scatter | `Library/Device/Nuvoton/m3331/Config` | RAM/flash 配置與 map 檢查基準 |
| StdDriver common | `Library/StdDriver/inc`, `Library/StdDriver/src` | NuMicro peripheral APIs |
| FreeRTOS | `ThirdParty/FreeRTOS` | RTOS kernel、queue、task、timer、message/stream buffer |
| Retarget | `Library/StdDriver/src/retarget.c` | UART printf debug output |

> 註解：NuAILink 不應自行定義暫存器 bit 或硬寫 magic address；凡是 BSP 已有的 module clock、pin macro、driver API，都應從 `NuMicro.h` 與 StdDriver 取得。

### 3.2 Transport 與 RTOS 必用 driver

| Driver/Library | Source | NuAILink 職責 |
| :--- | :--- | :--- |
| `clk.c` | `Library/StdDriver/src/clk.c` | 180MHz HCLK、UART0、TMR、HSUSBD、各 peripheral clock 設定 |
| `sys.c` | `Library/StdDriver/src/sys.c` | protected register unlock/lock、MFP pin macro 支援 |
| `gpio.c` | `Library/StdDriver/src/gpio.c` | PC14 heartbeat、GPIO plugin、button interrupt notification |
| `uart.c` | `Library/StdDriver/src/uart.c` | UART0 debug console、RS485/Modbus plugin 基礎 |
| `timer.c` | `Library/StdDriver/src/timer.c` | latency timestamp、debounce、plugin timeout |
| `hsusbd.c` | `Library/StdDriver/src/hsusbd.c` | High-Speed USB device CDC transport |
| `pdma.c` | `Library/StdDriver/src/pdma.c` | 後續加速 USB-adjacent buffer、EADC、I2C/UART、LLSI 資料搬移 |
| FreeRTOS `heap_4.c` | `ThirdParty/FreeRTOS/portable/MemMang/heap_4.c` | cJSON 與 message buffer 動態配置時降低碎片風險 |

> 註解：目前 Blinky CMSIS project 使用 `heap_2.c`，建議改為 `heap_4.c`。`heap_4.c` 支援相鄰 free block 合併，比 `heap_2.c` 更適合 JSON parse/free 的生命週期。

### 3.3 MCP plugin 相關 driver

| Plugin | BSP driver/library | 參考 sample |
| :--- | :--- | :--- |
| LED/GPIO | `gpio.c/h` | `SampleCode/StdDriver/GPIO_OutputInput`, `GPIO_INT`, `GPIO_EINTAndDebounce` |
| Button notification | `gpio.c/h`, `timer.c/h` | `GPIO_EINTAndDebounce` |
| PWM dimming | `epwm.c/h`, `bpwm.c/h` | `EPWM_OutputWaveform`, `EPWM_SwitchDuty`, `BPWM_OutputWaveform` |
| ADC read | `eadc.c/h` | `EADC_SWTRG_Trigger`, `EADC_Timer_Trigger`, `EADC_PDMA_EPWM_Trigger` |
| I2C sensor | `i2c.c/h`, optional `usci_i2c.c/h` | `I2C_Master`, `I2C_EEPROM`, `I2C_PDMA_TRX` |
| Modbus RTU | `uart.c/h`, optional `usci_uart.c/h`, `pdma.c/h` | `UART_RS485`, `UART_PDMA`, `ISP_RS485` |
| LED matrix | `llsi.c/h`, `ellsi.c/h`, `pdma.c/h`, `Library/LlsiYcableLib` | `LLSI_Y_Cable_Control`, `ELLSI_Marquee`, `ELLSI_PDMA_Marquee_Y_Cable` |
| SPI template | `spi.c/h`, optional `usci_spi.c/h`, `qspi.c/h` | `SPI_*`, `USCI_SPI_*`, `QSPI_*` samples |
| Watchdog/System reliability | `wdt.c/h`, `wwdt.c/h` | `WDT_*`, `WWDT_*` samples |

> 註解：不是每個 driver 都要在 Phase 1 編進來。Phase 1 先完成 USB + MCP + GPIO/LED 最小閉環；其餘 driver 以 feature switch 加入，避免一開始就讓 RAM/flash 與 IRQ 關係過度複雜。

### 3.4 不建議作為 Phase 1 依賴的 library

| Library | 判斷 |
| :--- | :--- |
| `Library/UsbHostLib` | NuAILink 目前是 USB device 連 Linux host，不是 M3331 當 USB host；暫不納入 firmware core |
| `ThirdParty/FatFs` | 可作為後續 log/recording plugin，但 Phase 1 不需要檔案系統 |
| `ThirdParty/LibMAD`, `emWin`, `lvgl` | 屬於多媒體/UI sample，與 MCP transport/core 無直接關係 |
| `Library/NuMaker/xmodem` | 可留作未來 firmware update 工具，不放在初始 MCP core |

> 註解：「使用 BSP 所有相關 driver/library」不等於把所有 BSP source 都無條件編入。這裡採用相關性分層：core 必用先進 project，plugin 需要時再開 feature，非相關 library 保持外部可選。

## 4. RTOS 任務與資料流

建議將 NuAILink 拆成四個 application task，加上一個 heartbeat/diagnostic task：

| Task | 建議優先權 | Stack 初始值 | 職責 |
| :--- | :--- | :--- | :--- |
| `USB_Comm_Task` | 最高 application priority | 1024 words | 啟動 HSUSBD、收 CDC bytes、組 newline-delimited JSON-RPC frame、送 TX response |
| `MCP_Parser_Task` | 次高 | 4096 words | cJSON parse、JSON-RPC method dispatch、`tools/list` response builder |
| `Plugin_Action_Task` | 中 | 2048 words | 執行 GPIO/PWM/EADC/I2C/Modbus/LLSI 等 BSP driver callback |
| `Notify_Task` | 中低 | 1024 words | 將 button/event/threshold 轉成 MCP notification |
| `Heartbeat_Task` | 低 | 512 words | PC14 LED heartbeat、fault pattern、alive indicator |

> 註解：目前 `configMAX_PRIORITIES` 是 5，Timer task 又設在最高 priority。NuAILink 實作時建議調整為 6 或 7 個 priority，並避免 software timer task 與 USB task 同優先權互搶。

資料流建議如下：

```text
Linux Host / MCP Bridge
        |
        | USB CDC, /dev/ttyACM*, newline-delimited JSON-RPC
        v
USBD20_IRQHandler
        |
        | 只搬 endpoint 狀態與 bytes，使用 ISR-safe signal
        v
USB_Comm_Task
        |
        | 完整 JSON-RPC message
        v
MCP_Parser_Task
        |
        | tool call command
        v
Plugin_Action_Task ----> BSP StdDriver / LlsiYcableLib
        |
        | result/error
        v
USB_Comm_Task ----> Linux Host

GPIO/EADC/Timer ISR ----> Notify_Task ----> USB_Comm_Task ----> Linux Host
```

> 註解：`USBD20_IRQHandler()` 不做 cJSON parsing，也不直接呼叫 plugin。ISR 只做最小工作，否則 USB burst + JSON parse 會造成 interrupt latency 與 RTOS 排程不可控。

## 5. USB CDC Transport 計畫

Phase 1 建議從 `SampleCode/StdDriver/HSUSBD_VCOM_SerialEmulator` 改造，而不是從零寫 USB descriptor。

沿用項目：

- `hsusbd.c/h` 的 HSUSBD controller API。
- `descriptors.c` 的 CDC ACM descriptor 結構。
- `vcom_serial.c/h` 的 CEP/EPA/EPB/EPC endpoint init flow。
- EPA Bulk IN endpoint 1、EPB Bulk OUT endpoint 2、EPC Interrupt IN endpoint 3。
- High-Speed bulk max packet 512 bytes，Full-Speed fallback 64 bytes。
- `CEP_BUF_BASE=0x000`、`EPA_BUF_BASE=0x200`、`EPB_BUF_BASE=0x400`、`EPC_BUF_BASE=0x600` 的 USB SRAM endpoint buffer 配置。

NuAILink 需要改造項目：

- 將 sample 的 USB-UART bridge buffer 改成 USB-to-MCP message buffer。
- Product string 改為 `NuAILink MCP Bridge`。
- CDC serial framing 採 newline-delimited JSON-RPC：每筆 JSON 以 `\n` 結尾。
- RX application buffer 初始規劃 1024 bytes，TX application buffer 初始規劃 512 bytes。
- 所有 USB DMA/application buffer 使用 4-byte alignment。
- 若一筆 request 超過 RX buffer，回覆 JSON-RPC error：`message_too_large`。

> 註解：CDC ACM 對 Linux 最容易 bring-up，會自然出現 `/dev/ttyACM*`。標準 MCP client 通常期待 stdio/SSE，因此建議 host 端再做一個 bridge，把 stdio MCP request 轉成 CDC serial JSON-RPC。

## 6. MCP Core 設計

核心資料結構：

```c
typedef struct
{
    const char *name;
    const char *description;
    const char *input_schema_json;
    int32_t (*callback)(const cJSON *arguments, cJSON *result, void *context);
} mcp_tool_t;
```

> 註解：`name`、`description`、`input_schema_json` 建議放在 flash 的 `const` 字串，避免開機後動態建立大型 schema 造成 SRAM 壓力。

Phase 1 method：

| JSON-RPC method | 韌體行為 |
| :--- | :--- |
| `initialize` | 回覆 NuAILink protocol version、firmware version、capabilities |
| `tools/list` | 從 static registry 產生 MCP tools list |
| `tools/call` | 查找工具、驗證 arguments、排程 plugin callback |
| `ping` | 回覆 alive 與 tick count，用於 latency 測試 |

錯誤處理：

- JSON parse 失敗：`-32700 Parse error`。
- method 不存在：`-32601 Method not found`。
- arguments 不合法：`-32602 Invalid params`。
- tool 執行失敗：使用 MCP tool result 或 JSON-RPC application error，依呼叫階段區分。
- queue 滿載：回覆 `server_busy`，並記錄 drop counter。

> 註解：`tools/call` 不應長時間阻塞 parser task。耗時硬體動作交給 `Plugin_Action_Task`，parser task 只負責驗證與排程。

## 7. FreeRTOSConfig 調整建議

| 設定 | 目前 Blinky | NuAILink 建議 |
| :--- | :--- | :--- |
| `configTOTAL_HEAP_SIZE` | 4096 | 初始 64KB，實測後依 map/high-water mark 調整 |
| Heap source | `heap_2.c` | 改為 `heap_4.c` |
| `configMAX_PRIORITIES` | 5 | 6 或 7，保留 USB/parser/plugin 層級差 |
| `configUSE_TIME_SLICING` | 0 | 可維持 0，讓優先權設計更可預測 |
| `configUSE_TRACE_FACILITY` | 0 | bring-up 階段可設 1，穩定後再關閉 |
| `configGENERATE_RUN_TIME_STATS` | 0 | latency tuning 階段可設 1，需 timer counter 支援 |
| `configCHECK_FOR_STACK_OVERFLOW` | 2 | 維持 2 |
| `configUSE_MALLOC_FAILED_HOOK` | 0 | 建議設 1，捕捉 cJSON/queue allocation 問題 |

> 註解：M3331 linker/scatter 檔顯示 RAM 應以 BSP 設定為準。若需求文件寫 384KB，但 linker 實際配置不同，實作時要以 map file 與實際料號確認，不可直接假設。

## 8. VSCode CMSIS Project 整合點

在 `VSCode/NuAILink.cproject.yml` 內建議分階段加入：

### Phase 1 source group

- `../nualink/src/platform/nualink_board.c`
- `../nualink/src/platform/nualink_tasks.c`
- `../nualink/src/transport/nualink_usb_cdc.c`
- `../nualink/src/transport/nualink_usb_descriptors.c`
- `../nualink/src/core/mcp_registry.c`
- `../nualink/src/core/mcp_jsonrpc.c`
- `../nualink/src/core/mcp_response.c`
- `../nualink/src/plugins/plugin_system.c`
- `../nualink/src/plugins/plugin_led_gpio.c`
- cJSON source，例如 `../../../../ThirdParty/cJSON/cJSON.c`

### Phase 1 include path

- `../nualink/include`
- `../../../../Library/CMSIS/Core/Include`
- `../../../../Library/Device/Nuvoton/m3331/Config`
- `../../../../Library/Device/Nuvoton/m3331/Include`
- `../../../../Library/StdDriver/inc`
- `../../../../ThirdParty/FreeRTOS/include`
- `../../../../ThirdParty/FreeRTOS/portable/GCC/ARM_CM33_NTZ/non_secure`
- `../../../../ThirdParty/cJSON`

### Driver source 增加順序

1. 保留現有 `clk.c`, `sys.c`, `uart.c`, `gpio.c`, `retarget.c`。
2. 加入 USB：`hsusbd.c`。
3. 加入 timing：`timer.c`。
4. Phase 2 加入 plugin driver：`pdma.c`, `eadc.c`, `epwm.c`, `bpwm.c`, `i2c.c`, `usci_i2c.c`, `llsi.c`, `ellsi.c`, `usci_uart.c`。
5. 需要 SPI template 時再加入：`spi.c`, `usci_spi.c`, `qspi.c`。
6. 需要 reliability tool 時再加入：`wdt.c`, `wwdt.c`。

> 註解：若使用 root CMake 的 `standard_driver`，driver source 已幾乎全包；但目前 NuAILink VSCode CMSIS project 是手動列檔，所以要在 `.cproject.yml` 明確加入 NuAILink 需要的 source。

## 9. 實作階段

### Phase 0：保護 NuAILink 初始基準

- 確認目前 NuAILink 可 build、可燒錄、PC14 可閃爍。
- 保留 PC14 heartbeat task，之後任何階段都用它判斷 RTOS scheduler 是否仍在跑。

> 註解：先保住 heartbeat，可以在 USB 或 cJSON 出錯時快速分辨「系統死掉」或只是「通訊壞掉」。

### Phase 1：最小 NuAILink 閉環

- 整合 HSUSBD CDC，使 Linux host 看到 `/dev/ttyACM*`。
- 實作 newline-delimited JSON-RPC parser。
- 實作 `initialize`、`ping`、`tools/list`。
- 實作 `led.set`，控制 PC14。
- 實作 `system.info`，回報 clock、tick、heap、firmware version。

> 註解：Phase 1 的驗收標準是 Linux host 發一行 JSON，M3331 回一行 JSON，並能透過 `tools/call` 改變 PC14 LED 狀態。

### Phase 2：基礎硬體模板

- GPIO read/write 與 button notification。
- LLSI RGB LED 燈條控制 10個 WS2812B RGB LED
- EADC single channel read。
- EPWM/BPWM duty/frequency control。
- I2C scan/read/write 與 HTU21-style sensor template。
- UART/RS485 Modbus RTU wrapper。

> 註解：每個 plugin 都必須有自己的 tool schema、argument validation、timeout 與錯誤碼；不可讓錯誤直接卡住 `Plugin_Action_Task`。

### Phase 3：高效能與 LED matrix

- LLSI/ELLSI LED matrix plugin。
- 導入 `Library/LlsiYcableLib/led_control.c`, `led_gen2_control.c`, `pdma_config.c`, `hdiv.c`。
- PDMA 加速 EADC/I2C/UART/LLSI 資料搬移。
- 加入 latency counter 與 queue depth telemetry。

> 註解：LLSI/ELLSI 與 PDMA 的互動較複雜，建議等 MCP core 與 USB transport 穩定後再導入，否則除錯面會太大。

### Phase 4：Host bridge 與壓力測試

- 新增 Python smoke test：直接開 `/dev/ttyACM*` 發送 JSON-RPC（`host_tools/nualink_serial_smoke.py`）。
- 新增 MCP stdio bridge：上層接標準 MCP client，下層轉 CDC serial（`host_tools/nualink_stdio_bridge.py`）。
- 模擬 10 個 logical clients，連續呼叫 `tools/list` 與 `tools/call`（`host_tools/nualink_stress_test.py`）。
- 記錄 latency、timeout/lost response、notification 數、`system.info` 的 heap/USB drop 指標。

> 註解：MCU 端先保持 serial framing 簡單，MCP 生態相容性放在 Linux bridge 處理，整體更容易測試與維護。

## 10. 驗證清單

| 驗證項目 | 預期結果 |
| :--- | :--- |
| Build | CMSIS build 無 error，map file 產生成功 |
| RAM usage | `.bss`、FreeRTOS heap、task stacks、USB buffers 未超過 linker RAM |
| Boot | UART0 印出 NuAILink version，PC14 heartbeat 正常 |
| USB enumeration | Linux `dmesg`/`lsusb` 可看到 NuAILink CDC device |
| JSON-RPC smoke | `initialize`、`ping`、`tools/list` 回覆格式正確 |
| LED tool | `tools/call led.set` 可控制 PC14 |
| ISR boundary | 大量 USB OUT 封包下 ISR 不做 JSON parsing，不造成 RTOS deadlock |
| Queue overflow | 滿載時回覆 `server_busy` 或明確 error，不靜默丟資料 |
| Stack/heap | high-water mark 與 malloc failed hook 無異常 |
| Long run | 8 小時混合 request/notification 不 leak、不卡住、不斷線 |

> 註解：NuAILink 是 protocol + driver + RTOS 的交界系統，不能只用「能編譯」當完成標準。USB enumeration、JSON framing、queue 壓力與長時間穩定性都要列入驗收。

## 11. 主要風險與決策

| 風險/決策 | 規劃處理 |
| :--- | :--- |
| cJSON 不是 BSP 既有元件 | 以 `ThirdParty/cJSON` 加入，並用 `cJSON_InitHooks()` 接 FreeRTOS allocator |
| USB CDC 不是標準 MCP transport | MCU 端用 CDC newline JSON-RPC，Linux 端 bridge 到 stdio MCP |
| RAM 規格與 linker 可能不一致 | 以 `Library/Device/Nuvoton/m3331/Config` 與 map file 為準 |
| ISR 呼叫 FreeRTOS API 的 priority 限制 | 若 ISR 使用 `xTaskNotifyFromISR()`，`USBD20_IRQn` priority 必須符合 `configMAX_SYSCALL_INTERRUPT_PRIORITY` |
| plugin 太多導致 bring-up 困難 | Phase 1 只做 USB/MCP/GPIO；Phase 2 後再逐一加 driver |
| 大型 `tools/list` 超過 TX buffer | response builder 支援分段或固定 buffer 檢查，超限要回錯誤或分批送出 |

> 註解：此計畫採用「先通、再穩、再擴充」的節奏。先把 USB-MCP-GPIO 跑通，後續 driver plugin 才有共同測試框架可以依附。

## 12. 建議下一步

1. 先把 `main.c` 拆成 `nualink_board.c` 與 `nualink_tasks.c`，保留 PC14 heartbeat。
2. 將 `heap_2.c` 改為 `heap_4.c`，並把 heap 提升到 64KB 起測。
3. 從 `HSUSBD_VCOM_SerialEmulator` 複製 CDC transport 必要檔案，改名為 NuAILink module。
4. 加入 cJSON 與最小 `initialize`/`ping`/`tools/list`/`led.set`。
5. 撰寫 `host_tools/nualink_serial_smoke.py`，用 Linux `/dev/ttyACM*` 做端到端 smoke test。

> 註解：完成上述五步後，NuAILink 就會從概念文件變成可被 host 呼叫、可控制板上 LED 的最小 MCP hardware SDK 原型。