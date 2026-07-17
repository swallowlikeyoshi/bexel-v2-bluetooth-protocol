#!/usr/bin/env python3
"""Read a LWS-1608 BMS over BLE and print live telemetry.

Cross-platform (macOS / Linux / Windows) via `bleak`.
Protocol: see ../../docs/PROTOCOL.md — read-only, changes no BMS setting.

Usage:
    pip install -r requirements.txt
    python read_bms.py

License: MIT
"""
import asyncio
import struct

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "LWS-1608"
SVC = "0000ffe0-0000-1000-8000-00805f9b34fb"
CH_WRITE = "0000ffe1-0000-1000-8000-00805f9b34fb"
CH_NOTIFY = "0000ffe2-0000-1000-8000-00805f9b34fb"

HANDSHAKE = [
    bytes([0x00, 0xA1, 0xA2, 0xA3]),
    bytes([0xAA, 0x55, 0xF3, 0x01, 0x77, 0x77, 0x7C, 0x3F, 0x5A, 0xF3]),
]

# fixed total frame length per frame ID (bytes, including 3A16 header and 0D0A)
FRAME_LEN = {0x24: 36, 0x2A: 32, 0x25: 32, 0x2B: 32, 0x10: 15}


def poll_cmd(frame_id: int) -> bytes:
    """Build an 8-byte poll command: 3A 16 <ID> 00 <cksum> 00 0D 0A."""
    cksum = (0x16 + frame_id) & 0xFF
    return bytes([0x3A, 0x16, frame_id, 0x00, cksum, 0x00, 0x0D, 0x0A])


class Parser:
    """Byte-stream frame parser, robust to split/concatenated notifications."""

    def __init__(self):
        self.buf = bytearray()

    def feed(self, data: bytes):
        for b in data:
            if len(self.buf) == 0 and b != 0x3A:
                continue
            if len(self.buf) == 1 and b != 0x16:
                self.buf.clear()
                continue
            self.buf.append(b)
            if len(self.buf) < 3:
                continue
            fl = FRAME_LEN.get(self.buf[2], 0)
            if fl == 0:
                self.buf.clear()
                continue
            if len(self.buf) < fl:
                continue
            if self.buf[fl - 2] != 0x0D or self.buf[fl - 1] != 0x0A:
                self.buf.clear()
                continue
            self._decode(bytes(self.buf))
            self.buf.clear()

    def _decode(self, f: bytes):
        fid = f[2]
        if fid == 0x2A:
            pack_mv = int.from_bytes(f[8:10], "little")
            current = int.from_bytes(f[10:12], "little", signed=True)  # sign unverified
            temps = list(f[12:16])
            remain = int.from_bytes(f[16:18], "little")
            soc, soh, cycles = f[24], f[25], f[26]
            print(f"Pack {pack_mv/1000:.2f}V  I {current}mA  SOC {soc}%  "
                  f"SOH {soh}%  T {temps[0]}°C  {remain}mAh  cyc {cycles}")
        elif fid == 0x24:
            cells = struct.unpack_from("<14H", f, 4)
            print("Cells (mV):", " ".join(str(c) for c in cells))


async def main():
    print(f"scanning for {DEVICE_NAME}...")
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
    if not dev:
        print("not found"); return

    parser = Parser()
    async with BleakClient(dev) as client:
        print("connected")
        await client.start_notify(CH_NOTIFY, lambda _, data: parser.feed(data))

        for hs in HANDSHAKE:                        # handshake
            await client.write_gatt_char(CH_WRITE, hs, response=False)
            await asyncio.sleep(0.1)

        while True:                                 # poll loop
            await client.write_gatt_char(CH_WRITE, poll_cmd(0x2A), response=False)
            await asyncio.sleep(0.2)
            await client.write_gatt_char(CH_WRITE, poll_cmd(0x24), response=False)
            await asyncio.sleep(0.8)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
