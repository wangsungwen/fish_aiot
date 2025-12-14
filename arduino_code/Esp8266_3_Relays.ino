// 檔案名稱: Esp8266_3_Relays_NonBlocking.ino
// 功能: MQTT 3個繼電器控制 (含非阻塞式自動餵食器點動)

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

// 繼電器腳位
// ⚠️ 注意: D3 (GPIO0) 上電時不可被拉低，否則無法開機。請確保硬體接線正確。
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
const unsigned long FEEDER_PULSE_MS = 500; // 點動時間改為 500ms (符合文件)
const int FEEDER_INDEX = 2;                // D3 是第3個繼電器，索引為 2
// -------------------------------

void setupTopics() {
  // (維持原樣...)
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

  // 餵食器狀態不需在此發布，由主迴圈處理
  if (relayIndex != FEEDER_INDEX && mqtt.connected()) {
    mqtt.publish(topic_pub[relayIndex].c_str(), on ? "ON" : "OFF", true);
    Serial.printf("[MQTT] Relay %d State Published: %s\n", relayIndex + 1,
                  on ? "ON" : "OFF");
  }
}

void toggleRelay(int relayIndex) {
  if (relayIndex < 0 || relayIndex >= NUM_RELAYS)
    return;
  // 餵食器不使用 toggle
  if (relayIndex == FEEDER_INDEX)
    return;
  applyRelay(relayIndex, !relayStates[relayIndex]);
}

// 觸發餵食器 (非阻塞)
void triggerFeeder() {
  if (feederActive)
    return; // 如果已經在動作中，忽略新的觸發

  Serial.printf("[Logic] Feeder Triggered (%lums pulse)\n", FEEDER_PULSE_MS);
  // 假設餵食器是高電位觸發 (這與您的原始程式碼一致)
  digitalWrite(RELAY_PINS[FEEDER_INDEX], HIGH);
  feederActive = true;
  feederStartTime = millis();
  // 發布狀態為 ON
  mqtt.publish(topic_pub[FEEDER_INDEX].c_str(), "ON", true);
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  // (前面維持原樣...)
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
      // D3 自動餵食器特殊邏輯
      if (msg.equalsIgnoreCase("ON") || msg == "1" ||
          msg.equalsIgnoreCase("TOGGLE")) {
        triggerFeeder(); // 呼叫非阻塞觸發函式
      }
      // 忽略 OFF 指令
    } else {
      // 其他繼電器維持既有邏輯
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
  // (維持原樣...)
  while (!mqtt.connected()) {
    String clientId = "ESP8266_3Relays_" + String(ESP.getChipId(), HEX);
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("[MQTT] Connected");
      for (int i = 0; i < NUM_RELAYS; i++) {
        mqtt.subscribe(topic_sub[i].c_str());
        // 重新連線時，回報目前狀態 (餵食器除外，因為它常態是 OFF)
        if (i != FEEDER_INDEX) {
          mqtt.publish(topic_pub[i].c_str(), relayStates[i] ? "ON" : "OFF",
                       true);
        }
      }
      // 確保餵食器狀態為 OFF
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
    // 初始化為 OFF (使用 applyRelay 以符合 ACTIVE_HIGH 設定)
    applyRelay(i, false);
  }

  // 再次確保餵食器 D3 為 LOW (假設它是高電位觸發)
  digitalWrite(RELAY_PINS[FEEDER_INDEX], LOW);

  WiFiManager wm;
  // wm.resetSettings(); // 測試時可取消註解以重設 WiFi
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
      // 時間到，關閉餵食器
      digitalWrite(RELAY_PINS[FEEDER_INDEX],
                   LOW); // 假設高電位觸發，所以 LOW 是關閉
      feederActive = false;
      Serial.println("[Logic] Feeder Auto-OFF");
      // 發布狀態為 OFF
      mqtt.publish(topic_pub[FEEDER_INDEX].c_str(), "OFF", true);
    }
  }
  // -------------------------------
}