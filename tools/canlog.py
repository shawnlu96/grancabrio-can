"""Read CAN logs: candump -L ("(1.234) can0 0CC#9D071AF38C"), optionally gzipped.

Also accepts the "F <ms> <ID> <len> <bytes...>" format of the original recorder.

    import canlog
    for t, cid, data in canlog.frames('samples/2026-09-05_city-drive.log.gz'):
        ...                      # t in seconds (float), cid int, data bytes
    by_id = canlog.load(path)    # {cid: [(t, data), ...]}
"""
import gzip
from collections import defaultdict


def _open(path):
    return gzip.open(path, 'rt', errors='replace') if path.endswith('.gz') else open(path, errors='replace')


def frames(path):
    with _open(path) as f:
        for line in f:
            if line.startswith('('):
                try:
                    ts, _ch, fr = line.split()[:3]
                    cid, data = fr.split('#', 1)
                    yield float(ts[1:-1]), int(cid, 16), bytes.fromhex(data)
                except ValueError:
                    continue
            elif line.startswith('F '):
                p = line.split()
                try:
                    n = int(p[3])
                    d = bytes(int(x, 16) for x in p[4:4 + n])
                    if len(d) == n:
                        yield int(p[1]) / 1000.0, int(p[2], 16), d
                except (ValueError, IndexError):
                    continue


def load(path):
    out = defaultdict(list)
    for t, cid, d in frames(path):
        out[cid].append((t, d))
    return out


def field(data, byte, width=1, endian='be', signed=False):
    """Pick a 1- or 2-byte field. None if the frame is too short."""
    if len(data) < byte + width:
        return None
    if width == 1:
        v = data[byte]
        return v - 256 if signed and v > 127 else v
    v = int.from_bytes(data[byte:byte + 2], 'big' if endian == 'be' else 'little')
    return v - 65536 if signed and v > 32767 else v
