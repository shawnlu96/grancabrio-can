#pragma once
//
// Latest decoded value of every known signal, with a timestamp.
//
// Getters return NAN when a signal has not been seen for FRESH_MS. A stale value shown as
// if it were live is worse than no value: whoever reads it will believe it.
//
// Called from the CAN receive path, so: no allocation, no blocking, only byte picking and
// arithmetic. No ESP-IDF headers (unit-testable on a laptop).
//
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "candec.h"

namespace vehicle {

static const uint32_t FRESH_MS = 1000;

struct Sig { float v = 0; uint32_t at = 0; };

static Sig s_steer, s_speed, s_speed2, s_rpm, s_load, s_gear, s_brake, s_coolant, s_oil;
static Sig s_wheel[4];
static uint32_t s_frames = 0;

// Trip distance since power-up, unwrapped from the 8-bit counter in 0x560.
static uint32_t s_trip_m = 0;
static uint8_t  s_trip_prev = 0;
static bool     s_trip_have = false;
static uint32_t s_trip_at = 0;

inline void put(Sig &s, float v, uint32_t now) { s.v = v; s.at = now; }

inline void on_frame(uint32_t id, const uint8_t *d, size_t n, uint32_t now) {
  s_frames++;
  switch (id) {
    case 0x0CC: { float a; if (candec::steering_deg(d, n, &a)) put(s_steer, a, now); return; }
    case 0x146: { float v; if (candec::speed_kmh(d, n, &v)) put(s_speed, v, now); return; }
    case 0x566: { float v; if (candec::speed2_kmh(d, n, &v)) put(s_speed2, v, now); return; }
    case 0x361: {
      uint16_t r; uint8_t l;
      if (candec::rpm(d, n, &r)) put(s_rpm, (float)r, now);
      if (candec::load(d, n, &l)) put(s_load, (float)l / 2.55f, now);
      return;
    }
    case 0x0CB: { uint8_t g; if (candec::gear(d, n, &g)) put(s_gear, (float)g, now); return; }
    case 0x061: { uint8_t b; if (candec::brake(d, n, &b)) put(s_brake, (float)b, now); return; }
    case 0x761: { int c; if (candec::coolant_c(d, n, &c)) put(s_coolant, (float)c, now); return; }
    case 0x581: { int o; if (candec::oil_c(d, n, &o)) put(s_oil, (float)o, now); return; }
    case 0x560: {
      uint8_t c;
      if (!candec::trip_count(d, n, &c)) { s_trip_have = false; return; }
      if (s_trip_have) {
        uint8_t delta = (uint8_t)(c - s_trip_prev);       // unsigned: handles the 8-bit wrap
        if (delta > 0 && delta < 20) s_trip_m += (uint32_t)delta * 10u;   // >= 20 = glitch / reset
      }
      s_trip_prev = c; s_trip_have = true; s_trip_at = now;
      return;
    }
    case 0x166: {
      uint16_t w[4];
      if (!candec::wheels_raw(d, n, w)) return;
      for (int k = 0; k < 4; k++) put(s_wheel[k], candec::wheel_kmh(w[k]), now);
      return;
    }
    default:
      return;
  }
}

inline float get(const Sig &s, uint32_t now) {
  return (s.at != 0 && now - s.at < FRESH_MS) ? s.v : NAN;
}

inline float steering_deg(uint32_t now) { return get(s_steer, now); }
inline float speed_kmh(uint32_t now)    { return get(s_speed, now); }
inline float speed2_kmh(uint32_t now)   { return get(s_speed2, now); }
inline float rpm(uint32_t now)          { return get(s_rpm, now); }
inline float load_pct(uint32_t now)     { return get(s_load, now); }
inline float gear(uint32_t now)         { return get(s_gear, now); }
inline float brake(uint32_t now)        { return get(s_brake, now); }
inline float coolant_c(uint32_t now)    { return get(s_coolant, now); }
inline float oil_c(uint32_t now)        { return get(s_oil, now); }
inline float wheel_kmh(int k, uint32_t now) { return get(s_wheel[k], now); }
inline float trip_m(uint32_t now) {
  return (s_trip_at != 0 && now - s_trip_at < 5000) ? (float)s_trip_m : NAN;
}
inline uint32_t frames() { return s_frames; }

}  // namespace vehicle
