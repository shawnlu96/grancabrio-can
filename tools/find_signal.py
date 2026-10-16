#!/usr/bin/env python3
"""Rank every byte / 16-bit field of every ID by correlation with a reference signal.

The reference can come from the same log (a signal you already decoded), e.g. vehicle speed:

    python3 tools/find_signal.py samples/2026-09-05_city-drive.log.gz --ref 146:5:be16/128
    python3 tools/find_signal.py LOG --ref 146:5:be16/128 --ref-min 1     # only while moving

or from a CSV of "seconds,value" lines on the same time base (--ref-csv), e.g. values your
head unit or an OBD reader logged.

--ref-min drops samples where the reference is below a threshold. This matters: the four
wheel speeds in 0x166 sit at an unrelated "invalid" value (0x002C) whenever the car is
stationary, which wrecks a plain correlation (r < 0.6). Correlating only while moving gives
r = 0.9998. Always look at the value distribution of a candidate before rejecting it.
"""
import argparse
import math
import sys
from collections import defaultdict

import canlog


def parse_ref(spec):
    # "146:5:be16/128" -> id 0x146, byte 5, big-endian 16 bit, divide by 128
    cid, byte, kind = spec.split(':')
    div = 1.0
    if '/' in kind:
        kind, d = kind.split('/')
        div = float(d)
    width = 2 if kind.endswith('16') else 1
    endian = 'le' if kind.startswith('le') else 'be'
    signed = kind.startswith('s')
    return int(cid, 16), int(byte), width, endian, signed, div


def bucketize(series, step):
    b = defaultdict(list)
    for t, v in series:
        b[int(t / step)].append(v)
    return {k: sorted(v)[len(v) // 2] for k, v in b.items()}


def pearson(xs, ys):
    n = len(xs)
    if n < 20:
        return 0.0
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    syy = sum((y - my) ** 2 for y in ys)
    if sxx == 0 or syy == 0:
        return 0.0
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / math.sqrt(sxx * syy)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('log')
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--ref', help='ID:byte:kind[/divisor], kind = u8 s8 be16 le16 sbe16 sle16')
    g.add_argument('--ref-csv', help='CSV of seconds,value')
    ap.add_argument('--ref-min', type=float, default=None, help='ignore samples where reference < this')
    ap.add_argument('--step', type=float, default=0.1, help='resampling step in seconds (default 0.1)')
    ap.add_argument('--top', type=int, default=20)
    a = ap.parse_args()

    by_id = canlog.load(a.log)
    if a.ref:
        cid, byte, width, endian, signed, div = parse_ref(a.ref)
        ref = [(t, canlog.field(d, byte, width, endian, signed) / div) for t, d in by_id.get(cid, [])
               if canlog.field(d, byte, width, endian, signed) is not None]
    else:
        ref = [tuple(map(float, l.split(',')[:2])) for l in open(a.ref_csv) if l.strip() and l[0].isdigit()]
    if not ref:
        sys.exit('reference signal is empty')
    rb = bucketize(ref, a.step)
    keep = {k for k, v in rb.items() if a.ref_min is None or v >= a.ref_min}

    res = []
    for cid, seq in by_id.items():
        n = min(len(d) for _, d in seq)
        cands = [(b, 1, 'be', False) for b in range(n)]
        cands += [(b, 2, e, s) for b in range(n - 1) for e in ('be', 'le') for s in (False, True)]
        for byte, width, endian, signed in cands:
            ser = [(t, canlog.field(d, byte, width, endian, signed)) for t, d in seq]
            fb = bucketize(ser, a.step)
            ks = sorted(keep & fb.keys())
            r = pearson([fb[k] for k in ks], [rb[k] for k in ks])
            if abs(r) > 0.3:
                res.append((abs(r), r, cid, byte, width, endian, signed, len(ks)))
    res.sort(key=lambda x: (x[0], not x[6]), reverse=True)   # on a tie prefer unsigned
    print('reference: %s, %d samples%s' % (a.ref or a.ref_csv, len(keep),
          ' (only where ref >= %g)' % a.ref_min if a.ref_min is not None else ''))
    print('%-5s %-6s %-7s %8s %7s' % ('ID', 'bytes', 'type', 'r', 'n'))
    seen = set()
    for _, r, cid, byte, width, endian, signed, n in res:
        key = (cid, byte, width)
        if key in seen:              # keep the best type per field
            continue
        seen.add(key)
        typ = 'u8' if width == 1 else ('s' if signed else '') + endian + '16'
        loc = str(byte) if width == 1 else '%d:%d' % (byte, byte + 1)
        print('%03X   %-6s %-7s %+8.4f %7d' % (cid, loc, typ, r, n))
        if len(seen) >= a.top:
            break


if __name__ == '__main__':
    main()
