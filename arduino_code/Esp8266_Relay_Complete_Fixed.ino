// 檔案名稱: Esp8266_Relay_Complete_Fixed.ino
// 功能: MQTT 繼電器控制

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

// 使用者設定區
// 1 = 循環過濾馬達 (Pump)
// 2 = 加熱棒 (Heater)
// 3 = 自動餵食器 (Feeder)
#define DEVICE_TYPE 1 // 燒錄不同設備時請修改此處

// 繼電器腳位 (建議改用 D1 或 D2，D0 上電時不穩定)
#define RELAY_PIN D1
#define RELAY_ACTIVE_HIGH false

const char *mqtt_server = "mqttgo.io";
const int mqtt_port = 1883;

String topic_sub = "";
String topic_pub = "";

WiFiClient espClient;
PubSubClient mqtt(espClient);
bool relayState = false;

void setupTopics() {
  // 統一 Topic 結構為 fish/control/...
  if (DEVICE_TYPE == 1) {
    topic_sub = "fish/control/pump";
    Serial.println("[Device] Mode: Filter Pump");
  } else if (DEVICE_TYPE == 2) {
    topic_sub = "fish/control/heater";
    Serial.println("[Device] Mode: Heater");
  } else if (DEVICE_TYPE == 3) {
    topic_sub = "fish/control/feeder";
    Serial.println("[Device] Mode: Feeder");
  }
  topic_pub = topic_sub + "/status";
}

void applyRelay(bool on) {
  int level =
      on ? (RELAY_ACTIVE_HIGH ? HIGH : LOW) : (RELAY_ACTIVE_HIGH ? LOW : HIGH);
  digitalWrite(RELAY_PIN, level);
  relayState = on;

  if (mqtt.connected()) {
    mqtt.publish(topic_pub.c_str(), on ? "ON" : "OFF", true);
    Serial.printf("[MQTT] State Published: %s\n", on ? "ON" : "OFF");
  }
}

void toggleRelay() { applyRelay(!relayState); }

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; ++i)
    msg += (char)payload[i];
  msg.trim();
  Serial.printf("[MQTT] Recv: %s -> %s\n", topic, msg.c_str());

  if (msg.equalsIgnoreCase("ON") || msg == "1") {
    applyRelay(true);
  } else if (msg.equalsIgnoreCase("OFF") || msg == "0") {
    applyRelay(false);
  } else if (msg.equalsIgnoreCase("TOGGLE")) {
    toggleRelay();
  }
}

void reconnect() {
  while (!mqtt.connected()) {
    String clientId = "ESP8266_Relay_" + String(DEVICE_TYPE) + "_" +
                      String(ESP.getChipId(), HEX);
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("[MQTT] Connected");
      mqtt.subscribe(topic_sub.c_str());
      mqtt.publish(topic_pub.c_str(), relayState ? "ON" : "OFF", true);
    } else {
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  setupTopics();
  applyRelay(false);

  WiFiManager wm;
  // 設定 AP 名稱，避免衝突
  String apName = "Fish_Relay_" + String(DEVICE_TYPE);
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
