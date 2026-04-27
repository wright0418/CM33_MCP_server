# USB MCP Hardware Bridge 的應用想像

> 這份文件刻意放開想像力：如果一顆 M3334 可以用 MCP / JSON-RPC / USB CDC 被 AI agent、測試系統、工廠主機或教學平台直接「語義化」控制，它能在哪些地方創造巨大價值？

## 1. 核心概念：把 MCU 變成 AI 可以理解的硬體 API

傳統 MCU 對上位機通常暴露的是：

- raw UART command
- binary protocol
- Modbus register
- vendor-specific packet
- pin toggling script

NuAILink 的不同之處是：

```text
低階硬體能力
  GPIO / PWM / ADC / LED / Sensor / Bus
        |
        v
語義化 Tool
  led.set, eadc.read, gpio.auto, llsi.autoplay
        |
        v
MCP / JSON-RPC
  schema + structuredContent + notification
        |
        v
AI Agent / 測試程式 / 工廠系統 / 教學平台
```

也就是把 MCU 從「需要工程師記 command byte」變成「可以被工具探索、被 agent 推理、被軟體自動編排」的硬體節點。

## 2. 最直接的價值

| 價值 | 說明 |
| :--- | :--- |
| 可探索 | `tools/list` 讓 host 知道這片板子會什麼，不需要查 PDF 或 header |
| 可驗證 | JSON Schema + structuredContent 讓測試程式可自動判斷結果 |
| 可擴充 | 新增 plugin 就是新增 tool，不必重寫 transport |
| 可橋接 | USB CDC 容易 bring-up；stdio bridge 可接 MCP client |
| 可觀測 | `system.info` 回 heap、tick、USB drop、LED state |
| 可事件化 | button / threshold / auto mode 可主動 notification |
| 可由 AI 操作 | LLM 可以呼叫具名工具，而不是猜 register bit |

## 3. 應用場景一：AI Lab Assistant / 自動化實驗助理

### 情境

工程師在實驗室對新 sensor、LED、actuator 做 bring-up。AI assistant 不只回答問題，還能直接呼叫 NuAILink：

- 打開 LED。
- 掃描 GPIO 狀態。
- 讀 ADC 電壓。
- 根據 button event 切換測試步驟。
- 產生 WS2812 pattern 做視覺提示。

### 效益

| 傳統方式 | NuAILink MCP 方式 |
| :--- | :--- |
| 工程師手寫 Python + serial protocol | AI 直接呼叫 `tools/call` |
| 查 pin map / 查 command | `tools/list` 自動描述能力 |
| log 分散 | structuredContent 可直接寫入實驗紀錄 |
| 反覆手動操作 | agent 可跑完整測試流程 |

### 想像範例

工程師說：

> 幫我確認 PB8 的電壓是否超過 1.2V；如果超過，讓 LED 條顯示綠色 chase，否則顯示紅色。

AI 可以自動：

1. 呼叫 `eadc.read {channel:8}`。
2. 解析 `structuredContent.mV`。
3. 呼叫 `llsi.pattern` 或 `llsi.autoplay`。
4. 回報結果並附上 timestamp。

## 4. 應用場景二：產線治具 / Production Test Jig

### 情境

每片產品出廠前要測：

- GPIO continuity。
- Button / connector 是否正常。
- ADC input calibration。
- LED / light pipe 是否亮。
- USB enumeration 是否正常。
- 韌體版本與 heap baseline。

NuAILink 可以變成治具控制板，也可以內建在 DUT firmware 中作為 test mode。

### 效益

- 測試項目由 JSON tool call 描述，可版本控管。
- `structuredContent` 直接轉成 MES / CSV / database 欄位。
- 測試失敗可回具體欄位，例如 `raw`, `mV`, `pin`, `usbRxDropCount`。
- 不需要每個產品重寫一套 binary protocol parser。

### 高效益延伸

讓 AI 根據失敗結果自動給維修建議：

