/*
 * 檔案名稱: Esp32_Sensors.ino
 * 功能: 整合 5 種感測器並透過 MQTT 上傳數據
 */
#include <ArduinoJson.h> // 請安裝 ArduinoJson 5.x 或 6.x
#include <DallasTemperature.h>
#include <OneWire.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiManager.h> // 請安裝 ESP32 版本的 WiFiManager

// --- 腳位定義  ---
#define PIN_DS18B20 4
#define PIN_TDS 34
#define PIN_WATER_LEVEL 35
#define PIN_TURBIDITY 32
#define PIN_PH 33

// --- MQTT 設定 ---
const char *mqtt_server = "MQTTGO.io";
const int mqtt_port = 1883;
const char *mqtt_topic = "ttu_fish/sensors";

WiFiClient espClient;
PubSubClient client(espClient);

// --- 感測器物件 ---
OneWire oneWire(PIN_DS18B20);
DallasTemperature sensors(&oneWire);

void setup() {
  Serial.begin(115200);

  // 感測器初始化
  sensors.begin();
  analogReadResolution(12); // ESP32 使用 12-bit ADC (0-4095)
  pinMode(PIN_TDS, INPUT);
  pinMode(PIN_WATER_LEVEL, INPUT);
  pinMode(PIN_TURBIDITY, INPUT);
  pinMode(PIN_PH, INPUT);

  // WiFiManager 自動配網
  WiFiManager wm;
  if (!wm.autoConnect("Fish_Sensor_Node")) {
    Serial.println("WiFi 連線失敗，重啟中...");
    ESP.restart();
  }

  client.setServer(mqtt_server, mqtt_port);
}

void reconnect() {
  while (!client.connected()) {
    String clientId = "ESP32_Sensor_" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("MQTT 已連線");
    } else {
      delay(5000);
    }
  }
}

// 讀取平均值函式，減少雜訊 [cite: 582]
int readAverage(int pin) {
  long sum = 0;
  for (int i = 0; i < 10; i++) {
    sum += analogRead(pin);
    delay(10);
  }
  return sum / 10;
}

void loop() {
  if (!client.connected())
    reconnect();
  client.loop();

  // 1. 讀取數據
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);

  int raw_level = readAverage(PIN_WATER_LEVEL);
  int raw_tds = readAverage(PIN_TDS);
  int raw_turb = readAverage(PIN_TURBIDITY);
  int raw_ph = readAverage(PIN_PH);

  // 2. 轉換電壓 (ESP32 3.3V / 4095) [cite: 563]
  float volt_tds = raw_tds * (3.3 / 4095.0);
  // 簡易 TDS 轉換公式 (需依校正調整) [cite: 568]
  float tds_val = (133.42 * pow(volt_tds, 3) - 255.86 * pow(volt_tds, 2) +
                   857.39 * volt_tds) *
                  0.5;

  // PH 與 濁度建議傳送原始值或電壓，由 Python 後端進行複雜的校正運算

  // 3. 打包 JSON
  StaticJsonDocument<256> doc;
  doc["temp"] = temp;
  doc["level"] = raw_level;
  doc["tds"] = tds_val;
  doc["turbidity"] = raw_turb;
  doc["ph_raw"] = raw_ph;

  char buffer[256];
  serializeJson(doc, buffer);

  // 4. 發送 MQTT (每分鐘一次) [cite: 60]
  client.publish(mqtt_topic, buffer);
  Serial.println(buffer);

  delay(60000);
}