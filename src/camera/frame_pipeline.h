#ifndef FRAME_PIPELINE_H
#define FRAME_PIPELINE_H

#include <Arduino.h>

// capture -> (zoom crop / resize) -> JPEG, one call per frame.
namespace FramePipeline {

enum class Status {
  Ok,
  CaptureFailed,  // esp_camera_fb_get() returned nothing
  EncodeFailed,   // RGB565 -> JPEG conversion failed
};

struct Timings {
  uint32_t captureUs;  // waiting for / taking a frame from the camera
  uint32_t encodeUs;   // zoom crop + resize + JPEG encoding
};

// Grab one frame and produce a JPEG image of it.
// On Status::Ok the caller owns *jpgBuf and must free() it.
// timings is always filled with what was measured before returning.
Status captureJpeg(
    uint8_t** jpgBuf,
    size_t* jpgLen,
    Timings* timings);

}  // namespace FramePipeline

#endif
