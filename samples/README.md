# Sample logs

Real recordings from the car, in `candump -L` format (gzipped):

```
(0.001) can0 0CC#9D071AF38C
```

They open directly in SavvyCAN, python-can (`can.LogReader`) and cantools
(`python3 -m cantools decode dbc/maserati_grancabrio_2016.dbc < log`). `tools/canlog.py` reads
them in Python.

| File | Content |
|---|---|
| `2026-09-05_city-drive.log.gz` | 9 min of city driving, 199 431 frames, all 33 IDs. Up to 41 km/h, gears 1–4, several full-lock turns, stops. |
| `2026-09-02_cold-start-idle_1hz.log.gz` | 29 min: ignition on, engine started after ~4 min, then idle. **Downsampled to one frame per ID per second.** Shows oil temperature climbing from ambient to its plateau. Use `--step 10` with `tools/find_temp.py`. |

## Caveats

- **Partial capture.** Both were recorded before the RX queue fix (see
  [`../docs/esp32-pitfalls.md`](../docs/esp32-pitfalls.md)), so they hold only about 30 % of the
  frames on the bus. A 48 Hz signal shows up at ~33 Hz. That is fine for decoding work, but not
  for precise timing.
- **Timestamps** are seconds since the first frame, not wall-clock time.
- **No vehicle identification.** Both logs were searched for the VIN, as ASCII inside single
  frames and across consecutive frames of the same ID. It does not appear.