```text
PB14 button event missing
  -> 檢查 pull-up / switch solder / GPB IRQ / MFP

EADC CH8 stuck at 4095
  -> 檢查 input over-voltage / divider open / VREF assumption

usbRxDropCount increasing
  -> 檢查 host test script burst / firmware blocking callback
```

## 5. 應用場景三：Firmware CI + Hardware-in-the-loop

### 情境

CI 不只編譯 firmware，還把 image 燒到實板，跑 host regression。

NuAILink 現有工具已接近這個模式：

- pyOCD flash。
- `nualink_regression.py`。
- `nualink_stress_test.py`。
- `system.info` telemetry。

### 可做的 CI stages

| Stage | 動作 |
| :--- | :--- |
| Build | CMSIS `cbuild` |
| Flash | pyOCD `load` |
| Enumerate | 等 CDC COM port |
| Smoke | initialize / ping / tools/list |
| Functional | LED / EADC / LLSI / GPIO |
| Notification | button / auto notify |
| Stress | 10 logical clients / 600 ops |
| Telemetry | heap / usb drops / latency trend |

### 巨大效益

硬體 regression 從「release 前人工測」變成「每次 commit 自動測」。尤其 USB transport / RTOS queue / JSON buffer 這種問題，越早抓越便宜。

## 6. 應用場景四：工廠設備的語義化 Gateway

### 情境

工廠現場可能有：

- RS485 / Modbus sensors。
- GPIO interlock。
- ADC pressure / temperature signal。
- LED tower light。
- relay / actuator。

NuAILink 可以把這些硬體資源包成 MCP tools：

```text
modbus.read_holding_registers
relay.set
pressure.read
tower_light.set
interlock.status
motor.jog
```

### 為什麼 MCP 有價值

AI agent 或 MES 系統不必知道 Modbus address 40017 是什麼，只要看到 tool description：

```text
pressure.read: Read current hydraulic pressure in kPa.
```

就能做更高階的編排：

- 「壓力超過門檻時停止馬達並亮紅燈」。
- 「每 10 秒記錄一次趨勢」。
- 「根據異常碼查 SOP 並提示操作員」。

## 7. 應用場景五：教育與開發者體驗

### 情境

學生或 maker 不必先學：

- USB descriptor。
- FreeRTOS queue。
- EADC register。
- PWM polarity。
- WS2812 timing。

可以先用語義 tool 建立直覺：

```json
{"name":"led.set","arguments":{"on":true}}
```

再逐步深入底層實作。

### 教學價值

| 教學主題 | NuAILink 展示方式 |
| :--- | :--- |
| JSON-RPC | ping / tools/call |
| USB CDC | serial bridge |
| RTOS | heartbeat / queues / task priority |
| GPIO interrupt | button.event |
| ADC | eadc.read / threshold notification |
| PWM | led.bpwm.set |
| LED protocol | llsi.pattern / autoplay |
| 測試工程 | regression / stress test |

## 8. 應用場景六：現場維修 / Remote Debug Pod

### 情境

設備在客戶現場出問題。維修工程師接上一個 NuAILink USB dongle：

- 讀設備 GPIO 狀態。
- 量測幾個 analog node。
- 控制指示燈或 relay。
- 收集事件。
- 讓 AI 根據 structured data 產生排查流程。

### 效益

- 不需要把完整 JTAG / debugger 開放給現場。
- 工具能力受 schema 限制，安全邊界較清楚。
- Debug log 與 telemetry 可標準化。
- 可遠端讓 AI assistant 指導非嵌入式背景人員操作。

## 9. 應用場景七：Robotics / 小型自動化控制節點

NuAILink 可以作為 robot host（Linux SBC）與低階 I/O 的 MCP bridge：

```text
Linux SBC / ROS / AI agent
        |
        | USB MCP
        v
M3334 NuAILink
        |
        +-- GPIO limit switches
        +-- ADC battery / sensor
        +-- PWM LED / buzzer
        +-- WS2812 status strip
        +-- future I2C / SPI / UART motor controllers
```

### 為什麼不是直接在 Linux bit-bang？

