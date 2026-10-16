#pragma once
//
// Every received CAN frame -> one line on UART0, in Linux `candump -L` format:
//
//   (174.183) can0 0CB#00FF004D30EF00D1
//
// Timestamp = board uptime in seconds. The format opens directly in SavvyCAN, python-can,
// cantools (`python3 -m cantools decode`), can-utils' canplayer, and tools/ in this repo.
// Capture it on a laptop with tools/serial_capture.py.
//
// Bandwidth: the bus carries ~1300 frames/s, ~35 bytes per line = ~45 KB/s. 115200 baud
// (11.5 KB/s) cannot keep up, so the port runs at 921600 (92 KB/s). On an
// esp32-s3-devkitc-1, UART0 is the USB-C port labelled "UART" (on-board USB bridge),
// not the native "USB" port.
//
// Non-blocking: a line is written only if it fits in the TX ring buffer; otherwise it is
// dropped and counted. The CAN receive path never waits for the serial port.
// Requires `logger: baud_rate: 0` so ESPHome's own log does not share the UART.
//
#include <cstdint>
#include <vector>
#include "driver/uart.h"

namespace canlog {

static const int         BAUD    = 921600;
static const uart_port_t UART    = UART_NUM_0;
static const int         TX_BUF  = 8192;
static const size_t      BACKLOG = 6144;      // bytes queued beyond this -> drop the line

static bool     s_on = false, s_ok = false;
static uint32_t s_sent = 0, s_dropped = 0;

inline void init() {
  if (uart_is_driver_installed(UART)) { uart_set_baudrate(UART, BAUD); s_ok = true; return; }
  uart_config_t cfg = {};
  cfg.baud_rate  = BAUD;
  cfg.data_bits  = UART_DATA_8_BITS;
  cfg.parity     = UART_PARITY_DISABLE;
  cfg.stop_bits  = UART_STOP_BITS_1;
  cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  // RX is unused, but the driver needs an RX buffer > 0 to install.
  s_ok = uart_param_config(UART, &cfg) == ESP_OK &&
         uart_driver_install(UART, 256, TX_BUF, 0, nullptr, 0) == ESP_OK;
}

inline void set_on(bool on) { s_on = on; }
inline uint32_t sent()    { return s_sent; }
inline uint32_t dropped() { return s_dropped; }

inline size_t backlog() {
  size_t free_sz = 0;
  if (uart_get_tx_buffer_free_size(UART, &free_sz) != ESP_OK) return (size_t)TX_BUF;
  return free_sz >= (size_t)TX_BUF ? 0 : (size_t)TX_BUF - free_sz;
}

// Called for every frame (~1300/s), so the hex is formatted by hand instead of snprintf.
inline void frame(uint32_t now_ms, uint32_t can_id, const std::vector<uint8_t> &data) {
  if (!s_on || !s_ok) return;
  static const char HEX[] = "0123456789ABCDEF";
  char buf[64];
  size_t n = 0;
  buf[n++] = '(';
  char tmp[11]; int tl = 0;
  uint32_t s = now_ms / 1000, ms = now_ms % 1000;
  do { tmp[tl++] = (char)('0' + s % 10); s /= 10; } while (s != 0 && tl < 10);
  while (tl > 0) buf[n++] = tmp[--tl];
  buf[n++] = '.';
  buf[n++] = (char)('0' + ms / 100); buf[n++] = (char)('0' + ms / 10 % 10); buf[n++] = (char)('0' + ms % 10);
  const char *ch = ") can0 ";
  while (*ch) buf[n++] = *ch++;
  if (can_id > 0x7FF) {                       // extended frame (never seen on this bus)
    for (int sh = 28; sh >= 0; sh -= 4) buf[n++] = HEX[(can_id >> sh) & 0xF];
  } else {
    buf[n++] = HEX[(can_id >> 8) & 0x7]; buf[n++] = HEX[(can_id >> 4) & 0xF]; buf[n++] = HEX[can_id & 0xF];
  }
  buf[n++] = '#';
  size_t cnt = data.size() > 8 ? 8 : data.size();
  for (size_t i = 0; i < cnt; i++) { buf[n++] = HEX[data[i] >> 4]; buf[n++] = HEX[data[i] & 0xF]; }
  buf[n++] = '\n';
  if (backlog() + n <= BACKLOG && uart_write_bytes(UART, buf, n) == (int)n) s_sent++;
  else s_dropped++;
}

}  // namespace canlog
