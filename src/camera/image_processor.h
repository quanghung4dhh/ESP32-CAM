#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <stdint.h>

#include "esp_camera.h"

namespace ImageProcessor {

constexpr uint16_t OUTPUT_WIDTH = 320;
constexpr uint16_t OUTPUT_HEIGHT = 240;

// Crop the center region and resize an RGB565 frame.
uint8_t* cropAndScaleRGB565(
    const camera_fb_t* fb,
    float zoom,
    camera_fb_t* outFb);

}  // namespace ImageProcessor

#endif