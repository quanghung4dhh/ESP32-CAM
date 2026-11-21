
#include <Arduino.h>
#include <WiFi.h>
#include <lwip/sockets.h>

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

// ================= TUNING =================

// frame2jpg() quality: 0-100, HIGHER = better image (10 gave blocky output)
static const int JPEG_QUALITY = 50;

// Capture size. A bigger size is sharper (and keeps detail when zooming) but
// the CPU needs longer to encode each frame, which increases latency.
// Try FRAMESIZE_VGA (640x480) on boards with PSRAM.
static const framesize_t STREAM_FRAME_SIZE = FRAMESIZE_QVGA;

// Frames discarded after init so auto exposure / white balance can settle
static const int WARMUP_FRAMES = 3;

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

  config.frame_size = STREAM_FRAME_SIZE;
  config.jpeg_quality = 12;  // not used in RGB565 mode (see JPEG_QUALITY)

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

  // Discard the first frames: exposure and white balance are not stable yet
  for (int i = 0; i < WARMUP_FRAMES; i++) {
    camera_fb_t* warm = esp_camera_fb_get();
    if (warm) {
      esp_camera_fb_return(warm);
    }
    delay(50);
  }

  Serial.println("Camera initialized successfully!");
  return true;
}

// ================= CAMERA CONTROL =================

static float requestedZoom = 1.0f;

// Protect shared zoom value between HTTP server tasks
static portMUX_TYPE zoomMux = portMUX_INITIALIZER_UNLOCKED;

// Crop the center region of an RGB565 frame
static uint8_t* cropRGB565(
    const camera_fb_t* fb,
    float zoom,
    camera_fb_t* croppedFb) {
  if (fb == nullptr || fb->format != PIXFORMAT_RGB565) {
    return nullptr;
  }

  // Calculate cropped dimensions
  uint16_t cropW = (uint16_t)(fb->width / zoom);
  uint16_t cropH = (uint16_t)(fb->height / zoom);

  // JPEG encoding works in 8x8 blocks: keep the crop size a multiple of 8
  cropW &= ~7;
  cropH &= ~7;

  if (cropW == 0 || cropH == 0) {
    return nullptr;
  }

  // Calculate the center crop position
  uint16_t startX = (fb->width - cropW) / 2;
  uint16_t startY = (fb->height - cropH) / 2;

  // RGB565 uses 2 bytes per pixel
  size_t cropLen = (size_t)cropW * cropH * 2;

  // Allocate crop buffer in PSRAM
  uint8_t* cropBuf = (uint8_t*)ps_malloc(cropLen);

  if (cropBuf == nullptr) {
    return nullptr;
  }

  // Copy the center region row by row
  for (uint16_t y = 0; y < cropH; y++) {
    size_t srcOffset =
        ((size_t)(startY + y) * fb->width + startX) * 2;

    size_t dstOffset = (size_t)y * cropW * 2;

    memcpy(
        cropBuf + dstOffset,
        fb->buf + srcOffset,
        (size_t)cropW * 2);
  }

  // Create a frame descriptor for the cropped image
  *croppedFb = *fb;

  croppedFb->buf = cropBuf;
  croppedFb->len = cropLen;
  croppedFb->width = cropW;
  croppedFb->height = cropH;

  return cropBuf;
}

// ================= WEB SERVER =================

const char INDEX_HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">

  <title>ESP32-CAM Control Panel</title>

  <style>
    * {
      box-sizing: border-box;
    }

    body {
      margin: 0;
      padding: 24px;
      font-family: Arial, sans-serif;
      background: #111827;
      color: #f9fafb;
    }

    .container {
      max-width: 1100px;
      margin: auto;
    }

    header {
      margin-bottom: 24px;
    }

    header h1 {
      margin-bottom: 8px;
    }

    .status {
      color: #4ade80;
      font-size: 14px;
    }

    .layout {
      display: grid;
      grid-template-columns: 2fr 1fr;
      gap: 20px;
    }

    .card {
      background: #1f2937;
      border-radius: 12px;
      padding: 20px;
    }

    .preview {
      width: 100%;
      min-height: 240px;
      background: #000;
      border-radius: 8px;
      display: flex;
      align-items: center;
      justify-content: center;
      overflow: hidden;
    }

    .preview img {
      display: block;
      width: 100%;
      height: auto;
      max-height: 65vh;
      object-fit: contain;
    }

    .setting {
      margin: 20px 0;
      padding-bottom: 16px;
      border-bottom: 1px solid #374151;
    }

    .setting label {
      display: block;
      margin-bottom: 8px;
      font-weight: bold;
    }

    input, select {
      width: 100%;
    }

    input:disabled,
    select:disabled {
      opacity: 0.5;
    }

    .note {
      color: #9ca3af;
      font-size: 13px;
      line-height: 1.5;
    }

    @media (max-width: 750px) {
      body {
        padding: 14px;
      }

      .layout {
        grid-template-columns: 1fr;
      }
    }
  </style>
  
</head>

