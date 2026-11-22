
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

  // Do not write credentials to flash on every WiFi.begin()
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);

  // IMPORTANT: the ESP32 enables modem-sleep by default. In that mode the
  // radio sleeps between AP beacons, which causes delayed/dropped TCP packets
  // and is a classic source of random "read timeout" / "connection refused"
  // errors on HTTPClient. Streaming a camera frame needs a continuous link.
  WiFi.setSleep(false);
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

  // Re-apply after association: some core versions reset the power-save mode
  // when the station starts.
  WiFi.setSleep(false);

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
    if (reconnecting) {
      // Link just came back: restore the no-sleep setting and report it
      WiFi.setSleep(false);
      Serial.print("Wi-Fi reconnected, IP: ");
      Serial.println(WiFi.localIP());
    }
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

// Force a clean reconnect (stale association, bad DHCP lease, etc.)
void forceReconnectWiFi() {
  Serial.println("Forcing Wi-Fi reconnect...");

  WiFi.disconnect();
  delay(200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  lastReconnectAttempt = millis();
  reconnecting = true;
}
