
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_camera.h>

#include "secrets.h"
#include "wifi_manager.h"

// ==========================================
// TUNING
// ==========================================

static const uint32_t CAPTURE_INTERVAL_MS = 5000;

// HTTP timeouts
static const int32_t HTTP_CONNECT_TIMEOUT_MS = 8000;
static const uint16_t HTTP_READ_TIMEOUT_MS = 15000;

// Retry policy
static const int UPLOAD_MAX_ATTEMPTS = 3;               // per captured frame
static const uint32_t UPLOAD_RETRY_BASE_DELAY_MS = 1500;  // grows per attempt
static const int FAILS_BEFORE_WIFI_RESET = 3;           // failed frames in a row
static const int FAILS_BEFORE_REBOOT = 10;              // failed frames in a row

// ==========================================
// CAMERA PIN CONFIGURATION - AI THINKER
// ==========================================

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27

#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5

#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// ==========================================
// CAMERA INITIALIZATION
// ==========================================

// true  = sensor outputs JPEG natively (OV2640 etc.)
// false = sensor only supports RGB565, frames are converted with frame2jpg()
static bool useNativeJpeg = true;

bool initCamera(bool nativeJpeg) {
  camera_config_t config = {};

  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 20000000;

  // Prefer the sensor's hardware JPEG encoder: a native JPEG frame is only a
  // few KB, while RGB565 at QVGA is ~150 KB and must be converted on the CPU
  // (slow, fragments the heap). If the sensor cannot output JPEG, initCamera()
  // is called again with nativeJpeg=false and uses the RGB565 path.
  config.jpeg_quality = 12;  // 0-63, lower = better quality / bigger file

  if (nativeJpeg) {
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA;
  } else {
    // Fallback for sensors without JPEG output ("JPEG format is not
    // supported on this sensor", error 0x106)
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = psramFound() ? FRAMESIZE_QVGA : FRAMESIZE_QQVGA;
  }

  if (psramFound()) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count = nativeJpeg ? 2 : 1;
    config.grab_mode = nativeJpeg ? CAMERA_GRAB_LATEST : CAMERA_GRAB_WHEN_EMPTY;
  } else {
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Camera init (%s) failed! Error: 0x%x\n",
                  nativeJpeg ? "JPEG" : "RGB565", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    Serial.printf("Camera sensor PID: 0x%x, mode: %s\n",
                  s->id.PID, nativeJpeg ? "native JPEG" : "RGB565->JPEG");
  }

  useNativeJpeg = nativeJpeg;
  return true;
}

// ==========================================
// IMAGE UPLOAD
// ==========================================

enum class UploadResult {
  Success,
  NetworkError,    // could not reach the server / timeout -> worth retrying
  ServerRejected,  // server answered with a non-200 status -> do not retry
};

// One HTTP POST attempt
static UploadResult uploadOnce(uint8_t* imageData, size_t imageLength) {
  WiFiClient client;  // must outlive http
  HTTPClient http;

  if (!http.begin(client, SERVER_URL)) {
    Serial.println("http.begin() failed - check SERVER_URL in secrets.h");
    return UploadResult::NetworkError;
  }

  http.setReuse(false);  // sends "Connection: close", no stale keep-alive socket
  http.setConnectTimeout(HTTP_CONNECT_TIMEOUT_MS);
  http.setTimeout(HTTP_READ_TIMEOUT_MS);
  http.addHeader("Content-Type", "image/jpeg");

  int httpCode = http.POST(imageData, imageLength);

  UploadResult result;

  if (httpCode == HTTP_CODE_OK) {
    Serial.printf("Image uploaded successfully! Server says: %s\n",
                  http.getString().c_str());
    result = UploadResult::Success;
  } else if (httpCode > 0) {
    Serial.printf("Server returned HTTP %d\n", httpCode);
    result = UploadResult::ServerRejected;
  } else {
    // -1 = connection failed (refused, unreachable OR connect timeout)
    // -11 = read timeout (server accepted the data but never answered)
    Serial.printf("HTTP request failed: %d (%s)\n",
                  httpCode,
                  http.errorToString(httpCode).c_str());
    result = UploadResult::NetworkError;
  }

  http.end();
  return result;
}

// Upload with retries and diagnostics
static UploadResult uploadImage(uint8_t* imageData, size_t imageLength) {
  for (int attempt = 1; attempt <= UPLOAD_MAX_ATTEMPTS; attempt++) {
    if (!isWiFiConnected()) {
      Serial.println("Wi-Fi disconnected. Waiting before retry...");
      maintainWiFi();
      delay(UPLOAD_RETRY_BASE_DELAY_MS * attempt);
      continue;
    }

    Serial.printf("Upload attempt %d/%d -> %s\n",
                  attempt, UPLOAD_MAX_ATTEMPTS, SERVER_URL);
    Serial.printf("  IP: %s | RSSI: %d dBm | free heap: %u bytes\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI(),
                  (unsigned int)ESP.getFreeHeap());

    UploadResult result = uploadOnce(imageData, imageLength);

    if (result != UploadResult::NetworkError) {
      return result;
    }

    if (attempt < UPLOAD_MAX_ATTEMPTS) {
      delay(UPLOAD_RETRY_BASE_DELAY_MS * attempt);
    }
  }

  return UploadResult::NetworkError;
}

