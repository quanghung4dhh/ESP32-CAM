#include <Arduino.h>
#include <WiFi.h>

#include "camera/camera_manager.h"
#include "network/wifi_manager.h"
#include "web/web_server.h"
// ================= TUNING =================

// ================= SETUP =================

void setup() {
  Serial.begin(115200);
  Serial.println("\nESP32-CAM Stream Test");

  // Check PSRAM
  Serial.printf("PSRAM found: %s\n", psramFound() ? "YES" : "NO");

  // Initialize camera
  if (!initCamera()) {
    Serial.println("Camera setup failed. Restart required.");
    return;
  }

  // Initialize Wi-Fi
  if (!initWiFi()) {
    Serial.println("Wi-Fi setup failed. Restart required.");
    return;
  }

  // Start web server
  startCameraServer();

  Serial.println("Open the IP address in your browser.");
}

// ================= LOOP =================

void loop() {
  // Camera capture is handled by streamHandler().
  delay(1000);
}