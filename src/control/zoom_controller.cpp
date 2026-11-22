
#include "zoom_controller.h"

#include <Arduino.h>

// ================= ZOOM CONFIGURATION =================

static float requestedZoom = 1.0f;

// Protect shared zoom value between HTTP server tasks.
static portMUX_TYPE zoomMux = portMUX_INITIALIZER_UNLOCKED;

// ================= ZOOM CONTROL =================

void ZoomController::setZoom(float zoom) {
  portENTER_CRITICAL(&zoomMux);
  requestedZoom = zoom;
  portEXIT_CRITICAL(&zoomMux);
}

float ZoomController::getZoom() {
  float zoom;

  portENTER_CRITICAL(&zoomMux);
  zoom = requestedZoom;
  portEXIT_CRITICAL(&zoomMux);

  return zoom;
}