// A JPEG always starts with the SOI marker FF D8
static bool isValidJpeg(const camera_fb_t* fb) {
  return fb->format == PIXFORMAT_JPEG && fb->len > 100 &&
         fb->buf[0] == 0xFF && fb->buf[1] == 0xD8;
}

// ==========================================
// SETUP
// ==========================================

bool cameraReady = false;
int consecutiveFailures = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("===== ESP32-CAM INITIALIZATION =====");

  Serial.printf("PSRAM: %s\n",
                psramFound() ? "Available" : "Not found");

  // Initialize Wi-Fi
  if (!initWiFi()) {
    Serial.println("Wi-Fi initialization failed! Will keep retrying in loop().");
  }

  Serial.print("ESP32-CAM IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("Server URL: ");
  Serial.println(SERVER_URL);

  // Initialize camera (a cold start on a weak supply sometimes needs a retry)
  // Try native JPEG first, then fall back to RGB565 if the sensor can't do it
  for (int attempt = 1; attempt <= 3 && !cameraReady; attempt++) {
    cameraReady = initCamera(true);

    if (!cameraReady) {
      esp_camera_deinit();
      Serial.println("Native JPEG not available, trying RGB565...");
      cameraReady = initCamera(false);
    }

    if (!cameraReady) {
      Serial.printf("Camera init attempt %d/3 failed\n", attempt);
      esp_camera_deinit();
      delay(1000);
    }
  }

  if (!cameraReady) {
    Serial.println("Camera initialization FAILED! Restarting in 5 s...");
    delay(5000);
    ESP.restart();
  }

  Serial.println("Camera initialized successfully!");
}

// ==========================================
// MAIN LOOP
// ==========================================

void loop() {
  maintainWiFi();

  if (!isWiFiConnected()) {
    delay(1000);
    return;
  }

  Serial.println();
  Serial.println("===== CAPTURING IMAGE =====");

  // Capture a JPEG frame straight from the sensor
  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("Failed to capture image!");
    delay(3000);
    return;
  }

  uint8_t* jpgBuf = nullptr;  // only used in RGB565 fallback mode
  size_t jpgLen = 0;
  uint8_t* sendBuf = nullptr;
  size_t sendLen = 0;

  if (useNativeJpeg) {
    if (!isValidJpeg(fb)) {
      Serial.printf("Invalid frame (format=%d, len=%u), skipping.\n",
                    (int)fb->format, (unsigned int)fb->len);
      esp_camera_fb_return(fb);
      delay(1000);
      return;
    }

    Serial.printf("JPEG: %u bytes, %u x %u\n",
                  (unsigned int)fb->len, fb->width, fb->height);

    // Upload the frame buffer directly - no extra copy or conversion
    sendBuf = fb->buf;
    sendLen = fb->len;
  } else {
    Serial.printf("RGB565: %u bytes, %u x %u\n",
                  (unsigned int)fb->len, fb->width, fb->height);

    bool converted = frame2jpg(fb, 12, &jpgBuf, &jpgLen);
    // Release the camera buffer right after conversion, before the upload
    esp_camera_fb_return(fb);
    fb = nullptr;

    if (!converted || jpgBuf == nullptr) {
      Serial.println("JPEG conversion FAILED!");
      delay(5000);
      return;
    }

    Serial.printf("JPEG size: %u bytes\n", (unsigned int)jpgLen);
    sendBuf = jpgBuf;
    sendLen = jpgLen;
  }

  UploadResult result = uploadImage(sendBuf, sendLen);

  if (fb) {
    esp_camera_fb_return(fb);
  }
  if (jpgBuf) {
    free(jpgBuf);
    jpgBuf = nullptr;
  }

  if (result == UploadResult::Success) {
    consecutiveFailures = 0;
  } else if (result == UploadResult::NetworkError) {
    consecutiveFailures++;
    Serial.printf("Upload failed (%d in a row)\n", consecutiveFailures);

    if (consecutiveFailures >= FAILS_BEFORE_REBOOT) {
      Serial.println("Too many failures - rebooting.");
      delay(500);
      ESP.restart();
    } else if (consecutiveFailures % FAILS_BEFORE_WIFI_RESET == 0) {
      forceReconnectWiFi();
    }
  }

  Serial.println("Image processing completed.");

  delay(CAPTURE_INTERVAL_MS);
}
