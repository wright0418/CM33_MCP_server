# NuAILink MCP 資料模型與 Tool Reference

> 本文件整理 JSON-RPC / MCP result 資料格式，以及目前 firmware registry 中的 13 個 tools。  
> 對應程式：`nualink/src/core` 與 `nualink/src/plugins`。

## 1. MCP tool call 基本資料格式

所有 tool 皆透過 JSON-RPC `tools/call` 呼叫：

```json
{
  "jsonrpc": "2.0",
  "id": 100,
  "method": "tools/call",
  "params": {
    "name": "tool.name",
    "arguments": {}
  }
}
```

成功時，多數 tool 回傳 MCP-style result：

```json
{
  "jsonrpc": "2.0",
  "result": {
    "content": [
      {"type": "text", "text": "human readable summary"}
    ],
    "structuredContent": {
      "machine": "readable fields"
    },
    "isError": false
  },
  "id": 100
}
```

| 欄位 | 說明 |
| :--- | :--- |
| `content` | MCP client / 人類可讀內容，目前通常是一個 text item array |
| `structuredContent` | host 程式最應該使用的 machine-readable result |
| `isError` | 目前成功 callback 皆設 `false`；參數錯誤多以 JSON-RPC error 回傳 |

## 2. Tool 一覽

| Tool | Plugin | 用途 |
| :--- | :--- | :--- |
| `system.info` | `plugin_system.c` | 回報 firmware、clock、heap、USB、LED 狀態 |
| `led.set` | `plugin_led_gpio.c` | PC14 active-low LED GPIO on/off |
| `led.bpwm.set` | `plugin_led_gpio.c` | PC14 透過 BPWM2_CH0 調光 |
| `led.auto` | `plugin_led_gpio.c` | MCU-side PC14 自動 blink |
| `gpio.read` | `plugin_gpio.c` | 讀 GPIO A-H pin level |
| `gpio.write` | `plugin_gpio.c` | 設 GPIO output |
| `button.read` | `plugin_gpio.c` | 讀 PB14 active-low button |
| `gpio.auto` | `plugin_gpio.c` | MCU-side GPIO toggle |
| `eadc.read` | `plugin_eadc.c` | 讀 EADC0 CH8/PB8 或 CH9/PB9 |
| `eadc.auto` | `plugin_eadc.c` | MCU-side periodic EADC sampling / threshold notify |
| `llsi.fill` | `plugin_llsi.c` | LLSI0 PB15 WS2812 pixels 填色 |
| `llsi.pattern` | `plugin_llsi.c` | LLSI pattern 單張 frame |
| `llsi.autoplay` | `plugin_llsi.c` | MCU-side LLSI animation playback |

## 3. `system.info`

### Arguments

```json
{}
```

### Example request

```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"system.info","arguments":{}}}
```

### structuredContent

| Field | Type | 說明 |
| :--- | :--- | :--- |
| `firmwareVersion` | string | 來自 `NUALINK_FIRMWARE_VERSION`，目前 `0.1.0` |
| `hclkHz` | number | `SystemCoreClock` |
| `tick` | number | FreeRTOS tick |
| `freeHeapBytes` | number | `xPortGetFreeHeapSize()` |
| `minimumEverFreeHeapBytes` | number | `xPortGetMinimumEverFreeHeapSize()` |
| `usbRxDropCount` | number | USB OUT packet drop counter |
| `usbAttached` | boolean | `HSUSBD_IS_ATTACHED()` |
| `ledOn` | boolean | PC14 LED logical state |
| `ledMode` | string | `gpio` 或 `bpwm` |
| `ledDutyPercent` | number | LED brightness duty 0..100 |

## 4. LED tools

### 4.1 `led.set`

控制 PC14 LED GPIO。PC14 是 active-low，但 API 使用語義化 `on`。

Arguments：

```json
{"on": true}
```

Validation：

| Field | Type | Required | Range |
| :--- | :--- | :--- | :--- |
| `on` | boolean | yes | `true` / `false` |

Side effects：

- 停止 heartbeat。
- 若 PC14 正在 BPWM mode，會切回 GPIO mode。
- 更新 `ledDutyPercent`：on -> 100，off -> 0。

structuredContent：

| Field | Type | 說明 |
| :--- | :--- | :--- |
| `on` | boolean | logical LED state |
| `mode` | string | `gpio` |
| `pin` | string | `PC14` |

