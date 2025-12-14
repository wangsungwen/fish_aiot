/*
 * Simple Servo Test (No MQTT, No WiFi)
 * 測試 MG996R 是否能正常轉動
 *
 * 接線說明：
 * Signal (橘/黃) -> D1 (GPIO 5)
 * VCC (紅)      -> 5V (外部電源，不要用 ESP8266 的 3.3V)
 * GND (棕/黑)    -> GND (必須與 ESP8266 共地)
 */

#include <Servo.h>

#define SERVO_PIN D1

Servo myServo;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== Servo Sweep Test Start ===");
  Serial.println("Pin: D1");

  myServo.attach(SERVO_PIN);
}

void loop() {
  Serial.println("Turn Forward (2000us)");
  myServo.writeMicroseconds(2000);
  delay(2000);

  Serial.println("Stop (1500us)");
  myServo.writeMicroseconds(1500);
  delay(2000);

  Serial.println("Turn Reverse (1000us)");
  myServo.writeMicroseconds(1000);
  delay(2000);
}
