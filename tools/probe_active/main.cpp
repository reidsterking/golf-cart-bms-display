// Active probe for the GC2RB module. Sends a short list of documented READ-type
// requests from common battery protocol families to the module's data
// characteristics, and logs every reply.
//
// Safety rules baked in:
//  - writes ONLY to characteristics inside service 0xFFE0 (FFE1, FFE2, FFE3, FFE6)
//  - never touches the TI firmware-update service (f000ffc0)
//  - only read/query frames; no configuration, no AT commands
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <algorithm>
#include <map>
#include <vector>

struct Frame {
  const char *name;
  std::string bytes;
};

static NimBLEClient *client = nullptr;
static std::vector<Frame> frames;
static std::vector<NimBLERemoteCharacteristic *> writers;   // FFE1, FFE2, FFE3, FFE6
static std::vector<NimBLERemoteCharacteristic *> readers;   // readable chars of FFE0
static std::vector<std::string> lastVal;
static std::string currentTag = "idle";
static std::map<std::string, int> hits;
static bool ran = false;
static uint32_t lastBeat = 0;

static uint16_t crc16(const uint8_t *d, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++) c = (c & 1) ? (c >> 1) ^ 0xA001 : c >> 1;
  }
  return c;
}

// Pylontech-style ASCII frame: ~ VER ADR CID1 CID2 LENGTH INFO CHKSUM CR
static std::string pylon(const char *adr, const char *cid2, const std::string &info) {
  size_t lenid = info.size();
  uint8_t s = ((lenid >> 8) & 0xF) + ((lenid >> 4) & 0xF) + (lenid & 0xF);
  uint8_t lchk = ((~(s & 0xF)) + 1) & 0xF;
  char lenbuf[8];
  snprintf(lenbuf, sizeof lenbuf, "%X%03X", lchk, (unsigned)(lenid & 0xFFF));
  std::string body = std::string("20") + adr + "46" + cid2 + lenbuf + info;
  uint32_t sum = 0;
  for (char c : body) sum += (uint8_t)c;
  uint16_t chk = ((~(sum & 0xFFFF)) + 1) & 0xFFFF;
  char cbuf[8];
  snprintf(cbuf, sizeof cbuf, "%04X", chk);
  return "~" + body + cbuf + "\r";
}

static void buildFrames() {
  frames.push_back({"pylon-analog-adr1", pylon("01", "42", "01")});
  frames.push_back({"pylon-analog-adr2", pylon("02", "42", "02")});
  frames.push_back({"pylon-version-adr1", pylon("01", "4F", "")});
  frames.push_back({"jbd-basic-info", std::string("\xDD\xA5\x03\x00\xFF\xFD\x77", 7)});
  uint8_t mb[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x10, 0, 0};
  uint16_t c = crc16(mb, 6);
  mb[6] = c & 0xFF;
  mb[7] = c >> 8;
  frames.push_back({"modbus-01-03-0000-x16", std::string((char *)mb, 8)});
  frames.push_back({"bare-CR", "\r"});
}

static void printHex(const std::string &v, size_t maxn = 64) {
  for (size_t i = 0; i < v.size() && i < maxn; i++) Serial.printf("%02X ", (uint8_t)v[i]);
  if (v.size() > maxn) Serial.print("...");
}
static void printAscii(const std::string &v, size_t maxn = 64) {
  for (size_t i = 0; i < v.size() && i < maxn; i++) {
    char c = v[i];
    Serial.print((c >= 32 && c < 127) ? c : (c == '\r' ? '|' : '.'));
  }
}

static void onNotify(NimBLERemoteCharacteristic *c, uint8_t *d, size_t n, bool) {
  std::string v((char *)d, n);
  hits[currentTag]++;
  Serial.printf("[%8lu] RX  %s len=%u  after '%s'\n           hex: ", millis(), c->getUUID().toString().c_str(),
                (unsigned)n, currentTag.c_str());
  printHex(v);
  Serial.print("\n           txt: ");
  printAscii(v);
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
    writers.clear();
    readers.clear();
    lastVal.clear();
    for (auto *c : *svc->getCharacteristics(true)) {
      uint16_t id = 0;
      std::string u = c->getUUID().toString();  // e.g. "0xffe6"
      if (u.size() >= 6) id = (uint16_t)strtol(u.c_str() + 2, nullptr, 16);
      if (c->canNotify()) c->subscribe(true, onNotify);
      if (c->canRead()) {
        readers.push_back(c);
        lastVal.push_back(c->readValue());
      }
      bool isData = id == 0xFFE1 || id == 0xFFE2 || id == 0xFFE3 || id == 0xFFE6;
      if (isData && (c->canWrite() || c->canWriteNoResponse())) writers.push_back(c);
    }
    Serial.printf("CONNECTED. %u write targets, %u readable slots.\n", (unsigned)writers.size(), (unsigned)readers.size());
    scan->clearResults();
    return true;
  }
  scan->clearResults();
  return false;
}

static void checkSlots(const char *tag) {
  for (size_t i = 0; i < readers.size(); i++) {
    std::string v = readers[i]->readValue();
    if (v != lastVal[i]) {
      hits[tag]++;
      Serial.printf("[%8lu] CHG %s after '%s': ", millis(), readers[i]->getUUID().toString().c_str(), tag);
      printHex(v, 48);
      Serial.println();
      lastVal[i] = v;
    }
  }
}

static void runProbe() {
  Serial.println("=== probing: read-type requests only ===");
  for (auto &f : frames) {
    for (auto *w : writers) {
      std::string tag = std::string(f.name) + " -> " + w->getUUID().toString();
      currentTag = tag;
      Serial.printf("[%8lu] TX  %s : ", millis(), tag.c_str());
      printHex(f.bytes);
      Serial.println();
      bool ok = w->writeValue((uint8_t *)f.bytes.data(), f.bytes.size(), !w->canWriteNoResponse() && w->canWrite());
      if (!ok) Serial.println("           (write failed)");
      delay(1500);
      checkSlots(tag.c_str());
      if (!client->isConnected()) { Serial.println("module disconnected during probe"); return; }
    }
  }
  currentTag = "idle";
  Serial.println("=== probe finished ===");
  if (hits.empty()) Serial.println("SUMMARY: the module did not react to any frame.");
  else {
    Serial.println("SUMMARY: it reacted to:");
    for (auto &kv : hits) Serial.printf("  %s  (%d events)\n", kv.first.c_str(), kv.second);
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== GC2RB active probe (read-type requests only) ===");
  buildFrames();
  NimBLEDevice::init("probe-active");
}

void loop() {
  if (!client || !client->isConnected()) {
    if (client) { NimBLEDevice::deleteClient(client); client = nullptr; }
    if (!connectModule()) delay(1000);
    return;
  }
  if (!ran) {
    ran = true;
    runProbe();
  }
  if (millis() - lastBeat > 10000) {
    lastBeat = millis();
    Serial.printf("[%8lu] listening (probe done)...\n", millis());
  }
  delay(200);
}