### 4.2 `led.bpwm.set`

使用 BPWM2_CH0 控制 PC14 LED 亮度。

Arguments：

```json
{"duty": 35}
```

Validation：

| Field | Type | Required | Range |
| :--- | :--- | :--- | :--- |
| `duty` | integer | yes | 0..100 |

Side effects：

- 停止 heartbeat。
- PC14 pin mux 切成 `BPWM2_CH0_PC14`。
- PC14 active-low，所以 firmware 會將使用者亮度轉成 BPWM high-duty：`bpwm_duty = 100 - duty`。

structuredContent：

| Field | Type | 說明 |
| :--- | :--- | :--- |
| `mode` | string | `bpwm` |
| `pin` | string | `PC14` |
| `duty` | number | 使用者語義亮度 0..100 |
| `activeLow` | boolean | `true` |

### 4.3 `led.auto`

MCU-side LED blink state machine，由 `USB_Comm` loop 呼叫 `NuAILink_LedAutoProcess()` 推進。

Arguments schema 摘要：

| Field | Type | Required | Range / Enum | 說明 |
| :--- | :--- | :--- | :--- | :--- |
| `action` | string | yes | `start`, `update`, `stop`, `status` | 控制動作 |
| `mode` | string | no | `blink` | 目前只支援 blink |
| `interval_ms` | integer | no | 20..5000 | blink interval |
| `initial_on` | boolean | no | - | start/update 初始 LED state |
| `notify` | boolean | no | - | 是否送 `led.auto.event` |

Default：

| Field | Value |
| :--- | :--- |
| interval | 500 ms |
| initial state | off |
| notify | false |

Example：

```json
{"action":"start","mode":"blink","interval_ms":80,"initial_on":true,"notify":true}
```

structuredContent：

| Field | Type |
| :--- | :--- |
| `pin` | string |
| `action` | string |
| `mode` | string |
| `running` | boolean |
| `on` | boolean |
| `notify` | boolean |
| `event_count` | number |
| `interval_ms` | number |

Notification：

```json
{"jsonrpc":"2.0","method":"led.auto.event","params":{"pin":"PC14","on":true,"event_count":1,"tick":123}}
```

## 5. GPIO / Button tools

### 5.1 `gpio.read`

Arguments：

```json
{"port":"C","pin":14}
```

Validation：

| Field | Type | Required | Range |
| :--- | :--- | :--- | :--- |
| `port` | string | yes | one char `A`..`H` or `a`..`h` |
| `pin` | integer | yes | 0..15 |

structuredContent：

| Field | Type | 說明 |
| :--- | :--- | :--- |
| `port` | string | normalized uppercase |
| `pin` | number | pin index |
| `value` | number | 0 or 1 |

### 5.2 `gpio.write`

Arguments：

```json
{"port":"C","pin":14,"value":1}
```

Validation：

| Field | Type | Required | Range |
| :--- | :--- | :--- | :--- |
| `port` | string | yes | one char `A`..`H` or `a`..`h` |
| `pin` | integer | yes | 0..15 |
| `value` | integer | yes | 0..1 |

Protected pins：

| Pin | 原因 |
| :--- | :--- |
| PB12 | UART0 RXD debug console |
| PB13 | UART0 TXD debug console |
| PB14 | button input + interrupt |

Special behavior：

- 寫 PC14 時，如果 BPWM active，會先呼叫 `NuAILink_BoardLedUseGpio()` 停止 BPWM。
- GPIO mode 會被設成 output。

structuredContent：

| Field | Type |
| :--- | :--- |
| `port` | string |
| `pin` | number |
| `value` | number |
| `ok` | boolean |

### 5.3 `button.read`

Arguments：

```json
{}
```

PB14 是 active-low + internal pull-up。

structuredContent：

| Field | Type | 說明 |
| :--- | :--- | :--- |
| `pin` | string | `PB14` |
| `pressed` | boolean | `true` 表示 PB14 讀到 0 |

Button interrupt notification：

```json
{"jsonrpc":"2.0","method":"button.event","params":{"pin":"PB14","pressed":true}}
```

Debounce：30 ms tick-based debounce in `GPB_IRQHandler()`。

### 5.4 `gpio.auto`

MCU-side GPIO toggle。

