/*
 * 檔案名稱: ESP32_ThingSpeak_Fixed_v2.ino
 * 功能：魚菜共生 AIoT - ESP32 旗艦版 (含 TDS 溫度補償 & 水位軟體歸零)
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

// 濁度感測器參數
const float vRef = 3.3;         // ESP32 運作電壓
const int adcResolution = 4095; // ESP32 12-bit 解析度
const int samples = 30;         // 濾波採樣數
// 分壓電路參數 (R1=1k, R2=2k) -> 0~5V to 0~3.3V
const float R1 = 1000.0;
const float R2 = 2000.0;
const float voltageDividerRatio = (R1 + R2) / R2;

// --- pH 分壓電路參數 ---
const float PH_R1 = 2000.0;
const float PH_R2 = 3000.0;
const float PH_dividerRatio = (PH_R1 + PH_R2) / PH_R2;

// --- pH 校正參數 (請依實際校正更新) ---
float PH_slope = -5.70;     // 斜率 m
float PH_intercept = 21.34; // 截距 c (Offset)

// Topic 定義
const char *mqttTopicPump = "fish/control/pump";
const char *mqttTopicHeater = "fish/control/heater";
const char *mqttTopicSensors = "ttu_fish/sensors";
const char *mqttTopicLog = "ttu_fish/log";

// 硬體腳位定義
const int PIN_TEMP = 4;         // DS18B20 水溫
const int PIN_TDS = 34;         // TDS (ADC1)
const int PIN_TURBIDITY = 35;   // 濁度 (ADC1)
const int PIN_PH = 32;          // PH (ADC1)
const int PIN_WATER_LEVEL = 33; // 水位訊號讀取 (Signal)
const int PIN_WATER_POWER = 25; // 水位供電控制 (VCC) - 用於防腐蝕
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

// 手動模式控制變數
bool manual_mode = false;
unsigned long manual_timer = 0;
const long manual_timeout = 60000; // 手動模式持續 60 秒

// 計時器
unsigned long lastMsg = 0;
const long interval = 20000; // 20秒上傳一次 ThingSpeak

unsigned long lastMqttPub = 0;
const long intervalMqtt = 2000; // 2秒發送一次 MQTT (即時)

// --- 平滑濾波函式 ---
float getSmoothedVoltage(int pin) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(10);
  }
  float averageReading = sum / (float)samples;
  return averageReading * (vRef / adcResolution);
}

// --- pH 專用：讀取電壓 (含濾波與分壓還原) ---
float readPHVoltage(int pin) {
  int buf[10], temp;
  unsigned long int avgValue = 0;
  for (int i = 0; i < 10; i++) {
    buf[i] = analogRead(pin);
    delay(10);
  }
  // 排序 (泡沫排序法)
  for (int i = 0; i < 9; i++) {
    for (int j = i + 1; j < 10; j++) {
      if (buf[i] > buf[j]) {
        temp = buf[i];
        buf[i] = buf[j];
        buf[j] = temp;
      }
    }
  }
  // 去除最大與最小各 2 個值，取中間 6 個平均
  for (int i = 2; i < 8; i++) {
    avgValue += buf[i];
  }
  float avgADC = (float)avgValue / 6.0;
  float vPin = avgADC * (vRef / adcResolution);
  float vSensor = vPin * PH_dividerRatio;
  return vSensor;
}

// --- 發送事件日誌函式 ---
void sendEventLog(String type, String message) {
  if (!client.connected())
    return;
  String json =
      "{\"event_type\":\"" + type + "\", \"message\":\"" + message + "\"}";
  client.publish(mqttTopicLog, json.c_str());
}

// --- MQTT 接收回調函式 ---
void callback(char *topic, byte *payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  Serial.print("收到指令 [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(msg);

  manual_mode = true;
  manual_timer = millis();
  Serial.println(">>> 進入手動模式 (暫停自動控制 60秒)");

  if (String(topic) == mqttTopicPump) {
    pump_status = (msg == "ON");
  }
  if (String(topic) == mqttTopicHeater) {
    heater_status = (msg == "ON");
  }
}

void reconnect() {
  if (!client.connected()) {
    Serial.print("嘗試連線 MQTT... ");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("已連線");
      client.subscribe("fish/control/#");
    } else {
      Serial.print("失敗, rc=");
      Serial.print(client.state());
      Serial.println(" (稍後重試)");
    }
  }
}

void readSensors() {
  // 1. 讀取水溫
  sensors.requestTemperatures();
  val_temp = sensors.getTempCByIndex(0);
  if (val_temp == -127.00) {
    Serial.println("溫度感測器錯誤! (請檢查 4.7k 電阻)");
    val_temp = 25.0; // 安全預設值
  }

  // 2. [修改] 讀取水位 - 間歇供電 + 軟體歸零
  digitalWrite(PIN_WATER_POWER, HIGH);     // 供電 ON
  delay(10);                               // 等待穩定
  val_level = analogRead(PIN_WATER_LEVEL); // 讀取數據
  digitalWrite(PIN_WATER_POWER, LOW);      // 供電 OFF (防腐蝕)

  // [新增] 軟體歸零：過濾懸空雜訊 (若小於 200 視為 0)
  if (val_level < 200) {
    val_level = 0;
  }

  // 3. [修改] 讀取 TDS (加入溫度補償)
  float raw_tds_voltage = analogRead(PIN_TDS) * 3.3 / 4095.0;

  // 溫度補償係數: 基準溫 25°C，每變化 1°C 補償 2%
  float compensationCoefficient = 1.0 + 0.02 * (val_temp - 25.0);
  // 避免除以 0 或負數 (雖然物理上不太可能)
  if (compensationCoefficient <= 0)
    compensationCoefficient = 1.0;

  float compensationVoltage = raw_tds_voltage / compensationCoefficient;

  // 將補償後的電壓轉換為 TDS ppm (通用公式)
  val_tds = (133.42 * compensationVoltage * compensationVoltage *
                 compensationVoltage -
             255.86 * compensationVoltage * compensationVoltage +
             857.39 * compensationVoltage) *
            0.5;
  if (val_tds < 0)
    val_tds = 0;

  // 4. 讀取 pH
  float voltagePH = readPHVoltage(PIN_PH);
  val_ph = (PH_slope * voltagePH) + PH_intercept;

  // 5. 讀取混濁度
  float espVoltage = getSmoothedVoltage(PIN_TURBIDITY);
  float sensorVoltage = espVoltage * voltageDividerRatio;
  val_turb = sensorVoltage;

  float ntu;
  // 依據您的感測器特性調整閾值
  if (sensorVoltage < 2.5) {
    ntu = 3000.0;
  } else if (sensorVoltage > 4.2) {
    ntu = 0.0;
  } else {
    ntu = -1120.4 * (sensorVoltage * sensorVoltage) + 5742.3 * sensorVoltage -
          4353.8;
  }
  if (ntu < 0)
    ntu = 0;
  val_ntu = (int)ntu;

  Serial.printf("[Sensors] T:%.1f, PH:%.1f, NTU:%d, Level:%.0f, TDS:%.0f\n",
                val_temp, val_ph, val_ntu, val_level, val_tds);
}

// 邏輯控制
void checkLogic() {
  if (manual_mode) {
    if (millis() - manual_timer > manual_timeout) {
      manual_mode = false;
      Serial.println("<<< 手動模式結束，恢復自動控制");
    } else {
      return;
    }
  }

  // 1. 加熱棒
  if (val_temp < 20 && val_temp > 5) {
    if (!heater_status) {
      client.publish(mqttTopicHeater, "ON");
      heater_status = true;
      sendEventLog("AUTO_CONTROL",
                   "水溫過低(" + String(val_temp, 1) + "C) -> 開啟加熱棒");
    }
  } else if (val_temp >= 20) {
    if (heater_status) {
      client.publish(mqttTopicHeater, "OFF");
      heater_status = false;
      sendEventLog("AUTO_CONTROL",
                   "水溫恢復(" + String(val_temp, 1) + "C) -> 關閉加熱棒");
    }
  }

  // 2. 過濾馬達
  bool pump_needed =
      (val_ntu >= 3000) || (val_tds > 200) || (val_ph < 6.5) || (val_ph > 8.5);
  if (pump_needed) {
    if (!pump_status) {
      client.publish(mqttTopicPump, "ON");
      pump_status = true;
      String reason = "";
      if (val_ntu >= 3000)
        reason = "混濁度高";
      else if (val_tds > 200)
        reason = "TDS過高";
      else if (val_ph < 6.5)
        reason = "PH過酸";
      else if (val_ph > 8.5)
        reason = "PH過鹼";
      sendEventLog("AUTO_CONTROL", "水質異常(" + reason + ") -> 開啟過濾馬達");
    }
  } else {
    if (pump_status) {
      client.publish(mqttTopicPump, "OFF");
      pump_status = false;
      sendEventLog("AUTO_CONTROL", "水質恢復正常 -> 關閉過濾馬達");
    }
  }
}

void sendMQTTData() {
  if (!client.connected())
    return;
  String json = "{";
  json += "\"temp\":" + String(val_temp, 1) + ",";
  json += "\"ph\":" + String(val_ph, 2) + ",";
  json += "\"tds\":" + String(val_tds, 0) + ",";
  json += "\"turbidity\":" + String(val_turb, 2) + ",";
  json += "\"ntu\":" + String(val_ntu) + ",";
  json += "\"level\":" + String(val_level, 0);
  json += "}";
  client.publish(mqttTopicSensors, json.c_str());
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  pinMode(PIN_WATER_LEVEL, INPUT);
  pinMode(PIN_WATER_POWER, OUTPUT);
  digitalWrite(PIN_WATER_POWER, LOW); // 預設關閉

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
  client.setCallback(callback);
}

void loop() {
  if (!client.connected())
    reconnect();
  client.loop();

  unsigned long now = millis();

  if (now - lastMqttPub > intervalMqtt) {
    lastMqttPub = now;
    readSensors();
    checkLogic();
    sendMQTTData();
  }

  if (now - lastMsg > interval) {
    lastMsg = now;
    ThingSpeak.setField(1, val_temp);
    ThingSpeak.setField(2, val_tds);
    ThingSpeak.setField(3, val_ph);
    ThingSpeak.setField(4, val_turb);
    ThingSpeak.setField(5, val_level);
    int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
    if (x == 200) {
      digitalWrite(PIN_LED, HIGH);
      delay(200);
      digitalWrite(PIN_LED, LOW);
      Serial.println("ThingSpeak Update OK");
    }
  }
}