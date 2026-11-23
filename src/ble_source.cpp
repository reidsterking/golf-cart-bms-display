#include "ble_source.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <ctype.h>
#include <string.h>

namespace {

const uint16_t SVC_UUID = 0xFFE0;
const uint16_t NOTIFY_UUID = 0xFFE4;
const uint32_t STALE_MS = 10000;  // readings older than this are treated as lost

SemaphoreHandle_t mtx;
Snapshot snap;
uint32_t lastData = 0;
NimBLEClient *client = nullptr;
volatile bool linkedFlag = false;

// TODO(protocol): decode one notification from the module into the snapshot's
// two batteries (s.b[0], s.b[1]). Return true only when the packet really
// contained readings. The protocol is not known yet, so this must not invent values.
bool decode(const uint8_t *data, size_t len, Snapshot &s) {
  (void)data;
  (void)len;
  (void)s;
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

class LinkCb : public NimBLEClientCallbacks {
  void onDisconnect(NimBLEClient *) override { linkedFlag = false; }
};
LinkCb linkCb;

void onNotify(uint8_t *d, size_t n) {
  Serial.printf("[%10lu] NOTIFY len=%u: ", millis(), (unsigned)n);
  for (size_t i = 0; i < n && i < 64; i++) Serial.printf("%02X ", d[i]);
  Serial.println();

  lock();
  Snapshot copy = snap;
  unlock();
  if (decode(d, n, copy)) {
    lock();
    snap.b[0] = copy.b[0];
    snap.b[1] = copy.b[1];
    snap.b[0].valid = copy.b[0].valid;
    snap.b[1].valid = copy.b[1].valid;
    lastData = millis();
    unlock();
  }
}

bool connectModule(NimBLEAdvertisedDevice &d) {
  if (client) {
    NimBLEDevice::deleteClient(client);
    client = nullptr;
  }
  NimBLEClient *c = NimBLEDevice::createClient();
  c->setClientCallbacks(&linkCb, false);
  if (!c->connect(&d)) {
    NimBLEDevice::deleteClient(c);
    return false;
  }
  NimBLERemoteService *svc = c->getService(NimBLEUUID(SVC_UUID));
  NimBLERemoteCharacteristic *ch = svc ? svc->getCharacteristic(NimBLEUUID(NOTIFY_UUID)) : nullptr;
  if (!ch || !ch->canNotify()) {
    c->disconnect();
    NimBLEDevice::deleteClient(c);
    return false;
  }
  ch->subscribe(true, [](NimBLERemoteCharacteristic *, uint8_t *d2, size_t n, bool) { onNotify(d2, n); });
  client = c;
  linkedFlag = true;
  char name[16];
  sanitize(d.getName(), name, sizeof name);
  lock();
  strncpy(snap.module, name, sizeof(snap.module) - 1);
  unlock();
  Serial.printf("Module linked: %s\n", name);
  return true;
}

void bleTask(void *) {
  NimBLEDevice::init("cart-display");
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);

  for (;;) {
    if (client && !client->isConnected()) linkedFlag = false;
    if (!linkedFlag) {
      NimBLEScanResults res = scan->start(4, false);
      for (int j = 0; j < res.getCount(); j++) {
        NimBLEAdvertisedDevice d = res.getDevice(j);
        if (isModule(d.getName()) && connectModule(d)) break;
      }
      scan->clearResults();
    }
    lock();
    snap.linked = linkedFlag;
    if (!linkedFlag) {
      snap.b[0].valid = snap.b[1].valid = false;
    }
    unlock();
    vTaskDelay(pdMS_TO_TICKS(linkedFlag ? 2000 : 500));
  }
}

}  // namespace

void ble_start() {
  mtx = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(bleTask, "ble", 8192, nullptr, 1, nullptr, 0);
}

void ble_snapshot(Snapshot &out) {
  lock();
  out = snap;
  if (millis() - lastData > STALE_MS) out.b[0].valid = out.b[1].valid = false;
  unlock();
  out.demo = false;
}
