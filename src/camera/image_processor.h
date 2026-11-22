#ifndef IMAGE_PROCESSOR_H
#define IMAGE_PROCESSOR_H

#include <stdint.h>

#include "esp_camera.h"

namespace ImageProcessor {

// Crop the center region of an RGB565 frame and resize it (nearest neighbour)
// to OUTPUT_WIDTH x OUTPUT_HEIGHT (see app_config.h).
// On success returns a new buffer that the caller must free(); returns
// nullptr if the frame is not RGB565, the crop is too small or memory is out.
uint8_t* cropAndScaleRGB565(
    const camera_fb_t* fb,
    float zoom,
    camera_fb_t* outFb);

}  // namespace ImageProcessor

#endif