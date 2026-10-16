# How the signals were found

None of this needs manufacturer data. Two families of methods: **compare against a ground truth**
when you have one, and **use physics** when you don't. Every finding in
[signals.md](signals.md) came from one of the recipes below. Each recipe has a tool in
[`../tools`](../tools) that you can run on the [sample logs](../samples).

## 1. Ground truth + correlation

If anything already decodes the value you are after, record it alongside the raw frames.
Correlate every byte and every 16-bit field (both byte orders, signed and unsigned) against it.
Possible sources:

- a standard OBD-II PID;
- a phone GPS;
- an aftermarket Android head unit. Many of them log the vehicle data they decode to a text
  file with millisecond timestamps — this one did, for speed and rpm.

```
python3 tools/find_signal.py LOG --ref-csv truth.csv          # seconds,value per line
python3 tools/find_signal.py LOG --ref 146:5:be16/128         # or a signal you already decoded
```

Lessons that cost time:

- **One time base for everything.** Find the lag between the two recordings once, using the
  signal that changes most sharply (rpm). Then reuse that lag for every other signal. Two
  series from the same capture have the same lag; fitting the lag per signal produced offsets
  that made no sense.
- **The value must actually change.** A 249-second log with the car mostly parked could not find
  speed at all, while rpm from the same log correlated at 0.993. Using a log with real
  acceleration and braking gave 0.999 immediately. When a search fails, check the variance of
  the reference first.
- **Sanity-check your maths.** An FFT cross-correlation with wrong normalisation happily reports
  "correlations" above 1 and a nonsense lag.

## 2. Look at the distribution first

The four wheel speeds in `0x166` were missed on the first full scan, which concluded "no wheel
speeds on this bus". They had been there all along. When the car is stationary the fields read
`0x002C` (44); when moving, they read `0x2000 + 16 × km/h` (8200–9100). A field that sits
at two unrelated levels destroys a linear correlation.

```
python3 tools/find_signal.py samples/2026-09-05_city-drive.log.gz --ref 146:5:be16/128
#   -> 0x166 is not even in the top 12
python3 tools/find_signal.py samples/2026-09-05_city-drive.log.gz --ref 146:5:be16/128 --ref-min 1
#   -> all four 0x166 fields at r = 0.9995
```

Before declaring a candidate unrelated, histogram it. Remove invalid / sentinel states, then
correlate again.

## 3. Physics instead of ground truth

### Gear: ratio bands

For each value of a candidate byte, collect speed/rpm while that value is present. A real gear
field gives discrete, non-overlapping bands. Gears where the torque converter is locked give very
narrow bands whose steps must match the gearbox ratios: 3rd→4th measured 1.332, and the
ZF 6HP26 step is 1.331. This one check also validates the speed /128 and rpm ×17/16 factors —
if either were off, the ratio step would not land on the real gear ratios.

```
python3 tools/ratio_by_value.py samples/2026-09-05_city-drive.log.gz --field 0CB:4 --shift 5
```

### Distance: the car's own counter

A counter that ticks with distance gives a speed-independent check of the speed scale.
Integrate speed over a window and divide by the number of ticks. /128 gave 10.05 m per tick,
which is obviously 10 m. The alternatives gave 9.90 or 10.38 m.

### Brake: two pedals are not pressed together

There was no ground truth for the brake. A candidate bit that is never set while engine load is
high (0 of 327 samples), yet is set 36 % of the time while coasting, is very unlikely to be
anything else. The overlap expected if the two were unrelated was about 118 samples.

### Temperatures: the shape of a warm-up curve

There was also nothing to correlate against for temperatures. After a cold start, a
temperature climbs monotonically from ambient, then flattens at the thermostat plateau. Very
few of the thousands of candidate bytes do that.

```
python3 tools/find_temp.py samples/2026-09-02_cold-start-idle_1hz.log.gz --minutes 29 --step 10
```

On the sample, the 8-bit rows near the top are the four copies of oil temperature (`061.3`,
`081.4`, `561.3`, `581.1`), all 67 → 127 raw = 27 → 87 °C. The 16-bit rows above them are noise.

- **Monotonic counters** (distance, fuel used) pass a naive version of this test. What rejects
  them is the **tail slope**: a temperature stops rising and a counter does not.
- **Run a warm-engine log as a control.** It should find (almost) nothing. If it finds a lot, the
  thresholds are too loose.
- **Plot the winners.** Two early candidates scored well and converted to plausible
  temperatures (90 °C, 110 °C). Plotted, they jumped between two fixed values: status words, not
  temperatures. Scores narrow thousands of candidates down to a handful; the plot decides.

### Wheel order: turning geometry

- **Left and right:** in a turn the outer wheels are faster, and the steering angle sign is
  known.
- **Front and rear:** on one side, the front wheel follows a larger radius than the rear, so it
  is faster in turns.
- **Axle pairs:** on straights, wheels on the same axle pair up with a constant offset (different
  tyre sizes front and rear).

## 4. "Not found" does not mean "not there"

On this project the conclusion "signal X is not on this bus" was written down twice, and was
wrong both times:

| Signal | Why it was missed |
|---|---|
| Wheel speeds | the invalid stationary state ruined the correlation |
| Oil temperature | the first look was 8 minutes of a warm engine; a 25-minute idle from a start showed it plainly |

The honest form of a negative result is "not observed under these conditions, in this window".

## 5. Recording quality

The sample logs were recorded with the ESP32's CAN receive queue at its default of 5 frames
and contain only about 30 % of the bus traffic. Periodic IDs that should arrive at 48 Hz show up
at ~33 Hz. The analysis methods above are robust to that, but timing-sensitive work such as shift
durations needs full-rate logs. To check a logger, compare the interval of a known periodic ID
against its nominal rate, and watch the controller's "RX missed" counter. See
[esp32-pitfalls.md](esp32-pitfalls.md).
