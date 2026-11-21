
#include <Arduino.h>
#include <WiFi.h>

#include "esp_camera.h"
#include "esp_http_server.h"
#include "secrets.h"

// ================= WIFI CONFIGURATION =================

// ================= AI THINKER ESP32-CAM PINS =================

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

// ================= CAMERA INITIALIZATION =================

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

  // Initial test uses QVGA (320x240)
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 12;

  if (psramFound()) {
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);

  if (err != ESP_OK) {
    Serial.printf("Camera initialization failed: 0x%x\n", err);
    return false;
  }

  Serial.println("Camera initialized successfully!");
  return true;
}

// ================= WEB SERVER =================

static esp_err_t indexHandler(httpd_req_t* req) {
  const char html[] =
      "<!DOCTYPE html>"
      "<html>"
      "<head>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<title>ESP32-CAM Stream</title>"
      "</head>"
      "<body style='text-align:center;font-family:Arial'>"
      "<h2>ESP32-CAM Live Stream</h2>"
      "<img src='/stream' style='width:100%;max-width:640px'>"
      "</body>"
      "</html>";

  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t streamHandler(httpd_req_t* req) {
  camera_fb_t* fb = nullptr;
  esp_err_t res = ESP_OK;

  const char* boundary = "\r\n--frame\r\n";

  httpd_resp_set_type(
      req,
      "multipart/x-mixed-replace;boundary=frame");

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

  while (true) {
    fb = esp_camera_fb_get();

    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
      break;
    }

    if (fb->format != PIXFORMAT_JPEG) {
      esp_camera_fb_return(fb);
      res = ESP_FAIL;
      break;
    }

    // Send frame boundary
    res = httpd_resp_send_chunk(
        req, boundary, strlen(boundary));

    if (res != ESP_OK) {
      esp_camera_fb_return(fb);
      break;
    }

    // Send frame header
    char header[64];

    int headerLen = snprintf(
        header,
        sizeof(header),
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %u\r\n\r\n",
        (unsigned int)fb->len);

    res = httpd_resp_send_chunk(req, header, headerLen);

    if (res == ESP_OK) {
      // Send JPEG image
      res = httpd_resp_send_chunk(
          req,
          (const char*)fb->buf,
          fb->len);
    }

    esp_camera_fb_return(fb);

    if (res != ESP_OK) {
      break;
    }
  }

  return res;
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();

  config.server_port = 80;
  config.max_uri_handlers = 4;

  httpd_handle_t server = nullptr;

  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t indexUri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = indexHandler,
        .user_ctx = nullptr};

    httpd_uri_t streamUri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = streamHandler,
        .user_ctx = nullptr};

    httpd_register_uri_handler(server, &indexUri);
    httpd_register_uri_handler(server, &streamUri);

    Serial.println("HTTP camera server started!");
  } else {
    Serial.println("Failed to start HTTP server!");
  }
}

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

  // Connect WiFi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");

  unsigned long startTime = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed!");
    return;
  }

  Serial.println("WiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Start web server
  startCameraServer();

  Serial.println("Open the IP address in your browser.");
}

// ================= LOOP =================

void loop() {
  camera_fb_t* fb = esp_camera_fb_get();

  if (!fb) {
    Serial.println("Camera capture failed!");
    delay(2000);
    return;
  }

  Serial.printf(
      "Frame: %ux%u | Length: %u bytes | Format: %d\n",
      fb->width,
      fb->height,
      fb->len,
      fb->format);

  esp_camera_fb_return(fb);

  delay(2000);
}
