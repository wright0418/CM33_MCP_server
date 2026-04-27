# NuAILink 建置、燒錄與 Bring-up Debug

> 適用路徑：`SampleCode/FreeRTOS/NuAILink/VSCode`  
> CMSIS solution：`NuAILink.csolution.yml`  
> Target type：`FreeRTOS`  
> Device：`M3334KIGAE`

## 1. 建置環境

專案使用 CMSIS-Toolbox / AC6，相關工具由 `VSCode/vcpkg-configuration.json` 指定：

| Tool | Version requirement |
| :--- | :--- |
| CMake | `^3.28.4` |
| Ninja | `^1.12.0` |
| Arm Compiler | `armclang ^6.22.0` |
| CMSIS-Toolbox | `^2.7.0` |
| NuMicro pack | `Nuvoton::NuMicroM33_DFP@^1.0.3` |

CMSIS project 設定位於：

```text
SampleCode/FreeRTOS/NuAILink/VSCode/NuAILink.cproject.yml
```

solution 設定位於：

```text
SampleCode/FreeRTOS/NuAILink/VSCode/NuAILink.csolution.yml
```

## 2. Source / include 組成

`NuAILink.cproject.yml` 目前包含：

| Group | 內容 |
| :--- | :--- |
| System | `startup_m3331.c`, `system_m3331.c` |
| NuAILink App | `FreeRTOSConfig.h`, `main.c` |
| NuAILink SDK | `nualink/include`, platform, transport, core, plugins |
| FreeRTOS files | `tasks.c`, `timers.c`, `list.c`, `queue.c`, CM33 port, `heap_4.c` |
| ThirdParty | `cJSON.c`, `cJSON.h` |
| BSP Drivers | `clk`, `retarget`, `sys`, `uart`, `gpio`, `timer`, `hsusbd`, `pdma`, `eadc`, `epwm`, `bpwm`, `i2c`, `usci_i2c`, `usci_uart`, `llsi`, `ellsi` |

Include path 已包含：

```text
..
../../../../Library/CMSIS/Core/Include
../../../../Library/Device/Nuvoton/m3331/Config
../../../../Library/Device/Nuvoton/m3331/Include
../../../../Library/StdDriver/inc
../../../../ThirdParty/FreeRTOS/include
../../../../ThirdParty/FreeRTOS/Demo/Common/include
../../../../ThirdParty/FreeRTOS/portable/GCC/ARM_CM33_NTZ/non_secure
../../../../ThirdParty/cJSON
../nualink/include
```

## 3. 建置指令

從下列目錄執行建置：

```text
SampleCode/FreeRTOS/NuAILink/VSCode
```

常用 CMSIS build：

```powershell
cbuild NuAILink.csolution.yml --context NuAILink+FreeRTOS
```

若需要同步 packs / RTE：

```powershell
cbuild NuAILink.csolution.yml --packs --update-rte
```

成功後會產生：

```text
SampleCode/FreeRTOS/NuAILink/VSCode/out/NuAILink+FreeRTOS.cbuild-run.yml
```

## 4. 燒錄

使用 pyOCD / Nu-Link CMSIS-DAP：

```powershell
pyocd load --probe cmsisdap: --cbuild-run .\out\NuAILink+FreeRTOS.cbuild-run.yml
```

或使用 VS Code task：

| Task | 用途 |
| :--- | :--- |
| `CMSIS Load` | 燒錄 image |
| `CMSIS Erase` | erase target |
| `CMSIS Run` | 啟動 pyOCD gdbserver |
| `CMSIS TargetInfo` | 列出 target / probe 資訊 |

備註：過去 pyOCD 在讀 SVD 時可能出現 `xml.etree.ElementTree.ParseError`；若 log 已顯示 programmed bytes 且實際板子可跑，該 SVD parser 問題可能是良性，但仍建議優先修正/使用 sanitized SVD。

## 5. 開機驗證

### 5.1 Heartbeat LED

PC14 是 active-low LED。正常進入 scheduler 後，heartbeat task 會每 `NUALINK_HEARTBEAT_PERIOD_MS` toggle 一次，預設 500 ms。

若 PC14 solid ON，常見原因：

| 現象 | 可能原因 |
| :--- | :--- |
| solid ON before scheduler | task create failed、assert、fault、malloc failed |
| blink 一段時間後停住 | hard fault、stack overflow、USB callback 長時間卡住、assert |
| host 下 `led.set` 後 heartbeat 停止 | 正常；`led.set` 會 disable heartbeat |
| host 下 `led.bpwm.set` 後 heartbeat 停止 | 正常；PC14 切到 BPWM mode |

### 5.2 UART0 debug console

UART0 使用：

| Pin | Function |
| :--- | :--- |
| PB12 | UART0 RXD |
| PB13 | UART0 TXD |

Baud：115200。

`NUALINK_ERR()` 會透過 `printf` 印出錯誤。`NUALINK_LOG()` 只有在 `NUALINK_ENABLE_DEBUG_LOG == 1` 時輸出。

### 5.3 USB enumeration

Host 應看到 CDC ACM device，product string：

```text
NuAILink MCP Bridge
```

Windows 常見為 `COM4`；實際 COM port 請以 Device Manager 或 terminal probe 為準。

