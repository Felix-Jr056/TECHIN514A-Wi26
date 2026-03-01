#include <Arduino.h>

const int FSR_PIN = 2;

void setup() {
  Serial.begin(115200);
  pinMode(FSR_PIN, INPUT);
  Serial.println("FSR Sensor Ready");
}

void loop() {
  int state = digitalRead(FSR_PIN);

  if (state == HIGH) {
    Serial.println("Pressure detected!");
  } else {
    Serial.println("No pressure");
  }

  delay(500);
}
