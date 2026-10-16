#!/usr/bin/env python3
"""Record the board's raw frame stream (switch "CAN Log To Serial" on) to a candump log.

    pip install pyserial
    python3 tools/serial_capture.py /dev/ttyACM0 drive.log.gz      # Linux
    python3 tools/serial_capture.py /dev/cu.usbmodem1101 drive.log  # macOS

Plug into the board's "UART" USB-C port (the USB bridge), not the native "USB" port.
DTR and RTS are held low: on ESP32 dev boards they drive the auto-reset / boot-mode circuit,
and toggling them resets the board (or drops it into the bootloader).
Lines that do not look like candump frames (boot messages, half lines) are skipped.
"""
import gzip
import re
import sys
import time

import serial

LINE = re.compile(rb'^\(\d+\.\d{3}\) can0 [0-9A-F]{3,8}#[0-9A-F]{0,16}\n$')


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    port, out = sys.argv[1], sys.argv[2]
    ser = serial.Serial()
    ser.port, ser.baudrate, ser.timeout = port, 921600, 1
    ser.dtr = False
    ser.rts = False
    ser.open()
    f = gzip.open(out, 'wb') if out.endswith('.gz') else open(out, 'wb')
    n = bad = 0
    last = time.time()
    try:
        while True:
            line = ser.readline()
            if not line:
                continue
            if LINE.match(line):
                f.write(line)
                n += 1
            else:
                bad += 1
            if time.time() - last >= 5:
                f.flush()
                print('\r%d frames, %d skipped lines' % (n, bad), end='', flush=True)
                last = time.time()
    except KeyboardInterrupt:
        pass
    finally:
        f.close()
        ser.close()
        print('\nwrote %d frames to %s' % (n, out))


if __name__ == '__main__':
    main()
