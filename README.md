# grancabrio-can

CAN bus decoding for the **2016 Maserati GranCabrio** (M145, 4.7 L V8, ZF 6HP26). It contains:

- the broadcast frames we reverse-engineered, as a [DBC file](dbc/maserati_grancabrio_2016.dbc);
- the evidence and confidence for each one;
- the methods used to find them, with tools you can run on real sample logs;
- a passive ESP32 reader that decodes everything live.

Standard OBD-II queries do not give you steering angle, individual wheel speeds, the engaged gear,
the brake pedal or (on this car) oil temperature. The car broadcasts all of them on the OBD port,
in a format the manufacturer does not publish. We could not find any public decoding for this
model, so here is ours.

[中文说明](README.zh-CN.md)

## What is decoded

| Signal | ID | Confidence |
|---|---|---|
| Steering wheel angle (0.1°, + = left), 48 Hz | `0x0CC` | verified on the car |
| Vehicle speed | `0x146`, `0x566` | high — three independent checks |
| Four wheel speeds (1/16 km/h) | `0x166` | high; front/rear assignment medium-high |
| Engine speed, engine load | `0x361` | high / medium-high |
| Gear actually engaged | `0x0CB` | high for 1st–4th; reverse, 5th, 6th not yet seen |
| Brake pedal | `0x061` | medium-high |
| Coolant temperature | `0x761` | high |
| Engine oil temperature | `0x581` | ~90 % |
| Distance counter (10 m) | `0x560` | high |

Details, evidence and open questions: **[docs/signals.md](docs/signals.md)**.
Verified on one car only. Other GranTurismo / GranCabrio years probably match, but that is not
checked yet — see [below](#other-cars).

## Repository

```
dbc/        DBC file — open it in SavvyCAN, cantools, python-can, ...
docs/       signals.md       every signal, how it was confirmed, how sure we are
            methodology.md   how to find signals with and without ground truth
            hardware.md      wiring, power, listen-only
            esp32-pitfalls.md  things that silently go wrong on ESP32 / ESPHome
samples/    real logs from the car (candump format) to try everything on
tools/      find_signal.py   rank every field by correlation with a reference
            find_temp.py     find temperatures by the shape of a warm-up curve
            ratio_by_value.py  the gear test: speed/rpm bands per value
            serial_capture.py  record the ESP32's frame stream on a laptop
firmware/   ESPHome config + C++ for an ESP32-S3: live decoding, web page,
            optional raw log over USB, unit tests and replay against the samples
```

## Quick start

Decode a sample log with the DBC:

```sh
pip install cantools
gzip -dc samples/2026-09-05_city-drive.log.gz | head -50 | \
  python3 -m cantools decode --single-line dbc/maserati_grancabrio_2016.dbc
# (0.001) can0 0CC#9D071AF38C :: STEERING_0CC(SteeringWheelAngle: -318.3 deg)
```

Reproduce a finding — the wheel speeds only correlate with vehicle speed once the stationary
"invalid" state is filtered out:

```sh
python3 tools/find_signal.py samples/2026-09-05_city-drive.log.gz --ref 146:5:be16/128 --ref-min 1
```

Build the reader (needs [ESPHome](https://esphome.io)), after reading
[docs/hardware.md](docs/hardware.md):

```sh
cd firmware
cp secrets.yaml.example secrets.yaml   # fill it in
esphome run grancabrio-can.yaml
```

Run the tests (no board needed):

```sh
pip install cantools     # for the DBC cross-check
bash firmware/test/run.sh
```

## Safety

- **Read-only.** The firmware runs the CAN controller in listen-only mode: it transmits nothing,
  not even ACK bits. This project documents what the car says, not how to talk to it.
- **Power.** Power the reader from a USB port that switches off with the ignition, never from
  OBD pin 16 (permanent battery voltage) — that drains the battery.
- **No warranty.** Wrong wiring on a car bus can cause faults. This is the work of one owner on
  one car, offered as-is (see the licenses). You are responsible for what you connect to your car.

## Other cars

If you have a GranTurismo, GranCabrio or Quattroporte of a similar era, a few minutes of
`candump` from the OBD port would tell whether these IDs match. Open an issue with the model,
year and a short log (the samples show the format), or just "matches / doesn't match".

This is a personal project shared as-is. Issues and pull requests are welcome, but there is no
promise of support or response time.

## Background

This started as an attempt to make the parking-camera guide lines of an aftermarket Android head
unit follow the steering wheel. The car never sends the steering angle to the head unit. Finding
it on the bus led to everything else here.

## License

Code (`firmware/`, `tools/`): MIT, see [LICENSE](LICENSE).
Signal definitions, documentation and sample logs (`dbc/`, `docs/`, `samples/`): CC BY 4.0, see
[LICENSE-DATA](LICENSE-DATA).

Maserati, GranCabrio and GranTurismo are trademarks of their owner. This project is not
affiliated with or endorsed by Maserati.
