# Signals

Everything here was found on **one car**: a 2016 Maserati GranCabrio (M145 platform, 4.7 L F136
V8, ZF 6HP26 automatic), by listening to the CAN bus on the OBD-II port. Nothing was taken from
manufacturer documentation. Other GranTurismo / GranCabrio years and variants probably share
much of it — that is a guess until someone checks (please report what you find).

## The bus

| | |
|---|---|
| Pins | OBD-II 6 (CAN-H) / 14 (CAN-L) |
| Bit rate | 500 kbit/s, 11-bit identifiers |
| Traffic | 33 broadcast IDs with the engine running (36 seen with ignition on, engine off), about 1300 frames/s |
| Extended IDs | none seen |

IDs seen: `040 041 046 061 081 0AF 0CB 0CC 0DB 146 166 186 1A6 27B 361 3A1 44D 47B 4FB 560 561 566 56B 56C 56D 56F 57B 57D 581 584 586 5E0 6A0 6C0 6EF 761`.
The ones without an entry below are not decoded yet.

## Summary

| Signal | ID | Bytes | Encoding | Confidence |
|---|---|---|---|---|
| Steering wheel angle | `0x0CC` | 3:4 | signed BE, 0.1 °/count, centre −5, **+ = left** | verified on the car |
| Vehicle speed | `0x146` | 5:6 | BE, km/h = raw / 128 | high (three independent checks) |
| Vehicle speed (2nd source) | `0x566` | 2:3 | BE, km/h = raw / 16 | high |
| Wheel speeds ×4 | `0x166` | 0:1, 2:3, 4:5, 6:7 | BE, km/h = (raw − 8192) / 16; < 0x2000 = invalid | high; wheel order medium-high |
| Engine speed | `0x361` | 2:3 | BE, rpm = raw × 17 / 16 | high |
| Engine load | `0x361` | 1 | % = raw / 2.55 | medium-high |
| Gear engaged | `0x0CB` | 4, top 3 bits | 1–6 gear, 7 neutral, 0 engine not running | high (forward gears) |
| Brake pedal | `0x061` | 0, bit 4 | 1 = pressed | medium-high |
| Coolant temperature | `0x761` | 0 (1 = copy) | °C = raw − 40 | high |
| Engine oil temperature | `0x581` | 1 | °C = raw − 40 | ~90 % |
| Distance counter | `0x560` | 7 | 10 m / count | high (scale), medium (width) |

Machine-readable: [`../dbc/maserati_grancabrio_2016.dbc`](../dbc/maserati_grancabrio_2016.dbc).
Reference decoder in C++: [`../firmware/candec.h`](../firmware/candec.h). `firmware/test/run.sh`
checks that the two agree frame by frame on the sample logs.

---

## Steering wheel angle — `0x0CC` bytes 3:4

- 5-byte frame, 48 Hz.
- Signed 16-bit big-endian, 0.1° per count. Centre reads −5 counts; the DBC adds +0.5°.
- **Positive = turning left.**
- Full lock: +5449 / −5459 counts = ±545°, 3.03 turns lock to lock.
- Verified by turning the wheel lock to lock and holding known positions, and later by using it
  to drive the guide lines of a 360° parking camera — they follow the wheel.
- Derived, not measured: the 32.5° road-wheel angle comes from the 2942 mm wheelbase and the
  12.3 m turning circle. With that, the steering ratio is 545.4 / 32.5 ≈ 16.8 : 1.

## Vehicle speed — `0x146` bytes 5:6

km/h = raw / 128. Three checks that do not depend on each other:

1. **Against the head unit's own decoded speed:** r = 0.999, mean error 0.40 km/h.
2. **Gearbox ratio step:** with this speed and the rpm below, 3rd → 4th gives a speed/rpm step
   of 1.332. The ZF 6HP26 ratios give 1.521 / 1.143 = 1.331.
3. **Distance counter:** integrating speed over 274 s gave 1065.7 m for 106 counts of `0x560`,
   which is 10.05 m per count. With /130 it would be 9.90 m, with /124 10.38 m. Only /128
   gives a round number.

