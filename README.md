# Bexel V2 Bluetooth Protocol (LWS-1608 BMS)

Open documentation and example code for reading the **BEXEL / W-Works Korea
"자작자동차 2세대" (V2)** battery pack's smart BMS over Bluetooth Low Energy.

The pack's Bluetooth module advertises as **`LWS-1608`**, is paired with the
iOS/Android app **`smartbms-Lion1`**, and its PCB is marked
[`gspbattery.com`](https://www.gspbattery.com) — otherwise undocumented. The same
BMS appears in other generic lithium packs, so this may work beyond the Bexel V2.

Pack reference specs (14S16P, 51.8 V, 80 Ah): [`docs/BATTERY.md`](docs/BATTERY.md).

If your pack advertises as **`LWS-1608`** over BLE and its app is
**`smartbms-Lion1`**, this repo lets you read State-of-Charge, per-cell voltages,
current, temperature and remaining capacity **without the phone app** — e.g. to
drive your own dashboard/cluster or data logger.

> ⚠️ **Read-only telemetry, unofficial.** This protocol was reverse-engineered by
> capturing the app's own BLE traffic. It is **not** an official spec. The example
> code only *reads* data — it does not change any BMS setting. The BMS's own
> protection logic is what keeps your pack safe; nothing here replaces it. Batteries
> store dangerous amounts of energy — use at your own risk. See
> [`docs/PROTOCOL.md`](docs/PROTOCOL.md) for what is and isn't verified.

---

## What's here

| Path | Description |
|------|-------------|
| [`docs/PROTOCOL.md`](docs/PROTOCOL.md) | Full BLE + frame protocol spec (services, handshake, poll commands, frame layouts, checksum) |
| [`examples/esp32-platformio/`](examples/esp32-platformio/) | ESP32 (Arduino/PlatformIO + NimBLE) client that prints live telemetry |
| [`examples/python/`](examples/python/) | Cross-platform Python reader using [`bleak`](https://github.com/hbldh/bleak) |
| [`docs/BATTERY.md`](docs/BATTERY.md) | Bexel V2 pack reference specs (cell, pack, connectors) |

## The 30-second version

The BMS exposes a transparent BLE-UART bridge on service **`FFE0`**:

- **`FFE1`** — write (commands to the BMS)
- **`FFE2`** — notify (responses from the BMS)

It is **request/response**, not a broadcast — you must poll it. After connecting:

1. Enable notifications on `FFE2`.
2. Send two handshake writes to `FFE1`: `00 A1 A2 A3`, then `AA 55 F3 01 77 77 7C 3F 5A F3`.
3. Poll each frame you want, e.g. summary: `3A 16 2A 00 40 00 0D 0A`.
4. Parse the matching `3A 16 2A 18 …` response off `FFE2`.

All multi-byte values are **little-endian**. The two most useful frames:

- **`0x2A`** — total voltage, current, temperatures, remaining capacity, SOC, SOH, cycles
- **`0x24`** — 14 individual cell voltages (mV)

Full details, including the poll-command checksum and every observed frame, are in
[`docs/PROTOCOL.md`](docs/PROTOCOL.md).

## Quick start (ESP32)

```bash
cd examples/esp32-platformio
pio run -t upload && pio device monitor
```

Expected serial output:

```
Pack 57.40V I 0mA SOC 49% T 23°C 39691mAh
```

## Quick start (Python)

```bash
cd examples/python
pip install -r requirements.txt
python read_bms.py
```

## Contributing

Some frames (`0x2B`, `0x25`) and the post-handshake device-info dumps are only
partially decoded — see the open questions at the bottom of
[`docs/PROTOCOL.md`](docs/PROTOCOL.md). If you have a pack with a different cell
count (this one is 14S) or can help finish decoding, PRs and BLE captures are very
welcome.

## License

[Beerware (Revision 42)](LICENSE) — do whatever you want; keep the notice, and
buy me a beer if we ever meet. No warranty.
