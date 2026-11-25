#include "frame_pipeline.h"

#include <Arduino.h>
#include <stdlib.h>

#include "app_config.h"
#include "control/zoom_controller.h"
#include "esp_camera.h"
#include "image_processor.h"

// frame2jpg() lives in img_converters.h (included by esp_camera.h in most
// versions of the esp32-camera library)
#if __has_include("img_converters.h")
#include "img_converters.h"
#endif

FramePipeline::Status FramePipeline::captureJpeg(
    uint8_t** jpgBuf,
    size_t* jpgLen,
    Timings* timings) {
  // Validate output pointers.
  if (jpgBuf == nullptr ||
      jpgLen == nullptr ||
      timings == nullptr) {
    return Status::InvalidArgument;
  }

  // Initialize outputs.
  *jpgBuf = nullptr;
  *jpgLen = 0;

  timings->captureUs = 0;
  timings->encodeUs = 0;

  uint32_t t0 = micros();

  // 1. Capture RGB565 frame
  camera_fb_t* fb = esp_camera_fb_get();

  if (fb == nullptr) {
    Serial.println("Camera capture failed");
    timings->captureUs = micros() - t0;
    return Status::CaptureFailed;
  }

  uint32_t t1 = micros();
  timings->captureUs = t1 - t0;

  // Validate captured frame.
  if (fb->buf == nullptr ||
      fb->width == 0 ||
      fb->height == 0 ||
      fb->format != PIXFORMAT_RGB565) {
    Serial.println("Invalid camera frame");
    esp_camera_fb_return(fb);
    return Status::CaptureFailed;
  }

  // Validate framebuffer size.
  // RGB565 requires 2 bytes per pixel.
  if ((size_t)fb->width >
      SIZE_MAX / (size_t)fb->height / sizeof(uint16_t)) {
    Serial.println("Camera frame size overflow");
    esp_camera_fb_return(fb);
    return Status::CaptureFailed;
  }

  const size_t requiredLen =
      (size_t)fb->width * fb->height * sizeof(uint16_t);

  if (fb->len < requiredLen) {
    Serial.println("Camera frame buffer too small");
    esp_camera_fb_return(fb);
    return Status::CaptureFailed;
  }

  // 2. Read current zoom value safely
  float currentZoom = ZoomController::getZoom();

  // 3. Select frame for JPEG encoding
  camera_fb_t croppedFb;
  camera_fb_t* frameToEncode = fb;
  uint8_t* croppedBuf = nullptr;

  // Resample whenever the capture size differs from the output size or
  // the user zoomed in
  if (currentZoom > 1.01f ||
      fb->width != OUTPUT_WIDTH ||
      fb->height != OUTPUT_HEIGHT) {
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
      jpgBuf,
      jpgLen);

  // Release temporary crop buffer
  if (croppedBuf != nullptr) {
    free(croppedBuf);
    croppedBuf = nullptr;
  }

  // Return original camera frame buffer
  esp_camera_fb_return(fb);
  fb = nullptr;

  timings->encodeUs = micros() - t1;

  if (!converted || *jpgBuf == nullptr || *jpgLen == 0) {
    Serial.println("RGB565 to JPEG conversion failed");

    if (*jpgBuf != nullptr) {
      free(*jpgBuf);
    }

    *jpgBuf = nullptr;
    *jpgLen = 0;
    return Status::EncodeFailed;
  }

  return Status::Ok;
}
