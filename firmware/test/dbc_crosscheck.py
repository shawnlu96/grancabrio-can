#!/usr/bin/env python3
"""Check that the DBC file and firmware/candec.h decode the same log identically.

    c++ -std=c++17 -I.. -o /tmp/decode_dump decode_dump.cpp
    gzip -dc ../../samples/2026-09-05_city-drive.log.gz > /tmp/city.log
    /tmp/decode_dump < /tmp/city.log > /tmp/firmware.txt
    python3 dbc_crosscheck.py ../../dbc/maserati_grancabrio_2016.dbc /tmp/city.log /tmp/firmware.txt

Needs `pip install cantools`. Exits non-zero on any mismatch.
"""
import sys
from collections import Counter

import cantools

# candec.h computes rpm with integer maths (raw * 17 / 16, truncated); the DBC factor is exact.
TOL = {'EngineSpeed': 1.0}
WHEELS = ('WheelSpeed_RR', 'WheelSpeed_RL', 'WheelSpeed_FR', 'WheelSpeed_FL')


def firmware_rejects(name, dec, raw):
    """Frames candec.h deliberately ignores although the DBC still decodes them."""
    if name in WHEELS:
        # any wheel in the invalid state (raw < 0x2000 -> negative km/h) -> whole frame dropped
        return any(dec[w] < 0 for w in WHEELS)
    if name == 'TripCounter':
        return bool(raw[6] & 0x20)     # counter-reset flag
    return False


def main(dbc_path, log_path, fw_path):
    db = cantools.database.load_file(dbc_path)
    fw = {}
    for l in open(fw_path):
        ln, name, val = l.split()
        fw[(int(ln), name)] = float(val)

    ok, bad, skipped = Counter(), Counter(), Counter()
    for ln, l in enumerate(open(log_path), 1):
        if not l.startswith('('):
            continue
        cid, data = l.split()[2].split('#')
        try:
            msg = db.get_message_by_frame_id(int(cid, 16))
        except KeyError:
            continue
        raw = bytes.fromhex(data)
        dec = msg.decode(raw, decode_choices=False)
        for name, v in dec.items():
            key = (ln, name)
            if name == 'CoolantTemp2':
                continue                           # redundant copy, not in candec.h
            if firmware_rejects(name, dec, raw):
                skipped[name] += 1
                if key in fw:
                    bad[name] += 1
                    print('line %d %s: firmware should have rejected this frame' % (ln, name))
                continue
            if key not in fw:
                bad[name] += 1
                if bad[name] <= 3:
                    print('line %d %s: DBC %.4f, firmware rejected it' % (ln, name, v))
                continue
            if abs(fw[key] - v) <= TOL.get(name, 1e-3):
                ok[name] += 1
            else:
                bad[name] += 1
                if bad[name] <= 3:
                    print('line %d %s: DBC %.4f vs firmware %.4f' % (ln, name, v, fw[key]))

    for name in sorted(set(ok) | set(bad) | set(skipped)):
        print('%-20s agree %7d  differ %3d  rejected by firmware %5d' % (name, ok[name], bad[name], skipped[name]))
    print('OK' if not bad else 'MISMATCH')
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:4]))
