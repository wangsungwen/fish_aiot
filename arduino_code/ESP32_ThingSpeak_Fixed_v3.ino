/*
 * 專案：魚菜共生 AIoT - ESP32 旗艦版 (含自動控制日誌回報功能)
 * 修改紀錄：
 * 1. 新增 MQTT Log Topic 用於回報自動控制事件
 * 2. checkLogic 中加入 sendEventLog 呼叫
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

// --- pH 校正參數 ---
float PH_slope = -5.70;     // 斜率 m
float PH_intercept = 21.34; // 截距 c (Offset)

// Topic 定義
const char *mqttTopicPump = "fish/control/pump";
const char *mqttTopicHeater = "fish/control/heater";
const char *mqttTopicSensors = "ttu_fish/sensors";
// [新增] 日誌回報 Topic
const char *mqttTopicLog = "ttu_fish/log";

// 硬體腳位定義
const int PIN_TEMP = 4;         // DS18B20 水溫
const int PIN_TDS = 34;         // TDS (ADC1)
const int PIN_TURBIDITY = 35;   // 濁度 (ADC1)
const int PIN_PH = 32;          // PH (ADC1) -- 維持 32
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

// 手動模式控制變數
bool manual_mode = false;
unsigned long manual_timer = 0;
const long manual_timeout = 60000; // 手動模式持續 60 秒

// 計時器
unsigned long lastMsg = 0;   // ThingSpeak 計時
const long interval = 20000; // 20秒上傳一次

unsigned long lastMqttPub = 0;  // MQTT 即時更新計時
const long intervalMqtt = 2000; // 2秒發送一次 (即時)

// --- 平滑濾波函式 (一般用) ---
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

  // 取樣 10 次
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

  // 換算為 ESP32 讀到的電壓
  float vPin = avgADC * (vRef / adcResolution);

  // 還原為感測器原始輸出的電壓 (乘回分壓倍率)
  float vSensor = vPin * PH_dividerRatio;

  return vSensor;
}

// --- [新增] 發送事件日誌函式 ---
void sendEventLog(String type, String message) {
  if (!client.connected())
    return;
  // 建立 JSON: {"event_type": "AUTO_CONTROL", "message": "內容"}
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

  // 啟動手動模式保護
  manual_mode = true;
  manual_timer = millis();
  Serial.println(">>> 進入手動模式 (暫停自動控制 60秒)");

  // 可選擇回報手動模式訊息
  // sendEventLog("MANUAL_INTERRUPT", "使用者手動介入，暫停自動控制");

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
  sensors.requestTemperatures();
  val_temp = sensors.getTempCByIndex(0);

  if (val_temp == -127.00) {
    Serial.println("溫度感測器錯誤!");
    val_temp = 25.0; // 安全預設值
  }

  val_level = analogRead(PIN_WATER_LEVEL);

  float raw_tds = analogRead(PIN_TDS);
  val_tds = (raw_tds * 3.3 / 4095.0) * 100;

  // --- 新的 pH 讀取邏輯 ---
  float voltagePH = readPHVoltage(PIN_PH);
  val_ph = (PH_slope * voltagePH) + PH_intercept;

  float espVoltage = getSmoothedVoltage(PIN_TURBIDITY);
  float sensorVoltage = espVoltage * voltageDividerRatio;
  val_turb = sensorVoltage;

  float ntu;
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

  Serial.printf("[Sensors] T:%.1f, PH:%.1f, NTU:%d, Level:%.0f\n", val_temp,
                val_ph, val_ntu, val_level);
}

// [修改] 邏輯控制 (加入日誌回報)
void checkLogic() {

  // 手動模式檢查
  if (manual_mode) {
    if (millis() - manual_timer > manual_timeout) {
      manual_mode = false;
      Serial.println("<<< 手動模式結束，恢復自動控制");
      // sendEventLog("INFO", "恢復自動控制模式");
    } else {
      return;
    }
  }

  // 1. 加熱棒
  if (val_temp < 20 && val_temp > 5) {
    if (!heater_status) {
      client.publish(mqttTopicHeater, "ON");
      heater_status = true;
      // [新增] 回報開啟
      sendEventLog("AUTO_CONTROL",
                   "水溫過低(" + String(val_temp, 1) + "C) -> 開啟加熱棒");
    }
  } else if (val_temp >= 20) {
    if (heater_status) {
      client.publish(mqttTopicHeater, "OFF");
      heater_status = false;
      // [新增] 回報關閉
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

      // [修正] 更精確的回報原因
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
      // [新增] 回報關閉
      sendEventLog("AUTO_CONTROL", "水質恢復正常 -> 關閉過濾馬達");
    }
  }
}

// 打包並發送 JSON 數據
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