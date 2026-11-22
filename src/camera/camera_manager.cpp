#include "camera_manager.h"

#include <Arduino.h>

#include "../config/camera_pins.h"
#include "esp_camera.h"

// ================= CAMERA CONFIGURATION =================

// Size the SENSOR captures.
static const framesize_t CAPTURE_FRAME_SIZE = FRAMESIZE_QVGA;

// Field of view configuration for GC2145.
static const int SENSOR_VIEW_DEN = 3;

// Frames discarded after init so auto exposure / white balance can settle.
static const int WARMUP_FRAMES = 3;

// ================= SENSOR CONFIGURATION =================

// Widen the GC2145 field of view at QVGA by programming its window
// and subsample registers directly.
static void setSensorView(int den) {
  sensor_t* s = esp_camera_sensor_get();

  // 0x2145 = GC2145 product ID
  if (s == nullptr || s->id.PID != 0x2145 || s->set_reg == nullptr) {
    Serial.println("Sensor view: not a GC2145, skipped");
    return;
  }

  if (den < 3 || den > 5) {
    return;
  }

  const uint16_t outW = 320;
  const uint16_t outH = 240;
  const uint16_t winW = outW * den;
  const uint16_t winH = outH * den;
  const uint16_t rowS = (1200 - winH) / 2;
  const uint16_t colS = (1600 - winW) / 2;

  struct Reg {
    int reg;
    int value;
  };

  const Reg regs[] = {
      {0xfe, 0x00},
      {0x90, 0x01},
      {0x09, rowS >> 8},
      {0x0a, rowS & 0xff},
      {0x0b, colS >> 8},
      {0x0c, colS & 0xff},
      {0x0d, (winH + 8) >> 8},
      {0x0e, (winH + 8) & 0xff},
      {0x0f, (winW + 16) >> 8},
      {0x10, (winW + 16) & 0xff},
      {0x99, (den << 4) | den},
      {0x9b, 0x00},
      {0x9c, 0x00},
      {0x9d, 0x00},
      {0x9e, 0x00},
      {0x9f, 0x00},
      {0xa0, 0x00},
      {0xa1, 0x00},
      {0xa2, 0x00},
      {0x95, outH >> 8},
      {0x96, outH & 0xff},
      {0x97, outW >> 8},
      {0x98, outW & 0xff},
  };

  for (const Reg& r : regs) {
    s->set_reg(s, r.reg, 0xff, r.value);
  }

  Serial.printf(
      "Sensor view: window %ux%u, subsample 1/%d -> %ux%u\n",
      winW, winH, den, outW, outH);
}

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

  // VGA RGB565 needs ~600 KB per buffer, so it only fits in PSRAM.
  config.frame_size = psramFound()
                          ? CAPTURE_FRAME_SIZE
                          : FRAMESIZE_QVGA;

  config.jpeg_quality = 12;  // Not used in RGB565 mode.

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

  if (SENSOR_VIEW_DEN != 0 &&
      config.frame_size == FRAMESIZE_QVGA) {
    setSensorView(SENSOR_VIEW_DEN);
  }

  // Discard the first frames so exposure and white balance can settle.
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