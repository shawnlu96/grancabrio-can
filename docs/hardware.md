# Hardware

What this was built with. Nothing here is special — any CAN interface that can listen at
500 kbit/s will read the same frames.

## Parts

- ESP32-S3 DevKitC-1 (any ESP32 with the TWAI controller works; pins below are for this board)
- SN65HVD230 CAN transceiver module (3.3 V)
- OBD-II plug or a Y splitter, so the diagnostic port stays usable
- A USB-C cable to a USB port in the car that is **switched with the ignition**

## Wiring

| SN65HVD230 module | ESP32-S3 |
|---|---|
| 3.3V | 3V3 — **not 5 V**, this module is a 3.3 V part |
| GND | GND |
| TX | GPIO 4 |
| RX | GPIO 5 |

| SN65HVD230 module | OBD-II port |
|---|---|
| CANH | pin 6 |
| CANL | pin 14 |

**TX/RX labels are ambiguous.** Some modules label them from the transceiver's point of view,
others from the MCU's. With them the wrong way round the receive pin floats: in a fraction of a
second "CAN Bus Errors" climbed into the hundreds of thousands, more than any real 500 kbit/s bus
can produce. If you see that, swap `can_tx_pin` / `can_rx_pin` in the YAML before touching any
wire.

**Termination.** The car's bus is already terminated. Many SN65HVD230 boards carry their own
120 Ω resistor between CANH and CANL. Ours ran for weeks with zero bus errors, but on a short stub
like this the textbook choice is to remove the resistor — check whether yours has one.

## Power

**Power the board from a USB port that switches off with the ignition. Never from OBD pin 16.**
Pin 16 is permanent battery voltage. A board and transceiver drawing from it around the clock
will flatten the battery of a car that is driven once a week.

Also design for the board not being there. On the original car it gets unplugged when the OBD
port is needed, and everything that uses it must degrade silently. The firmware reports NaN for
any signal not seen in the last second, rather than repeating the last value.

## Listen-only

The YAML runs the CAN controller in `LISTENONLY` mode. In that mode it sends nothing — no
frames, and no ACK bits either. A normal CAN node acknowledges every frame; listen-only is the
mode where the logger is invisible to the car. Keep it that way unless you know exactly what you
want to send and why.
