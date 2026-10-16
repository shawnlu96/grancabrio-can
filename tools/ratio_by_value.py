#!/usr/bin/env python3
"""Group the speed/rpm ratio by the value of a candidate field - the gear test.

A real "engaged gear" field splits into a few discrete, non-overlapping ratio bands.
With the torque converter locked (3rd and up on this car) the bands are very narrow and their
ratios must match the gearbox: on the ZF 6HP26, 3rd->4th is 1.521 / 1.143 = 1.331.

    python3 tools/ratio_by_value.py samples/2026-09-05_city-drive.log.gz --field 0CB:4 --shift 5

Speed and rpm come from 0x146 and 0x361 (see dbc/). Samples below 5 km/h are ignored.
"""
import argparse
import statistics as st
from collections import defaultdict

import canlog


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('log')
    ap.add_argument('--field', required=True, help='ID:byte, e.g. 0CB:4')
    ap.add_argument('--shift', type=int, default=0, help='right-shift the byte (5 = top 3 bits)')
    a = ap.parse_args()
    fid, fbyte = a.field.split(':')
    fid, fbyte = int(fid, 16), int(fbyte)

    v = rpm = val = None
    groups = defaultdict(list)
    for t, cid, d in canlog.frames(a.log):
        if cid == 0x146 and len(d) >= 7:
            v = int.from_bytes(d[5:7], 'big') / 128
        elif cid == 0x361 and len(d) >= 4:
            rpm = int.from_bytes(d[2:4], 'big') * 17 / 16
        elif cid == fid and len(d) > fbyte:
            val = d[fbyte] >> a.shift
        else:
            continue
        if cid == 0x361 and v is not None and val is not None and v >= 5 and rpm > 500:
            groups[val].append(v / rpm * 1000)

    print('%-6s %8s %18s %8s' % ('value', 'median', 'interquartile', 'samples'))
    rows = []
    for k in sorted(groups):
        xs = sorted(groups[k])
        if len(xs) < 20:
            continue
        q1, med, q3 = xs[len(xs) // 4], st.median(xs), xs[3 * len(xs) // 4]
        rows.append((k, med))
        print('%-6s %8.2f %8.2f ~ %7.2f %8d   km/h per 1000 rpm' % (k, med, q1, q3, len(xs)))
    for (k1, m1), (k2, m2) in zip(rows, rows[1:]):
        print('step %s -> %s: %.3f' % (k1, k2, m2 / m1))


if __name__ == '__main__':
    main()
