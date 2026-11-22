#ifndef STREAM_STATS_H
#define STREAM_STATS_H

#include <Arduino.h>

// Prints "fps | capture | encode | send | size | heap | RSSI" on Serial every
// STATS_INTERVAL_FRAMES streamed frames. Use it to see what limits the fps.
class StreamStats {
 public:
  StreamStats();

  // Record the timings (microseconds) and JPEG size of one sent frame
  void record(
      uint32_t captureUs,
      uint32_t encodeUs,
      uint32_t sendUs,
      uint32_t jpegBytes);

 private:
  uint32_t captureUs_ = 0;
  uint32_t encodeUs_ = 0;
  uint32_t sendUs_ = 0;
  uint32_t totalBytes_ = 0;
  uint32_t frames_ = 0;
  uint32_t windowStart_ = 0;
};

#endif