Arguments schema 摘要：

| Field | Type | Required | Range / Enum |
| :--- | :--- | :--- | :--- |
| `action` | string | yes | `start`, `update`, `stop`, `status` |
| `mode` | string | no | `toggle` |
| `port` | string | no | `A`..`H` |
| `pin` | integer | no | 0..15 |
| `initial_value` | integer | no | 0..1 |
| `interval_ms` | integer | no | 20..5000 |
| `notify` | boolean | no | - |

Default：PC14，每 500 ms toggle，initial value 0，notify false。

Example：

```json
{"action":"start","mode":"toggle","port":"C","pin":14,"initial_value":0,"interval_ms":80,"notify":true}
```

structuredContent：

| Field | Type |
| :--- | :--- |
| `action` | string |
| `mode` | string |
| `running` | boolean |
| `port` | string |
| `pin` | number |
| `value` | number |
| `notify` | boolean |
| `event_count` | number |
| `interval_ms` | number |

Notification：

```json
{"jsonrpc":"2.0","method":"gpio.auto.event","params":{"port":"C","pin":14,"value":1,"event_count":1,"tick":123}}
```

## 6. EADC tools

### 6.1 `eadc.read`

讀 EADC0 single-ended CH8/PB8 或 CH9/PB9。

Arguments：

```json
{"channel":8}
```

Validation：

| Field | Type | Required | Range |
| :--- | :--- | :--- | :--- |
| `channel` | integer | yes | 8 or 9 |

Board behavior：

- EADC clock / pin lazy init。
- PB8/PB9 digital path disabled。
- `EADC_SOFTWARE_TRIGGER`。
- 12-bit raw value，`mV = raw * 3300 / 4095` with rounding。
- Conversion timeout 約 10 ms at current HCLK。

structuredContent：

| Field | Type | 說明 |
| :--- | :--- | :--- |
| `channel` | number | 8 or 9 |
| `pin` | string | `PB8` or `PB9` |
| `raw` | number | 12-bit ADC code |
| `mV` | number | estimated millivolts, VREF=3300 mV |

### 6.2 `eadc.auto`

Periodic EADC sampling cache，可選 threshold notification。

Arguments schema 摘要：

| Field | Type | Required | Range / Enum |
| :--- | :--- | :--- | :--- |
| `action` | string | yes | `start`, `update`, `stop`, `status` |
| `channel` | integer | no | 8 or 9 |
| `interval_ms` | integer | no | 20..10000 |
| `notify` | boolean | no | - |
| `threshold_enabled` | boolean | no | - |
| `threshold_mV` | integer | no | 0..3300 |
| `threshold_mode` | string | no | `above`, `below` |

Default：CH8，200 ms interval，notify false，threshold disabled。

Example：

```json
{"action":"start","channel":8,"interval_ms":100,"notify":true,"threshold_mV":1200,"threshold_mode":"above"}
```

structuredContent：

| Field | Type |
| :--- | :--- |
| `action` | string |
| `running` | boolean |
| `sample_valid` | boolean |
| `channel` | number |
| `pin` | string |
| `interval_ms` | number |
| `raw` | number |
| `mV` | number |
| `notify` | boolean |
| `event_count` | number |
| `threshold_enabled` | boolean |
| `threshold_mV` | number |
| `threshold_mode` | string |
| `threshold_active` | boolean |
| `last_sample_tick` | number |

Notification：

```json
{"jsonrpc":"2.0","method":"eadc.auto.event","params":{"event":"sample","channel":8,"pin":"PB8","raw":1234,"mV":994,"event_count":1,"tick":123}}
```

Threshold mode event name：

| Mode | Event |
| :--- | :--- |
| `above` | `threshold_above` |
| `below` | `threshold_below` |

Threshold notification 只在 inactive -> active transition 時送出；回到 inactive 後才會再次觸發。

## 7. LLSI / WS2812 tools

### 7.1 共通限制

| Field | Value |
| :--- | :--- |
| Output pin | PB15 / LLSI0 OUT |
| Pixel format | Firmware 以 RGB arguments 接收；LLSI open 設定 `LLSI_FORMAT_GRB` |
| Max pixels | 10 |
| WS2812 transfer timing | 1250 ns period, T0H 400 ns, T1H 850 ns, reset 50000 ns |
| LLSI mode | `LLSI_MODE_SW` |

