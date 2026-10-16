// Minimal stand-in for ESP-IDF's driver/uart.h so canlog.h compiles and runs on a laptop
// (test/canlog_test.cpp). Not a driver.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
typedef int uart_port_t; typedef int esp_err_t;
#define ESP_OK 0
#define UART_NUM_0 0
enum { UART_DATA_8_BITS, UART_PARITY_DISABLE, UART_STOP_BITS_1, UART_HW_FLOWCTRL_DISABLE, UART_SCLK_DEFAULT };
struct uart_config_t { int baud_rate, data_bits, parity, stop_bits, flow_ctrl, source_clk; };
inline std::string g_uart; inline size_t g_backlog = 0; inline int g_baud = 0;
inline bool uart_is_driver_installed(uart_port_t) { return false; }
inline esp_err_t uart_param_config(uart_port_t, const uart_config_t *c) { g_baud = c->baud_rate; return ESP_OK; }
inline esp_err_t uart_driver_install(uart_port_t, int, int, int, void *, int) { return ESP_OK; }
inline esp_err_t uart_set_baudrate(uart_port_t, int b) { g_baud = b; return ESP_OK; }
inline esp_err_t uart_get_tx_buffer_free_size(uart_port_t, size_t *f) { *f = 8192 - g_backlog; return ESP_OK; }
inline int uart_write_bytes(uart_port_t, const void *s, size_t n) { g_uart.append((const char *)s, n); return (int)n; }
