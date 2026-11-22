#include "stream_stats.h"

#include <Arduino.h>
#include <WiFi.h>

#include "app_config.h"

StreamStats::StreamStats() : windowStart_(micros()) {}

void StreamStats::record(
    uint32_t captureUs,
    uint32_t encodeUs,
    uint32_t sendUs,
    uint32_t jpegBytes) {
  captureUs_ += captureUs;
  encodeUs_ += encodeUs;
  sendUs_ += sendUs;
  totalBytes_ += jpegBytes;

  if (++frames_ < STATS_INTERVAL_FRAMES) {
    return;
  }

  float fps = (float)STATS_INTERVAL_FRAMES * 1000000.0f /
              (float)(micros() - windowStart_);

  Serial.printf(
      "%u frames | %.1f fps | chup %u ms | ma hoa %u ms | gui %u ms | %u KB/khung | heap %u (min %u) | RSSI %d\n",
      (unsigned int)STATS_INTERVAL_FRAMES,
      fps,
      (unsigned int)(captureUs_ / (STATS_INTERVAL_FRAMES * 1000)),
      (unsigned int)(encodeUs_ / (STATS_INTERVAL_FRAMES * 1000)),
      (unsigned int)(sendUs_ / (STATS_INTERVAL_FRAMES * 1000)),
      (unsigned int)(totalBytes_ / STATS_INTERVAL_FRAMES / 1024),
      (unsigned int)ESP.getFreeHeap(),
      (unsigned int)ESP.getMinFreeHeap(),
      (int)WiFi.RSSI());

  captureUs_ = encodeUs_ = sendUs_ = totalBytes_ = frames_ = 0;
  windowStart_ = micros();
}
