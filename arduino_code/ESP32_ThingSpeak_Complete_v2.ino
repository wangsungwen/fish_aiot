/*
 * 專案：魚菜共生 AIoT - ESP32 上傳至 ThingSpeak 整合版 + MQTT 控制
 * 說明：
 * 1. 讀取 5 種感測器並上傳至 ThingSpeak
 * 2. 透過 MQTT 發送濁度控制命令 (與 Python 邏輯同步)
 */

#include "ThingSpeak.h"
#include <DallasTemperature.h>
#include <OneWire.h>
#include <PubSubClient.h> // 需要安裝 PubSubClient 函式庫
#include <WiFi.h>

// =================== 1. 網路與 ThingSpeak / MQTT 設定 ===================
// const char *ssid = "YOUR_WIFI_SSID";         // 請輸入 WiFi 名稱
// const char *password = "YOUR_WIFI_PASSWORD"; // 請輸入 WiFi 密碼
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

// ThingSpeak 設定
unsigned long myChannelNumber = 3146597;
const char *myWriteAPIKey = "YX7R6GEYXQDDMWEZP";

// MQTT 設定
const char *mqttServer = "mqttgo.io";
const int mqttPort = 1883;
const char *mqttTopicPump = "fish/control/pump";
const char *mqttTopicHeater = "fish/control/heater";

// =================== 2. 硬體腳位定義 ===================
const int PIN_TEMP = 4;         // DS18B20 水溫
const int PIN_TDS = 34;         // TDS (類比輸入)
const int PIN_TURBIDITY = 35;   // 濁度 (類比輸入)
const int PIN_PH = 32;          // PH (類比輸入)
const int PIN_WATER_LEVEL = 33; // Adafruit 4965 水位偵測
const int PIN_LED = 2;          // 狀態燈

// =================== 3. 物件初始化 ===================
WiFiClient tsClient;            // 用於 ThingSpeak
WiFiClient espClient;           // 用於 MQTT
PubSubClient client(espClient); // MQTT Client Object

OneWire oneWire(PIN_TEMP);
DallasTemperature sensors(&oneWire);

// 變數儲存
float val_temp, val_tds, val_ph, val_turb, val_level;
int val_ntu = 0;
bool pump_status = false;   // 紀錄馬達狀態
bool heater_status = false; // 紀錄加熱棒狀態

// 計時器變數 (非阻塞 Delay)
unsigned long lastMsg = 0;
const long interval = 20000; // 20秒執行一次感測與上傳

// =================== 4. 函式定義 ===================

// 計算濁度 NTU (移植自 Python)
int calculate_ntu(float voltage) {
  float ntu;
  if (voltage < 2.5) {
    ntu = 3000;
  } else {
    ntu = -1120.4 * (voltage * voltage) + 5742.3 * voltage - 4352.9;
    if (ntu < 0)
      ntu = 0;
    if (ntu > 4550)
      ntu = 4550;
  }
  return (int)ntu;
}

// MQTT 連線
void reconnect() {
  // 如果未連線，嘗試連線
  // 注意: 為了不阻塞 Main Loop 太久，這裡只嘗試一次，失敗則下次 Loop 再試
  if (!client.connected()) {
    Serial.print("嘗試連線 MQTT... ");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("已連線");
    } else {
      Serial.print("失敗, rc=");
      Serial.print(client.state());
      Serial.println(" (稍後重試)");
    }
  }
}

// 讀取感測器
void readSensors() {
  // A. 溫度
  sensors.requestTemperatures();
  val_temp = sensors.getTempCByIndex(0);
  if (val_temp == -127.00)
    val_temp = 0.0;

  // B. 水位
  val_level = digitalRead(PIN_WATER_LEVEL) == HIGH ? 20.0 : 0.0;

  // C. 類比讀取 (模擬轉換)
  // TDS
  float raw_tds = analogRead(PIN_TDS);
  val_tds = (raw_tds * 3.3 / 4095.0) * 100;

  // PH
  float raw_ph = analogRead(PIN_PH);
  val_ph = 7.0 + ((2.5 - (raw_ph * 3.3 / 4095.0)) / 0.18);

  // 濁度 (電壓)
  float raw_turb = analogRead(PIN_TURBIDITY);
  val_turb = raw_turb * (3.3 / 4095.0);

  // 計算 NTU
  val_ntu = calculate_ntu(val_turb);

  // 監控顯示
  Serial.printf("T:%.1f, TDS:%.0f, PH:%.1f, V_Turb:%.2f, NTU:%d, Lv:%.1f\n",
                val_temp, val_tds, val_ph, val_turb, val_ntu, val_level);
}

