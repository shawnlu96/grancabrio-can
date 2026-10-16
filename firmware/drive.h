#pragma once
//
// Driving analysis computed on the board from CAN frame arrival times.
// Three outputs, each a text line handed to the sink registered with set_sink():
//
//   W  (5 Hz)   four wheel speeds, 200 ms average        -> e.g. indirect tyre-pressure monitoring
//   P  (event)  performance timing (0-100, 100-0, ...)
//   G  (event)  every gear change: rpm transition time and flare -> gearbox ageing trend
//
// Rules:
//   1. Read-only. Nothing in here transmits on the bus.
//   2. No allocation, no blocking; the CAN callback only picks bytes and does arithmetic.
//   3. No ESP-IDF headers, so test/drive_test.cpp can run it on a laptop against recorded logs.
//   4. If a signal has a gap (> 500 ms between frames), any timing in progress is discarded.
//      No interpolating across a gap to produce a "result".
//
// Line format (key=value; consumers should ignore keys they don't know):
//   W t=<board ms> n=<samples> w0=<km/h x100> w1= w2= w3=      wheel order: 0 RR, 1 RL, 2 FR, 3 FL
//   P t=<ms> k=<kind> ms=<duration> [s50=<0-50 split>] [cm=<distance cm>] [v0=<start speed x10>]
//       k=1 0-100   k=2 100-0 braking   k=3 60-100   k=4 80-120
//       k=5 hard stop (from >= 60 km/h, mean deceleration >= 3.5 m/s^2)
//       k=6 0-50 (launch passed 50 but lifted before 100)
//   G t=<ms> f=<from gear> n=<to gear> v=<speed x10> r0=<rpm before> r1=<rpm after>
//       d=<rpm transition 10%->90% in ms, -1 = no clear rpm change> fl=<upshift flare rpm>
//       l=<load 0..255> [e=<oil temp degC>]
//
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include "candec.h"

