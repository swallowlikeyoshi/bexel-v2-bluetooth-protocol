# LWS-1608 BMS — BLE Protocol Specification

Reverse-engineered from BLE captures of the **`smartbms-Lion1`** app talking to a
**14S** pack advertising as **`LWS-1608`** (PCB marked `gspbattery.com`).

**Confidence legend:** ✅ verified against the app's own displayed values ·
🟡 partially decoded · ❓ unknown / not yet decoded.

Everything multi-byte is **little-endian** unless stated otherwise.

---

## 1. BLE layout

The BMS presents a transparent BLE-UART bridge. All framed data below is carried
as the "UART" payload over these GATT characteristics:

| Service | Characteristic | Properties | Role |
|---------|----------------|------------|------|
| `FFE0`  | `FFE1` | Write / Write-Without-Response | Host → BMS (commands) |
| `FFE0`  | `FFE2` | Notify | BMS → Host (responses) |
| `FFE0`  | `FF03` | Write / Notify | Unused by app for telemetry |
| `AE00`  | `AE01` / `AE02` | — | Present but unused for telemetry |

The device name is `LWS-1608`. Only one telemetry path is needed: **write to
`FFE1`, receive notifications on `FFE2`.**

> **Important:** the BMS does **not** stream on its own. It only answers when
> polled. If you subscribe to `FFE2` but never write, you get nothing (this is why
> a passive BLE scanner sees silence until the phone app — which polls — connects).

---

## 2. Connection sequence

Exactly what the app does after connecting, in order:

1. **Enable notifications** on `FFE2` (write `01 00` to its CCCD `0x2902`; BLE
   stacks like NimBLE / bleak do this for you via `subscribe`/`start_notify`).
2. **Handshake #1** → write to `FFE1`:
   ```
   00 A1 A2 A3
   ```
   The BMS replies on `FFE2` with a device-info dump (see §5). 🟡
3. **Handshake #2** → write to `FFE1`:
   ```
   AA 55 F3 01 77 77 7C 3F 5A F3
   ```
   The BMS replies with a second device-info/config dump. 🟡
4. **Poll** the frames you want (see §3), repeatedly, for as long as you want
   fresh data. The app polls continuously at ~1 Hz.

In practice the handshakes appear optional for pure polling, but replicating them
exactly is the safe choice.

---

## 3. Poll commands (Host → BMS, `FFE1`)

A poll command is **8 bytes**:

```
3A 16 <ID> 00 <CKSUM> 00 0D 0A
```

- `3A 16` — fixed start marker
- `<ID>` — the frame you are requesting
- `<CKSUM>` = `(0x16 + ID) & 0xFF`
- terminated by `0D 0A` (CR LF)

Observed poll commands:

| Request bytes | ID | Requests |
|---|---|---|
| `3A 16 2A 00 40 00 0D 0A` | `0x2A` | Summary (V/I/temp/capacity/SOC) — **most useful** |
| `3A 16 24 00 3A 00 0D 0A` | `0x24` | 14 cell voltages — **most useful** |
| `3A 16 10 00 26 00 0D 0A` | `0x10` | Max/min cell |
| `3A 16 2B 00 41 00 0D 0A` | `0x2B` | Protection/config params 🟡 |
| `3A 16 25 00 3B 00 0D 0A` | `0x25` | Extended cells (15+) 🟡 |

---

## 4. Response frames (BMS → Host, `FFE2`)

Every response starts with `3A 16` and ends with `0D 0A`. The 3rd byte is the
frame ID. **Frame lengths are fixed per ID**, which is the robust way to
frame-sync (a single BLE notification may split or concatenate frames — do not
assume one notification = one frame):

| ID | Total length | Contents |
|----|--------------|----------|
| `0x2A` | 32 bytes | Summary ✅ |
| `0x24` | 36 bytes | 14 cell voltages ✅ |
| `0x10` | 15 bytes | Max/min cell ✅ |
| `0x25` | 32 bytes | Extended cells 🟡 |
| `0x2B` | 32 bytes | Protection/config 🟡 |

The byte just before `0D 0A` appears to be a checksum; its exact algorithm is
**not verified**. Framing (start marker + fixed length + `0D 0A`) is sufficient
and is what the example parsers rely on.

### 4.1 `0x2A` — Summary ✅

