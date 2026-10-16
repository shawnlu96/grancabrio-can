# ESP32 / ESPHome pitfalls

Each of these cost real time on this project. The fixes are already in
[`../firmware/grancabrio-can.yaml`](../firmware/grancabrio-can.yaml); this explains why they are
there.

## The CAN receive queue holds 5 frames by default

`esp32_can` without `rx_queue_len` uses the ESP-IDF default of **5 frames**.

- ESPHome's main loop drains the queue roughly every 16 ms.
- At ~1300 frames/s, about 20 frames arrive in that time. The rest are dropped by the driver
  without a word.
- On this car the controller's RX-missed counter rose by ~936 per second while driving: the
  board saw about 30 % of the bus. A 48 Hz signal arrived at ~33 Hz, which looks plausible
  enough not to notice.

**Fix:** `rx_queue_len: 256` (≈ 200 ms of headroom, ~5 KB RAM). Watch "CAN RX Missed" — it should
stay flat.

## The native API reboots the board every 15 minutes

`api:` has `reboot_timeout: 15min` by default: if no client (Home Assistant, `esphome logs`) is
connected for 15 minutes, the board reboots ("No clients; rebooting"). In a car nobody is
connected most of the time, so the board rebooted every 15 minutes, forever. A recording that
stopped exactly 15 min 0 s after power-up gave it away.

**Fix:** `reboot_timeout: 0s`.

## The web page is blank without internet

The ESPHome web server UI loads its JavaScript from an internet CDN by default. In a car there is
usually no internet, so the diagnostic page is blank exactly when you need it.

**Fix:** `web_server: local: true` (embeds the UI in the firmware), or the old `version: 1`.

## WiFi and Bluetooth share one radio

The first design bridged a Bluetooth OBD dongle to WiFi on the same ESP32. With BLE active, WiFi
latency averaged ~70 ms; with BLE off, 2.6 ms. The two time-share the 2.4 GHz radio. Reading the
bus directly through a transceiver removed the need for Bluetooth altogether.

**Fix:** if latency matters, do not run BLE and WiFi together. Also set
`power_save_mode: none`.

## `uart_tx_chars` writes half lines

`uart_tx_chars()` bypasses the driver's ring buffer and writes straight into the 128-byte
hardware FIFO. When the FIFO is nearly full, it writes whatever fits — half a line — and returns.
The receiving side then sees garbage lines.

**Fix:** check `uart_get_tx_buffer_free_size()`, then write the whole line with
`uart_write_bytes()`, or drop the whole line (see `canlog.h`).

## The serial port resets the board

On ESP32 dev boards, DTR and RTS on the USB-serial bridge drive the auto-reset and boot-mode
circuit. A program that opens the port with DTR/RTS asserted resets the board, or drops it into
the bootloader. `tools/serial_capture.py` holds both low.

Also: with `logger: baud_rate: 0`, ESPHome's own log stays off UART0, so it does not interleave
with the frame stream.

## 115200 baud is not enough for a full bus log

One candump line is ~35 bytes. At 1300 frames/s that is ~45 KB/s, while 115200 baud carries
11.5 KB/s.

**Fix:** use 921600 (92 KB/s). The on-board USB bridge on the DevKitC handles it. Even so, drop
frames rather than block: the CAN receive path must never wait for the serial port.
