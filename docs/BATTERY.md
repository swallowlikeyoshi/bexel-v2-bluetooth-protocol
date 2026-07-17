# Bexel V2 Pack — Reference Specs

Factual specifications of the pack this protocol was captured from, for context.
Figures are from the manufacturer's datasheet (BEXEL / W-Works Korea,
"더블유웍스 자작자동차 2세대 배터리팩"). Only factual data is reproduced here — no
manufacturer document is redistributed.

## Cell

| | |
|---|---|
| Model | BEXEL INR21700-50E (NMC / "ternary lithium") |
| Nominal voltage | 3.7 V |
| Capacity | 5.0 Ah |
| Size | 21.25 × 70.65 mm |
| Weight | 72 g |

## Pack

| | |
|---|---|
| Configuration | **14S16P** |
| Nominal | 51.8 V, 80.0 Ah (4.14 kWh) |
| Size | 195 × 440 × 150 mm (excl. handles/terminals) |
| Weight | ~18 kg (incl. SUS case) |
| Charge current | 39.2 A (0.5C) |
| Discharge — continuous | 157 A (2C) |
| Discharge — peak (<5 s) | 235 A (3C) |
| Output connector | Terminal Block, M8, 600 VDC / 300 A continuous |
| Protection | Built-in BMS |
| Wired comms | **RS-485** (+ Bluetooth module — this repo) |
| Features | BMS, info display (SoC/current/voltage), ON/OFF switch, Bluetooth |

## Notes

- The **14S** series count matches the `0x24` cell frame (14 cell voltages).
- The pack also exposes an **RS-485** port (A/B) at the same protocol layer; the
  Bluetooth module reverse-engineered here is a BLE-UART bridge in front of the
  same BMS, which is why the framing (`3A 16 … 0D 0A`) is likely shared. RS-485
  captures to confirm this are welcome.

**Source:** BEXEL/W-Works Korea 2nd-gen homebuilt-EV battery pack datasheet, and
work instruction `BEXEL-PACK-HOW-008` Rev 00 (2026-07-07). Documents not
redistributed; cited for provenance only.