<body>
  <div class="container">

    <header>
      <h1>ESP32-CAM Control Panel</h1>
      <div class="status">
        ● Camera streaming
      </div>
    </header>

    <div class="layout">

      <section class="card">

<div class="setting">
  <label for="zoomRange">
    Digital Zoom:
    <span id="zoomValue">1.0×</span>
  </label>

  <input
  type="range"
  id="zoomRange"
  min="1.0"
  max="4.0"
  step="0.1"
  value="1.0"
  oninput="
    document.getElementById('zoomValue').textContent =
    Number(this.value).toFixed(1) + '×';
  "
  onchange="sendZoom(this.value)"
>

  <p class="note">
    Adjust digital zoom from 1.0× to 4.0×.
  </p>
</div>

        <h2>Live Preview</h2>

        <div class="preview">
          <img id="cameraStream"
  alt="Camera Stream"
  style="width: 100%; height: auto;">
        </div>

        <p class="note">
          Live MJPEG stream from ESP32-CAM.
        </p>
      </section>

      <section class="card">
        <h2>Camera Settings</h2>

        <div class="setting">
          <label>JPEG Quality</label>
          <input type="range" min="10" max="90"
                 value="80" disabled>
          <p class="note">
            Available in the next development step.
          </p>
        </div>

        <div class="setting">
          <label>Brightness</label>
          <input type="range" min="-2" max="2"
                 value="0" disabled>
        </div>

        <div class="setting">
          <label>Contrast</label>
          <input type="range" min="-2" max="2"
                 value="0" disabled>
        </div>

        <div class="setting">
          <label>Resolution</label>
          <select disabled>
            <option>QVGA (320x240)</option>
          </select>
        </div>

        <p class="note">
          Camera controls will be enabled
          after the control API is implemented.
        </p>
      </section>

    </div>
  </div>
  <script>
  // Initialize camera stream
  const streamUrl =
    'http://' + window.location.hostname + ':81/stream';

  document.getElementById('cameraStream').src = streamUrl;


  // Send zoom command to ESP32
  async function sendZoom(value) {
    try {
      const response = await fetch(
        `/control?zoom=${encodeURIComponent(value)}`
      );

      const message = await response.text();

      if (!response.ok) {
        throw new Error(message);
      }

      console.log(message);

    } catch (error) {
      console.error('Zoom control failed:', error);
    }
  }
</script>
</body>
</html>
)rawliteral";

static esp_err_t indexHandler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");

  return httpd_resp_send(
      req,
      INDEX_HTML,
      HTTPD_RESP_USE_STRLEN);
}

