// 檔案名稱: Esp8266_3_Relays.ino
// 功能: MQTT 3個繼電器控制

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

// 繼電器腳位 (建議改用 D1, D2, D3, D4, D5, D6, D7, D8)
// D0 (GPIO16) 在上電時不穩定，不建議用於繼電器
#define NUM_RELAYS 3
const int RELAY_PINS[NUM_RELAYS] = {D1, D2, D3}; // ESP8266 D1, D2, D3 腳位
const bool RELAY_ACTIVE_HIGH = false; // 繼電器高電位觸發為 true, 低電位觸發為 false

const char *mqtt_server = "mqttgo.io";
const int mqtt_port = 1883;

String topic_sub[NUM_RELAYS];
String topic_pub[NUM_RELAYS];

WiFiClient espClient;
PubSubClient mqtt(espClient);
bool relayStates[NUM_RELAYS];

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

void applyRelay(int relayIndex, bool on) {
  if (relayIndex < 0 || relayIndex >= NUM_RELAYS) return; // 檢查索引是否有效

  int level =
      on ? (RELAY_ACTIVE_HIGH ? HIGH : LOW) : (RELAY_ACTIVE_HIGH ? LOW : HIGH);
  digitalWrite(RELAY_PINS[relayIndex], level);
  relayStates[relayIndex] = on;

  if (mqtt.connected()) {
    mqtt.publish(topic_pub[relayIndex].c_str(), on ? "ON" : "OFF", true);
    Serial.printf("[MQTT] Relay %d State Published: %s\n", relayIndex + 1, on ? "ON" : "OFF");
  }
}

void toggleRelay(int relayIndex) {
  if (relayIndex < 0 || relayIndex >= NUM_RELAYS) return; // 檢查索引是否有效
  applyRelay(relayIndex, !relayStates[relayIndex]);
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; ++i)
    msg += (char)payload[i];
  msg.trim();
  Serial.printf("[MQTT] Recv: %s -> %s\n", topic, msg.c_str());

  int relayIndex = -1;
  String topicStr(topic);

  // 根據 topic 判斷是哪個繼電器
  if (topicStr.equals(topic_sub[0])) {
    relayIndex = 0;
  } else if (topicStr.equals(topic_sub[1])) {
    relayIndex = 1;
  } else if (topicStr.equals(topic_sub[2])) {
    relayIndex = 2;
  }

  if (relayIndex != -1) {
    if (msg.equalsIgnoreCase("ON") || msg == "1") {
      applyRelay(relayIndex, true);
    } else if (msg.equalsIgnoreCase("OFF") || msg == "0") {
      applyRelay(relayIndex, false);
    } else if (msg.equalsIgnoreCase("TOGGLE")) {
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
        mqtt.publish(topic_pub[i].c_str(), relayStates[i] ? "ON" : "OFF", true);
      }
    } else {
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < NUM_RELAYS; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    relayStates[i] = false; // 初始化為 OFF
  }
  setupTopics();
  for (int i = 0; i < NUM_RELAYS; i++) {
    applyRelay(i, false); // 確保所有繼電器初始狀態為 OFF
  }

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
}
