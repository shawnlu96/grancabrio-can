// Unit tests for candec.h and vehicle.h with hand-made and recorded frames.
//   c++ -std=c++17 -I.. -Wall -Wextra -o /tmp/candec_test candec_test.cpp && /tmp/candec_test
#include <cmath>
#include <cstdio>
#include "candec.h"
#include "vehicle.h"

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; printf("  FAIL "); } else printf("  ok   "); \
                              printf(__VA_ARGS__); printf("\n"); } while (0)
static bool near(float a, float b, float tol = 1e-3f) { return std::fabs(a - b) <= tol; }

static void test_decode() {
  printf("decode\n");
  float f; uint16_t u16; uint8_t u8; int i; uint16_t w[4];

  const uint8_t steer[] = {0x9D, 0x07, 0x1A, 0xF3, 0x8C};          // recorded frame
  CHECK(candec::steering_deg(steer, 5, &f) && near(f, -318.3f), "0x0CC F38C -> %.1f deg (expect -318.3, i.e. right)", f);
  const uint8_t centre[] = {0, 0, 0, 0xFF, 0xFB};                    // -5 counts = centre
  CHECK(candec::steering_deg(centre, 5, &f) && near(f, 0.0f), "0x0CC centre -5 counts -> %.1f deg", f);
  CHECK(!candec::steering_deg(steer, 4, &f), "0x0CC short frame rejected");

  const uint8_t spd[] = {0, 0, 0, 0, 0, 0x14, 0x00, 0};              // 5120 / 128
  CHECK(candec::speed_kmh(spd, 8, &f) && near(f, 40.0f), "0x146 -> %.2f km/h (expect 40)", f);
  const uint8_t spd2[] = {0, 0, 0x02, 0x80, 0, 0, 0, 0};             // 640 / 16
  CHECK(candec::speed2_kmh(spd2, 8, &f) && near(f, 40.0f), "0x566 -> %.2f km/h (expect 40)", f);

  const uint8_t eng[] = {0, 52, 0x03, 0x20, 0, 0, 0, 0};             // raw 800 -> 850 rpm, load 52
  CHECK(candec::rpm(eng, 8, &u16) && u16 == 850, "0x361 rpm %u (expect 850)", (unsigned)u16);
  CHECK(candec::load(eng, 8, &u8) && u8 == 52, "0x361 load raw %u", (unsigned)u8);

  const uint8_t g3[] = {0, 0, 0, 0, 0x70, 0, 0, 0}, gn[] = {0, 0, 0, 0, 0xF0, 0, 0, 0};
  CHECK(candec::gear(g3, 8, &u8) && u8 == 3, "0x0CB 0x70 -> gear %u", (unsigned)u8);
  CHECK(candec::gear(gn, 8, &u8) && u8 == 7, "0x0CB 0xF0 -> %u (neutral)", (unsigned)u8);

  const uint8_t br1[] = {0x10}, br0[] = {0xEF};
  CHECK(candec::brake(br1, 1, &u8) && u8 == 1, "0x061 bit 4 set -> pressed");
  CHECK(candec::brake(br0, 1, &u8) && u8 == 0, "0x061 other bits only -> released");

  const uint8_t cool[] = {0x82, 0x82, 0, 0, 0, 0, 0, 0}, oil[] = {0, 0x7D, 0, 0, 0, 0, 0, 0};
  CHECK(candec::coolant_c(cool, 8, &i) && i == 90, "0x761 0x82 -> %d degC", i);
  CHECK(candec::oil_c(oil, 8, &i) && i == 85, "0x581 0x7D -> %d degC", i);

  const uint8_t trip[] = {0, 0x20, 0x40, 0x86, 0x04, 0x07, 0x08, 0x1F};   // recorded: flag 0x08 in byte 6
  CHECK(candec::trip_count(trip, 8, &u8) && u8 == 0x1F, "0x560 counter is byte 7 (flags in byte 6 ignored)");
  const uint8_t trip_off[] = {0, 0x20, 0x40, 0x86, 0, 0x09, 0x20, 0x00};
  CHECK(!candec::trip_count(trip_off, 8, &u8), "0x560 with 0x20 flag rejected");

  const uint8_t wh[] = {0x23, 0x20, 0x23, 0x20, 0x23, 0x28, 0x23, 0x18};  // 50, 50, 50.5, 49.5 km/h
  CHECK(candec::wheels_raw(wh, 8, w) && near(candec::wheel_kmh(w[candec::FR]), 50.5f) &&
        near(candec::wheel_kmh(w[candec::FL]), 49.5f), "0x166 wheel speeds");
  const uint8_t wh_stop[] = {0x00, 0x2C, 0x00, 0x2C, 0x00, 0x2C, 0x00, 0x2C};
  CHECK(!candec::wheels_raw(wh_stop, 8, w), "0x166 stationary 0x002C rejected");
  const uint8_t wh_one[] = {0x20, 0x2C, 0x00, 0x2C, 0x20, 0x2C, 0x20, 0x2C};
  CHECK(!candec::wheels_raw(wh_one, 8, w), "0x166 one invalid wheel rejects the frame");
}

static void trip_frame(uint8_t flags, uint8_t count, uint32_t t) {
  uint8_t d[8] = {0, 0x20, 0x40, 0x86, 0x04, 0x07, flags, count};
  vehicle::on_frame(0x560, d, 8, t);
}

static void test_vehicle() {
  printf("vehicle state\n");
  trip_frame(0x00, 0xFE, 100);
  trip_frame(0x00, 0xFF, 200);
  trip_frame(0x08, 0x00, 300);          // wrap + a flag in byte 6
  trip_frame(0x00, 0x01, 400);
  CHECK(near(vehicle::trip_m(400), 30.0f), "trip unwraps the 8-bit counter: %.0f m (expect 30)", vehicle::trip_m(400));
  trip_frame(0x00, 0x40, 500);          // jump of 63 counts = glitch, ignored
  trip_frame(0x00, 0x41, 600);
  CHECK(near(vehicle::trip_m(600), 40.0f), "trip ignores a glitch jump: %.0f m (expect 40)", vehicle::trip_m(600));
  trip_frame(0x20, 0x00, 700);          // counter reset
  trip_frame(0x00, 0x05, 800);          // first count after reset: no delta yet
  trip_frame(0x00, 0x06, 900);
  CHECK(near(vehicle::trip_m(900), 50.0f), "trip resumes after a reset: %.0f m (expect 50)", vehicle::trip_m(900));

  const uint8_t spd[] = {0, 0, 0, 0, 0, 0x14, 0x00, 0};
  vehicle::on_frame(0x146, spd, 8, 1000);
  CHECK(near(vehicle::speed_kmh(1500), 40.0f), "fresh speed is reported");
  CHECK(std::isnan(vehicle::speed_kmh(2100)), "stale speed (> 1 s) is NaN, not the old value");
  CHECK(std::isnan(vehicle::oil_c(2100)), "never-seen signal is NaN");
}

int main() {
  test_decode();
  test_vehicle();
  printf(fails == 0 ? "\nall candec/vehicle tests passed\n" : "\nFAILED: %d checks\n", fails);
  return fails == 0 ? 0 : 1;
}
