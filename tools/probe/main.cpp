// Read-only probe for the GC2RB module. Connects, subscribes to notifications,
// then re-reads every readable characteristic of service 0xFFE0 every 1.5 s and
// prints whenever a value changes. It never writes anything to the module.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <algorithm>
#include <vector>

static NimBLEClient *client = nullptr;
static std::vector<NimBLERemoteCharacteristic *> readable;
static std::vector<std::string> lastVal;
static uint32_t reads = 0, changes = 0, lastBeat = 0;

static std::string clean(std::string s) {
  s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
  return s;
}

static void printHex(const std::string &v, size_t maxn = 48) {
  for (size_t i = 0; i < v.size() && i < maxn; i++) Serial.printf("%02X ", (uint8_t)v[i]);
  if (v.size() > maxn) Serial.print("...");
}

static int nonZero(const std::string &v) {
  int n = 0;
  for (char c : v) if (c) n++;
  return n;
}

static void onNotify(NimBLERemoteCharacteristic *c, uint8_t *d, size_t n, bool) {
  Serial.printf("[%8lu] NOTIFY %s len=%u: ", millis(), c->getUUID().toString().c_str(), (unsigned)n);
  printHex(std::string((char *)d, n));
  Serial.println();
}

static bool connectModule() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  Serial.println("Scanning for module...");
  NimBLEScanResults res = scan->start(6, false);
  for (int i = 0; i < res.getCount(); i++) {
    NimBLEAdvertisedDevice d = res.getDevice(i);
    std::string n = d.getName();
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    if (n.find("gc2rb") == std::string::npos) continue;
    client = NimBLEDevice::createClient();
    if (!client->connect(&d)) {
      NimBLEDevice::deleteClient(client);
      client = nullptr;
      continue;
    }
    NimBLERemoteService *svc = client->getService(NimBLEUUID((uint16_t)0xFFE0));
    if (!svc) { client->disconnect(); continue; }
    readable.clear();
    lastVal.clear();
    for (auto *c : *svc->getCharacteristics(true)) {
      if (c->canNotify()) c->subscribe(true, onNotify);
      if (c->canRead()) {
        readable.push_back(c);
        std::string v = c->readValue();
        lastVal.push_back(v);
        Serial.printf("  %s initial: len=%u nonzero=%d  ", c->getUUID().toString().c_str(), (unsigned)v.size(), nonZero(v));
        printHex(v, 24);
        Serial.println();
      }
    }
    Serial.printf("CONNECTED to %s. Polling (read-only)...\n", clean(d.getName()).c_str());
    scan->clearResults();
    return true;
  }
  scan->clearResults();
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== GC2RB read-only probe ===");
  NimBLEDevice::init("probe");
}

void loop() {
  if (!client || !client->isConnected()) {
    if (client) { NimBLEDevice::deleteClient(client); client = nullptr; }
    if (!connectModule()) delay(1000);
    return;
  }
  for (size_t i = 0; i < readable.size(); i++) {
    std::string v = readable[i]->readValue();
    reads++;
    if (v != lastVal[i]) {
      changes++;
      size_t k = 0;
      while (k < v.size() && k < lastVal[i].size() && v[k] == lastVal[i][k]) k++;
      Serial.printf("[%8lu] CHANGED %s len=%u nonzero=%d first diff at byte %u: ", millis(),
                    readable[i]->getUUID().toString().c_str(), (unsigned)v.size(), nonZero(v), (unsigned)k);
      printHex(v);
      Serial.println();
      lastVal[i] = v;
    }
  }
  if (millis() - lastBeat > 10000) {
    lastBeat = millis();
    Serial.printf("[%8lu] alive: %lu reads, %lu changes so far\n", millis(), reads, changes);
  }
  delay(1500);
}
