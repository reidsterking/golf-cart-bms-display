// Phase 1: BLE protocol explorer.
// Finds the battery Bluetooth modules (name starts with "asGC2RB"), connects,
// prints every service and characteristic, and logs all notifications as hex.
// Capture the serial output and use it to write the real decoder.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <vector>

static const char *TARGET_PREFIX = "asGC2RB";
static const uint32_t SCAN_SECONDS = 10;

static std::vector<NimBLEClient *> clients;

static void printHex(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) Serial.printf("%02X ", data[i]);
}

static void onNotify(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool isNotify) {
  Serial.printf("[%10lu] %s %s %s len=%u: ", millis(),
                chr->getRemoteService()->getClient()->getPeerAddress().toString().c_str(),
                isNotify ? "NOTIFY" : "INDICATE", chr->getUUID().toString().c_str(), (unsigned)len);
  printHex(data, len);
  Serial.println();
}

static void dumpAndSubscribe(NimBLEClient *client) {
  auto *services = client->getServices(true);
  for (auto *svc : *services) {
    Serial.printf("  SERVICE %s\n", svc->getUUID().toString().c_str());
    auto *chars = svc->getCharacteristics(true);
    for (auto *chr : *chars) {
      Serial.printf("    CHAR %s props:%s%s%s%s%s\n", chr->getUUID().toString().c_str(),
                    chr->canRead() ? " read" : "", chr->canWrite() ? " write" : "",
                    chr->canWriteNoResponse() ? " writeNR" : "", chr->canNotify() ? " notify" : "",
                    chr->canIndicate() ? " indicate" : "");
      if (chr->canRead()) {
        std::string v = chr->readValue();
        Serial.print("      value: ");
        printHex((const uint8_t *)v.data(), v.size());
        Serial.println();
      }
      if (chr->canNotify() || chr->canIndicate()) {
        chr->subscribe(chr->canNotify(), onNotify);
      }
    }
  }
}

static bool nameMatches(const std::string &name) {
  return name.rfind(TARGET_PREFIX, 0) == 0;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Golf cart BMS explorer ===");
  NimBLEDevice::init("cyd-explorer");
}

void loop() {
  Serial.printf("Scanning %us for '%s*'...\n", (unsigned)SCAN_SECONDS, TARGET_PREFIX);
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  NimBLEScanResults results = scan->start(SCAN_SECONDS, false);

  for (int i = 0; i < results.getCount(); i++) {
    NimBLEAdvertisedDevice dev = results.getDevice(i);
    Serial.printf("  seen: '%s' %s rssi=%d\n", dev.getName().c_str(),
                  dev.getAddress().toString().c_str(), dev.getRSSI());
    if (!nameMatches(dev.getName())) continue;
    if (clients.size() >= 2) continue;

    NimBLEClient *client = NimBLEDevice::createClient();
    if (client->connect(&dev)) {
      Serial.printf("CONNECTED to %s (%s)\n", dev.getName().c_str(), dev.getAddress().toString().c_str());
      dumpAndSubscribe(client);
      clients.push_back(client);
    } else {
      Serial.println("connect failed");
      NimBLEDevice::deleteClient(client);
    }
  }
  scan->clearResults();

  if (clients.empty()) {
    delay(2000);
    return;
  }
  // Stay connected and log notifications. Drop dead clients so we rescan.
  for (int t = 0; t < 600; t++) {
    delay(100);
    for (size_t i = 0; i < clients.size();) {
      if (!clients[i]->isConnected()) {
        Serial.println("client disconnected");
        NimBLEDevice::deleteClient(clients[i]);
        clients.erase(clients.begin() + i);
      } else {
        i++;
      }
    }
  }
}