Example: `3A16 2A18 0000 0000 36E0 0000 1717 1716 0B9B 0000 8080 8080 3164 0000 0405 0D0A`

| Offset | Bytes (example) | Field | Decode | Unit |
|--------|-----------------|-------|--------|------|
| 0  | `3A 16 2A 18` | header + ID | — | |
| 4  | `00 00 00 00` | reserved / power ❓ | 0 | |
| 8  | `36 E0` | **Total pack voltage** | `0xE036` = 57398 | mV |
| 10 | `00 00` | **Current** (sign unverified, see §6) | 0 | mA |
| 12 | `17 17 17 16` | **Temperatures** (1 byte each, 4 sensors) | 23,23,23,22 | °C |
| 16 | `0B 9B` | **Remaining capacity** | `0x9B0B` = 39691 | mAh |
| 18 | `00 00` | reserved ❓ | | |
| 20 | `80 80 80 80` | status flags ❓ | | |
| 24 | `31` | **SOC** | `0x31` = 49 | % |
| 25 | `64` | **SOH** | `0x64` = 100 | % |
| 26 | `00 00` | **Cycle count** | 0 | |
| 28 | `04 05` | checksum ❓ | | |
| 30 | `0D 0A` | terminator | | |

### 4.2 `0x24` — Cell voltages ✅

Example: `3A16 2418 0310 0310 0310 0410 0410 0410 0410 0310 0410 0310 0310 0410 3C01 0D0A`

```
3A 16 24 18 | [cell1 LE16] [cell2] … [cell14] | [checksum ❓ 2B] | 0D 0A
```

- 14 consecutive little-endian uint16 values, each a cell voltage in **mV**.
- `03 10` → `0x1003` = 4099 mV, `04 10` → `0x1004` = 4100 mV, etc.
- For packs with a different series count, expect the count to change; verify
  against your app's per-cell screen.

### 4.3 `0x10` — Max/min cell ✅

Example: `3A16 1007 0410 0703 100E 0E77 000D 0A` →
`3A 16 10 07 | 04 10 | 07 | 03 10 | 0E | 0E | 77 | 00 | 0D 0A`

| Field | Bytes | Decode |
|-------|-------|--------|
| Max cell voltage | `04 10` | 4100 mV |
| Max cell index | `07` | cell 7 |
| Min cell voltage | `03 10` | 4099 mV |
| Min cell index | `0E` | cell 14 |
| Cell count | `0E` | 14 |
| checksum ❓ | `77` | |

(Note: this frame's header is `3A 16 10 07`, not `…18`.) All of this is derivable
from `0x24`, so `0x10` is optional.

### 4.4 `0x25` — Extended cells 🟡

Example (14S pack): `3A16 2518 0310 0310 0000 …(zeros)… 7900 0D0A`

Appears to carry cells 15+ for higher-series packs; mostly zero on a 14S unit.
Not needed if you already parse `0x24`.

### 4.5 `0x2B` — Protection / configuration 🟡

Example: `3A16 2B18 0035 0C00 6CE8 0000 8038 0100 6CE8 0000 B80B 1000 0124 0000 F304 0D0A`

Looks like static protection thresholds / design parameters. Individual fields are
not yet reliably mapped. Contributions welcome.

---

## 5. Post-handshake device-info dumps 🟡

Sent once, right after each handshake write. These do **not** use the `3A 16 …
0D 0A` framing — they have their own symmetric markers:

- After `00 A1 A2 A3`:
  ```
  3F 55 68 … 68 55 3F
  ```
- After `AA 55 F3 01 77 77 7C 3F 5A F3`:
  ```
  F3 5A 3F 01 … 3F 5A F3
  ```

They contain static device info / configuration (design capacity, cell count,
protection setpoints). Not required for live telemetry; parsers should simply
ignore any notification that does not start with `3A 16`.

---

## 6. Open questions / help wanted

- **Current sign** — every capture so far had current = 0 A. The sign convention
  (charge negative vs. positive) and scaling need confirmation under real load.
- **Checksum algorithm** for response frames (the byte before `0D 0A`).
- **`0x2B` / `0x25`** field mapping.
- **Device-info dumps** (§5) field mapping.
- **Non-14S packs** — do frame IDs/lengths change with series count?

If you can help, please open an issue with a raw BLE capture
(`nRF Connect` log, Android HCI snoop, or Apple PacketLogger `.pklg`).
