#!/usr/bin/env python3
"""Find coolant / oil temperature in a cold-start log by the SHAPE of the curve - no ground truth needed.

    python3 tools/find_temp.py samples/2026-09-02_cold-start-idle_1hz.log.gz --minutes 25

After a cold start a temperature climbs monotonically from ambient, then flattens at the
thermostat plateau. Among thousands of candidate bytes very few do that. Oil follows the same
shape, slower. Each candidate (every byte and 16-bit field, resampled to 1 Hz) is scored on:
  1. monotonicity   - share of steps that do not go down
  2. rise           - last minus first; tiny rises are noise
  3. plateau        - the last 20 % must be flat
  4. tail slope     - must be small relative to the overall slope. This is what rejects
                      monotonic COUNTERS (distance, fuel used): they also "rise smoothly", but
                      never stop. (The 0x560 distance counter got in this way once.)

Always run a warm-engine log through it as a control: it should find (almost) nothing there.
If it does, the thresholds are too loose. And plot the winners: two of our early candidates
scored well and converted to plausible temperatures, but the plot showed them jumping between
two values - status words, not temperatures.
"""
import argparse
import sys

import canlog

NO_PLATEAU = False


def resample(seq, bi, width, t0, t1, step=1.0):
    """One value per step (median inside the window, to kill jitter)."""
    n = int((t1 - t0) / step) + 1
    buckets = [[] for _ in range(n)]
    for t, d in seq:
        v = canlog.field(d, bi, width)
        if v is None:
            continue
        k = int((t - t0) / step)
        if 0 <= k < n:
            buckets[k].append(v)
    out, last = [], None
    for bk in buckets:
        if bk:
            bk.sort()
            last = bk[len(bk) // 2]
        out.append(last)
    return [x for x in out if x is not None]


def score(v, step=1.0):
    """(score, details). Higher = more like a warm-up curve."""
    n = len(v)
    if n * step < 60:
        return 0, None
    rise = v[-1] - v[0]
    if rise <= 0:
        return 0, None
    span = max(v) - min(v)
    if span < 8:
        return 0, None
    up = sum(1 for i in range(1, n) if v[i] >= v[i - 1]) / (n - 1)
    if up < 0.93:
        return 0, None
    tail = v[int(n * 0.8):]
    mt = sum(tail) / len(tail)
    sd = (sum((x - mt) ** 2 for x in tail) / len(tail)) ** 0.5
    if not NO_PLATEAU and sd > span * 0.12:
        return 0, None
    tail_slope = (tail[-1] - tail[0]) / max(len(tail) - 1, 1)
    total_slope = (v[-1] - v[0]) / (n - 1)
    if not NO_PLATEAU and total_slope > 0 and tail_slope / total_slope > 0.30:
        return 0, None
    front = (v[n // 2] - v[0]) / rise          # temperatures rise fast first, then slow down
    s = up * 100 + front * 50 + min(span, 120) * 0.3 - sd
    return s, dict(rise=rise, span=span, up=up, sd=sd, front=front, first=v[0], last=v[-1])


def as_temp(raw):
    out = []
    if 50 <= raw - 40 <= 125:
        out.append('raw-40=%d C' % (raw - 40))
    if 50 <= raw * 0.75 - 48 <= 125:
        out.append('raw*0.75-48=%.0f C' % (raw * 0.75 - 48))
    if 50 <= raw * 0.5 <= 125:
        out.append('raw*0.5=%.0f C' % (raw * 0.5))
    return ', '.join(out) if out else '-'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('log')
    ap.add_argument('--minutes', type=float, default=15)
    ap.add_argument('--top', type=int, default=20)
    ap.add_argument('--step', type=float, default=1.0,
                    help='resampling step in seconds; use ~10 on downsampled logs so the median smooths jitter')
    ap.add_argument('--no-plateau', action='store_true',
                    help='for logs that stop before the plateau; lets monotonic counters back in')
    a = ap.parse_args()
    global NO_PLATEAU
    NO_PLATEAU = a.no_plateau
    if NO_PLATEAU:
        print('plateau test OFF - monotonic counters (distance, fuel) will show up, check by hand\n')

    by_id = canlog.load(a.log)
    if not by_id:
        sys.exit('no frames')
    t0 = min(s[0][0] for s in by_id.values())
    t1 = min(t0 + a.minutes * 60, max(s[-1][0] for s in by_id.values()))
    print('%d IDs, window %.1f min\n' % (len(by_id), (t1 - t0) / 60))

    res = []
    for cid, seq in by_id.items():
        n = min(len(d) for _, d in seq)
        for width in (1, 2):
            for bi in range(n - width + 1):
                s, d = score(resample(seq, bi, width, t0, t1, a.step), a.step)
                if s > 0:
                    res.append((s, cid, bi, width, d))
    res.sort(reverse=True, key=lambda x: x[0])
    if not res:
        print('No field looks like a warm-up curve (expected for a warm-engine control run).')
        return 0
    print('%-4s %-6s %-6s %6s %6s %6s %6s  %s' % ('ID', 'byte', 'width', 'first', 'last', 'mono', 'tailsd', 'common encodings'))
    for s, cid, bi, width, d in res[:a.top]:
        print('%03X  %-6s %-6s %6d %6d %5.0f%% %6.1f  %s' % (
            cid, bi if width == 1 else '%d:%d' % (bi, bi + 1), '8' if width == 1 else '16',
            d['first'], d['last'], d['up'] * 100, d['sd'], as_temp(d['last']) if width == 1 else '-'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
