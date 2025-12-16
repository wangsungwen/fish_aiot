/*
 * 檔案名稱: Esp8266_Motor_Fixed.ino
 * 功能: 清洗平台馬達控制
 */
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

#define IN1 D1
#define IN2 D2
#define LIMIT_SWITCH_FORWARD D5
#define LIMIT_SWITCH_REVERSE D6
#define RESET_PIN D7

const char *mqtt_server = "mqttgo.io";
const int mqtt_port = 1883;

// 統一 Topic
const char *topic_cmd = "fish/control/motor1";
const char *topic_status = "fish/control/motor1/status";

WiFiClient espClient;
PubSubClient client(espClient);

enum MotorDirection { STOPPED, FORWARD, REVERSE };
MotorDirection currentMotorDirection = STOPPED;
enum SequenceState { SEQ_IDLE, SEQ_FORWARD, SEQ_REVERSE };
SequenceState sequenceState = SEQ_IDLE;
unsigned long sequenceStartTime = 0;

bool isForwardLimit = false, isReverseLimit = false;
unsigned long fwdLastChange = 0, revLastChange = 0;
const unsigned long DEBOUNCE_MS = 20;

const unsigned long BACKOFF_MS = 200;
unsigned long backoffUntil = 0;

void publishStatus(const String &s) {
  client.publish(topic_status, s.c_str(), true);
}

void stopMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  if (currentMotorDirection != STOPPED) {
    currentMotorDirection = STOPPED;
    publishStatus("STOP");
    Serial.println("[MOTOR] STOP");
  }
}

void moveForward() {
  if (isForwardLimit || millis() < backoffUntil) {
    stopMotor();
    return;
  }
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  if (currentMotorDirection != FORWARD) {
    currentMotorDirection = FORWARD;
    publishStatus("FORWARD");
  }
}

void moveReverse() {
  if (isReverseLimit || millis() < backoffUntil) {
    stopMotor();
    return;
  }
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  if (currentMotorDirection != REVERSE) {
    currentMotorDirection = REVERSE;
    publishStatus("REVERSE");
  }
}

void readLimitSwitches() {
  int rawF = digitalRead(LIMIT_SWITCH_FORWARD);
  int rawR = digitalRead(LIMIT_SWITCH_REVERSE);
  unsigned long now = millis();
  static int lastF = HIGH, lastR = HIGH;

  if (rawF != lastF) {
    fwdLastChange = now;
    lastF = rawF;
  }
  if (rawR != lastR) {
    revLastChange = now;
    lastR = rawR;
  }

  if ((now - fwdLastChange) > DEBOUNCE_MS)
    isForwardLimit = (rawF == LOW);
  if ((now - revLastChange) > DEBOUNCE_MS)
    isReverseLimit = (rawR == LOW);
}

void handleLimitActions() {
  if (currentMotorDirection == FORWARD && isForwardLimit) {
    publishStatus("LIMIT_FORWARD");
    backoffUntil = millis() + BACKOFF_MS;
    moveReverse();
    if (sequenceState == SEQ_FORWARD) {
      sequenceState = SEQ_REVERSE;
      sequenceStartTime = millis();
    }
  } else if (currentMotorDirection == REVERSE && isReverseLimit) {
    publishStatus("LIMIT_REVERSE");
    backoffUntil = millis() + BACKOFF_MS;
    moveForward();
    if (sequenceState == SEQ_REVERSE) {
      sequenceState = SEQ_IDLE;
    }
  }
}

void handleSequence() {
  if (sequenceState == SEQ_IDLE)
    return;
  unsigned long now = millis();

  if (sequenceState == SEQ_FORWARD) {
    if (now - sequenceStartTime >= 3000) {
      sequenceState = SEQ_REVERSE;
      sequenceStartTime = now;
      moveReverse();
    }
  } else if (sequenceState == SEQ_REVERSE) {
    if (now - sequenceStartTime >= 3000) {
      sequenceState = SEQ_IDLE;
      stopMotor();
    }
  }
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++)
    msg += (char)payload[i];

  String numStr = "";
  for (char c : msg)
    if (isdigit(c))
      numStr += c;
  int cmd = numStr.toInt();

  Serial.printf("[MQTT] CMD: %d\n", cmd);

  if (cmd != 3)
    sequenceState = SEQ_IDLE;

  switch (cmd) {
  case 0:
    stopMotor();
    break;
  case 1:
    moveForward();
    break;
  case 2:
    moveReverse();
    break;
  case 3:
    publishStatus("SEQ_START");
    sequenceState = SEQ_FORWARD;
    sequenceStartTime = millis();
    moveForward();
    break;
  case 99:
    WiFiManager wm;
    wm.resetSettings();
    ESP.restart();
    break;
  }
}

void reconnect() {
  while (!client.connected()) {
    String clientId = "ESP8266_Motor_" + String(ESP.getChipId(), HEX);
    if (client.connect(clientId.c_str())) {
      client.subscribe(topic_cmd);
      publishStatus("ONLINE");
    } else {
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(RESET_PIN, INPUT_PULLUP);
  pinMode(LIMIT_SWITCH_FORWARD, INPUT_PULLUP);
  pinMode(LIMIT_SWITCH_REVERSE, INPUT_PULLUP);
  stopMotor();

  if (digitalRead(RESET_PIN) == LOW) {
    WiFiManager wm;
    wm.resetSettings();
    ESP.restart();
  }

  WiFiManager wm;
  if (!wm.autoConnect("Fish_Motor_Setup"))
    ESP.restart();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    stopMotor();
  } else {
    if (!client.connected())
      reconnect();
    client.loop();
  }
  readLimitSwitches();
  handleLimitActions();
  handleSequence();
}