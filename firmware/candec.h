#pragma once
//
// Pure decode functions for the reverse-engineered broadcast frames.
// Same formulas as dbc/maserati_grancabrio_2016.dbc; test/dbc_crosscheck.py checks
// that the two agree frame by frame on the sample logs.
//
// No ESP-IDF / ESPHome headers here on purpose: this file (and drive.h) compile on a
// laptop so they can be unit-tested and replayed against recorded logs (see test/).
//
// Evidence and confidence for every signal: docs/signals.md.
//
#include <cstddef>
#include <cstdint>

namespace candec {

inline uint16_t be16(const uint8_t *d) { return (uint16_t)(((uint16_t)d[0] << 8) | d[1]); }

// 0x0CC bytes 3:4, signed big-endian, 0.1 deg/count, centre reads -5 counts, positive = LEFT.
// Full lock is about +/-545 deg (3.03 turns lock to lock).
inline bool steering_deg(const uint8_t *d, size_t n, float *out) {
  if (n < 5) return false;
  int16_t raw = (int16_t)be16(d + 3);
  *out = (float)(raw + 5) * 0.1f;
  return true;
}

// 0x146 bytes 5:6, big-endian, km/h = raw / 128 (mean error 0.40 km/h).
inline bool speed_kmh(const uint8_t *d, size_t n, float *out) {
  if (n < 7) return false;
  *out = (float)be16(d + 5) / 128.0f;
  return true;
}

// 0x566 bytes 2:3, big-endian, km/h = raw / 16. A second, slower speed source.
inline bool speed2_kmh(const uint8_t *d, size_t n, float *out) {
  if (n < 4) return false;
  *out = (float)be16(d + 2) / 16.0f;
  return true;
}

// 0x361 bytes 2:3, big-endian, rpm = raw * 17 / 16. (Factor 1 gives 74 rpm mean error,
// 17/16 gives 21 rpm. Why 17/16 is unknown, but the gearbox ratio check depends on it.)
inline bool rpm(const uint8_t *d, size_t n, uint16_t *out) {
  if (n < 4) return false;
  *out = (uint16_t)((uint32_t)be16(d + 2) * 17 / 16);
  return true;
}

// 0x361 byte 1, engine load 0..255 (= 0..100 %). Idle reads ~52 (20 %).
inline bool load(const uint8_t *d, size_t n, uint8_t *out) {
  if (n < 2) return false;
  *out = d[1];
  return true;
}

// 0x0CB byte 4, high 3 bits: 1..6 = gear actually engaged, 7 = neutral / none,
// 0 = engine not running.
inline bool gear(const uint8_t *d, size_t n, uint8_t *out) {
  if (n < 5) return false;
  *out = (uint8_t)(d[4] >> 5);
  return true;
}

// 0x061 byte 0 bit 4: brake pedal. Always set at standstill (automatic held on the brake);
// "brake OR stopped" has not been ruled out yet.
inline bool brake(const uint8_t *d, size_t n, uint8_t *out) {
  if (n < 1) return false;
  *out = (uint8_t)((d[0] & 0x10) ? 1 : 0);
  return true;
}

// 0x761 byte 0 (byte 1 is a redundant copy), degC = raw - 40 (standard OBD encoding).
inline bool coolant_c(const uint8_t *d, size_t n, int *out) {
  if (n < 1) return false;
  *out = (int)d[0] - 40;
  return true;
}

// 0x581 byte 1, degC = raw - 40. Engine oil (~90 % confidence, see docs/signals.md).
inline bool oil_c(const uint8_t *d, size_t n, int *out) {
  if (n < 2) return false;
  *out = (int)d[1] - 40;
  return true;
}

// 0x560 byte 7: distance counter, 1 count = 10 m. Byte 6 holds flags, not counter bits
// (0x04 / 0x08 flicker while driving; 0x20 = counter reset / not running).
// Returns false while the 0x20 flag is set.
inline bool trip_count(const uint8_t *d, size_t n, uint8_t *out) {
  if (n < 8) return false;
  if (d[6] & 0x20) return false;
  *out = d[7];
  return true;
}

// 0x166: four 16-bit big-endian wheel speeds, km/h = (raw - 8192) / 16.
// When the car is stationary all four drop to 0x002C - any value < 0x2000 means "invalid",
// and the whole frame is rejected.
// Field order (inferred from turns, see docs/signals.md): 0 = RR, 1 = RL, 2 = FR, 3 = FL.
enum Wheel { RR = 0, RL = 1, FR = 2, FL = 3 };
inline bool wheels_raw(const uint8_t *d, size_t n, uint16_t out[4]) {
  if (n < 8) return false;
  for (int k = 0; k < 4; k++) {
    uint16_t v = be16(d + 2 * k);
    if (v < 0x2000) return false;
    out[k] = v;
  }
  return true;
}
inline float wheel_kmh(uint16_t raw) { return (float)(raw - 0x2000) / 16.0f; }

}  // namespace candec