### 7.2 `llsi.fill`

Arguments：

```json
{"r":255,"g":0,"b":0,"count":10}
```

Validation：

| Field | Type | Required | Range | Default |
| :--- | :--- | :--- | :--- | :--- |
| `r` | integer | yes | 0..255 | - |
| `g` | integer | yes | 0..255 | - |
| `b` | integer | yes | 0..255 | - |
| `count` | integer | no | 1..10 | 10 |

structuredContent：

| Field | Type |
| :--- | :--- |
| `pin` | string |
| `count` | number |
| `r` | number |
| `g` | number |
| `b` | number |

### 7.3 `llsi.pattern`

Render one frame。

Arguments schema 摘要：

| Field | Type | Required | Range / Enum | Default |
| :--- | :--- | :--- | :--- | :--- |
| `pattern` | string | yes | `off`, `solid`, `chase`, `gradient`, `rainbow` | - |
| `count` | integer | no | 1..10 | 10 |
| `phase` | integer | no | 0..4095 | 0 |
| `r` | integer | no | 0..255 | 255 |
| `g` | integer | no | 0..255 | 0 |
| `b` | integer | no | 0..255 | 0 |

Pattern 行為：

| Pattern | 行為 |
| :--- | :--- |
| `off` | 全暗 |
| `solid` | 所有 pixels 同色 |
| `chase` | `phase % count` 的 pixel 亮，其餘暗 |
| `gradient` | 依 pixel index / phase 產生 brightness gradient |
| `rainbow` | 使用 wheel function 產生彩虹 |

structuredContent：

| Field | Type |
| :--- | :--- |
| `pin` | string |
| `pattern` | string |
| `count` | number |
| `phase` | number |
| `r` | number |
| `g` | number |
| `b` | number |

### 7.4 `llsi.autoplay`

MCU-side pattern playback。

Arguments schema 摘要：

| Field | Type | Required | Range / Enum | Default on start |
| :--- | :--- | :--- | :--- | :--- |
| `action` | string | yes | `start`, `update`, `stop`, `status` | - |
| `pattern` | string | no | `off`, `solid`, `chase`, `gradient`, `rainbow` | `rainbow` |
| `count` | integer | no | 1..10 | 10 |
| `phase` | integer | no | 0..4095 | 0 |
| `step` | integer | no | 1..4095 | 1 |
| `interval_ms` | integer | no | 10..5000 | 50 |
| `r` | integer | no | 0..255 | 255 |
| `g` | integer | no | 0..255 | 0 |
| `b` | integer | no | 0..255 | 0 |

Example：

```json
{"action":"start","pattern":"rainbow","count":10,"phase":0,"step":3,"interval_ms":60}
```

structuredContent：

| Field | Type |
| :--- | :--- |
| `pin` | string |
| `action` | string |
| `running` | boolean |
| `pattern` | string |
| `count` | number |
| `phase` | number |
| `step` | number |
| `interval_ms` | number |
| `r` | number |
| `g` | number |
| `b` | number |

## 8. Tool 設計慣例

新增 tool 時請遵守目前慣例：

1. `input_schema_json` 是 compact JSON string，放在 flash `static const char[]`。
2. Callback 必須檢查：
   - `arguments != NULL`
   - `cJSON_IsObject(arguments)`
   - 必填欄位存在與型別正確
   - range / enum 正確
3. 若硬體操作可能失敗，回 `MCP_STATUS_INTERNAL_ERROR`。
4. 若參數不合法，回 `MCP_STATUS_INVALID_PARAMS`。
5. 成功 result 儘量同時提供：
   - `content[0].text`：人類可讀
   - `structuredContent`：host 程式可讀
   - `isError=false`
6. 若 tool 有 autonomous mode，應提供 `start` / `update` / `stop` / `status` 四個 action。

## 9. Host 端資料處理建議

Host 程式請優先解析 `structuredContent`，不要依賴 `content[].text` 文字格式，因為文字可能隨文件化或 UX 調整而改變。

建議 host result parser：

```python
def get_structured(response):
    if "error" in response:
        raise RuntimeError(response["error"])
    result = response.get("result", {})
    structured = result.get("structuredContent", {}) if isinstance(result, dict) else {}
    if not isinstance(structured, dict):
        raise RuntimeError("missing structuredContent")
    return structured
```
