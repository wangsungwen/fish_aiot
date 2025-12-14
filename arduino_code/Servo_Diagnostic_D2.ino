/*
 * Hardware Diagnostic Test
 *
 * TEST OBJECTIVE: Check if Pin D2 is alive and if Servo works.
 *
 * WIRING CHANGE:
 * Please move Signal Wire to **Pin D2** (GPIO 4)
 *
 * BEHAVIOR:
 * 1. checks "Blink" on D2 for 5 seconds (HIGH/LOW every 1 sec).
 *    -> Use a Multimeter or LED to see if voltage changes (0V <-> 3.3V).
 * 2. Attaches Servo and tries to move.
 */

#include <Servo.h>

#define TEST_PIN D2 // CHANGED TO D2

Servo myServo;

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n=== Diagnostic Test Start ===");

  // Phase 1: Digital IO Test (Blink)
  Serial.println("Phase 1: Blinking D2 (Check voltage with Multimeter)...");
  pinMode(TEST_PIN, OUTPUT);
  for (int i = 0; i < 5; i++) {
    digitalWrite(TEST_PIN, HIGH); // 3.3V
    Serial.println("  D2 -> HIGH (3.3V)");
    delay(1000);
    digitalWrite(TEST_PIN, LOW); // 0V
    Serial.println("  D2 -> LOW  (0V)");
    delay(1000);
  }

  // Phase 2: Servo Test
  Serial.println("Phase 2: Attaching Servo to D2...");
  myServo.attach(TEST_PIN);
}

void loop() {
  Serial.println("Servo: 0 degree (1000us)");
  myServo.writeMicroseconds(1000);
  delay(1500);

  Serial.println("Servo: 90 degree (1500us)");
  myServo.writeMicroseconds(1500);
  delay(1500);

  Serial.println("Servo: 180 degree (2000us)");
  myServo.writeMicroseconds(2000);
  delay(1500);
}
