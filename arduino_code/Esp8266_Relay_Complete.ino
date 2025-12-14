/*
 * 檔案名稱: Esp8266_Relay_Complete.ino
 * 功能: MQTT 繼電器控制 (支援 ON/OFF/TOGGLE 與 狀態回報)
 */
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

// ================= 使用者設定區 =================
// 請依據您現在要燒錄的設備，修改這個數字：
// 1 = 循環過濾馬達 (Pump)
// 2 = 加熱棒 (Heater)
// 3 = 自動餵食器 (Feeder)
#define DEVICE_TYPE 1

// 繼電器腳位 (D0 = GPIO16, 安全腳位)
#define RELAY_PIN D0

// 繼電器觸發模式: 多數模組為低電位觸發(Low Trigger)，請設為 false
#define RELAY_ACTIVE_HIGH false
// =============================================

// 設定 Topic
const char *mqtt_server = "MQTTGO.io";
const int mqtt_port = 1883;

String topic_sub = "";
String topic_pub = "";

WiFiClient espClient;
PubSubClient mqtt(espClient);
bool relayState = false; // false = OFF

void setupTopics() {
  if (DEVICE_TYPE == 1) {
    topic_sub = "ttu_fish/relay/pump";
    Serial.println("[Device] Mode: Filter Pump");
  } else if (DEVICE_TYPE == 2) {
    topic_sub = "ttu_fish/relay/heater";
    Serial.println("[Device] Mode: Heater");
  } else if (DEVICE_TYPE == 3) {
    topic_sub = "ttu_fish/relay/feeder";
    Serial.println("[Device] Mode: Feeder");
  }
  // 狀態回報 Topic 設為相同 (或可自訂)
  topic_pub = topic_sub + "/status";
}

// 執行繼電器動作
void applyRelay(bool on) {
  // 根據高/低電位觸發設定輸出 [cite: 90-91]
  int level =
      on ? (RELAY_ACTIVE_HIGH ? HIGH : LOW) : (RELAY_ACTIVE_HIGH ? LOW : HIGH);
  digitalWrite(RELAY_PIN, level);
  relayState = on;

  // 回報狀態 (Retained = true)
  if (mqtt.connected()) {
    mqtt.publish(topic_sub.c_str(), on ? "ON" : "OFF", true);
    Serial.printf("[MQTT] State Published: %s\n", on ? "ON" : "OFF");
  }
}

void toggleRelay() { applyRelay(!relayState); }

// MQTT 訊息處理 [cite: 116-130]
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
      // 連線後主動回報一次目前狀態
      mqtt.publish(topic_sub.c_str(), relayState ? "ON" : "OFF", true);
    } else {
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(RELAY_PIN, OUTPUT);
  setupTopics();

  // 預設關閉 [cite: 151]
  applyRelay(false);

  // WiFiManager 自動配網 [cite: 153-159]
  WiFiManager wm;
  if (!wm.autoConnect("Fish_Relay_Config")) {
    Serial.println("WiFi Config failed");
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