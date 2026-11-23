// BLE man-in-the-middle logger for the GC2RB battery Bluetooth module.
//
// The board connects to the real module as a BLE client, then advertises a clone
// (same name, same 0xFFE0 service) so the official phone app connects to the board
// instead. Every write the app makes and every notification the module sends is
// forwarded and logged over serial. That log is the protocol.
//
// Nothing is written to the module except what the official app itself writes.
// The TI firmware-update service (f000ffc0) is deliberately not mirrored.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <algorithm>

static const char *MATCHES[] = {"asgc2rb", "gc2rb"};  // lower case substring match
static const uint16_t SVC_UUID = 0xFFE0;
static const int NCHAR = 6;  // 0xFFE1 .. 0xFFE6

struct Msg {
  uint8_t dir;  // 0 = app to module (write), 1 = module to app (notify), 2 = app read
  uint8_t idx;  // 1..6 for FFE1..FFE6
  uint16_t len;
  uint8_t data[244];
};

static QueueHandle_t q;
static NimBLEServer *server;
static NimBLEClient *real = nullptr;
static NimBLERemoteCharacteristic *rc[NCHAR + 1];
static NimBLECharacteristic *cc[NCHAR + 1];
static volatile bool realUp = false;
static volatile bool needAdvert = false;
static volatile bool phoneUp = false;
static std::string realName;
static std::string realMfg;
static std::string clean(std::string s) { s.erase(std::remove(s.begin(), s.end(), '\r'), s.end()); return s; }

static void printHex(const uint8_t *d, size_t n, size_t maxn = 64) {
  for (size_t i = 0; i < n && i < maxn; i++) Serial.printf("%02X ", d[i]);
  if (n > maxn) Serial.print("...");
}

static void enqueue(uint8_t dir, uint8_t idx, const uint8_t *d, size_t n) {
  Msg m;
  m.dir = dir;
  m.idx = idx;
  m.len = n > sizeof(m.data) ? sizeof(m.data) : n;
  if (m.len) memcpy(m.data, d, m.len);
  xQueueSend(q, &m, 0);
}

static int idxOf(NimBLECharacteristic *c) {
  for (int i = 1; i <= NCHAR; i++) if (cc[i] == c) return i;
  return 0;
}
static int idxOf(NimBLERemoteCharacteristic *c) {
  for (int i = 1; i <= NCHAR; i++) if (rc[i] == c) return i;
  return 0;
}

class CharCb : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *c) override {
    std::string v = c->getValue();
    enqueue(0, idxOf(c), (const uint8_t *)v.data(), v.size());
  }
  void onRead(NimBLECharacteristic *c) override { enqueue(2, idxOf(c), nullptr, 0); }
};
static CharCb charCb;

class SrvCb : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *) override { phoneUp = true; }
  void onDisconnect(NimBLEServer *) override {
    phoneUp = false;
    needAdvert = true;
  }
};
static SrvCb srvCb;

class CliCb : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *) override { realUp = false; }
};
static CliCb cliCb;

static void onNotify(NimBLERemoteCharacteristic *c, uint8_t *d, size_t n, bool) {
  enqueue(1, idxOf(c), d, n);
}

static bool matches(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(), ::tolower);
  for (auto *m : MATCHES) if (name.find(m) != std::string::npos) return true;
  return false;
}

static void startAdvertising() {
  NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData ad;
  ad.setFlags(0x06);
  ad.setCompleteServices(NimBLEUUID(SVC_UUID));
  ad.setName(realName);
  adv->setAdvertisementData(ad);
  if (!realMfg.empty()) {
    NimBLEAdvertisementData sr;
    sr.setManufacturerData(realMfg);
    adv->setScanResponseData(sr);
  }
  adv->start();
  Serial.printf("ADVERTISING clone '%s'. Open the official app now.\n", clean(realName).c_str());
}

static bool connectReal() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  Serial.println("Scanning for module...");
  NimBLEScanResults res = scan->start(6, false);
  for (int i = 0; i < res.getCount(); i++) {
    NimBLEAdvertisedDevice d = res.getDevice(i);
    if (!matches(d.getName())) continue;

    real = NimBLEDevice::createClient();
    real->setClientCallbacks(&cliCb, false);
    if (!real->connect(&d)) {
      Serial.println("connect failed");
      NimBLEDevice::deleteClient(real);
      real = nullptr;
      continue;
    }
    NimBLERemoteService *svc = real->getService(NimBLEUUID(SVC_UUID));
    if (!svc) {
      Serial.println("module has no 0xFFE0 service");
      real->disconnect();
      continue;
    }
    realName = d.getName();
    realMfg = d.haveManufacturerData() ? d.getManufacturerData() : std::string();
    Serial.printf("CONNECTED to module '%s' %s\n", clean(realName).c_str(), d.getAddress().toString().c_str());

    for (int i2 = 1; i2 <= NCHAR; i2++) {
      rc[i2] = svc->getCharacteristic(NimBLEUUID((uint16_t)(SVC_UUID + i2)));
      if (!rc[i2]) continue;
      if (rc[i2]->canNotify()) rc[i2]->subscribe(true, onNotify);
      if (rc[i2]->canRead()) {
        std::string v = rc[i2]->readValue();
        cc[i2]->setValue(v);
      }
    }
    realUp = true;
    needAdvert = true;
    scan->clearResults();
    return true;
  }
  scan->clearResults();
  Serial.println("module not found, retrying");
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Golf cart BMS proxy logger v3 ===");
  q = xQueueCreate(24, sizeof(Msg));
  NimBLEDevice::init("GC2RB-proxy");
  NimBLEDevice::setMTU(247);

  server = NimBLEDevice::createServer();
  server->setCallbacks(&srvCb);
  NimBLEService *svc = server->createService(NimBLEUUID(SVC_UUID));
  uint32_t props[NCHAR + 1] = {
      0,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE_NR,
      NIMBLE_PROPERTY::NOTIFY,
      NIMBLE_PROPERTY::READ,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR,
  };
  for (int i = 1; i <= NCHAR; i++) {
    cc[i] = svc->createCharacteristic(NimBLEUUID((uint16_t)(SVC_UUID + i)), props[i]);
    cc[i]->setCallbacks(&charCb);
    if (props[i] & NIMBLE_PROPERTY::READ) {
      uint8_t zeros[128] = {0};
      cc[i]->setValue(zeros, sizeof(zeros));
    }
  }
  svc->start();
  server->start();  // must happen before any connection exists
}

void loop() {
  if (!realUp) {
    if (real && real->isConnected()) real->disconnect();
    if (phoneUp) server->disconnect(0);
    connectReal();
    return;
  }

  if (needAdvert && !phoneUp) {
    needAdvert = false;
    startAdvertising();
  }

  Msg m;
  while (xQueueReceive(q, &m, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (m.dir == 0) {
      Serial.printf("[%10lu] APP>MOD ffe%d len=%u: ", millis(), m.idx, (unsigned)m.len);
      printHex(m.data, m.len);
      Serial.println();
      if (rc[m.idx]) rc[m.idx]->writeValue(m.data, m.len, rc[m.idx]->canWrite());
    } else if (m.dir == 1) {
      Serial.printf("[%10lu] MOD>APP ffe%d len=%u: ", millis(), m.idx, (unsigned)m.len);
      printHex(m.data, m.len);
      Serial.println();
      if (cc[m.idx]) {
        cc[m.idx]->setValue(m.data, m.len);
        cc[m.idx]->notify();
      }
    } else {
      Serial.printf("[%10lu] APP reads ffe%d\n", millis(), m.idx);
    }
  }
}