namespace drive {

typedef void (*Sink)(const char *line, size_t n);
static Sink s_sink = nullptr;
inline void set_sink(Sink s) { s_sink = s; }
inline void out(const char *line, int n) {
  if (s_sink != nullptr && n > 0) s_sink(line, (size_t)n);
}

static const uint32_t GAP_MS = 500;          // two frames of one signal further apart than this = gap

// ── latest signals ────────────────────────────────────────────────
static float    s_v = 0;     static uint32_t s_v_at = 0;
static uint16_t s_rpm = 0;   static uint32_t s_rpm_at = 0;
static uint8_t  s_load = 0;
static uint8_t  s_brake = 0; static uint32_t s_brake_at = 0;
static int      s_oil = 0;   static uint32_t s_oil_at = 0;

inline bool fresh(uint32_t at, uint32_t now, uint32_t ttl) { return at != 0 && (now - at) < ttl; }
inline bool braking(uint32_t now) { return fresh(s_brake_at, now, 300) && s_brake != 0; }

// ═════════════════════════ wheel speeds (W) ═════════════════════════
static uint32_t w_sum[4] = {0, 0, 0, 0};
static uint16_t w_n = 0;

inline void on_wheels(const uint16_t raw[4]) {
  if (w_n >= 1000) return;                    // overflow guard: normally ~10 frames per 200 ms
  for (int k = 0; k < 4; k++) w_sum[k] += (uint32_t)(raw[k] - 0x2000);
  w_n++;
}

inline void emit_wheels(uint32_t now) {
  if (w_n == 0) return;
  unsigned long v[4];
  // km/h x100 = sum / (16 n) x 100 = sum x 25 / (4 n), rounded
  for (int k = 0; k < 4; k++) v[k] = (unsigned long)((w_sum[k] * 25u + 2u * w_n) / (4u * w_n));
  char line[96];
  int n = snprintf(line, sizeof(line), "W t=%lu n=%u w0=%lu w1=%lu w2=%lu w3=%lu\n",
                   (unsigned long)now, (unsigned)w_n, v[0], v[1], v[2], v[3]);
  for (int k = 0; k < 4; k++) w_sum[k] = 0;
  w_n = 0;
  if (n > 0 && (size_t)n < sizeof(line)) out(line, n);
}

// ═════════════════════════ performance timing (P) ═════════════════════════
static float    p_pv = 0;    static uint32_t p_pt = 0;   // previous speed sample

// Linear interpolation of the moment the speed crossed a threshold between two samples.
inline uint32_t cross(float thr, float pv, uint32_t pt, float v, uint32_t t) {
  if (v == pv) return t;
  float f = (thr - pv) / (v - pv);
  if (f < 0) f = 0;
  if (f > 1) f = 1;
  return pt + (uint32_t)(f * (float)(t - pt) + 0.5f);
}

inline void emit_perf(uint32_t now, int kind, uint32_t ms, const char *extra) {
  char line[112];
  int n = snprintf(line, sizeof(line), "P t=%lu k=%d ms=%lu%s\n",
                   (unsigned long)now, kind, (unsigned long)ms, extra != nullptr ? extra : "");
  if (n > 0 && (size_t)n < sizeof(line)) out(line, n);
}

// — launch 0-50 / 0-100 —
enum { L_MOVING = 0, L_ARMED = 1, L_RUN = 2, L_DONE = 3 };
static uint8_t  l_state = L_MOVING;
static uint32_t l_stop_since = 0, l_t0 = 0, l_t50 = 0;
static float    l_vmax = 0;

inline void launch_abort(uint32_t now) {
  if (l_state == L_RUN && l_t50 != 0) emit_perf(now, 6, l_t50 - l_t0, nullptr);
  l_state = L_DONE;
}

inline void launch(float v, uint32_t t, bool prev_ok) {
  if (v < 0.5f) {
    if (l_state == L_RUN) launch_abort(t);            // stopped again
    if (l_stop_since == 0) l_stop_since = t;
    if (l_state != L_ARMED && t - l_stop_since >= 1000) l_state = L_ARMED;   // armed after 1 s at rest
    return;
  }
  l_stop_since = 0;
  if (l_state == L_ARMED) {
    l_state = L_RUN;
    l_t0 = (prev_ok && p_pv < 0.5f) ? cross(0.5f, p_pv, p_pt, v, t) : t;
    l_vmax = v; l_t50 = 0;
    return;
  }
  if (l_state != L_RUN) return;
  if (!prev_ok) { l_state = L_DONE; return; }         // gap: discard, no result
  if (v > l_vmax) l_vmax = v;
  if (l_t50 == 0 && v >= 50.0f) l_t50 = cross(50.0f, p_pv, p_pt, v, t);
  if (v >= 100.0f) {
    uint32_t t100 = cross(100.0f, p_pv, p_pt, v, t);
    char ex[24];
    snprintf(ex, sizeof(ex), " s50=%lu", (unsigned long)(l_t50 != 0 ? l_t50 - l_t0 : 0));
    emit_perf(t, 1, t100 - l_t0, ex);
    l_state = L_DONE;
    return;
  }
  if (l_vmax - v > 3.0f || braking(t) || t - l_t0 > 30000) launch_abort(t);
}

// — rolling acceleration 60-100 / 80-120 —
struct Roll { float lo, hi; int kind; bool on; uint32_t t0; };
static Roll rolls[2] = { {60.0f, 100.0f, 3, false, 0}, {80.0f, 120.0f, 4, false, 0} };

inline void rolling(float v, uint32_t t, bool prev_ok) {
  for (int i = 0; i < 2; i++) {
    Roll &r = rolls[i];
    if (!r.on) {
      if (prev_ok && p_pv < r.lo && v >= r.lo && !braking(t)) { r.on = true; r.t0 = cross(r.lo, p_pv, p_pt, v, t); }
      continue;
    }
    if (!prev_ok || v < r.lo - 2.0f || braking(t) || t - r.t0 > 20000) { r.on = false; continue; }
    if (v >= r.hi) { emit_perf(t, r.kind, cross(r.hi, p_pv, p_pt, v, t) - r.t0, nullptr); r.on = false; }
  }
}

// — braking 100-0 / hard stop —
static bool     b_on = false;
static uint32_t b_t0 = 0, b_t100 = 0, b_off_since = 0;
static float    b_v0 = 0, b_vmin = 0, b_dist = 0, b_dist100 = 0;

inline void brake_run(float v, uint32_t t, bool prev_ok) {
  bool br = braking(t);
  if (!b_on) {
    if (br && v >= 60.0f) {
      b_on = true; b_t0 = t; b_v0 = v; b_vmin = v; b_dist = 0;
      b_t100 = 0; b_dist100 = 0; b_off_since = 0;
    }
    return;
  }
  if (!prev_ok) { b_on = false; return; }
  float seg = (p_pv + v) * 0.5f / 3.6f * (float)(t - p_pt) / 1000.0f;   // metres in this step
  b_dist += seg;
  if (b_t100 != 0) {
    b_dist100 += seg;
  } else if (b_v0 >= 100.0f && p_pv >= 100.0f && v < 100.0f) {
    b_t100 = cross(100.0f, p_pv, p_pt, v, t);
    b_dist100 = (100.0f + v) * 0.5f / 3.6f * (float)(t - b_t100) / 1000.0f;
  }
  if (v < b_vmin) b_vmin = v;
  if (!br) { if (b_off_since == 0) b_off_since = t; } else b_off_since = 0;
  if ((b_off_since != 0 && t - b_off_since > 400) || v > b_vmin + 2.0f || t - b_t0 > 20000) { b_on = false; return; }
  if (v < 0.5f) {
    uint32_t ms = t - b_t0;
    float decel = ms > 0 ? (b_v0 / 3.6f) / ((float)ms / 1000.0f) : 0;   // mean deceleration m/s^2
    char ex[48];
    if (decel >= 3.5f) {
      snprintf(ex, sizeof(ex), " cm=%lu v0=%d", (unsigned long)(b_dist * 100.0f + 0.5f), (int)(b_v0 * 10.0f + 0.5f));
      emit_perf(t, 5, ms, ex);
    }
    if (b_t100 != 0) {
      snprintf(ex, sizeof(ex), " cm=%lu", (unsigned long)(b_dist100 * 100.0f + 0.5f));
      emit_perf(t, 2, t - b_t100, ex);
    }
    b_on = false;
  }
}

inline void on_speed(float v, uint32_t t) {
  bool prev_ok = p_pt != 0 && (t - p_pt) < GAP_MS;
  launch(v, t, prev_ok);
  rolling(v, t, prev_ok);
  brake_run(v, t, prev_ok);
  p_pv = v; p_pt = t;
}

// ═════════════════════════ gear changes (G) ═════════════════════════
// An ageing automatic (ZF 6-speed here) first shows slower shifts and an rpm "flare" on
// upshifts (clutch packs slipping). One line per shift: rpm before/after, the 10%->90%
// transition time and the flare.
// A single line means little - with the torque converter unlocked, light-throttle shifts at
// low speed are naturally soft and scattered. The value is the trend: same gear pair, similar
// speed and load, d and fl creeping up over months.
static const int RING = 128;                 // rpm sample ring: ~2.5 s at 50 Hz, covers [-400, +1200] ms
static uint32_t r_t[RING];
static uint16_t r_v[RING];
static int      r_head = 0, r_cnt = 0;

static uint8_t  g_prev = 0;
static bool     sh_on = false;
static uint32_t sh_t = 0;
static uint8_t  sh_from = 0, sh_to = 0, sh_load = 0;
static float    sh_v = 0;
static const uint32_t SH_PRE = 400, SH_POST = 1200;

inline void on_rpm_sample(uint16_t rpm, uint32_t t) {
  r_t[r_head] = t; r_v[r_head] = rpm;
  r_head = (r_head + 1) % RING;
  if (r_cnt < RING) r_cnt++;
}

inline void on_gear(uint8_t g, uint32_t now) {
  if (g >= 1 && g <= 6 && g_prev >= 1 && g_prev <= 6 && g != g_prev && !sh_on &&
      fresh(s_v_at, now, GAP_MS) && s_v >= 5.0f) {
    sh_on = true; sh_t = now; sh_from = g_prev; sh_to = g; sh_v = s_v; sh_load = s_load;
  }
  g_prev = g;
}

// Analyse a shift once its window has passed. Called from tick(), never from the CAN callback.
inline void finish_shift(uint32_t now) {
  if (!sh_on || now - sh_t < SH_POST) return;
  sh_on = false;
  // samples inside the window, in time order
  static uint32_t ts[RING]; static uint16_t vs[RING]; int m = 0;   // static: keep off the main loop stack
  int start = (r_head - r_cnt + RING) % RING;
  for (int i = 0; i < r_cnt; i++) {
    int k = (start + i) % RING;
    int32_t dt = (int32_t)(r_t[k] - sh_t);
    if (dt < -(int32_t)SH_PRE || dt > (int32_t)SH_POST) continue;
    ts[m] = r_t[k]; vs[m] = r_v[k]; m++;
  }
  // mean before [-400, -100] ms and after [+800, +1200] ms
  uint32_t a = 0, an = 0, b = 0, bn = 0;
  for (int i = 0; i < m; i++) {
    int32_t dt = (int32_t)(ts[i] - sh_t);
    if (dt <= -100) { a += vs[i]; an++; }
    if (dt >= 800)  { b += vs[i]; bn++; }
  }
  if (an < 2 || bn < 2) return;                  // too few samples (gap): no report
  int r0 = (int)(a / an), r1 = (int)(b / bn);
  int delta = r1 - r0;
  int d = -1;
  if (delta >= 80 || delta <= -80) {
    int32_t t10 = -1, t90 = -1;
    for (int i = 0; i < m; i++) {
      int moved = ((int)vs[i] - r0) * (delta > 0 ? 1 : -1);
      int span = delta > 0 ? delta : -delta;
      if (t10 < 0 && moved * 10 >= span)     t10 = (int32_t)(ts[i] - sh_t);
      if (t90 < 0 && moved * 10 >= span * 9) { t90 = (int32_t)(ts[i] - sh_t); break; }
    }
    if (t10 >= -(int32_t)SH_PRE && t90 >= t10) d = (int)(t90 - t10);
  }
  int fl = 0;
  if (sh_to > sh_from) {                          // upshift: rpm should only fall; any rise = slip
    for (int i = 0; i < m; i++) {
      int32_t dt = (int32_t)(ts[i] - sh_t);
      if (dt < -200 || dt > 700) continue;
      int up = (int)vs[i] - r0;
      if (up > fl) fl = up;
    }
  }
  char line[128];
  int n = snprintf(line, sizeof(line), "G t=%lu f=%u n=%u v=%d r0=%d r1=%d d=%d fl=%d l=%u",
                   (unsigned long)sh_t, (unsigned)sh_from, (unsigned)sh_to,
                   (int)(sh_v * 10.0f + 0.5f), r0, r1, d, fl, (unsigned)sh_load);
  if (n < 0 || (size_t)n >= sizeof(line) - 12) return;
  if (fresh(s_oil_at, now, 5000)) n += snprintf(line + n, sizeof(line) - (size_t)n, " e=%d", s_oil);
  line[n++] = '\n';
  out(line, n);
}

// ═════════════════════════ entry points ═════════════════════════
inline void on_frame(uint32_t can_id, const uint8_t *d, size_t n, uint32_t now) {
  switch (can_id) {
    case 0x146: { float v; if (candec::speed_kmh(d, n, &v)) { s_v = v; s_v_at = now; on_speed(v, now); } return; }
    case 0x361: {
      uint16_t r; uint8_t l;
      if (candec::rpm(d, n, &r)) { s_rpm = r; s_rpm_at = now; on_rpm_sample(r, now); }
      if (candec::load(d, n, &l)) s_load = l;
      return;
    }
    case 0x0CB: { uint8_t g; if (candec::gear(d, n, &g)) on_gear(g, now); return; }
    case 0x061: { uint8_t b; if (candec::brake(d, n, &b)) { s_brake = b; s_brake_at = now; } return; }
    case 0x581: { int o; if (candec::oil_c(d, n, &o)) { s_oil = o; s_oil_at = now; } return; }
    case 0x166: { uint16_t w[4]; if (candec::wheels_raw(d, n, w)) on_wheels(w); return; }
    default: return;
  }
}

// Call every 200 ms: emits the W line and finishes shift windows that are due.
inline void tick(uint32_t now) {
  emit_wheels(now);
  finish_shift(now);
}

}  // namespace drive
