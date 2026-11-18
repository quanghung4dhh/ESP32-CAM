#include <Arduino.h>

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("ESP32-CAM STARTED");
}

void loop() {
    Serial.println("Serial communication OK");
    delay(1000);
}