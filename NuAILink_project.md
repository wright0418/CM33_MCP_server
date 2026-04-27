# Project: NuAILink - 基於 Nuvoton M3331 的 AI 語義硬體 SDK
開發一個名為 NuAILink 的專案，使用 Nuvoton M3331 MCU。這是一個 MCP (Model Context Protocol) Server，透過 High-Speed USB 與 Linux 主機通訊。

## NuAILink 專案開發規劃書

# NuAILink 專案開發規劃書
### 基於 Nuvoton M3331 之 AI 語義硬體 SDK 架構

## 1. 專案願景與核心價值
本專案旨在打造一個「AI 原生硬體層」，利用 **Model Context Protocol (MCP)** 作為標準語義接口，消除大語言模型（LLM/SLM）與底層微控制器（MCU）之間的開發摩擦力。透過 **M3331** 的強大硬體特性，實現即時、安全且易於擴展的邊緣控制系統。

> **核心目標：** 讓專業廠商只需專注於「物理控制邏輯」，其餘的「語義解析、通訊調度、資源管理」由 NuAILink SDK 自動完成。

## 2. 硬體架構規格 (M3331 平台優勢)

| 組件 | 規格特性 | 對專案的貢獻 |
| :--- | :--- | :--- |
| **運算核心** | Cortex-M33, 180MHz | 支援高速 JSON 解析 (cJSON) 與 FPU/DSP 運算。 |
| **記憶體** | 384KB SRAM | 充足緩衝區容納大型 MCP 工具描述與多任務堆疊。 |
| **通訊接口** | HS USB (480Mbps) | 搭配 HS USB 專用 DMA**，通訊不佔 CPU 週期。 |
| **驅動介面** | LLSI (Linked List) | 硬體自動刷新 LED 矩陣，實現零閃爍。 |

## 3. 韌體架構與 SDK 分層設計
採用「核心封裝、介面開放」的策略，降低客戶整合難度。

### 3.1 SDK 模組化結構
* **NuAILink Core (封裝庫)**：包含 HS USB 驅動核心、MCP 狀態機、JSON-RPC 分發器、專用 DMA 流量管理。
* **User Wrapper (介面層)**：提供 `mcp_user_config.c`，供客戶定義工具名稱、JSON Schema 與對應的回調函數。
* **Hardware Templates (範例插件)**：提供 GPIO、I2C Sensor、Modbus RTU 等標準工業對接範本。

### 3.2 任務調度 (FreeRTOS Tasks)
1.  **USB_Comm_Task**: 優先權最高。專責處理 1.5KB 專用 DMA 的數據進出。
2.  **MCP_Parser_Task**: 核心邏輯。執行 cJSON 解析，將語義請求映射至 C 指標函數。
3.  **Plugin_Action_Task**: 執行客戶定義的驅動程式，如 Modbus 命令發送或 LED 刷新。

---

## NuAILink SDK 開發任務清單 (Copilot Agent 專用)

# NuAILink SDK 開發任務清單

## Phase 1: 基礎系統構建
- [ ] **環境搭建**：設定 Nuvoton M3331 BSP 並初始化 HS USB 控制器。
- [ ] **記憶體規劃**：配置專用 HS USB DMA 緩衝區分配 (Rx: 1024B, Tx: 512B)。
- [ ] **RTOS 導入**：導入 FreeRTOS 並建立通訊與任務隊列。
- [ ] **JSON 整合**：整合 cJSON 庫，針對 M3331 的 384KB SRAM 優化記憶體分配區。

## Phase 2: MCP 核心實作
- [ ] **結構定義**：設計 `mcp_tool_t` 結構體，包含工具名、描述、JSON Schema 與回調指標。
- [ ] **清單生成**：實作 `MCP_Build_Tool_List_Response()`：將註冊表自動轉換為 MCP 標準的 `tools/list` JSON 回應。
- [ ] **指令分發**：實作 `MCP_Handle_Tool_Call()`：解析傳入參數並執行對應的 User Callback。

## Phase 3: 客戶插件示範 (Templates)
- [ ] **I/O 插件**：實現 LED 切換工具。
- [ ] **主動回報**：實現按鈕狀態變更時，透過 MCP Notification 主動推播。
- [ ] **GPIO 插件**：實作 GPIO 讀寫工具，並封裝為 MCP 可識別的語義接口。
- [ ] **LED 插件**：利用 LLSI 實現 LED 矩陣控制，確保無閃爍效果。
- [ ] **PWM 插件**：實作 PWM 調光工具，並封裝為 MCP 可識別的語義接口。
- [ ] **ADC 插件**：實現模擬量讀取工具，並封裝為 MCP 可識別的語義接口。
- [ ] **I2C 插件**：實現標準感測器 (如 SHT3x) 數據讀取與語義封裝。
- [ ] **Modbus 插件**：實作指令轉化邏輯，將 MCP 語義轉為 Modbus RTU 寫入指令。

## Phase 4: 系統整合測試
- [ ] **併發壓力測試**：撰寫 Python 腳本模擬 10 個節點同時發送大量 MCP 請求。
- [ ] **性能監測**：驗證 180MHz 高頻下，從 USB 接收到執行動作的延遲時間 (Latency)。
