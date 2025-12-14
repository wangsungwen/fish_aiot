# 🐟 魚菜共生 AIoT 系統整合教學手冊 (readme_iot.md)

本文件整合了前後端程式 (`app_thingspeak_v2.py`, `index.html`) 與硬體韌體 (`ESP32`, `ESP8266`) 的邏輯重點，旨在提供一份完整的架設與操作教學備忘錄。

---

## 1. 系統核心架構 (System Architecture)

本專案採用 **「混合雲端與邊緣控制」** 的架構，兼具數據紀錄與即時控制的優點：

*   **感知層 (Sensors)**：`ESP32` 读取 5 種感測器，透過 WiFi 上傳數據至 **ThingSpeak** 雲端。
*   **決策層 (Brain)**：`Python (Flask)` 從 ThingSpeak 下載最新數據，進行邏輯判斷 (如：水溫過低?)，並發送 **Telegram 通知** 與 **MQTT 控制指令**。
*   **執行層 (Actuators)**：`ESP8266` 監聽 MQTT 指令，即時開關繼電器 (加熱棒、馬達) 或驅動清洗平台。
*   **展示層 (Dashboard)**：`index.html` 提供網頁介面，直接與 MQTT Broker 連線進行即時監控與手動控制。

---

## 2. 硬體準備與接線 (Hardware Setup)

### A. 感測與上傳端 (ESP32)
**韌體檔案**：`arduino_code/ESP32_ThingSpeak_Complete.ino`

ESP32 負責採集環境數據，請依照下表連接：

| 感測器 (Sensor) | 腳位 (GPIO) | 備註 | ThingSpeak Field |
| :--- | :--- | :--- | :--- |
| **LED 狀態燈** | `Pin 2` | 上傳成功時閃爍 | - |
| **DS18B20 水溫** | `Pin 4` | 需接 4.7k 上拉電阻 | Field 1 |
| **PH 酸鹼值** | `Pin 32` | 類比輸入 | Field 3 |
| **水位感測 (4965)**| `Pin 33` | HIGH/LOW (模擬 20/0cm) | Field 5 |
| **TDS 水質** | `Pin 34` | 類比輸入 | Field 2 |
| **濁度 (Turbidity)**| `Pin 35` | 類比輸入 | Field 4 |

### B. 設備控制端 (ESP8266)
本系統使用 MQTT 進行控制，依功能分為兩種韌體：

**1. 通用開關 (繼電器)**
**韌體檔案**：`arduino_code/Esp8266_Relay.ino`
*   用途：控制過濾馬達、加熱棒、餵食器。
*   接線：繼電器訊號腳接 `D0 (GPIO16)`。
*   **重要修改**：燒錄前請依用途修改程式碼中的 `TOPIC_RELAY`：
    *   過濾馬達：`ttu_fish/relay/pump`
    *   加熱棒：`ttu_fish/relay/heater`
    *   餵食器：`ttu_fish/relay/feeder`

**2. 清洗平台馬達**
**韌體檔案**：`arduino_code/Esp8266_Motor.ino`
*   用途：控制清洗裝置的前進、後退與自動行程。
*   接線：
    *   馬達驅動 (L298N)：`IN1 -> D1`, `IN2 -> D2`
    *   限位開關 (前)：`D5`
    *   限位開關 (後)：`D6`

---

## 3. 軟體服務設定 (Services Setup)

在執行程式前，請確保已申請並設定以下服務：

### A. ThingSpeak (雲端資料庫)
1.  建立一個 Channel。
2.  啟用 **Field 1 ~ Field 5**。
3.  取得 **Channel ID**、**Write API Key** (給 ESP32 用) 與 **Read API Key** (給 Python 用)。

### B. Telegram Bot (通知機器人)
1.  向 `@BotFather` 申請新 Bot，取得 **Token**。
2.  向 `@userinfobot` 查詢自己的 **Chat ID**。

### C. MQTT Broker
*   本專案預設使用公用 Broker：`mqttgo.io`
*   TCP Port: `1883` (給 Python/ESP 使用)
*   WebSocket Port: `8084` (給網頁 index.html 使用)

