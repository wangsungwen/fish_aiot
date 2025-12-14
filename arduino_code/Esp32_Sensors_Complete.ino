/*
 * 檔案名稱: Esp32_Sensors_Complete.ino
 * 功能: 讀取 5 種水質感測器、電壓換算、校正補償、MQTT 上傳
 * 硬體: ESP32 DevKit V1
 */
#include <ArduinoJson.h> // 請安裝 ArduinoJson 6.x
#include <DallasTemperature.h>
#include <OneWire.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiManager.h> // 請確認安裝 ESP32 版本的 WiFiManager

// --- 腳位定義 (依據文件建議) [cite: 517] ---
#define PIN_DS18B20 4      // 需接 4.7k 上拉電阻
#define PIN_TDS 34         // Analog Input (僅輸入)
#define PIN_WATER_LEVEL 35 // Analog Input (僅輸入)
#define PIN_TURBIDITY 32   // Analog Input (需分壓)
#define PIN_PH 33          // Analog Input (需分壓)

// --- MQTT 設定 ---
const char *mqtt_server = "MQTTGO.io";
const int mqtt_port = 1883;
const char *mqtt_topic = "ttu_fish/sensors";

WiFiClient espClient;
PubSubClient client(espClient);

// --- 感測器物件 ---
OneWire oneWire(PIN_DS18B20);
DallasTemperature sensors(&oneWire);

// --- 校正參數 (依據文件 PH_fish.ino)  ---
// 請使用 PH4.0 與 PH7.0 標準液重新校正這兩個數值
float pH_Slope = -4.6322;
float pH_Intercept = 18.587;

// --- 變數 ---
unsigned long lastMsgTime = 0;
const long interval = 60000; // 每 60 秒發送一次

void setup() {
  Serial.begin(115200);
  Serial.println("\n[System] ESP32 Sensor Node Starting...");

  // 1. 初始化感測器
  sensors.begin();
  analogReadResolution(12); // 設定 ADC 為 12-bit (0-4095) [cite: 544]

  pinMode(PIN_TDS, INPUT);
  pinMode(PIN_WATER_LEVEL, INPUT);
  pinMode(PIN_TURBIDITY, INPUT);
  pinMode(PIN_PH, INPUT);

  // 2. WiFi 連線 (WiFiManager)
  WiFiManager wm;
  // wm.resetSettings(); // 若要清除舊 WiFi 設定，請取消註解並燒錄一次
  if (!wm.autoConnect("Fish_Sensor_Node")) {
    Serial.println("WiFi 連線失敗，系統重啟...");
    ESP.restart();
  }
  Serial.println("[WiFi] Connected!");

  // 3. MQTT 設定
  client.setServer(mqtt_server, mqtt_port);
}

void reconnect() {
  while (!client.connected()) {
    String clientId = "ESP32_Sensors_" + String(random(0xffff), HEX);
    Serial.print("[MQTT] Connecting...");
    if (client.connect(clientId.c_str())) {
      Serial.println("Connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5s");
      delay(5000);
    }
  }
}

// 讀取平均值 (去除雜訊)
int readStableAnalog(int pin) {
  long sum = 0;
  int sampleCount = 30;
  for (int i = 0; i < sampleCount; i++) {
    sum += analogRead(pin);
    delay(10);
  }
  return sum / sampleCount;
}

void loop() {
  if (!client.connected())
    reconnect();
  client.loop();

  unsigned long now = millis();
  if (now - lastMsgTime > interval) {
    lastMsgTime = now;

    // --- 1. 讀取溫度 (DS18B20) [cite: 554-555] ---
    sensors.requestTemperatures();
    float tempC = sensors.getTempCByIndex(0);
    if (tempC == -127.0)
      tempC = 0.0; // 錯誤處理

    // --- 2. 讀取水位 ---
    int rawLevel = readStableAnalog(PIN_WATER_LEVEL);

    // --- 3. 讀取 TDS 並轉換 ---
    int rawTds = readStableAnalog(PIN_TDS);
    float tdsVoltage = rawTds * (3.3 / 4095.0);
    // 基礎 TDS 公式 (建議加入溫度補償會更準) [cite: 568]
    float tdsValue = (133.42 * pow(tdsVoltage, 3) -
                      255.86 * pow(tdsVoltage, 2) + 857.39 * tdsVoltage) *
                     0.5;

    // --- 4. 讀取 濁度 (Turbidity) ---
    int rawTurb = readStableAnalog(PIN_TURBIDITY);
    float turbVoltage = rawTurb * (3.3 / 4095.0);
    // 濁度通常：電壓越低 = 水越髒 (需依模組特性調整)

    // --- 5. 讀取 PH 值 ---
    int rawPh = readStableAnalog(PIN_PH);
    float phVoltage = rawPh * (3.3 / 4095.0);
    // PH 線性公式: y = mx + b [cite: 486]
    float phValue = (phVoltage * pH_Slope) + pH_Intercept;

    // --- 6. 打包 JSON ---
    StaticJsonDocument<300> doc;
    doc["temp"] = tempC;
    doc["level"] = rawLevel;
    doc["tds"] = tdsValue;
    doc["turbidity"] = turbVoltage; // 傳送電壓值給後端判斷較靈活
    doc["ph"] = phValue;
    doc["ph_volt"] = phVoltage; // 方便除錯用

    char jsonBuffer[300];
    serializeJson(doc, jsonBuffer);

    // --- 7. 發送 ---
    Serial.println(jsonBuffer);
    client.publish(mqtt_topic, jsonBuffer);
  }
}