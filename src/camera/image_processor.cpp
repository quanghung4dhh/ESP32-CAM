#include "image_processor.h"

#include <Arduino.h>
#include <math.h>

#include "app_config.h"
// ================= IMAGE PROCESSING =================

uint8_t* ImageProcessor::cropAndScaleRGB565(
    const camera_fb_t* fb,
    float zoom,
    camera_fb_t* outFb) {
  if (fb == nullptr ||
      outFb == nullptr ||
      fb->buf == nullptr ||
      fb->format != PIXFORMAT_RGB565 ||
      fb->width == 0 ||
      fb->height == 0 ||
      !isfinite(zoom)) {
    return nullptr;
  }
  
  const size_t requiredLen =
      (size_t)fb->width * fb->height * sizeof(uint16_t);

  if (fb->len < requiredLen) {
    return nullptr;
  }

  if (zoom < 1.0f) {
    zoom = 1.0f;
  }

  uint32_t cropW = (uint32_t)(fb->width / zoom);
  uint32_t cropH = (uint32_t)(fb->height / zoom);

  if (cropW < 8 || cropH < 8) {
    return nullptr;
  }

  uint32_t startX = (fb->width - cropW) / 2;
  uint32_t startY = (fb->height - cropH) / 2;

  // RGB565 uses 2 bytes per pixel.
  size_t outLen =
      (size_t)OUTPUT_WIDTH * OUTPUT_HEIGHT * 2;

  uint16_t* dst = (uint16_t*)ps_malloc(outLen);

  if (dst == nullptr) {
    return nullptr;
  }

  const uint16_t* src = (const uint16_t*)fb->buf;

  // Source column for every output column.
  uint16_t colMap[OUTPUT_WIDTH];

  for (uint16_t x = 0; x < OUTPUT_WIDTH; x++) {
    colMap[x] =
        (uint16_t)(startX +
                   (uint32_t)x * cropW / OUTPUT_WIDTH);
  }

  for (uint16_t y = 0; y < OUTPUT_HEIGHT; y++) {
    uint32_t sy =
        startY + (uint32_t)y * cropH / OUTPUT_HEIGHT;

    const uint16_t* srcRow =
        src + (size_t)sy * fb->width;

    uint16_t* dstRow =
        dst + (size_t)y * OUTPUT_WIDTH;

    for (uint16_t x = 0; x < OUTPUT_WIDTH; x++) {
      dstRow[x] = srcRow[colMap[x]];
    }
  }

  // Create the descriptor for the processed frame.
  *outFb = *fb;

  outFb->buf = (uint8_t*)dst;
  outFb->len = outLen;
  outFb->width = OUTPUT_WIDTH;
  outFb->height = OUTPUT_HEIGHT;

  return (uint8_t*)dst;
}