static esp_err_t controlHandler(httpd_req_t* req) {
  char query[64];
  char value[16];

  // Get URL query string
  if (httpd_req_get_url_query_str(
          req, query, sizeof(query)) != ESP_OK) {
    httpd_resp_send_err(
        req, HTTPD_400_BAD_REQUEST, "Missing query");
    return ESP_FAIL;
  }

  // Extract zoom parameter
  if (httpd_query_key_value(
          query, "zoom", value, sizeof(value)) != ESP_OK) {
    httpd_resp_send_err(
        req, HTTPD_400_BAD_REQUEST, "Missing zoom parameter");
    return ESP_FAIL;
  }

  // Convert string to float
  char* endPtr = nullptr;
  float zoom = strtof(value, &endPtr);

  // Validate input
  if (endPtr == value || *endPtr != '\0' ||
      zoom < 1.0f || zoom > 4.0f) {
    httpd_resp_send_err(
        req, HTTPD_400_BAD_REQUEST, "Invalid zoom value");
    return ESP_FAIL;
  }

  // Store requested zoom safely
  portENTER_CRITICAL(&zoomMux);
  requestedZoom = zoom;
  portEXIT_CRITICAL(&zoomMux);

  Serial.printf(
      "Requested Zoom: %.1fx\n", zoom);

  // Send response
  char response[64];
  snprintf(
      response,
      sizeof(response),
      "Zoom updated: %.1fx",
      requestedZoom);

  httpd_resp_set_type(req, "text/plain");

  return httpd_resp_send(
      req, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t streamHandler(httpd_req_t* req) {
  esp_err_t res = ESP_OK;

  const char* boundary = "\r\n--frame\r\n";

  httpd_resp_set_type(
      req,
      "multipart/x-mixed-replace;boundary=frame");

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

  int sock = httpd_req_to_sockfd(req);
  int one = 1;
  setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

  // Timing statistics (printed every 30 frames)
  uint32_t tCap = 0, tEnc = 0, tSend = 0, totalBytes = 0, frames = 0;

  while (true) {
    uint32_t t0 = micros();

    // 1. Capture RGB565 frame
    camera_fb_t* fb = esp_camera_fb_get();

    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
      break;
    }

    uint32_t t1 = micros();

    uint8_t* jpgBuf = nullptr;
    size_t jpgLen = 0;

    // 2. Read current zoom value safely
    float currentZoom;

    portENTER_CRITICAL(&zoomMux);
    currentZoom = requestedZoom;
    portEXIT_CRITICAL(&zoomMux);

    // 3. Select frame for JPEG encoding
    camera_fb_t croppedFb;
    camera_fb_t* frameToEncode = fb;
    uint8_t* croppedBuf = nullptr;

    if (currentZoom > 1.01f) {
      croppedBuf = cropRGB565(fb, currentZoom, &croppedFb);

      if (croppedBuf != nullptr) {
        frameToEncode = &croppedFb;
      } else {
        Serial.println("Crop failed. Using original frame.");
      }
    }

    // 4. Convert selected frame to JPEG
    bool converted = frame2jpg(
        frameToEncode,
        JPEG_QUALITY,
        &jpgBuf,
        &jpgLen);

    // Release temporary crop buffer
    if (croppedBuf != nullptr) {
      free(croppedBuf);
      croppedBuf = nullptr;
    }

    // Return original camera frame buffer
    esp_camera_fb_return(fb);
    fb = nullptr;

    if (!converted || jpgBuf == nullptr || jpgLen == 0) {
      Serial.println("RGB565 to JPEG conversion failed");

      if (jpgBuf != nullptr) {
        free(jpgBuf);
      }

      res = ESP_FAIL;
      break;
    }

    uint32_t t2 = micros();

    // 5. Send boundary + header + JPEG as ONE chunk
    uint32_t thisJpgLen = (uint32_t)jpgLen;

    char head[128];
    int headLen = snprintf(
        head, sizeof(head),
        "\r\n--frame\r\n"
        "Content-Type: image/jpeg\r\n"
        "Content-Length: %u\r\n\r\n",
        (unsigned int)jpgLen);

    size_t packetLen = (size_t)headLen + jpgLen;
    uint8_t* packet = (uint8_t*)malloc(packetLen);

    if (packet == nullptr) {
      free(jpgBuf);
      res = ESP_FAIL;
      break;
    }

    memcpy(packet, head, headLen);
    memcpy(packet + headLen, jpgBuf, jpgLen);

    free(jpgBuf);
    jpgBuf = nullptr;

    res = httpd_resp_send_chunk(req, (const char*)packet, packetLen);
    free(packet);

    if (res != ESP_OK) {
      Serial.println("Stream client disconnected");
      break;
    }

    // 9. Statistics
    uint32_t t3 = micros();

    tCap += t1 - t0;
    tEnc += t2 - t1;
    tSend += t3 - t2;
    totalBytes += thisJpgLen;

    if (++frames >= 30) {
      Serial.printf(
          "30 frames | chup %u ms | ma hoa %u ms | gui %u ms | %u KB/khung | heap %u (min %u) | RSSI %d\n",
          (unsigned int)(tCap / 30000),
          (unsigned int)(tEnc / 30000),
          (unsigned int)(tSend / 30000),
          (unsigned int)(totalBytes / 30 / 1024),
          (unsigned int)ESP.getFreeHeap(),
          (unsigned int)ESP.getMinFreeHeap(),
          (int)WiFi.RSSI());

      tCap = tEnc = tSend = totalBytes = frames = 0;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  return res;
}

void startCameraServer() {
  // Server 1: Control Panel on port 80
  httpd_config_t controlConfig = HTTPD_DEFAULT_CONFIG();

  controlConfig.server_port = 80;
  controlConfig.ctrl_port = 32768;
  controlConfig.max_uri_handlers = 4;
  controlConfig.lru_purge_enable = true;  // free stale sockets automatically

  httpd_handle_t controlServer = nullptr;

  if (httpd_start(&controlServer, &controlConfig) == ESP_OK) {
    httpd_uri_t indexUri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = indexHandler,
        .user_ctx = nullptr};

    httpd_register_uri_handler(controlServer, &indexUri);
    httpd_uri_t controlUri = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = controlHandler,
        .user_ctx = nullptr};

    httpd_register_uri_handler(controlServer, &controlUri);

    Serial.println("Control Panel server started on port 80!");
  } else {
    Serial.println("Failed to start Control Panel server!");
    return;
  }

  // Server 2: Video stream on port 81
  httpd_config_t streamConfig = HTTPD_DEFAULT_CONFIG();

  streamConfig.server_port = 81;
  streamConfig.ctrl_port = 32769;
  streamConfig.max_uri_handlers = 4;
  streamConfig.lru_purge_enable = true;  // free stale sockets automatically

  httpd_handle_t streamServer = nullptr;

  if (httpd_start(&streamServer, &streamConfig) == ESP_OK) {
    httpd_uri_t streamUri = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = streamHandler,
        .user_ctx = nullptr};

    httpd_register_uri_handler(streamServer, &streamUri);

    Serial.println("Camera stream server started on port 81!");
  } else {
    Serial.println("Failed to start camera stream server!");
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
  WiFi.setTxPower(WIFI_POWER_15dBm);  // sau đó thử WIFI_POWER_8_5dBm
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
  WiFi.setSleep(false);
  Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Start web server
  startCameraServer();

  Serial.println("Open the IP address in your browser.");
}

// ================= LOOP =================

void loop() {
  // Camera capture is handled by streamHandler().
  delay(1000);
}
