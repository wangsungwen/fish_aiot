# AIoT 智慧魚菜共生系統 (AIoT Fish & Veg System)這是一個整合了物聯網技術的智慧魚菜共生監控與控制系統。系統以樹莓派 (Raspberry Pi) 為中央伺服器，透過 ESP32 與 ESP8266 微控制器收集環境數據（溫度、pH 值、TDS、水位等）並控制致動器（馬達、加溫棒）。數據透過 MQTT 協定傳輸，並整合 ThingSpeak 雲端平台進行資料視覺化，最後透過 Flask 網頁介面提供即時監控與控制功能，並利用 Ngrok 實現遠端訪問。


## 📖 目錄1. [系統架構](https://www.google.com/search?q=%23%E7%B3%BB%E7%B5%B1%E6%9E%B6%E6%A7%8B)
2. [硬體需求](https://www.google.com/search?q=%23%E7%A1%AC%E9%AB%94%E9%9C%80%E6%B1%82)
3. [軟體準備](https://www.google.com/search?q=%23%E8%BB%9F%E9%AB%94%E6%BA%96%E5%82%99)
4. [樹莓派伺服器架設 (Raspberry Pi Setup)](https://www.google.com/search?q=%23%E6%A8%B9%E8%8E%93%E6%B4%BE%E4%BC%BA%E6%9C%8D%E5%99%A8%E6%9E%B6%E8%A8%AD-raspberry-pi-setup)
5. [微控制器設定 (ESP32/ESP8266 Setup)](https://www.google.com/search?q=%23%E5%BE%AE%E6%8E%A7%E5%88%B6%E5%99%A8%E8%A8%AD%E5%AE%9A-esp32esp8266-setup)
6. [系統配置](https://www.google.com/search?q=%23%E7%B3%BB%E7%B5%B1%E9%85%8D%E7%BD%AE)
7. [啟動系統](https://www.google.com/search?q=%23%E5%95%9F%E5%8B%95%E7%B3%BB%E7%B5%B1)
8. [疑難排解](https://www.google.com/search?q=%23%E7%96%91%E9%9B%A3%E6%8E%92%E8%A7%A3)


## 🏗️ 系統架構```mermaid
graph TD
    subgraph "感測層 (Sensors & Actuators)"
        Temp[溫度感測器] --> ESP32
        TDS[TDS 感測器] --> ESP32
        pH[pH 感測器] --> ESP32
        WaterLevel[水位感測器] --> ESP32
        ESP8266 --> Relay1[繼電器 1 (馬達)]
        ESP8266 --> Relay2[繼電器 2 (加熱棒)]
        ESP8266 --> Relay3[繼電器 3 (備用)]
    end

    subgraph "傳輸層 (Transport)"
        ESP32 --MQTT Publish--> MQTT_Broker((MQTT Broker))
        MQTT_Broker --MQTT Subscribe--> RPi
        RPi --MQTT Publish--> MQTT_Broker
        MQTT_Broker --MQTT Subscribe--> ESP8266
    end

    subgraph "核心層 (Core Server)"
        RPi[樹莓派 (Flask Server)]
        RPi --> ThingSpeak[ThingSpeak Cloud API]
    end

    subgraph "應用層 (Application)"
        User[使用者裝置 (手機/電腦)] --Internet--> Ngrok[Ngrok Tunnel]
        Ngrok --> RPi
    end


## 🛠️ 硬體需求
### 核心控制* **Raspberry Pi 4 Model B** (建議 4GB RAM 以上): 作為中央伺服器、MQTT Broker 及網頁主機。

### 微控制器* **ESP-32S DevKit V1**: 負責連接多種感測器並上傳數據。
* **NodeMCU V3 (ESP8266)**: 負責控制繼電器（馬達、加熱棒等）。

### 感測器與模組* **DS18B20** 防水溫度感測器。
* **TDS 水質檢測模組** (類比訊號)。
* **pH 酸鹼值檢測模組** (類比訊號)。
* **水位感測器** (類比或數位)。
* **繼電器模組** (Relay Module, 建議 3路以上)。

> **⚠️ 接線注意：**
> 請務必參考專案提供的 `接線圖` 進行硬體連接。錯誤的接線可能導致元件損壞。特別注意感測器的電壓需求 (3.3V vs 5V) 以及 ESP32 的 ADC 腳位限制。


## 💻 軟體準備在開始之前，請確保您已準備好以下軟體環境：

1. **Raspberry Pi OS (64-bit)**: 已安裝並設定好網路連線。
2. **Arduino IDE**: 用於燒錄程式碼至 ESP32/ESP8266。
3. **Ngrok 帳號**: 用於建立外部存取隧道。
4. **ThingSpeak 帳號**: 用於雲端數據儲存與圖表繪製。


## 🚀 樹莓派伺服器架設 (Raspberry Pi Setup)請依序在樹莓派終端機執行以下步驟。

### 步驟 1: 更新系統與安裝基礎套件更新套件列表並安裝必要的系統工具、Python 3、pip 以及 OpenCV 所需的依賴庫。

```bash
sudo apt-get update && sudo apt-get upgrade -y
sudo apt-get install -y python3-pip python3-venv python3-dev build-essential
sudo apt-get install -y libatlas-base-dev gfortran
sudo apt-get install -y libhdf5-dev libhdf5-serial-dev libhdf5-103
sudo apt-get install -y libqtgui4 libqtwebkit4 libqt4-test python3-pyqt5
sudo apt-get install -y libopencv-dev python3-opencv

```

### 步驟 2: 建立 Python 虛擬環境 (Virtual Environment)為了避免套件衝突，強烈建議使用虛擬環境。

```bash
# 1. 建立專案資料夾 (如果尚未建立)
mkdir -p ~/Fish_System
cd ~/Fish_System

# 2. 建立名為 'venv' 的虛擬環境
python3 -m venv venv

# 3. 啟動虛擬環境 (重要！每次操作前都需執行此步)
source venv/bin/activate

# 啟動後，你的終端機提示字元前應該會出現 (venv)
# 例如: (venv) pi@raspberrypi:~/Fish_System $

```

### 步驟 3: 安裝 Python 相依套件在虛擬環境啟動的狀態下，安裝專案所需的 Python 庫。

```bash
# 確保 pip 是最新的
pip install --upgrade pip

# 安裝專案套件
pip install Flask
pip install paho-mqtt
pip install requests
pip install opencv-python-headless  # 伺服器環境建議使用 headless 版本
# 如果需要生產環境部署伺服器，可安裝 gunicorn
pip install gunicorn

```

### 步驟 4: 安裝與設定 NgrokNgrok 用於將樹莓派的本地網頁伺服器暴露到網際網絡。

1. 前往 [Ngrok 官網](https://ngrok.com/) 註冊並取得 Authtoken。
2. 依照官網指示安裝 Ngrok (針對 Linux/Raspberry Pi)。
3. 設定 Authtoken：

```bash
# 將 <YOUR_AUTH_TOKEN> 替換為你的實際 Token
ngrok config add-authtoken <YOUR_AUTH_TOKEN>

```

---

## 🤖 微控制器設定 (ESP32/ESP8266 Setup)使用 Arduino IDE 上傳韌體。

### 準備工作1. 在 Arduino IDE 中安裝 **ESP32** 和 **ESP8266** 的開發板支援包 (Board Manager)。
2. 透過 **Library Manager** (庫管理員) 安裝以下必要函式庫：
* `WiFiManager` (by tzapu)
* `PubSubClient` (by Nick O'Leary) - 用於 MQTT
* `ThingSpeak` (by MathWorks)
* `OneWire`
* `DallasTemperature` (用於 DS18B20)



### 燒錄韌體請分別開啟專案中的 `.ino` 檔案進行燒錄：

* ** ESP32 (感測器端):** 使用 `ESP32_ThingSpeak_Fixed.ino`
* ** 注意:** 上傳前請務必修改程式碼中的 `ThingSpeak Channel ID` 和 `Write API Key`。


* ** ESP8266 (控制端):** 使用 `Esp8266_3_Relays.ino` (或其他控制馬達的程式碼)。

> ** ℹ️ WiFi 設定說明 (WiFiManager):**
> 首次啟動時，微控制器會建立一個無線熱點 (AP)。請用手機或電腦連接該熱點，瀏覽器會自動跳轉至設定頁面 (或手動輸入預設 IP，通常是 `192.168.4.1`)，在此輸入你家中的 WiFi SSID 和密碼。設定完成後裝置會自動重啟並連線。

---

## ⚙️ 系統配置###Flask 應用程式設定確保你的 Flask 主程式 (例如 `app.py`) 中設定了正確的 MQTT Broker 地址以及 ThingSpeak 相關參數。

### ThingSpeak 設定在 ThingSpeak 平台上建立一個 Channel，並設定對應的 Fields 來接收感測器數據 (例如 Field1: 溫度, Field2: pH, Field3: TDS 等)。

---

## ▶️ 啟動系統請確保所有硬體已連接並通電。

### 1. 啟動 Flask 伺服器 (在樹莓派上)開啟一個新的終端機視窗，進入專案目錄並啟動虛擬環境：

```bash
cd ~/Fish_System
source venv/bin/activate
# 啟動 Flask 應用程式 (假設主程式為 app.py，並監聽所有 IP 的 5000 port)
python app.py
# 或使用 gunicorn (生產環境建議):
# gunicorn --bind 0.0.0.0:5000 app:app

```

### 2. 啟動 Ngrok 隧道開啟另一個新的終端機視窗，啟動 Ngrok 將 Port 5000 暴露出去：

```bash
# 將本地 5000 埠映射到一個隨機的公開 URL
ngrok http 5000

# 如果你有 Ngrok 的固定域名 (Paid Plan)，可以使用:
# ngrok http --domain=your-static-domain.ngrok.dev 5000

```

Ngrok 啟動後，終端機介面會顯示一個 `Forwarding` 的網址 (例如 `https://xxxx-xxxx.ngrok-free.app`)。複製這個網址，在瀏覽器中開啟，即可遠端訪問你的魚菜共生系統儀表板。

---

## ❓ 疑難排解* **Python 套件安裝失敗**: 確保你已執行 `sudo apt-get update` 並且安裝了所有基礎依賴套件 (如 `build-essential`, `python3-dev`)。
* ** 虛擬環境無法啟動**: 檢查 `source venv/bin/activate` 指令路徑是否正確。
* ** ESP32/8266 無法連線 WiFi**:
* 檢查 WiFiManager 是否成功設定了正確的 SSID 和密碼。
* 使用序列埠監控視窗 (Serial Monitor, Baud rate 115200) 查看除錯訊息。


* ** MQTT 數據未更新**:
* 確保樹莓派與微控制器連接的是同一個 MQTT Broker。
* 檢查 Topic 名稱是否一致。



* ** Ngrok 網址無法訪問**: Ngrok 免費版的網址每次啟動都會改變，請確認使用了最新的網址。確保 Flask 伺服器正在執行且沒有報錯。


