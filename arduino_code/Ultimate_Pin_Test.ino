/*
 * ULTIMATE PIN TESTER
 * 找不到腳位？這支程式會輪流測試 D0 ~ D8！
 *
 * 1. Upload this sketch.
 * 2. Connect Servo Signal wire to the pin you THINK is D1.
 * 3. Watch the Servo AND the Serial Monitor.
 *
 * The code will print: "Testing PIN D1..." -> If motor moves, you are on D1.
 * Then "Testing PIN D2..." -> If motor moves, you are on D2.
 * ...
 */

#include <Servo.h>

Servo myServo;

// ESP8266 Pin Mapping
// D0=16, D1=5, D2=4, D3=0, D4=2, D5=14, D6=12, D7=13, D8=15
int pins[] = {D1, D2, D3, D4, D5, D6, D7, D8};
const char *pinNames[] = {"D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8"};

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== ULTIMATE PIN SWEEP START ===");
}

void loop() {
  for (int i = 0; i < 8; i++) {
    int currentPin = pins[i];

    Serial.printf("Now Testing Pin: [%s] ... ", pinNames[i]);

    myServo.attach(currentPin);

    // Wiggle
    myServo.write(45);
    delay(300);
    myServo.write(135);
    delay(300);
    myServo.write(90);
    delay(300);

    myServo.detach();
    Serial.println("Done.");
    delay(500);
  }
  Serial.println("--- Cycle Complete ---\n");
  delay(2000);
}
