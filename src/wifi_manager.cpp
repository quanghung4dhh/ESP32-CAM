
#include "wifi_manager.h"

#include "secrets.h"

// Connection settings
const unsigned long WIFI_TIMEOUT = 15000;
const unsigned long RECONNECT_INTERVAL = 10000;

// Reconnection management
unsigned long lastReconnectAttempt = 0;
bool reconnecting = false;

// Initialize Wi-Fi
bool initWiFi() {
  Serial.println();
  Serial.println("===== WIFI INITIALIZATION =====");

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);

  Serial.print("Connecting to: ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime >= WIFI_TIMEOUT) {
      Serial.println("Wi-Fi connection timeout!");
      return false;
    }

    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("Wi-Fi connected successfully!");

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("Signal strength (RSSI): ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");

  return true;
}

// Check connection status
bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

// Get local IP address
IPAddress getLocalIP() {
  return WiFi.localIP();
}

// Maintain connection and reconnect when disconnected
void maintainWiFi() {
  if (isWiFiConnected()) {
    reconnecting = false;
    return;
  }

  unsigned long currentTime = millis();

  if (!reconnecting ||
      currentTime - lastReconnectAttempt >= RECONNECT_INTERVAL) {
    Serial.println("Wi-Fi disconnected!");
    Serial.println("Attempting to reconnect...");

    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    lastReconnectAttempt = currentTime;
    reconnecting = true;
  }
}
