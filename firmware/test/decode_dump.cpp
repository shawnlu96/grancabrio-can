// Reads candump -L lines on stdin and prints what candec.h decodes, one value per line:
//   <line number> <DBC signal name> <value>
// test/dbc_crosscheck.py decodes the same log with the DBC file and compares the two.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "candec.h"

int main() {
  char buf[256];
  long ln = 0;
  while (fgets(buf, sizeof(buf), stdin)) {
    ln++;
    char *hash = strchr(buf, '#');
    if (buf[0] != '(' || !hash) continue;
    char *q = hash;
    while (q > buf && q[-1] != ' ') q--;
    unsigned long id = strtoul(q, nullptr, 16);
    uint8_t d[8]; size_t n = 0;
    for (char *h = hash + 1; n < 8 && isxdigit((unsigned char)h[0]) && isxdigit((unsigned char)h[1]); h += 2) {
      char b[3] = {h[0], h[1], 0};
      d[n++] = (uint8_t)strtoul(b, nullptr, 16);
    }
    float f; uint16_t u16; uint8_t u8; int i; uint16_t w[4];
    switch (id) {
      case 0x0CC: if (candec::steering_deg(d, n, &f)) printf("%ld SteeringWheelAngle %.4f\n", ln, f); break;
      case 0x146: if (candec::speed_kmh(d, n, &f)) printf("%ld VehicleSpeed %.4f\n", ln, f); break;
      case 0x566: if (candec::speed2_kmh(d, n, &f)) printf("%ld VehicleSpeed2 %.4f\n", ln, f); break;
      case 0x361:
        if (candec::rpm(d, n, &u16)) printf("%ld EngineSpeed %u\n", ln, (unsigned)u16);
        if (candec::load(d, n, &u8)) printf("%ld EngineLoad %.4f\n", ln, u8 / 2.55);
        break;
      case 0x0CB: if (candec::gear(d, n, &u8)) printf("%ld GearEngaged %u\n", ln, (unsigned)u8); break;
      case 0x061: if (candec::brake(d, n, &u8)) printf("%ld BrakePressed %u\n", ln, (unsigned)u8); break;
      case 0x761: if (candec::coolant_c(d, n, &i)) printf("%ld CoolantTemp %d\n", ln, i); break;
      case 0x581: if (candec::oil_c(d, n, &i)) printf("%ld EngineOilTemp %d\n", ln, i); break;
      case 0x560: if (candec::trip_count(d, n, &u8)) printf("%ld TripCounter %u\n", ln, u8 * 10u); break;
      case 0x166:
        if (candec::wheels_raw(d, n, w)) {
          printf("%ld WheelSpeed_RR %.4f\n", ln, candec::wheel_kmh(w[candec::RR]));
          printf("%ld WheelSpeed_RL %.4f\n", ln, candec::wheel_kmh(w[candec::RL]));
          printf("%ld WheelSpeed_FR %.4f\n", ln, candec::wheel_kmh(w[candec::FR]));
          printf("%ld WheelSpeed_FL %.4f\n", ln, candec::wheel_kmh(w[candec::FL]));
        }
        break;
      default: break;
    }
  }
  return 0;
}
