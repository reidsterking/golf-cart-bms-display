// BLE protocol explorer. Lists the strongest nearby devices with full advertising
// details, and connects to any whose name contains one of the match strings.
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <algorithm>
#include <vector>

static const char *MATCHES[] = {"asgc2rb", "gc2rb"};  // lower case, substring match
static const uint32_t SCAN_SECONDS = 10;
static const int SHOW_MIN_RSSI = -85;
static std::vector<NimBLEClient *> clients;

static void printHex(const uint8_t *d, size_t n) {
  for (size_t i = 0; i < n; i++) Serial.printf("%02X ", d[i]);
}

static void onNotify(NimBLERemoteCharacteristic *c, uint8_t *d, size_t n, bool notify) {
  Serial.printf("[%10lu] %s %s %s len=%u: ", millis(),
                c->getRemoteService()->getClient()->getPeerAddress().toString().c_str(),
                notify ? "NOTIFY" : "INDICATE", c->getUUID().toString().c_str(), (unsigned)n);
  printHex(d, n);
  Serial.println();
}

static void dumpAndSubscribe(NimBLEClient *client) {
  for (auto *svc : *client->getServices(true)) {
    Serial.printf("  SERVICE %s\n", svc->getUUID().toString().c_str());
    for (auto *c : *svc->getCharacteristics(true)) {
      Serial.printf("    CHAR %s props:%s%s%s%s%s\n", c->getUUID().toString().c_str(),
                    c->canRead() ? " read" : "", c->canWrite() ? " write" : "",
                    c->canWriteNoResponse() ? " writeNR" : "", c->canNotify() ? " notify" : "",
                    c->canIndicate() ? " indicate" : "");
      if (c->canRead()) {
        std::string v = c->readValue();
        Serial.print("      value: ");
        printHex((const uint8_t *)v.data(), v.size());
        Serial.println();
      }
      if (c->canNotify() || c->canIndicate()) c->subscribe(c->canNotify(), onNotify);
    }
  }
}

static bool matches(std::string name) {
  std::transform(name.begin(), name.end(), name.begin(), ::tolower);
  for (auto *m : MATCHES) if (name.find(m) != std::string::npos) return true;
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Golf cart BMS explorer v2 ===");
  NimBLEDevice::init("cyd-explorer");
}

void loop() {
  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setActiveScan(true);
  Serial.printf("\nScanning %us...\n", (unsigned)SCAN_SECONDS);
  NimBLEScanResults res = scan->start(SCAN_SECONDS, false);

  std::vector<int> order;
  for (int i = 0; i < res.getCount(); i++) order.push_back(i);
  std::sort(order.begin(), order.end(),
            [&](int a, int b) { return res.getDevice(a).getRSSI() > res.getDevice(b).getRSSI(); });

  bool found = false;
  Serial.printf("Strongest devices (rssi >= %d):\n", SHOW_MIN_RSSI);
  for (int i : order) {
    NimBLEAdvertisedDevice d = res.getDevice(i);
    if (d.getRSSI() < SHOW_MIN_RSSI) break;
    Serial.printf("  rssi=%d %s type=%d conn=%d name='%s'", d.getRSSI(), d.getAddress().toString().c_str(),
                  d.getAddress().getType(), d.isConnectable(), d.getName().c_str());
    for (int s = 0; s < d.getServiceUUIDCount(); s++)
      Serial.printf(" svc=%s", d.getServiceUUID(s).toString().c_str());
    if (d.haveManufacturerData()) {
      std::string m = d.getManufacturerData();
      Serial.print(" mfg=");
      printHex((const uint8_t *)m.data(), std::min<size_t>(m.size(), 12));
    }
    Serial.println();

    if (matches(d.getName()) && clients.size() < 2) {
      found = true;
      NimBLEClient *client = NimBLEDevice::createClient();
      if (client->connect(&d)) {
        Serial.printf("CONNECTED to %s\n", d.getName().c_str());
        dumpAndSubscribe(client);
        clients.push_back(client);
      } else {
        Serial.println("connect failed");
        NimBLEDevice::deleteClient(client);
      }
    }
  }
  scan->clearResults();
  if (!found && clients.empty()) Serial.println("NO MATCH for asGC2RB in this scan");

  if (clients.empty()) return;
  for (int t = 0; t < 600; t++) {
    delay(100);
    for (size_t i = 0; i < clients.size();) {
      if (!clients[i]->isConnected()) {
        Serial.println("client disconnected");
        NimBLEDevice::deleteClient(clients[i]);
        clients.erase(clients.begin() + i);
      } else i++;
    }
  }
}
