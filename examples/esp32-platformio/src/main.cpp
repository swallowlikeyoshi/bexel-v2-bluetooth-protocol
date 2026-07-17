// LWS-1608 BMS reader for ESP32 (Arduino / NimBLE-Arduino).
//
// Connects to a BLE BMS advertising as "LWS-1608", performs the handshake,
// polls the summary (0x2A) and cell (0x24) frames, and prints decoded telemetry.
//
// Protocol: see ../../docs/PROTOCOL.md
// Read-only: this sketch never changes any BMS setting.
//
// License: Beerware (Revision 42) — see LICENSE

#include <NimBLEDevice.h>

// ─── Decoded telemetry ──────────────────────────────────────────────────────
struct bms_t {
  uint16_t pack_mv;        // total pack voltage (mV)
  int16_t  current_ma;     // current (mA); sign convention UNVERIFIED (see docs)
  uint16_t remain_mah;     // remaining capacity (mAh)
  uint16_t cell_mv[14];    // per-cell voltage (mV)
  uint8_t  soc, soh;       // %
  uint8_t  temp[4];        // °C, 4 sensors
  uint8_t  cycles;
};
static bms_t g;

// ─── Frame parser ───────────────────────────────────────────────────────────
// Feed raw bytes from FFE2 notifications one at a time. Robust against BLE
// notifications that split or concatenate frames: we sync on "3A 16", read the
// fixed length for the frame ID, and validate the "0D 0A" terminator.
static uint8_t buf[64];
static int     blen = 0;

static int frame_len(uint8_t id) {
  switch (id) {
    case 0x24: return 36;  // 14 cells
    case 0x2A: return 32;  // summary
    case 0x25: return 32;  // extended cells
    case 0x2B: return 32;  // protection/config
    case 0x10: return 15;  // max/min
    default:   return 0;   // unknown -> resync
  }
}

static void feed(uint8_t b) {
  if (blen == 0 && b != 0x3A) return;         // wait for start marker
  if (blen == 1 && b != 0x16) { blen = 0; return; }
  buf[blen++] = b;
  if (blen < 3) return;                       // need the ID byte
  int fl = frame_len(buf[2]);
  if (fl == 0) { blen = 0; return; }          // unknown ID
  if (blen < fl) return;                       // not complete yet
  if (buf[fl - 2] != 0x0D || buf[fl - 1] != 0x0A) { blen = 0; return; }

  switch (buf[2]) {
    case 0x2A:
      g.pack_mv    = buf[8]  | (buf[9]  << 8);
      g.current_ma = buf[10] | (buf[11] << 8);
      for (int i = 0; i < 4; i++) g.temp[i] = buf[12 + i];
      g.remain_mah = buf[16] | (buf[17] << 8);
      g.soc = buf[24]; g.soh = buf[25]; g.cycles = buf[26];
      Serial.printf("Pack %.2fV  I %dmA  SOC %d%%  SOH %d%%  T %d°C  %dmAh  cyc %d\n",
                    g.pack_mv / 1000.0, g.current_ma, g.soc, g.soh,
                    g.temp[0], g.remain_mah, g.cycles);
      break;
    case 0x24:
      for (int i = 0; i < 14; i++)
        g.cell_mv[i] = buf[4 + i * 2] | (buf[5 + i * 2] << 8);
      Serial.print("Cells:");
      for (int i = 0; i < 14; i++) Serial.printf(" %d", g.cell_mv[i]);
      Serial.println(" mV");
      break;
  }
  blen = 0;
}

// ─── BLE client ─────────────────────────────────────────────────────────────
static NimBLEUUID SVC("FFE0"), CH_NFY("FFE2"), CH_WR("FFE1");
static const NimBLEAdvertisedDevice* target = nullptr;
static NimBLERemoteCharacteristic*   g_wr   = nullptr;

static void onNotify(NimBLERemoteCharacteristic*, uint8_t* d, size_t n, bool) {
  for (size_t i = 0; i < n; i++) feed(d[i]);
}

// Build and send an 8-byte poll command for the given frame ID.
static void poll(uint8_t id) {
  uint8_t cksum = (0x16 + id) & 0xFF;
  uint8_t f[] = { 0x3A, 0x16, id, 0x00, cksum, 0x00, 0x0D, 0x0A };
  if (g_wr) g_wr->writeValue(f, sizeof(f), false);
}

class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* d) override {
    if (d->getName() == "LWS-1608" || d->isAdvertisingService(SVC)) {
      target = d;
      NimBLEDevice::getScan()->stop();
    }
  }
};

void setup() {
  Serial.begin(115200);
  NimBLEDevice::init("");

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(new ScanCB());
  scan->setActiveScan(true);
  scan->getResults(5000, false);
  if (!target) { Serial.println("LWS-1608 not found"); return; }

  NimBLEClient* cli = NimBLEDevice::createClient();
  if (!cli->connect(target)) { Serial.println("connect failed"); return; }

  NimBLERemoteService* svc = cli->getService(SVC);
  if (!svc) { Serial.println("service FFE0 not found"); return; }

  // 1) enable notifications on FFE2
  svc->getCharacteristic(CH_NFY)->subscribe(true, onNotify);
  g_wr = svc->getCharacteristic(CH_WR);

  // 2) handshake (see docs/PROTOCOL.md §2)
  uint8_t h1[] = { 0x00, 0xA1, 0xA2, 0xA3 };
  g_wr->writeValue(h1, sizeof(h1), false);
  delay(100);
  uint8_t h2[] = { 0xAA, 0x55, 0xF3, 0x01, 0x77, 0x77, 0x7C, 0x3F, 0x5A, 0xF3 };
  g_wr->writeValue(h2, sizeof(h2), false);
  delay(100);

  Serial.println("connected, polling...");
}

void loop() {
  // 3) poll the frames we care about; responses arrive async on FFE2
  poll(0x2A);  delay(200);   // summary: V / I / temp / capacity / SOC
  poll(0x24);  delay(800);   // 14 cell voltages
}
