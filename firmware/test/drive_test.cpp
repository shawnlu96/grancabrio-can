// Tests for drive.h
//   1. Synthetic: launches / stops / rolling acceleration / shifts / wheel speeds with known
//      answers, checked within tolerance.
//   2. Replay: pass candump -L logs (plain text) to print the W/P/G lines the board would emit.
//      The sample logs were recorded with a 5-frame RX queue and contain only ~30 % of the
//      bus traffic, so replay is a "does the logic stay sane" check, not a measurement.
//
//   c++ -std=c++17 -I.. -Wall -Wextra -o /tmp/drive_test drive_test.cpp
//   /tmp/drive_test                       # synthetic only
//   /tmp/drive_test city-drive.log        # plus replay
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "drive.h"

static std::vector<std::string> g_lines;
static bool g_echo = false;
static void sink(const char *l, size_t n) {
  g_lines.emplace_back(l, n);
  if (g_echo && l[0] != 'W') fwrite(l, 1, n, stdout);
}

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("  FAIL " __VA_ARGS__); printf("\n"); } \
                              else { printf("  ok   " __VA_ARGS__); printf("\n"); } } while (0)

// integer value of key in a line; -999999 if absent
static long kv(const std::string &line, const char *key) {
  std::string k = std::string(" ") + key + "=";
  size_t p = line.find(k);
  if (p == std::string::npos) return -999999;
  return strtol(line.c_str() + p + k.size(), nullptr, 10);
}
static const std::string *find_perf(int kind) {
  for (auto &l : g_lines) if (l[0] == 'P' && kv(l, "k") == kind) return &l;
  return nullptr;
}

// ── synthetic frames ──
static void f_speed(uint32_t t, float kmh) {
  uint16_t raw = (uint16_t)lroundf(kmh * 128.0f);
  uint8_t d[8] = {0, 0, 0, 0, 0, (uint8_t)(raw >> 8), (uint8_t)raw, 0};
  drive::on_frame(0x146, d, 8, t);
}
static void f_brake(uint32_t t, bool on) {
  uint8_t d[8] = {(uint8_t)(on ? 0x10 : 0), 0, 0, 0, 0, 0, 0, 0};
  drive::on_frame(0x061, d, 8, t);
}
static void f_rpm(uint32_t t, int rpm, uint8_t load = 80) {
  uint16_t raw = (uint16_t)((rpm * 16 + 16) / 17);           // the board decodes x17/16; invert it here
  uint8_t d[8] = {0, load, (uint8_t)(raw >> 8), (uint8_t)raw, 0, 0, 0, 0};
  drive::on_frame(0x361, d, 8, t);
}
static void f_gear(uint32_t t, int g) {
  uint8_t d[8] = {0, 0, 0, 0, (uint8_t)(g << 5), 0, 0, 0};
  drive::on_frame(0x0CB, d, 8, t);
}
static void f_wheels(uint32_t t, const float kmh[4]) {
  uint8_t d[8];
  for (int k = 0; k < 4; k++) {
    uint16_t raw = (uint16_t)lroundf(8192.0f + kmh[k] * 16.0f);
    d[2 * k] = (uint8_t)(raw >> 8); d[2 * k + 1] = (uint8_t)raw;
  }
  drive::on_frame(0x166, d, 8, t);
}

static void reset() {
  g_lines.clear();
  drive::l_state = drive::L_MOVING; drive::l_stop_since = 0;
  drive::p_pt = 0; drive::b_on = false; drive::rolls[0].on = drive::rolls[1].on = false;
  drive::sh_on = false; drive::g_prev = 0; drive::r_cnt = 0; drive::r_head = 0;
  drive::w_n = 0; for (auto &s : drive::w_sum) s = 0;
  drive::s_brake_at = 0; drive::s_v_at = 0;
}

static void test_launch() {
  printf("launch 0-100 (constant 5 m/s^2: 0-50 2.778 s / 0-100 5.556 s in theory; starting the clock at 0.5 km/h takes 28 ms off)\n");
  reset();
  uint32_t t = 1000;
  for (int i = 0; i < 100; i++, t += 20) { f_speed(t, 0); f_brake(t, false); }    // 2 s at rest
  uint32_t go = t;
  for (; t < go + 7000; t += 20) {
    float v = 18.0f * (float)(t - go) / 1000.0f;                              // 5 m/s² = 18 km/h/s
    f_speed(t, v); f_brake(t, false);
  }
  const std::string *l = find_perf(1);
  CHECK(l != nullptr, "0-100 result emitted");
  if (l) {
    long ms = kv(*l, "ms"), s50 = kv(*l, "s50");
    CHECK(labs(ms - 5528) <= 30, "0-100 = %ld ms (expect ~5528)", ms);
    CHECK(labs(s50 - 2750) <= 30, "0-50 split = %ld ms (expect ~2750)", s50);
  }
  CHECK(find_perf(3) != nullptr, "60-100 emitted on the way");
  if (const std::string *r = find_perf(3)) CHECK(labs(kv(*r, "ms") - 2222) <= 30, "60-100 = %ld ms (expect ~2222)", kv(*r, "ms"));
}

