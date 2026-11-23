#include "ble_source.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <ctype.h>
#include <string.h>

namespace {

const uint16_t SVC_UUID = 0xFFE0;
const uint16_t NOTIFY_UUID = 0xFFE4;
const uint32_t STALE_MS = 10000;  // readings older than this are treated as lost

SemaphoreHandle_t mtx;
Snapshot snap;
uint32_t lastData[2] = {0, 0};
NimBLEClient *cl[2] = {nullptr, nullptr};
volatile bool linkedFlag[2] = {false, false};
Preferences prefs;

// TODO(protocol): decode a notification from battery `slot` into `b`.
// Return true only when the packet really contained readings. The module's
// protocol is not known yet, so this must not invent values.
bool decode(int slot, const uint8_t *data, size_t len, Battery &b) {
  (void)slot;
  (void)data;
  (void)len;
  (void)b;
  return false;
}

void lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
void unlock() { xSemaphoreGive(mtx); }

void sanitize(const std::string &in, char *out, size_t n) {
  size_t k = 0;
  for (char ch : in) {
    if (isalnum((unsigned char)ch) && k + 1 < n) out[k++] = ch;
  }
  out[k] = 0;
}

bool isModule(const std::string &name) {
  std::string l = name;
  for (auto &ch : l) ch = tolower((unsigned char)ch);
  return l.find("gc2rb") != std::string::npos;
}

// Which dashboard slot (battery 1 or 2) a module belongs to. Remembered across
// reboots so BAT 1 and BAT 2 never swap.
int slotFor(const char *serial) {
  lock();
  int slot = -1;
  for (int i = 0; i < 2; i++) {
    if (strcmp(snap.b[i].serial, serial) == 0) slot = i;
  }
  if (slot < 0) {
    for (int i = 0; i < 2; i++) {
      if (snap.b[i].serial[0] == 0) {
        slot = i;
        strncpy(snap.b[i].serial, serial, sizeof(snap.b[i].serial) - 1);
        prefs.putString(i ? "s1" : "s0", serial);
        break;
      }
    }
  }
  unlock();
  return slot;
}

class SlotCb : public NimBLEClientCallbacks {
 public:
  explicit SlotCb(int s) : slot(s) {}
  void onDisconnect(NimBLEClient *) override { linkedFlag[slot] = false; }
  int slot;
};
SlotCb slotCb[2] = {SlotCb(0), SlotCb(1)};

void onNotify(int slot, uint8_t *d, size_t n) {
  Serial.printf("[%10lu] BAT%d NOTIFY len=%u: ", millis(), slot + 1, (unsigned)n);
  for (size_t i = 0; i < n && i < 64; i++) Serial.printf("%02X ", d[i]);
  Serial.println();

  lock();
  Battery b = snap.b[slot];
  unlock();
  if (decode(slot, d, n, b)) {
    lock();
    snap.b[slot] = b;
    snap.b[slot].valid = true;
    snap.b[slot].linked = true;
    lastData[slot] = millis();
    unlock();
  }
}

void connectSlot(int slot, NimBLEAdvertisedDevice &d) {
  if (cl[slot]) {
    NimBLEDevice::deleteClient(cl[slot]);
    cl[slot] = nullptr;
  }
  NimBLEClient *c = NimBLEDevice::createClient();
  c->setClientCallbacks(&slotCb[slot], false);
  if (!c->connect(&d)) {
    NimBLEDevice::deleteClient(c);
    return;
  }
  NimBLERemoteService *svc = c->getService(NimBLEUUID(SVC_UUID));
  NimBLERemoteCharacteristic *ch = svc ? svc->getCharacteristic(NimBLEUUID(NOTIFY_UUID)) : nullptr;
  if (!ch || !ch->canNotify()) {
    c->disconnect();
    NimBLEDevice::deleteClient(c);
    return;
  }
  ch->subscribe(true, [slot](NimBLERemoteCharacteristic *, uint8_t *d2, size_t n, bool) { onNotify(slot, d2, n); });
  cl[slot] = c;
  linkedFlag[slot] = true;
  Serial.printf("BAT%d linked: %s\n", slot + 1, snap.b[slot].serial);
}

void bleTask(void *) {
  NimBLEDevice::init("cart-display");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);

  for (;;) {
    for (int i = 0; i < 2; i++) {
      if (cl[i] && !cl[i]->isConnected()) linkedFlag[i] = false;
    }
    bool needScan = !linkedFlag[0] || !linkedFlag[1];
    if (needScan) {
      NimBLEScanResults res = scan->start(4, false);
      for (int j = 0; j < res.getCount(); j++) {
        NimBLEAdvertisedDevice d = res.getDevice(j);
        if (!isModule(d.getName())) continue;
        char serial[16];
        sanitize(d.getName(), serial, sizeof serial);
        int slot = slotFor(serial);
        if (slot < 0 || linkedFlag[slot]) continue;
        connectSlot(slot, d);
      }
      scan->clearResults();
    }
    lock();
    for (int i = 0; i < 2; i++) {
      snap.b[i].linked = linkedFlag[i];
      if (!linkedFlag[i]) snap.b[i].valid = false;
    }
    unlock();
    vTaskDelay(pdMS_TO_TICKS(needScan ? 500 : 2000));
  }
}

}  // namespace

void ble_start() {
  mtx = xSemaphoreCreateMutex();
  prefs.begin("cart", false);
  String s0 = prefs.getString("s0", ""), s1 = prefs.getString("s1", "");
  strncpy(snap.b[0].serial, s0.c_str(), sizeof(snap.b[0].serial) - 1);
  strncpy(snap.b[1].serial, s1.c_str(), sizeof(snap.b[1].serial) - 1);
  xTaskCreatePinnedToCore(bleTask, "ble", 8192, nullptr, 1, nullptr, 0);
}

void ble_snapshot(Snapshot &out) {
  lock();
  out = snap;
  for (int i = 0; i < 2; i++) {
    if (out.b[i].valid && millis() - lastData[i] > STALE_MS) out.b[i].valid = false;
  }
  unlock();
  out.demo = false;
}
