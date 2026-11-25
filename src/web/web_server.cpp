
#include "web_server.h"

#include <Arduino.h>
#include <lwip/sockets.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "camera/frame_pipeline.h"
#include "control/zoom_controller.h"
#include "esp_http_server.h"
#include "index_html.h"
#include "stream_stats.h"

// ================= CONTROL SERVER =================

// GET /  ->  control panel page
static esp_err_t indexHandler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");

  return httpd_resp_send(
      req,
      INDEX_HTML,
      HTTPD_RESP_USE_STRLEN);
}

// GET /control?zoom=2.5  ->  change the digital zoom
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
  if (endPtr == value || *endPtr != '\0' || !isfinite(zoom) ||
      zoom < ZOOM_MIN || zoom > ZOOM_MAX) {
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

// ================= STREAM SERVER =================

// Send boundary + header + JPEG as ONE chunk (fewer TCP segments / syscalls).
// Takes ownership of jpgBuf: it is always freed before returning.
// Returns ESP_ERR_NO_MEM if the packet buffer could not be allocated, or the
// httpd_resp_send_chunk() result (!= ESP_OK when the client went away).
static esp_err_t sendFrame(httpd_req_t* req, uint8_t* jpgBuf, size_t jpgLen) {
  if (jpgBuf == nullptr || jpgLen == 0) {
    free(jpgBuf);
    return ESP_ERR_INVALID_ARG;
  }
  char head[128];
  int headLen = snprintf(
      head, sizeof(head),
      "\r\n--frame\r\n"
      "Content-Type: image/jpeg\r\n"
      "Content-Length: %u\r\n\r\n",
      (unsigned int)jpgLen);
  if (headLen < 0 || (size_t)headLen >= sizeof(head)) {
    free(jpgBuf);
    return ESP_FAIL;
  }

  if (jpgLen > SIZE_MAX - (size_t)headLen) {
    free(jpgBuf);
    return ESP_ERR_INVALID_SIZE;
  }

  size_t packetLen = (size_t)headLen + jpgLen;
  uint8_t* packet = (uint8_t*)malloc(packetLen);

  if (packet == nullptr) {
    free(jpgBuf);
    return ESP_ERR_NO_MEM;
  }

  memcpy(packet, head, headLen);
  memcpy(packet + headLen, jpgBuf, jpgLen);

  free(jpgBuf);

  esp_err_t res = httpd_resp_send_chunk(req, (const char*)packet, packetLen);
  free(packet);

  return res;
}

// GET /stream  ->  endless multipart/x-mixed-replace (MJPEG) response
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

  // Timing statistics (printed every STATS_INTERVAL_FRAMES frames)
  StreamStats stats;

  while (true) {
    // 1-4. Capture, zoom / resize and JPEG-encode one frame
    uint8_t* jpgBuf = nullptr;
    size_t jpgLen = 0;
    FramePipeline::Timings timings;

    if (FramePipeline::captureJpeg(&jpgBuf, &jpgLen, &timings) !=
        FramePipeline::Status::Ok) {
      res = ESP_FAIL;
      break;
    }

    uint32_t t2 = micros();
    uint32_t thisJpgLen = (uint32_t)jpgLen;

    // 5. Send the frame
    res = sendFrame(req, jpgBuf, jpgLen);

    if (res == ESP_ERR_NO_MEM) {
      Serial.println("Stream packet allocation failed");
      res = ESP_FAIL;
      break;
    }

    if (res != ESP_OK) {
      Serial.println("Stream client disconnected");
      break;
    }

    // 6. Statistics
    uint32_t t3 = micros();

    stats.record(timings.captureUs, timings.encodeUs, t3 - t2, thisJpgLen);
  }

  return res;
}

// ================= START =================

void startCameraServer() {
  // Server 1: Control Panel
  httpd_config_t controlConfig = HTTPD_DEFAULT_CONFIG();

  controlConfig.server_port = CONTROL_PORT;
  controlConfig.ctrl_port = CONTROL_CTRL_PORT;
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

    if (httpd_register_uri_handler(controlServer, &indexUri) != ESP_OK) {
      Serial.println("Failed to register index URI!");
      httpd_stop(controlServer);
      return;
    }
    httpd_uri_t controlUri = {
        .uri = "/control",
        .method = HTTP_GET,
        .handler = controlHandler,
        .user_ctx = nullptr};

    if (httpd_register_uri_handler(controlServer, &controlUri) != ESP_OK) {
      Serial.println("Failed to register control URI!");
      httpd_stop(controlServer);
      return;
    }

    Serial.printf("Control Panel server started on port %u!\n",
                  (unsigned int)CONTROL_PORT);
  } else {
    Serial.println("Failed to start Control Panel server!");
    return;
  }

  // Server 2: Video stream
  httpd_config_t streamConfig = HTTPD_DEFAULT_CONFIG();

  streamConfig.server_port = STREAM_PORT;
  streamConfig.ctrl_port = STREAM_CTRL_PORT;
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

    if (httpd_register_uri_handler(streamServer, &streamUri) != ESP_OK) {
      Serial.println("Failed to register stream URI!");

      httpd_stop(streamServer);
      httpd_stop(controlServer);

      Serial.println("Both servers stopped.");
      return;
    }

    Serial.printf("Camera stream server started on port %u!\n",
                  (unsigned int)STREAM_PORT);
  } else {
    Serial.println("Failed to start camera stream server!");

    // Stop control server to avoid partial initialization.
    httpd_stop(controlServer);
    Serial.println("Control server stopped.");
  }
}