// Tất cả thông số có thể chỉnh của firmware nằm ở đây.
// Muốn thay đổi hành vi: sửa giá trị trong file này, không sửa trong module.

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <Arduino.h>
#include <WiFi.h>

#include "esp_camera.h"

// ================= IMAGE =================

// frame2jpg() quality: 0-100, HIGHER = better image (10 gave blocky output)
constexpr int JPEG_QUALITY = 50;

// Size the SENSOR captures. QVGA is the fastest (VGA RGB565 only gave ~2 fps).
constexpr framesize_t CAPTURE_FRAME_SIZE = FRAMESIZE_QVGA;

// Field of view of the GC2145 when it captures QVGA (320x240).
// The driver reads only a small centre window of the 1600x1200 pixel array
// (looks "zoomed in"). Here the sensor reads a (320*N) x (240*N) window and
// subsamples it by 1/N, so the output stays 320x240:
//   N = 5 -> whole 1600x1200 array (widest view)
//   N = 4 -> 1280x960 window
//   N = 3 -> 960x720 window
//   N = 0 -> keep the driver default
// A wider view may lower the sensor frame rate: try 5, then 4, then 3.
constexpr int SENSOR_VIEW_DEN = 3;

// Size that is JPEG-encoded and sent to the browser. The sensor window
// (camera_manager.cpp) and the resampler (image_processor.cpp) both use
// these values, so change them here only.
constexpr uint16_t OUTPUT_WIDTH = 320;
constexpr uint16_t OUTPUT_HEIGHT = 240;

// Frames discarded after init so auto exposure / white balance can settle
constexpr int WARMUP_FRAMES = 3;

// Digital zoom limits (the slider in index_html.h must use the same range)
constexpr float ZOOM_MIN = 1.0f;
constexpr float ZOOM_MAX = 4.0f;

// ================= Wi-Fi =================

// Wi-Fi transmit power. 15 dBm gave the best results in testing
// (8.5 dBm was too weak, default 19.5 dBm was worse).
constexpr wifi_power_t WIFI_TX_POWER = WIFI_POWER_15dBm;

// How long initWiFi() waits for the connection
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;

// ================= HTTP SERVERS =================

// Control panel ("/" and "/control")
constexpr uint16_t CONTROL_PORT = 80;
constexpr uint16_t CONTROL_CTRL_PORT = 32768;

// Video stream ("/stream")
constexpr uint16_t STREAM_PORT = 81;
constexpr uint16_t STREAM_CTRL_PORT = 32769;

// ================= DEBUG =================

// One statistics line is printed on Serial every N streamed frames
constexpr uint32_t STATS_INTERVAL_FRAMES = 30;

#endif