static void test_brake() {
  printf("braking from 110 at constant 8 m/s^2 (100-0: 3.472 s / 48.2 m; 110-0: 3.819 s / 58.4 m)\n");
  reset();
  uint32_t t = 1000;
  for (int i = 0; i < 50; i++, t += 20) { f_brake(t, false); f_speed(t, 110); }
  uint32_t b0 = t;
  for (;; t += 20) {
    float v = 110.0f - 28.8f * (float)(t - b0) / 1000.0f;                    // 8 m/s² = 28.8 km/h/s
    if (v < 0) v = 0;
    f_brake(t, true); f_speed(t, v);
    if (v == 0) { t += 20; f_brake(t, true); f_speed(t, 0); break; }
  }
  const std::string *l = find_perf(2);
  CHECK(l != nullptr, "100-0 result emitted");
  if (l) {
    CHECK(labs(kv(*l, "ms") - 3472) <= 40, "100-0 = %ld ms (expect ~3472)", kv(*l, "ms"));
    CHECK(labs(kv(*l, "cm") - 4823) <= 150, "100-0 distance = %ld cm (expect ~4823)", kv(*l, "cm"));
  }
  const std::string *h = find_perf(5);
  CHECK(h != nullptr, "hard stop emitted");
  if (h) CHECK(labs(kv(*h, "cm") - 5835) <= 200 && kv(*h, "v0") == 1100, "hard stop %ld cm / from %ld", kv(*h, "cm"), kv(*h, "v0"));

  printf("gentle stop (from 60 at 2 m/s^2) must not count as a hard stop\n");
  reset(); t = 1000;
  for (int i = 0; i < 50; i++, t += 20) { f_brake(t, false); f_speed(t, 60); }
  b0 = t;
  for (;; t += 20) {
    float v = 60.0f - 7.2f * (float)(t - b0) / 1000.0f; if (v < 0) v = 0;
    f_brake(t, true); f_speed(t, v);
    if (v == 0) { t += 20; f_brake(t, true); f_speed(t, 0); break; }
  }
  CHECK(find_perf(5) == nullptr && find_perf(2) == nullptr, "no result for a gentle stop");
}

static void test_gap() {
  printf("gap: 1 s of data missing during a launch -> no result\n");
  reset();
  uint32_t t = 1000;
  for (int i = 0; i < 100; i++, t += 20) { f_speed(t, 0); f_brake(t, false); }
  uint32_t go = t;
  for (; t < go + 7000; t += 20) {
    if (t > go + 2000 && t < go + 3000) continue;
    f_speed(t, 18.0f * (float)(t - go) / 1000.0f); f_brake(t, false);
  }
  CHECK(find_perf(1) == nullptr, "no 0-100 across a gap");
}

