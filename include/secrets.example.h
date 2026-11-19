#pragma once

#define WIFI_SSID "ten_wifi"
#define WIFI_PASSWORD "mat_khau_wifi"
// LAN IP of the PC running server/app.py (printed when the server starts).
// Give that PC a fixed IP / DHCP reservation in your router, otherwise the IP
// changes after a reboot and the ESP32-CAM gets "connection refused".
#define IP_ADDRESS "dia_chi_ip_mang"
#define SERVER_URL "http://" IP_ADDRESS ":5000/upload"