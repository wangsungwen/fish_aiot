/*
 * 檔案名稱: Esp8266_3_Relays_Fixed_v2.ino
 * 功能: MQTT 3個繼電器控制 (修復餵食器邏輯)
 */

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

// 繼電器腳位
// ⚠️ 注意: D3 (GPIO0) 上電時不可被拉低
#define NUM_RELAYS 3
const int RELAY_PINS[NUM_RELAYS] = {D1, D2, D3};

// 設定繼電器觸發模式: true 為高電位觸發, false 為低電位觸發
const bool RELAY_ACTIVE_HIGH = false;

const char *mqtt_server = "mqttgo.io";
const int mqtt_port = 1883;
String topic_sub[NUM_RELAYS];
String topic_pub[NUM_RELAYS];

WiFiClient espClient;
PubSubClient mqtt(espClient);
bool relayStates[NUM_RELAYS];

// --- 自動餵食器非阻塞控制變數 ---
bool feederActive = false;
unsigned long feederStartTime = 0;
const unsigned long FEEDER_PULSE_MS = 500;
const int FEEDER_INDEX = 2; // D3
// -------------------------------

void setupTopics() {
  topic_sub[0] = "fish/control/pump";
  topic_pub[0] = topic_sub[0] + "/status";
  Serial.println("[Device] Relay 1 Topic: fish/control/pump");

  topic_sub[1] = "fish/control/heater";
  topic_pub[1] = topic_sub[1] + "/status";
  Serial.println("[Device] Relay 2 Topic: fish/control/heater");

  topic_sub[2] = "fish/control/feeder";
  topic_pub[2] = topic_sub[2] + "/status";
  Serial.println("[Device] Relay 3 Topic: fish/control/feeder");
}

// 通用的繼電器控制函式
void applyRelay(int relayIndex, bool on) {
  if (relayIndex < 0 || relayIndex >= NUM_RELAYS)
    return;

  // 根據設定決定 HIGH 或 LOW 代表 ON
  int level =
      on ? (RELAY_ACTIVE_HIGH ? HIGH : LOW) : (RELAY_ACTIVE_HIGH ? LOW : HIGH);
  digitalWrite(RELAY_PINS[relayIndex], level);
  relayStates[relayIndex] = on;

  if (relayIndex != FEEDER_INDEX && mqtt.connected()) {
    mqtt.publish(topic_pub[relayIndex].c_str(), on ? "ON" : "OFF", true);
    Serial.printf("[MQTT] Relay %d State Published: %s\n", relayIndex + 1,
                  on ? "ON" : "OFF");
  }
}

void toggleRelay(int relayIndex) {
  if (relayIndex < 0 || relayIndex >= NUM_RELAYS)
    return;
  if (relayIndex == FEEDER_INDEX)
    return;
  applyRelay(relayIndex, !relayStates[relayIndex]);
}

// [修改] 觸發餵食器 (修復極性錯誤)
void triggerFeeder() {
  if (feederActive)
    return;

  Serial.printf("[Logic] Feeder Triggered (%lums pulse)\n", FEEDER_PULSE_MS);

  // 修正：依據 RELAY_ACTIVE_HIGH 設定來決定「開啟」的電位
  // 若為 Low Trigger (false)，這裡輸出 LOW 才是開啟
  int onLevel = RELAY_ACTIVE_HIGH ? HIGH : LOW;
  digitalWrite(RELAY_PINS[FEEDER_INDEX], onLevel);

  feederActive = true;
  feederStartTime = millis();
  mqtt.publish(topic_pub[FEEDER_INDEX].c_str(), "ON", true);
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; ++i)
    msg += (char)payload[i];
  msg.trim();
  Serial.printf("[MQTT] Recv: %s -> %s\n", topic, msg.c_str());

  int relayIndex = -1;
  String topicStr(topic);
  if (topicStr.equals(topic_sub[0]))
    relayIndex = 0;
  else if (topicStr.equals(topic_sub[1]))
    relayIndex = 1;
  else if (topicStr.equals(topic_sub[2]))
    relayIndex = 2;

  if (relayIndex != -1) {
    if (relayIndex == FEEDER_INDEX) {
      if (msg.equalsIgnoreCase("ON") || msg == "1" ||
          msg.equalsIgnoreCase("TOGGLE")) {
        triggerFeeder();
      }
    } else {
      if (msg.equalsIgnoreCase("ON") || msg == "1")
        applyRelay(relayIndex, true);
      else if (msg.equalsIgnoreCase("OFF") || msg == "0")
        applyRelay(relayIndex, false);
      else if (msg.equalsIgnoreCase("TOGGLE"))
        toggleRelay(relayIndex);
    }
  }
}

void reconnect() {
  while (!mqtt.connected()) {
    String clientId = "ESP8266_3Relays_" + String(ESP.getChipId(), HEX);
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("[MQTT] Connected");
      for (int i = 0; i < NUM_RELAYS; i++) {
        mqtt.subscribe(topic_sub[i].c_str());
        if (i != FEEDER_INDEX) {
          mqtt.publish(topic_pub[i].c_str(), relayStates[i] ? "ON" : "OFF",
                       true);
        }
      }
      mqtt.publish(topic_pub[FEEDER_INDEX].c_str(), "OFF", true);
    } else {
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  setupTopics();
  for (int i = 0; i < NUM_RELAYS; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    relayStates[i] = false;
    applyRelay(i, false);
  }

  // 確保餵食器初始為關閉
  int offLevel = RELAY_ACTIVE_HIGH ? LOW : HIGH;
  digitalWrite(RELAY_PINS[FEEDER_INDEX], offLevel);

  WiFiManager wm;
  String apName = "Fish_3Relays_" + String(ESP.getChipId(), HEX);
  if (!wm.autoConnect(apName.c_str())) {
    ESP.restart();
  }

  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED)
    return;
  if (!mqtt.connected())
    reconnect();
  mqtt.loop();

  // --- 檢查並處理餵食器自動關閉 ---
  if (feederActive) {
    if (millis() - feederStartTime >= FEEDER_PULSE_MS) {
      // [修改] 時間到，關閉餵食器 (修復極性錯誤)
      // 若為 Low Trigger (false)，這裡輸出 HIGH 才是關閉
      int offLevel = RELAY_ACTIVE_HIGH ? LOW : HIGH;
      digitalWrite(RELAY_PINS[FEEDER_INDEX], offLevel);

      feederActive = false;
      Serial.println("[Logic] Feeder Auto-OFF");
      mqtt.publish(topic_pub[FEEDER_INDEX].c_str(), "OFF", true);
    }
  }
}