// 邏輯控制 (與 Python 一致)
void checkLogic() {
  // 1. 加熱棒控制 (Python: temp < 20 -> ON)
  if (val_temp < 20 && val_temp > 0) {
    if (!heater_status) {
      Serial.println("🥶 水溫過低！發送 MQTT 開啟加熱棒...");
      client.publish(mqttTopicHeater, "ON");
      heater_status = true;
    }
  } else if (val_temp >= 20) {
    if (heater_status) {
      Serial.println("🌡️ 水溫正常！發送 MQTT 關閉加熱棒...");
      client.publish(mqttTopicHeater, "OFF");
      heater_status = false;
    }
  }

  // 2. 過濾馬達控制 (Python: NTU >= 3000 OR TDS > 200)
  bool pump_needed = false;
  if (val_ntu >= 3000)
    pump_needed = true;
  if (val_tds > 200)
    pump_needed = true;

  if (pump_needed) {
    if (!pump_status) {
      Serial.println("💩 水質異常(濁度或TDS)！發送 MQTT 開啟過濾馬達...");
      client.publish(mqttTopicPump, "ON");
      pump_status = true;
    }
  } else {
    // Both OK
    if (pump_status) {
      Serial.println("💧 水質變清澈！發送 MQTT 關閉過濾馬達...");
      client.publish(mqttTopicPump, "OFF");
      pump_status = false;
    }
  }
}

void setup() {
  Serial.begin(115200);

  // 設定腳位
  pinMode(PIN_WATER_LEVEL, INPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_TDS, INPUT);
  pinMode(PIN_TURBIDITY, INPUT);
  pinMode(PIN_PH, INPUT);

  sensors.begin();

  // 使用 WiFiManager 自動管理 WiFi:
  // AP模式，沒連上網時會重新看到所有熱點的WiFi，選中其中熱點後，就自動寫入SSID及PASSWORD
  WiFiManager wm;

  // 設定 AP 模式的名稱與密碼 (沒連上網時會看到的 WiFi)
  bool res = wm.autoConnect("FishSystem_AP", "12345678");

  if (!res) {
    Serial.println("Failed to connect");
    // ESP.restart();
  } else {
    // 若執行到這裡代表已成功連上 WiFi
    Serial.println("connected...yeey :)");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP()); // 顯示取得的 IP
  }

  // 初始化服務
  ThingSpeak.begin(tsClient);
  client.setServer(mqttServer, mqttPort);
}

void loop() {
  // 維持 MQTT 連線
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // 非阻塞排程 (每 20 秒執行一次)
  unsigned long now = millis();
  if (now - lastMsg > interval) {
    lastMsg = now;

    // 1. 讀取數據 & 計算 NTU
    readSensors();

    // 2. 執行控制邏輯 (MQTT)
    checkLogic();

    // 3. 上傳至 ThingSpeak
    // Field 1: Temp, 2: TDS, 3: PH, 4: Turbidity(V), 5: Level
    // Python 端預期 Field 4 為電壓值，因此維持上傳 val_turb
    ThingSpeak.setField(1, val_temp);
    ThingSpeak.setField(2, val_tds);
    ThingSpeak.setField(3, val_ph);
    ThingSpeak.setField(4, val_turb);
    ThingSpeak.setField(5, val_level);

    Serial.println("正在上傳數據至 ThingSpeak...");
    int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);

    if (x == 200) {
      Serial.println("✅ 上傳成功！");
      digitalWrite(PIN_LED, HIGH);
      delay(500); // 短暫亮燈
      digitalWrite(PIN_LED, LOW);
    } else {
      Serial.print("❌ 上傳失敗 Code: ");
      Serial.println(x);
    }
  }
}