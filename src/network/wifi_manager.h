
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>

// Initialize Wi-Fi and connect to the access point
bool initWiFi();

// Check Wi-Fi connection status
bool isWiFiConnected();

// Get ESP32-CAM local IP address
IPAddress getLocalIP();

// Maintain Wi-Fi connection and reconnect if necessary
void maintainWiFi();

// Drop the current association and reconnect immediately
// (used when uploads keep failing although Wi-Fi reports "connected")
void forceReconnectWiFi();

#endif
