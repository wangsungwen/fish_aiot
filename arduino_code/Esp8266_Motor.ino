/*
 * 檔案名稱: Esp8266_Motor.ino (完整進階版)
 * 功能: 清洗平台馬達控制，包含自動清洗排程、限位回彈保護、WiFiManager
 */
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

// --- 腳位設定 ---
// 馬達驅動 (L298N)
#define IN1 D1 // GPIO5
#define IN2 D2 // GPIO4

// 限位開關 (NO 常開接點，COM 接 GND)
#define LIMIT_SWITCH_FORWARD D5 // GPIO14 (正轉極限)
#define LIMIT_SWITCH_REVERSE D6 // GPIO12 (反轉極限)

// 重置 WiFi 按鈕 (可選)
#define RESET_PIN D7 // GPIO13

// --- MQTT 設定 ---
const char *mqtt_server = "MQTTGO.io";
const int mqtt_port = 1883;
const char *topic_cmd = "ttu_fish/motor1";           // 接收指令
const char *topic_status = "ttu_fish/motor1/status"; // 回報狀態

WiFiClient espClient;
PubSubClient client(espClient);

// --- 變數與狀態 ---
enum MotorDirection { STOPPED, FORWARD, REVERSE };
MotorDirection currentMotorDirection = STOPPED;

enum SequenceState { SEQ_IDLE, SEQ_FORWARD, SEQ_REVERSE };
SequenceState sequenceState = SEQ_IDLE;
unsigned long sequenceStartTime = 0;

// 限位開關防抖動
bool isForwardLimit = false, isReverseLimit = false;
unsigned long fwdLastChange = 0, revLastChange = 0;
const unsigned long DEBOUNCE_MS = 20;

// 撞牆回彈機制 (Backoff)
const unsigned long BACKOFF_MS = 200; // 撞到後退 0.2 秒
unsigned long backoffUntil = 0;

// --- 輔助函式 ---
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
  // 如果已經撞到限位，或正在回彈中，則不允許前進
  if (isForwardLimit || millis() < backoffUntil) {
    stopMotor();
    return;
  }
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  if (currentMotorDirection != FORWARD) {
    currentMotorDirection = FORWARD;
    publishStatus("FORWARD");
    Serial.println("[MOTOR] FORWARD");
  }
}

void moveReverse() {
  // 如果已經撞到限位，或正在回彈中，則不允許後退
  if (isReverseLimit || millis() < backoffUntil) {
    stopMotor();
    return;
  }
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  if (currentMotorDirection != REVERSE) {
    currentMotorDirection = REVERSE;
    publishStatus("REVERSE");
    Serial.println("[MOTOR] REVERSE");
  }
}

// --- 讀取限位開關 (含防抖動) ---
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

  if ((now - fwdLastChange) > DEBOUNCE_MS) {
    isForwardLimit = (rawF == LOW); // LOW 代表被壓下 (觸發)
  }
  if ((now - revLastChange) > DEBOUNCE_MS) {
    isReverseLimit = (rawR == LOW);
  }
}

// --- 處理限位觸發後的動作 ---
void handleLimitActions() {
  // 正在前進且撞到前限位 -> 後退一點點
  if (currentMotorDirection == FORWARD && isForwardLimit) {
    Serial.println("[LIMIT] 撞到前限位 -> 回彈");
    publishStatus("LIMIT_FORWARD");
    backoffUntil = millis() + BACKOFF_MS;
    moveReverse(); // 回彈

    // 如果是在自動清洗流程中，直接切換到下一階段(後退)
    if (sequenceState == SEQ_FORWARD) {
      sequenceState = SEQ_REVERSE;
      sequenceStartTime = millis();
    }
  }
  // 正在後退且撞到後限位 -> 前進一點點
  else if (currentMotorDirection == REVERSE && isReverseLimit) {
    Serial.println("[LIMIT] 撞到後限位 -> 回彈");
    publishStatus("LIMIT_REVERSE");
    backoffUntil = millis() + BACKOFF_MS;
    moveForward(); // 回彈

    // 如果是在自動清洗流程中，結束流程
    if (sequenceState == SEQ_REVERSE) {
      sequenceState = SEQ_IDLE;
    }
  }
}

// --- 自動清洗排程邏輯 (指令 3) ---
void handleSequence() {
  if (sequenceState == SEQ_IDLE)
    return;
  unsigned long now = millis();

  if (sequenceState == SEQ_FORWARD) {
    // 前進 3 秒後 -> 轉後退
    if (now - sequenceStartTime >= 3000) {
      Serial.println("[SEQ] 前進結束 -> 切換後退");
      sequenceState = SEQ_REVERSE;
      sequenceStartTime = now;
      moveReverse();
    }
  } else if (sequenceState == SEQ_REVERSE) {
    // 後退 3 秒後 -> 停止
    if (now - sequenceStartTime >= 3000) {
      Serial.println("[SEQ] 流程結束 -> 停止");
      sequenceState = SEQ_IDLE;
      stopMotor();
    }
  }
}

// --- MQTT 指令處理 ---
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++)
    msg += (char)payload[i];

  // 過濾非數字字元
  String numStr = "";
  for (char c : msg) {
    if (isdigit(c))
      numStr += c;
  }

  int cmd = numStr.toInt();
  Serial.printf("[MQTT] 收到指令: %d\n", cmd);

  // 若收到一般指令，先中斷自動流程
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
  case 3: // 啟動自動清洗流程
    Serial.println("[CMD] 啟動自動清洗 (前3秒->後3秒)");
    publishStatus("SEQ_START");
    sequenceState = SEQ_FORWARD;
    sequenceStartTime = millis();
    moveForward();
    break;
  case 99: // 遠端清除 WiFi
    Serial.println("[CMD] 重置 WiFi 設定...");
    publishStatus("RESET_WIFI");
    WiFiManager wm;
    wm.resetSettings();
    delay(1000);
    ESP.restart();
    break;
  default:
    Serial.println("[CMD] 未知指令");
  }
}

void reconnect() {
  while (!client.connected()) {
    String clientId = "ESP8266_Motor_" + String(ESP.getChipId(), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("MQTT 已連線");
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

  // 實體按鈕重置 WiFi (按住 D7 開機)
  if (digitalRead(RESET_PIN) == LOW) {
    WiFiManager wm;
    wm.resetSettings();
    ESP.restart();
  }

  WiFiManager wm;
  if (!wm.autoConnect("Fish_Motor_Node")) {
    ESP.restart();
  }

  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    stopMotor(); // 斷線保護
  } else {
    if (!client.connected())
      reconnect();
    client.loop();
  }

  readLimitSwitches();  // 1. 讀取開關
  handleLimitActions(); // 2. 處理撞牆回彈
  handleSequence();     // 3. 處理自動流程
}