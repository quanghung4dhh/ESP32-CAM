
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_camera.h>

#include "secrets.h"
#include "wifi_manager.h"

// ==========================================
// SERVER CONFIGURATION
// ==========================================

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

bool initCamera() {
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
  config.pixel_format = PIXFORMAT_RGB565;

  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  if (psramFound()) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Camera init failed! Error: 0x%x\n", err);
    return false;
  }

  return true;
}

// ==========================================
// IMAGE UPLOAD
// ==========================================

bool uploadImage(uint8_t* imageData, size_t imageLength) {
  if (!isWiFiConnected()) {
    Serial.println("Wi-Fi disconnected. Cannot upload image.");
    return false;
  }

  HTTPClient http;

  Serial.println("Connecting to Flask Server...");
  Serial.println(SERVER_URL);
  Serial.print("WiFi status: ");
  Serial.println(WiFi.status());
  Serial.print("ESP32-CAM IP: ");
  Serial.println(WiFi.localIP());



  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "image/jpeg");

  http.setConnectTimeout(10000);
  http.setTimeout(15000);

  int httpCode = http.POST(imageData, imageLength);

  if (httpCode > 0) {
    Serial.printf("HTTP Response Code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
      String response = http.getString();
      Serial.println("Image uploaded successfully!");
      Serial.println(response);
    } else {
      Serial.println("Server returned an error.");
    }
  } else {
    Serial.printf("HTTP request failed: %s\n",
                  http.errorToString(httpCode).c_str());
  }

  http.end();

  return httpCode == HTTP_CODE_OK;
}

// ==========================================
// SETUP
// ==========================================

bool cameraReady = false;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println();
  Serial.println("===== ESP32-CAM INITIALIZATION =====");

  Serial.printf("PSRAM: %s\n",
                psramFound() ? "Available" : "Not found");

  // Initialize Wi-Fi
  if (!initWiFi()) {
    Serial.println("Wi-Fi initialization failed!");
  }

  Serial.print("ESP32-CAM IP: ");
  Serial.println(WiFi.localIP());

  Serial.print("Server URL: ");
  Serial.println(SERVER_URL);
  // Initialize camera
  cameraReady = initCamera();

  if (!cameraReady) {
    Serial.println("Camera initialization FAILED!");
    return;
  }

  Serial.println("Camera initialized successfully!");
}

// ==========================================
// MAIN LOOP
// ==========================================

void loop() {
  maintainWiFi();

  if (!cameraReady) {
    delay(1000);
    return;
  }

  if (!isWiFiConnected()) {
    delay(1000);
    return;
  }

  Serial.println();
  Serial.println("===== CAPTURING IMAGE =====");

  // Capture RGB565 frame
  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("Failed to capture image!");
    delay(3000);
    return;
  }

  Serial.printf("RGB565: %u bytes, %u x %u\n",
                (unsigned int)fb->len,
                fb->width,
                fb->height);

  // Convert RGB565 to JPEG
  uint8_t* jpg_buf = nullptr;
  size_t jpg_len = 0;

  bool converted = frame2jpg(fb, 12, &jpg_buf, &jpg_len);

  // Return camera frame buffer after conversion
  esp_camera_fb_return(fb);

  if (!converted || jpg_buf == nullptr) {
    Serial.println("JPEG conversion FAILED!");
    delay(5000);
    return;
  }

  Serial.printf("JPEG size: %u bytes\n",
                (unsigned int)jpg_len);

  // Upload JPEG to Flask Server
  uploadImage(jpg_buf, jpg_len);

  // Release JPEG memory after HTTP request
  free(jpg_buf);
  jpg_buf = nullptr;

  Serial.println("Image processing completed.");

  delay(5000);
}
