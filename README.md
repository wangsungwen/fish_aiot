# 🐟 魚菜共生智慧管理平台 (AIoT Aquaponics System) v2.5

> **整合 ThingSpeak 雲端大數據、Telegram 智慧告警與 MQTT 自動化控制的完整解決方案。**

本專案採用 **混合式通訊架構 (Hybrid Communication Architecture)**，結合了雲端儲存的便利性與 MQTT 的即時控制能力。系統能自動監測水質、水溫與水位，並在異常發生時自動觸發設備（如過濾、加熱、補水），同時即時推播訊息至管理員手機。

---

## 🌟 系統特色 (v2.5 更新)

* **☁️ 雲端數據中心**：ESP32 感測器直接將數據上傳至 **ThingSpeak**，無需自建資料庫伺服器即可擁有歷史線圖與 API 存取。
* **🔔 手機即時推播**：整合 **Telegram Bot**，當發生以下事件時自動通知：
    * ❄️ **低溫警報** (觸發加熱棒)
    * 💩 **水質混濁** (觸發過濾馬達)
    * ⛔ **水位危險** (防止馬達燒毀)
* **🚀 混合通訊架構**：
    * **上行 (Upload)**：ESP32 → ThingSpeak (HTTP REST API) - 確保數據完整紀錄。
    * **下行 (Control)**：Python → MQTT Broker → ESP8266 - 確保控制指令低延遲。
* **🤖 智慧防洗版機制**：內建冷卻時間 (Cool-down) 邏輯，避免單一異常重複發送通知干擾使用者。
* **📱 戰情室儀表板**：Web 介面同步顯示最新雲端數據、設備狀態與即時影像(選配)。

---

## 🛠 系統運作流程

請務必理解本系統的數據流向，以便於除錯與維護：

1.  **數據採集 (ESP32)**：每 20 秒讀取 5 種感測器數值，透過 WiFi 上傳至 **ThingSpeak Channel**。
2.  **大腦運算 (Python app.py)**：
    * 後端開啟背景執行緒，定期向 ThingSpeak 下載最新數據。
    * **邏輯判斷**：例如「若溫度 < 20°C」。
    * **觸發動作**：
        1.  發送 **Telegram** 訊息給使用者。
        2.  發送 **MQTT 指令** (`ON`) 給執行端設備。
3.  **設備執行 (ESP8266)**：持續監聽 MQTT Broker，接收到指令後切換繼電器開關。

---

## ⚙️ 硬體接線腳位 (Pinout)

### 1. ESP32 感測整合端
請依照下表連接感測器，並燒錄 `Esp32_Sensors_Complete.ino`。

| 感測器元件 | 介面類型 | ESP32 GPIO | ThingSpeak Field |
| :--- | :--- | :--- | :--- |
| **DS18B20 水溫** | 數位 (OneWire) | **Pin 4** | Field 1 |
| **TDS 水質檢測** | 類比 (Analog) | **Pin 34** | Field 2 |
| **PH 酸鹼值** | 類比 (Analog) | **Pin 32** | Field 3 |
| **濁度 (Turbidity)** | 類比 (Analog) | **Pin 35** | Field 4 |
| **HC-SR04 水位** | 數位 (Digital) | **Trig: 5 / Echo: 18** | Field 5 |

### 2. ESP8266 設備控制端
請連接繼電器模組，並燒錄 `Esp8266_Relay.ino`。

* **MQTT Broker**: `mqttgo.io`
* **Port**: `1883`

---

## 📝 安裝與設定指南

### 步驟 1：準備 API Keys (至關重要)
在開始之前，您需要準備以下金鑰並填入程式碼中：

1.  **ThingSpeak**:
    * **Channel ID**: `3146597`
    * **Write API Key**: (填入 ESP32 程式碼)
    * **Read API Key**: (填入 Python 程式碼)
2.  **Telegram**:
    * **Bot Token**: 向 @BotFather 申請。
    * **Chat ID**: 向 @userinfobot 查詢。

### 步驟 2：軟體環境安裝
本專案後端使用 Python 開發，請安裝必要套件：

```bash
pip install flask flask-mqtt requests pyserial

###步驟 3：修改配置檔
A. 修改 ESP32 韌體 (arduino_code/Esp32_Sensors_Complete.ino)
C++

const char* ssid = "您的WiFi名稱";
const char* password = "您的WiFi密碼";
unsigned long myChannelNumber = 3146597; 
const char* myWriteAPIKey = "您的_WRITE_API_KEY"; // ⚠️ 請填入 Write Key
B. 修改 Python 後端 (app.py)
Python

# ThingSpeak 設定
THINGSPEAK_CHANNEL_ID = '3146597'
THINGSPEAK_READ_API_KEY = '您的_READ_API_KEY'   # ⚠️ 請填入 Read Key

# Telegram 設定
TELEGRAM_BOT_TOKEN = '您的_BOT_TOKEN'
TELEGRAM_CHAT_ID = '您的_CHAT_ID'

###步驟 4：啟動系統
將 ESP32 與 ESP8266 上電。

在電腦端執行 Python 主程式：

Bash

python app.py
開啟瀏覽器訪問 http://localhost:5000。

📂 專案檔案結構
Plaintext

Fish_System/
├── app.py                      # 核心主程式 (Flask + ThingSpeak Polling + Telegram + MQTT)
├── fish_system.db              # SQLite 資料庫 (自動生成，儲存歷史紀錄)
├── requirements.txt            # Python 依賴清單
├── README.md                   # 本說明檔
├── templates/
│   └── index.html              # 前端儀表板 (視覺化介面)
├── static/
│   └── css/                    # 網頁樣式
└── arduino_code/               # 硬體韌體原始碼
    ├── Esp32_Sensors_Complete.ino   # [V2.5] 上傳數據至 ThingSpeak
    └── Esp8266_Relay.ino            # [V2.5] 接收 MQTT 指令控制馬達
⚠️ 常見問題 (FAQ)
Q1: 為什麼網頁上的數據更新有延遲？

本系統受限於 ThingSpeak 免費版限制，ESP32 每 20 秒上傳一次，Python 每 20 秒下載一次，因此最大可能會有約 20~40 秒的數據延遲，這對於魚菜共生系統是可以接受的。

Q2: 為什麼 Serial Monitor 顯示 Error -401 或 HTTP 0？

這通常表示上傳頻率太快。請確保 ESP32 程式碼中的 delay() 至少設定為 20000 (20秒)。

Q3: 為什麼水位低了但我沒收到 Telegram 通知？

系統設有 30 分鐘冷卻時間。若 30 分鐘內已經發送過一次水位警報，為了避免您的手機被訊息轟炸，系統會暫時阻擋重複的通知，直到時間結束。重啟 app.py 可以重置此計時器。

https://api.thingspeak.com/update?api_key=X7R6GEYXQDDMWEZP&field1=22.0&field2=900&field3=7.2&field4=3.2&field5=3600