## 6. Boot diagnostics 開關

在 `nualink/include/nualink_config.h`：

```c
#define NUALINK_ENABLE_DEBUG_LOG 0
#define NUALINK_ENABLE_BOOT_DIAGNOSTICS 0
```

Bring-up 時可暫時改成：

```c
#define NUALINK_ENABLE_DEBUG_LOG 1
#define NUALINK_ENABLE_BOOT_DIAGNOSTICS 1
```

打開後：

- BoardInit 每個 checkpoint 會透過 UART0 印 log。
- PC14 會顯示 early boot blink pattern。

完成 bring-up / release 時建議關回 0，避免 log 影響 timing 或塞爆 host serial output。

## 7. Fault / assert 訊息

`main.c` 已實作：

| Function | 輸出 |
| :--- | :--- |
| `vAssertCalled()` | file/line、VTOR、SVC vector、PendSV vector |
| `HardFault_Handler()` | HFSR、CFSR、MMFAR、BFAR、ICSR |
| `MemManage_Handler()` | 同上 |
| `BusFault_Handler()` | 同上 |
| `UsageFault_Handler()` | 同上 |
| `vApplicationStackOverflowHook()` | task name |
| `vApplicationMallocFailedHook()` | malloc failed |

Cortex-M / FreeRTOS bring-up 特別注意：

- `configCHECK_HANDLER_INSTALLATION` 目前設為 0，避免某些 vector-table / indirect routing 情境在 scheduler 啟動前誤觸 assert。
- 若 log 停在 `vTaskStartScheduler()` 後，優先懷疑 FreeRTOS assert、handler routing 或 interrupt priority 設定。

## 8. USB CDC Debug checklist

### Host 找不到 COM port

檢查：

1. Board 是否進入 heartbeat。
2. `NuAILink_BoardInit()` 是否完成 USB PHY power-up。
3. `HSUSBD_MODULE` clock 是否 enable。
4. USB cable 是否支援 data。
5. Windows driver 是否把 CDC ACM 綁為 COM port。
6. `NuAILink_TransportPoll()` 是否在 attach 後呼叫 `HSUSBD_Start()`。

### Host 可以開 port，但沒有 response

檢查：

1. Request 是否以 `\n` 結尾。
2. JSON top-level 是否為 object。
3. 是否送到正確 COM port。
4. 開 port 後是否等待 0.2~0.3 秒並清 buffer。
5. `system.info` 是否可回。
6. `NUALINK_ENABLE_DEBUG_LOG` 開啟後看 USB RX/TX log。

### 長 response 被截斷或 JSON parse fail

重點看 `NuAILink_TransportWrite()`：

- 必須用 `TXPKIF/TXPKIEN` pacing。
- 每個 chunk 前要 flush EPA FIFO。
- 只在最後 chunk 設 `SHORTTXEN`。
- Host reader 必須累積 bytes 到 `\n`，不能假設一次 `readline()` 或一次 USB packet 就是一筆完整 response。

### `usbRxDropCount` 增加

代表 firmware 在上一個 EPB packet 尚未被 task 取走時，又收到新的 OUT packet。

常見原因：

| 原因 | 改善 |
| :--- | :--- |
| Host 無限制 burst requests | host 使用 dispatcher + in-flight limit |
| Tool callback 太久 | 移到 parser/action task，讓 USB task 保持短工作 |
| Response 太大 / TX timeout | 壓縮 response 或提高 TX timeout / queue depth |
| Auto mode 或 hardware polling 太重 | 降低頻率或拆 task |

## 9. 回歸測試

Host tools 在：

```text
SampleCode/FreeRTOS/NuAILink/host_tools
```

建議先使用專案 venv：

```powershell
.\.venv\Scripts\python.exe .\nualink_regression.py COM4 --ping-count 80
```

PowerShell wrapper：

```powershell
.\run_regression.ps1 -Port COM4
```

壓力測試：

```powershell
.\.venv\Scripts\python.exe .\nualink_stress_test.py COM4 --clients 10 --ops-per-client 60
```

過去已驗證過的穩定情境：10 logical clients × 60 ops，0 timeout、0 invalid JSON frames、`usbRxDropCount` 維持 0。

## 10. 修改後驗證建議

| 修改類型 | 必跑測試 |
| :--- | :--- |
| USB transport | `tools/list`、regression、stress test、長 response read |
| JSON-RPC core | initialize/ping/tools/list/tools/call/error cases |
| 新增 tool | tools/list schema、valid call、invalid params、regression coverage |
| Button / notification | `nualink_button_listen.py`、auto notify checks |
| EADC | `nualink_eadc_read.py` CH8/CH9、多次讀取 |
| LLSI | `llsi.fill` off/on、pattern、autoplay start/status/stop |
| FreeRTOSConfig | heartbeat、malloc/stack hooks、stress test |

## 11. Release 前檢查

- `NUALINK_ENABLE_DEBUG_LOG == 0`
- `NUALINK_ENABLE_BOOT_DIAGNOSTICS == 0`
- `system.info` heap margin 合理
- `usbRxDropCount == 0` under expected workload
- `tools/list` response JSON 完整且可 parse
- regression / stress PASSED
- PC14 heartbeat 正常；host 控制 LED 後狀態符合預期
- 文件已同步更新