---

## 4. 程式碼修改清單 (Configuration)

### 📄 1. `arduino_code/ESP32_ThingSpeak_Complete.ino`
*   `ssid`, `password`: 修改為您的 WiFi。
*   `myChannelNumber`: 填入 ThingSpeak Channel ID。
*   `myWriteAPIKey`: 填入 Write API Key。

### 📄 2. `app_thingspeak_v2.py` (Python 後端)
*   `THINGSPEAK_CHANNEL_ID`: Channel ID。
*   `THINGSPEAK_READ_API_KEY`: Read API Key。
*   `TELEGRAM_BOT_TOKEN`: Bot Token。
*   `TELEGRAM_CHAT_ID`: Chat ID。
*   `RTSP_URL`: 若有攝影機，請修改串流位址 (預設為 `rtsp://10.197.186.146:554/11`)。

### 📄 3. `index.html` (前端介面)
*   `BROKER`, `PORT`: 若更换 MQTT Broker 需在此修改 (預設 `mqttgo.io`, `8084`)。
*   `TOPIC_SENSORS`: 確保與發送端一致 (預設 `ttu_fish/sensors`)，此專案目前主要透過 Polling API 顯示數據，MQTT 主要用於控制指令發送。

---

## 5. 啟動步驟 (Operation Steps)

1.  **硬體上電**：
    *   插上 ESP32，開啟 Serial Monitor 觀察是否成功連上 WiFi 並顯示 `✅ 上傳成功`。
    *   插上 ESP8266 (繼電器/馬達)，確認 MQTT 連線成功。

2.  **啟動後端**：
    在此專案目錄下開啟終端機 (Terminal)，執行：
    ```bash
    pip install flask flask-mqtt requests pyserial opencv-python
    python app_thingspeak_v2.py
    ```
    *   看到 `🚀 ThingSpeak 監聽執行緒已啟動...` 代表後端開始運作。

3.  **開啟儀表板**：
    *   打開瀏覽器訪問：`http://localhost:5000`
    *   點擊右上角 **「連線 MQTT」** 按鈕，確認燈號轉為綠色。
    *   觀察數據是否更新 (約 20 秒一次)。

---

## 6. 自動化邏輯說明 (Automation Logic)

系統會根據感測數值自動執行以下動作 (定義於 `app_thingspeak_v2.py` 的 `check_logic` 函式)：

| 情況 | 條件判定 | 觸發動作 | 備註 |
| :--- | :--- | :--- | :--- |
| **❄️ 水溫過低** | `temp < 20` | 1. 發送 MQTT `ON` 至 `fish/control/heater`<br>2. 傳送 Telegram 警報 | 30分鐘冷卻時間 |
| **💩 水質混濁** | `turbidity < 2.5` | 1. 發送 MQTT `ON` 至 `fish/control/pump`<br>2. 傳送 Telegram 警報 | 30分鐘冷卻時間 |
| **⛔ 水位危險** | `water_level < 10` | 1. 僅傳送 Telegram 警報 (防止馬達空轉) | 30分鐘冷卻時間 |

---

## 7. 常見問題排除 (Troubleshooting)

*   **Q: 網頁上的數據一直是 "--"？**
    *   A: 檢查 Python 後端是否有錯誤訊息。確認 ESP32 是否成功上傳數據至 ThingSpeak (免費版需間隔 15秒以上)。
*   **Q: 手機一直收到重複的警報？**
    *   A: 程式內建 `ALERT_COOLDOWN = 1800` (30分鐘) 防洗版機制。若重啟 Python 程式，計時器會重置。
*   **Q: 馬達不會動？**
    *   A: 檢查 ESP8266 是否連上 MQTT Broker。可用 MQTT 測試軟體 (如 MQTTX) 訂閱 `ttu_fish/#` 觀察是否有收到 `ON` 指令。
*   **Q: 影像出不來？**
    *   A: 確認 `app_thingspeak_v2.py` 中的 `RTSP_URL` 是否正確，且執行 Python 的電腦能 ping 得到攝影機 IP。