static void test_shift() {
  printf("shift 2->3, rpm 3000 -> 2000 falling linearly over +100..+400 ms (10%%->90%% = 240 ms)\n");
  reset();
  uint32_t t = 1000;
  for (int i = 0; i < 100; i++, t += 20) { f_speed(t, 60); f_rpm(t, 3000); f_gear(t, 2); }
  uint32_t ts = t;
  for (; t < ts + 2000; t += 20) {
    int32_t dt = (int32_t)(t - ts);
    int rpm = dt < 100 ? 3000 : (dt > 400 ? 2000 : 3000 - (dt - 100) * 1000 / 300);
    f_speed(t, 60); f_rpm(t, rpm); f_gear(t, 3);
    if ((t - ts) % 200 == 0) drive::tick(t);
  }
  const std::string *g = nullptr;
  for (auto &l : g_lines) if (l[0] == 'G') g = &l;
  CHECK(g != nullptr, "G line emitted");
  if (g) {
    CHECK(kv(*g, "f") == 2 && kv(*g, "n") == 3, "gears %ld->%ld", kv(*g, "f"), kv(*g, "n"));
    CHECK(labs(kv(*g, "r0") - 3000) <= 5 && labs(kv(*g, "r1") - 2000) <= 5, "r0=%ld r1=%ld", kv(*g, "r0"), kv(*g, "r1"));
    CHECK(labs(kv(*g, "d") - 240) <= 40, "transition d=%ld (expect ~240)", kv(*g, "d"));
    CHECK(kv(*g, "fl") <= 5, "no flare fl=%ld", kv(*g, "fl"));
  }

  printf("shift with a 180 rpm flare before the drop (slip)\n");
  reset(); t = 1000;
  for (int i = 0; i < 100; i++, t += 20) { f_speed(t, 60); f_rpm(t, 3000); f_gear(t, 2); }
  ts = t;
  for (; t < ts + 2000; t += 20) {
    int32_t dt = (int32_t)(t - ts);
    int rpm = dt < 0 ? 3000 : dt < 150 ? 3000 + dt * 180 / 150 : dt < 450 ? 3180 - (dt - 150) * 1180 / 300 : 2000;
    f_speed(t, 60); f_rpm(t, rpm); f_gear(t, 3);
    if ((t - ts) % 200 == 0) drive::tick(t);
  }
  g = nullptr;
  for (auto &l : g_lines) if (l[0] == 'G') g = &l;
  CHECK(g != nullptr && labs(kv(*g, "fl") - 180) <= 15, "flare fl=%ld (expect ~180)", g ? kv(*g, "fl") : -1);
}

static void test_wheels() {
  printf("wheel speeds: 200 ms average, km/h x100\n");
  reset();
  const float w[4] = {100.0f, 100.0625f, 100.5f, 99.9375f};
  for (int i = 0; i < 10; i++) f_wheels(1000 + i * 20, w);
  drive::tick(1200);
  const std::string *l = nullptr;
  for (auto &x : g_lines) if (x[0] == 'W') l = &x;
  CHECK(l != nullptr, "W line emitted");
  if (l) CHECK(kv(*l, "n") == 10 && kv(*l, "w0") == 10000 && kv(*l, "w1") == 10006 && kv(*l, "w2") == 10050 && kv(*l, "w3") == 9994,
               "%s", l->c_str());
  uint8_t stopped[8] = {0, 0x2C, 0, 0x2C, 0, 0x2C, 0, 0x2C};
  g_lines.clear();
  drive::on_frame(0x166, stopped, 8, 2000);
  drive::tick(2200);
  CHECK(g_lines.empty(), "no W line for the stationary state (0x002C)");
}

static void replay(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f) { printf("cannot open %s\n", path); return; }
  reset();
  g_echo = true;
  char buf[256];
  uint32_t next_tick = 0;
  long frames = 0;
  while (fgets(buf, sizeof(buf), f)) {
    // (174.183) can0 0CB#00FF004D30EF00D1
    if (buf[0] != '(') continue;
    char *p = buf + 1;
    double sec = strtod(p, &p);
    uint32_t t = (uint32_t)(sec * 1000.0 + 0.5);
    char *hash = strchr(p, '#');
    if (!hash) continue;
    char *q = hash;
    while (q > p && q[-1] != ' ') q--;
    unsigned long id = strtoul(q, nullptr, 16);
    uint8_t d[8]; size_t n = 0;
    for (char *h = hash + 1; n < 8 && isxdigit((unsigned char)h[0]) && isxdigit((unsigned char)h[1]); h += 2) {
      char b[3] = {h[0], h[1], 0};
      d[n++] = (uint8_t)strtoul(b, nullptr, 16);
    }
    if (next_tick == 0) next_tick = t;
    while (t >= next_tick) { drive::tick(next_tick); next_tick += 200; }
    drive::on_frame((uint32_t)id, d, n, t);
    frames++;
  }
  fclose(f);
  int w = 0, p = 0, g = 0;
  for (auto &l : g_lines) { if (l[0] == 'W') w++; else if (l[0] == 'P') p++; else if (l[0] == 'G') g++; }
  printf("replay %s: %ld frames -> W %d / P %d / G %d lines\n", path, frames, w, p, g);
  g_echo = false;
}

int main(int argc, char **argv) {
  drive::set_sink(sink);
  test_launch();
  test_brake();
  test_gap();
  test_shift();
  test_wheels();
  printf(fails == 0 ? "\nall synthetic tests passed\n" : "\nFAILED: %d checks\n", fails);
  for (int i = 1; i < argc; i++) replay(argv[i]);
  return fails == 0 ? 0 : 1;
}
