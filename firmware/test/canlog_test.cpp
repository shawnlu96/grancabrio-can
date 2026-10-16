// canlog.h on a laptop (ESP-IDF UART replaced by test/stub): candump -L format, 921600,
// off by default, drops whole lines when the TX buffer is backed up.
#include <cstdio>
#include <vector>
#include "canlog.h"

int main() {
  int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL "); } else printf("  ok   "); printf(__VA_ARGS__); printf("\n"); } while (0)
  canlog::init();
  CHECK(g_baud == 921600, "baud %d", g_baud);
  canlog::frame(1000, 0x0CB, {1, 2});
  CHECK(g_uart.empty(), "nothing written while switched off");
  canlog::set_on(true);
  canlog::frame(174183, 0x0CB, {0x00, 0xFF, 0x00, 0x4D, 0x30, 0xEF, 0x00, 0xD1});
  canlog::frame(5, 0x0CC, {0x9D, 0x07, 0x1A, 0xF3, 0x8C});
  canlog::frame(4294967295u, 0x7E8, {});
  canlog::frame(1, 0x18DAF110, {0xAA});
  const char *want = "(174.183) can0 0CB#00FF004D30EF00D1\n(0.005) can0 0CC#9D071AF38C\n"
                     "(4294967.295) can0 7E8#\n(0.001) can0 18DAF110#AA\n";
  CHECK(g_uart == want, "candump -L lines:\n%s", g_uart.c_str());
  g_uart.clear(); g_backlog = 6130;
  canlog::frame(1, 0x0CB, {1, 2, 3, 4, 5, 6, 7, 8});
  CHECK(g_uart.empty() && canlog::dropped() == 1, "whole line dropped when the buffer is backed up");
  printf(fails == 0 ? "all canlog tests passed\n" : "FAILED: %d checks\n", fails);
  return fails;
}
