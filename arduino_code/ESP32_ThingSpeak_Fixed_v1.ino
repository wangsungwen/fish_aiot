/*
 * 專案：魚菜共生 AIoT - ESP32 旗艦版 (JSON 即時推播 + ThingSpeak 備份)
 * 功能：
 * 1. 讀取 5 種感測器 (含 0-4095 類比水位)
 * 2. 每 2 秒透過 MQTT 發送 JSON 數據包 (給網頁即時顯示)
 * 3. 每 20 秒上傳至 ThingSpeak (雲端紀錄)
 * 4. 內建自動控制邏輯 (發送 MQTT 指令給 ESP8266)
 */

#include "ThingSpeak.h"
#include <DallasTemperature.h>
#include <OneWire.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiManager.h>

// ThingSpeak 設定
unsigned long myChannelNumber = 3146597;
const char *myWriteAPIKey = "YX7R6GEYXQDDMWEZP";

// MQTT 設定
const char *mqttServer = "mqttgo.io";
const int mqttPort = 1883;

// Topic 定義
const char *mqttTopicPump = "fish/control/pump";
const char *mqttTopicHeater = "fish/control/heater";
const char *mqttTopicSensors = "ttu_fish/sensors"; // 整合型數據 Topic

// 硬體腳位定義
const int PIN_TEMP = 4;         // DS18B20 水溫
const int PIN_TDS = 34;         // TDS (ADC1)
const int PIN_TURBIDITY = 35;   // 濁度 (ADC1)
const int PIN_PH = 32;          // PH (ADC1)
const int PIN_WATER_LEVEL = 33; // 水位 (ADC1, 0-4095)
const int PIN_LED = 2;          // 狀態燈

// 物件初始化
WiFiClient tsClient;
WiFiClient espClient;
PubSubClient client(espClient);
OneWire oneWire(PIN_TEMP);
DallasTemperature sensors(&oneWire);

// 變數儲存
float val_temp = 0.0, val_tds = 0.0, val_ph = 0.0, val_turb = 0.0,
      val_level = 0.0;
int val_ntu = 0;
bool pump_status = false;
bool heater_status = false;

// 計時器
unsigned long lastMsg = 0;   // ThingSpeak 計時
const long interval = 20000; // 20秒上傳一次

unsigned long lastMqttPub = 0;  // MQTT 即時更新計時
const long intervalMqtt = 2000; // 2秒發送一次 (即時)

// 計算濁度 NTU
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

void reconnect() {
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

void readSensors() {
  // A. 溫度
  sensors.requestTemperatures();
  val_temp = sensors.getTempCByIndex(0);
  if (val_temp == -127.00)
    val_temp = 0.0;

  // B. 水位 (類比讀取 0-4095)
  // 注意：請確保使用 INPUT 模式，而非 INPUT_PULLUP
  val_level = analogRead(PIN_WATER_LEVEL);

  // C. TDS
  float raw_tds = analogRead(PIN_TDS);
  val_tds = (raw_tds * 3.3 / 4095.0) * 100;

  // D. PH
  float raw_ph = analogRead(PIN_PH);
  val_ph = 7.0 + ((2.5 - (raw_ph * 3.3 / 4095.0)) / 0.18);

  // E. 濁度
  float raw_turb = analogRead(PIN_TURBIDITY);
  val_turb = raw_turb * (3.3 / 4095.0);
  val_ntu = calculate_ntu(val_turb);

  Serial.printf("[Sensors] T:%.1f, PH:%.1f, NTU:%d, Level:%.0f\n", val_temp,
                val_ph, val_ntu, val_level);
}

// 邏輯控制
void checkLogic() {
  // 1. 加熱棒
  if (val_temp < 20 && val_temp > 0) {
    if (!heater_status) {
      client.publish(mqttTopicHeater, "ON");
      heater_status = true;
    }
  } else if (val_temp >= 20) {
    if (heater_status) {
      client.publish(mqttTopicHeater, "OFF");
      heater_status = false;
    }
  }

  // 2. 過濾馬達
  bool pump_needed = (val_ntu >= 3000) || (val_tds > 200);
  if (pump_needed) {
    if (!pump_status) {
      client.publish(mqttTopicPump, "ON");
      pump_status = true;
    }
  } else {
    if (pump_status) {
      client.publish(mqttTopicPump, "OFF");
      pump_status = false;
    }
  }
}

// 打包並發送 JSON 數據
void sendMQTTData() {
  if (!client.connected())
    return;

  // 格式: {"temp":25.5, "ph":7.0, "tds":150, "turbidity":1.5, "ntu":100,
  // "level":2048}
  String json = "{";
  json += "\"temp\":" + String(val_temp, 1) + ",";
  json += "\"ph\":" + String(val_ph, 2) + ",";
  json += "\"tds\":" + String(val_tds, 0) + ",";
  json += "\"turbidity\":" + String(val_turb, 2) + ",";
  json += "\"ntu\":" + String(val_ntu) + ",";
  json += "\"level\":" + String(val_level, 0); // 傳送原始 0-4095 數值
  json += "}";

  client.publish(mqttTopicSensors, json.c_str());
}

void setup() {
  Serial.begin(115200);

  // 設定腳位 (類比輸入不使用 PULLUP)
  pinMode(PIN_WATER_LEVEL, INPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_TDS, INPUT);
  pinMode(PIN_TURBIDITY, INPUT);
  pinMode(PIN_PH, INPUT);

  sensors.begin();

  WiFiManager wm;
  if (!wm.autoConnect("FishSystem_AP", "12345678")) {
    Serial.println("WiFi Failed");
  } else {
    Serial.println("WiFi Connected");
  }

  ThingSpeak.begin(tsClient);
  client.setServer(mqttServer, mqttPort);
}

void loop() {
  if (!client.connected())
    reconnect();
  client.loop();

  unsigned long now = millis();

  // 任務 1: 每 2 秒即時更新 (MQTT)
  if (now - lastMqttPub > intervalMqtt) {
    lastMqttPub = now;
    readSensors();  // 讀取
    checkLogic();   // 判斷
    sendMQTTData(); // 發送 JSON 給網頁
  }

  // 任務 2: 每 20 秒備份紀錄 (ThingSpeak)
  if (now - lastMsg > interval) {
    lastMsg = now;
    ThingSpeak.setField(1, val_temp);
    ThingSpeak.setField(2, val_tds);
    ThingSpeak.setField(3, val_ph);
    ThingSpeak.setField(4, val_turb);
    ThingSpeak.setField(5, val_level); // 上傳水位

    int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (x == 200) {
      digitalWrite(PIN_LED, HIGH);
      delay(200);
      digitalWrite(PIN_LED, LOW);
      Serial.println("ThingSpeak Update OK");
    }
  }
}