- MCU 有 deterministic timing。
- LLSI / PWM / EADC 由硬體 peripheral 處理。
- USB 對 Linux host 友善。
- MCP 對 AI agent 友善。

## 10. 應用場景八：互動式展示 / Smart Exhibit

博物館、展場、demo booth 常需要：

- 燈效。
- 感測器。
- 按鈕。
- 主機內容播放同步。
- 遠端更新互動邏輯。

NuAILink 可以讓上位機用語義指令控制 MCU：

```text
visitor presses PB14
  -> button.event
  -> host AI chooses next story branch
  -> llsi.autoplay rainbow
  -> gpio.write trigger relay
```

### 效益

- 展示邏輯在 host / AI 層快速迭代。
- MCU 韌體維持穩定硬體 abstraction。
- 現場問題可透過 system.info 快速診斷。

## 11. 應用場景九：安全受控的硬體沙盒

AI agent 可以控制硬體，但必須受限。

NuAILink 的 tool schema 天然適合建立安全邊界：

- 只暴露允許的 pins。
- 每個參數有 range。
- 高風險操作必須拆成 explicit tool。
- `system.info` 可觀測狀態。
- Notification 可監控使用者介入。

未來可以加入：

| 安全機制 | 說明 |
| :--- | :--- |
| Tool allowlist | Host profile 決定可呼叫哪些 tools |
| Pin ownership map | 禁止 AI 寫保留腳位 |
| Rate limit | 限制 GPIO/PWM toggle 頻率 |
| Watchdog | tool callback 超時自動復位狀態 |
| Audit log | 每個 tool call 記錄 id/time/args/result |

## 12. 應用場景十：把硬體變成可組合的「Promptable Instrument」

想像未來每個小儀器都支援：

```text
tools/list
```

Host 一接上 USB 就知道：

- 這是什麼硬體。
- 有哪些 sensor。
- 有哪些 actuator。
- 每個 action 的 schema。
- 每個 result 的 structured data。

這會讓硬體像軟體 API 一樣可組合：

```text
AI workflow
  read temperature from Device A
  set fan PWM on Device B
  show color status on Device C
  log result to host
```

NuAILink 是這個方向的原型。

## 13. 最有商業價值的三條產品線

### 13.1 AI Hardware SDK for MCU vendors

提供 MCU BSP + MCP layer + plugin templates，讓開發者快速把 peripheral 變成 AI tools。

### 13.2 Production Test Controller

以 NuAILink 作為 USB test controller，搭配 Python/CI/MES，主打低成本、高可維護、可語義化測試。

### 13.3 Edge Device Debug / Service Port

在產品 firmware 中保留受控 MCP service mode，給製造、維修、現場診斷使用。

## 14. 未來可新增的高價值 tools

| Tool idea | 效益 |
| :--- | :--- |
| `board.pins` | 回報 pin ownership / function / safety |
| `i2c.scan` | 快速找 sensor / EEPROM |
| `i2c.read_register` | 通用 sensor bring-up |
| `spi.transfer` | SPI device debug |
| `uart.rs485.modbus_read` | 工業設備 gateway |
| `pwm.set` | 馬達/風扇/背光控制 |
| `timer.capture` | 頻率/脈寬量測 |
| `logic.sample` | 簡易 logic analyzer |
| `fault.info` | 回報上次 fault registers |
| `fw.update.begin/write/commit` | USB MCP firmware update workflow |
| `trace.start/stop/read` | 現場 telemetry capture |
| `policy.set` | 安全策略 / pin allowlist |

## 15. 產品化願景

最終 NuAILink 可以成為：

```text
USB 插上去，AI 就知道這片板子能做什麼。
```

這句話背後的價值是：

- 縮短 bring-up 時間。
- 降低硬體測試門檻。
- 讓 firmware 功能可被自動探索。
- 讓 AI agent 能安全地操作真實世界。
- 讓 MCU 從 peripheral controller 升級成語義化邊緣節點。

如果把 USB MCP 做穩、做成 SDK、補齊 pin policy 和更多 peripheral templates，它會非常適合成為下一代 AI-native embedded development kit 的核心技術。