The same speed is repeated in `0x566` bytes 2:3 at a lower rate (/16, r = 0.997).

## Wheel speeds — `0x166`

- Four 16-bit big-endian values: km/h = (raw − 8192) / 16. Resolution 1/16 km/h.
- Each wheel matches vehicle speed at r = 0.9998, with a mean error of 0.19–0.25 km/h —
  better than the main speed signal.
- **Invalid state:** stationary, all four read `0x002C` (44). Below about 3 km/h single wheels
  can drop to it while the others are still valid. Treat any raw value < 0x2000 as invalid.
  The reference decoder rejects the whole frame then.
- This invalid state is why the signal was missed at first: correlating the whole log, a field
  that sits at 44 when stopped and around 8200–9100 when moving does not look like speed
  (r < 0.6). Correlate only while moving and it jumps to 0.9998. See
  [methodology](methodology.md#look-at-the-distribution-first).

**Which field is which wheel:** 0 = rear right, 1 = rear left, 2 = front right, 3 = front left.

- **Left / right — high confidence.** In left turns (positive steering angle), fields 0 and 2
  run 3–5 % faster than 1 and 3. So 0 and 2 are the outer wheels, i.e. the right side.
- **Front / rear — medium-high confidence.** On one side the front wheel follows a larger radius
  than the rear one, so it turns faster. Field 2 is faster than 0 in left turns, and 3 is
  faster than 1 in right turns, so 2 and 3 are the fronts. On straights, (0, 1) and (2, 3) pair
  up with a steady 0.15 % offset, as expected from axles with different tyre sizes.
- **Not yet done:** a hard launch, where the driven rear wheels should spin faster, would
  confirm front / rear directly.

## Engine speed and load — `0x361`

- **Engine speed, bytes 2:3 big-endian:** rpm = raw × 17 / 16. Mean error is 21 rpm with the
  17/16 factor and 74 rpm with factor 1.
  - Why 17/16 is unknown. The gearbox ratio check above depends on it and matches, which
    suggests the factor is right.
- **Engine load, byte 1, % = raw / 2.55.** Idle reads 52 (20 %), coasting 31, heavy throttle
  around 130. The idle figure is typical for a large V8, which makes this look like calculated
  load rather than raw pedal position (a pedal would read near 0 at idle).
- **Engine not running:** `0x361` is not broadcast at all.

## Gear — `0x0CB` byte 4, top 3 bits

The head unit does not decode the gear, so there was nothing to compare against. Instead: for
each value of a candidate field, look at the speed/rpm ratio while that value is present. A real
gear field splits into discrete, non-overlapping ratio bands (tool: `tools/ratio_by_value.py`).

Byte 4 values and their ratio bands:

| byte 4 | top 3 bits | km/h per 1000 rpm | interquartile range | |
|---|---|---|---|---|
| `0x30` | 1 | 7.40 | 7.17–9.40 | 1st (converter slipping, wide band) |
| `0x50` | 2 | 12.74 | 10.43–13.85 | 2nd |
| `0x70` | 3 | 21.63 | 20.82–22.09 | 3rd (converter locked, narrow band) |
| `0x90` | 4 | 28.82 | 28.50–29.00 | 4th (locked) |
| `0xF0` | 7 | — | only at standstill | neutral / no gear |

- **Gearbox ratio check:** 4th / 3rd = 28.82 / 21.63 = 1.332. The ZF 6HP26 step is 1.331. This
  run was 2026-08-27, at higher speeds.
- **City sample:** the included city-drive sample is slower; there it gives 1.313, because 4th
  barely appears and the converter is less often locked.
- **Value 0** appears while the engine is not running.
- **Bit 4** is normally set. `0x60` (3rd, bit 4 clear) also occurs; its meaning is unknown.
- **Not covered:** reverse, 5th and 6th gear have not been captured yet.

## Brake pedal — `0x061` byte 0, bit 4

There was no ground truth for the brake either. The test used is that the two pedals are never
pressed together:

- With load > 100 (clearly on the throttle), the bit was set in **0 of 327** samples.
- With load < 35 (coasting), it was set in 36 % of samples. If the two were unrelated, about
  118 overlaps would be expected in the first group.
- Mean acceleration is −1.81 km/h/s with the bit set and +1.10 with it clear.
- The bit is set 100 % of the time at standstill (an automatic waiting at a light is held on the
  brake). This means **"brake OR stopped" is not ruled out**. Settling it needs a log in P/N,
  stationary, foot off the brake.
- The same bit also appears in `0x146` byte 0 bit 3 and `0x3A1` byte 2 bit 4.

## Coolant temperature — `0x761` byte 0

- **Encoding:** °C = raw − 40, the standard OBD-II encoding. Byte 1 is a copy (they differ by at
  most 1 °C). Byte 2 is a status value (16 / 48 / 112), bytes 3–7 are zero.
- **Warm-up curve after 1 h 42 min parked:** 73 → 80 → 86 → 89 °C, then a plateau at 87–89 °C
  (thermostat open), a peak of 100 °C under load, and back to 90 °C. The plain raw − 40
  encoding lands on physically sensible numbers without any fitting.
- **Dashboard:** the only temperature gauge (130 °C full scale) read about 90 at the same time.

## Engine oil temperature — `0x581` byte 1

- **Encoding:** °C = raw − 40. The same value is repeated in `0x561` byte 3, `0x061` byte 3 and
  `0x081` byte 4. The ECU does not answer OBD PID 0x5C (oil temperature) on this car, so the
  broadcast is the only source.
- **Found in a 25-minute idle log after a start** (sample included):
  1. It starts at 27 °C, which was the ambient temperature — not 0 like a counter.
  2. It lags far behind coolant: coolant was at 74–84 °C within minutes, this one took about
     10 minutes to reach ~84 °C.
  3. Then it flattens (84 → 87 °C over the next 15 minutes). A counter would keep climbing.
  4. It settles a bit above coolant, as oil normally does.
  5. The four copies move in lockstep.
- **Engine oil rather than gearbox oil — later drives:** it rises more slowly than coolant,
  plateaus next to coolant, and does not follow coolant spikes. At 82 km/h under load it stays
  pinned at 87–90 °C, which fits an engine oil cooler with a thermostat. Gearbox oil is a
  separate circuit and would drift with load.
- **Unexplained:** in the cold-start log, coolant already read 74 °C when the engine started,
  while this signal read 27 °C. Oil cooling to ambient much faster than coolant is not obviously
  right. This is part of why the confidence is ~90 % and not higher.
- **Confidence:** ~90 %. A factory scan tool reading next to it would settle it.

## Distance counter — `0x560` byte 7

- **Scale:** +1 per 10 m (measured 10.05 m per count, see vehicle speed above). Use
  differences; the absolute value has no meaning.
- **Byte 6 holds flags, not higher counter bits:**
  - `0x04` and `0x08` flicker briefly while driving.
  - `0x20` is set while the counter is reset / not running: during a whole stationary idle, and
    at the end of a drive with byte 7 back at 0.
  - Reading bytes 6:7 as one 16-bit counter loses about 10 % of the distance: every flag flicker
    looks like a jump of 1024 or 2048 counts and gets discarded as a glitch.
- **Width unknown:** it is not known whether byte 7 wraps at 256 or carries into byte 6 bits
  0–1. The logs never went past 0xB0 (1.76 km) and those two bits were never seen set. The
  reference decoder assumes 8 bits and unwraps.

---

## Open questions

- Reverse gear, 5th, 6th (`0x0CB`).
- Brake bit: pedal, or "pedal OR stationary"?
- Origin of the 17/16 rpm factor.
- Wheel order front/rear, by a direct test.
- `0x560` counter width.
- Everything not listed: 23 IDs untouched. The head unit on this car also shows doors, seat
  belt, handbrake, turn signals, outside temperature, total mileage and parking sensors. It gets
  them from some bus; whether that is this one is unknown.
