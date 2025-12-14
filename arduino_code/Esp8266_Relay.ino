/*
 * 檔案名稱: Esp8266_Relay.ino
 * 來源: 原始文件 [cite: 66-174]
 */
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

// --------- 使用者設定區 ---------
#define RELAY_PIN D0            // GPIO16
#define RELAY_ACTIVE_HIGH false // 低電位觸發填 false

const char *MQTT_SERVER = "MQTTGO.io";
const uint16_t MQTT_PORT = 1883;

// 重要：請依照設備修改此主題
// 過濾馬達用: "ttu_fish/relay/pump"
// 加熱棒用:   "ttu_fish/relay/heater"
// 餵食器用:   "ttu_fish/relay/feeder"
const char *TOPIC_RELAY = "ttu_fish/relay/pump";
// --------------------------------

WiFiClient espClient;
PubSubClient mqtt(espClient);
bool relayState = false;

void applyRelay(bool on) {
  digitalWrite(RELAY_PIN, on ? (RELAY_ACTIVE_HIGH ? HIGH : LOW)
                             : (RELAY_ACTIVE_HIGH ? LOW : HIGH));
  relayState = on;
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; ++i)
    msg += (char)payload[i];
  msg.trim();
  Serial.printf("收到指令: %s\n", msg.c_str());

  if (msg.equalsIgnoreCase("ON") || msg == "1") {
    applyRelay(true);
  } else if (msg.equalsIgnoreCase("OFF") || msg == "0") {
    applyRelay(false);
  }
}

void reconnect() {
  while (!mqtt.connected()) {
    String clientId = "ESP8266_Relay_" + String(ESP.getChipId(), HEX);
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("MQTT 已連線");
      mqtt.subscribe(TOPIC_RELAY);
    } else {
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  applyRelay(false); // 預設關閉

  WiFiManager wm;
  if (!wm.autoConnect("Fish_Relay_Node")) {
    ESP.restart();
  }

  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED)
    return;
  if (!mqtt.connected())
    reconnect();
  mqtt.loop();
}