
#include "web_server.h"

#include <Arduino.h>
#include <WiFi.h>
#include <lwip/sockets.h>

#include "camera/image_processor.h"
#include "control/zoom_controller.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "index_html.h"

// frame2jpg() quality: 0-100, HIGHER = better image (10 gave blocky output)
static const int JPEG_QUALITY = 50;

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

  // Update requested zoom
  ZoomController::setZoom(zoom);

  Serial.printf(
      "Requested Zoom: %.1fx\n", zoom);

  // Send response
  char response[64];
  snprintf(
      response,
      sizeof(response),
      "Zoom updated: %.1fx",
      ZoomController::getZoom());

  httpd_resp_set_type(req, "text/plain");

  return httpd_resp_send(
      req, response, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t streamHandler(httpd_req_t* req) {
  esp_err_t res = ESP_OK;

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
  uint32_t windowStart = micros();

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

    float currentZoom = ZoomController::getZoom();

    // 3. Select frame for JPEG encoding
    camera_fb_t croppedFb;
    camera_fb_t* frameToEncode = fb;
    uint8_t* croppedBuf = nullptr;

    // Resample whenever the capture size differs from the output size or
    // the user zoomed in
    if (currentZoom > 1.01f ||
        fb->width != ImageProcessor::OUTPUT_WIDTH ||
        fb->height != ImageProcessor::OUTPUT_HEIGHT) {
      croppedBuf = ImageProcessor::cropAndScaleRGB565(
          fb,
          currentZoom,
          &croppedFb);

      if (croppedBuf != nullptr) {
        frameToEncode = &croppedFb;
      } else {
        Serial.println("Resize failed. Using original frame.");
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
      float fps = 30000000.0f / (float)(micros() - windowStart);

      Serial.printf(
          "30 frames | %.1f fps | chup %u ms | ma hoa %u ms | gui %u ms | %u KB/khung | heap %u (min %u) | RSSI %d\n",
          fps,
          (unsigned int)(tCap / 30000),
          (unsigned int)(tEnc / 30000),
          (unsigned int)(tSend / 30000),
          (unsigned int)(totalBytes / 30 / 1024),
          (unsigned int)ESP.getFreeHeap(),
          (unsigned int)ESP.getMinFreeHeap(),
          (int)WiFi.RSSI());

      tCap = tEnc = tSend = totalBytes = frames = 0;
      windowStart = micros();
    }
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

  Serial.println("[DEBUG] Starting control server...");
  esp_err_t controlErr = httpd_start(&controlServer, &controlConfig);

  Serial.printf("[DEBUG] Control server result: 0x%x\n", controlErr);

  if (controlErr == ESP_OK) {
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

  Serial.println("[DEBUG] Starting stream server...");
  esp_err_t streamErr = httpd_start(&streamServer, &streamConfig);

  Serial.printf("[DEBUG] Stream server result: 0x%x\n", streamErr);

  if (streamErr == ESP_OK) {
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