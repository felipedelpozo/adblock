// C3 AdBlock — DNS sinkhole + web dashboard for the ESP32-C3 (no PSRAM).
// Blocklist = sorted 40-bit FNV-1a hashes in flash, binary-searched.
// Dashboard at http://c3adblock.local : per-client stats, system info,
// ban clients, add custom block domains. All control state persisted to flash.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Update.h>            // firmware OTA
#include <ArduinoOTA.h>        // network firmware flashing (pio run over wifi)
#include <DNSServer.h>         // captive-portal catch-all DNS
#include <Preferences.h>       // NVS store for provisioned WiFi creds
#include "lwip/etharp.h"
#include "lwip/netif.h"
#if __has_include("secrets.h")
#include "secrets.h"
#else
// No credentials in source: provision through the captive portal.
static const char* WIFI_SSID = "";
static const char* WIFI_PASS = "";
#endif
#include "blocking_state.h"
#include "blocked_log.h"
#include "ui.h"
#include "firmware_identity.h"
#include "github_updater.h"
#include "blocklist_manager.h"
#include "domain_rules.h"

// ---- config ----
static const IPAddress UPSTREAM(9, 9, 9, 9);     // Quad9
static const uint16_t DNS_PORT = 53;
static const int HASH_BYTES = 5;
static const uint64_t HASH_MASK = (1ULL << (HASH_BYTES * 8)) - 1;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
static const int BOOT_PIN = 0;
#else
static const int BOOT_PIN = 9;
#endif

// ---- globals ----
WiFiUDP dnsServer, upstreamCli;
WebServer web(80);
uint32_t totalBlocked = 0, totalAllowed = 0;
uint8_t buf[600];

struct Dev { uint32_t ip; uint8_t mac[6]; uint32_t blocked, allowed, lastSeen; bool banned; String label; };
static const int MAX_CLIENTS = 96;
Dev clients[MAX_CLIENTS]; int numClients = 0;

static const int MAX_CUSTOM = 200;
String customDom[MAX_CUSTOM]; uint64_t customHash[MAX_CUSTOM]; int numCustom = 0;
domain_rules::Table allowlist;

static const int MAX_BAN = 32;
uint32_t bannedIP[MAX_BAN]; int numBanned = 0;

// remote blocklist auto-update
String updateUrl = "";              // URL of a prebuilt blocklist.bin (e.g. GitHub release asset)
uint32_t updateIntervalH = 24;      // hours between auto-fetches
uint32_t lastCheckMs = 0;
String updateStatus = "never";
String githubCsrfNonce;

// WiFi provisioning (captive portal)
Preferences prefs;
DNSServer   dnsPortal;
String      portalOpts;             // <option> list of scanned networks, built once at portal start

// blocking pause (Pi-hole-style "disable for a while")
BlockingState blocking;

// ---------- hashing / matching ----------
static uint64_t fnv40(const char* s, size_t n) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 0x100000001b3ULL; }
  return h & HASH_MASK;
}
static bool inFlash(uint64_t h) { return blocklistManager.contains(h); }
static bool inCustom(uint64_t h) { for (int i = 0; i < numCustom; i++) if (customHash[i] == h) return true; return false; }
static bool isBlocked(const char* domain, blocked_log::Reason* reason) {
  // An allowlist only overrides domain rules.  Client bans are checked by the
  // caller first and therefore remain enforced regardless of this table.
  if (allowlist.containsSuffix(domain)) return false;
  const char* p = domain;
  while (p && *p) {
    uint64_t h = fnv40(p, strlen(p));
    if (inCustom(h)) { *reason = blocked_log::Reason::Custom; return true; }
    if (inFlash(h)) { *reason = blocked_log::Reason::Blocklist; return true; }
    const char* dot = strchr(p, '.'); if (!dot) break;
    const char* next = dot + 1; if (!strchr(next, '.')) break; p = next;
  }
  return false;
}

// ---------- persistence ----------
static void loadCustom() {
  numCustom = 0; if (!LittleFS.exists("/custom.txt")) return;
  File f = LittleFS.open("/custom.txt", "r"); if (!f) return;
  while (f.available() && numCustom < MAX_CUSTOM) {
    String l = f.readStringUntil('\n'); l.trim(); l.toLowerCase();
    if (l.length() && l.indexOf('.') > 0) { customDom[numCustom] = l; customHash[numCustom] = fnv40(l.c_str(), l.length()); numCustom++; }
  }
  f.close();
}
static void saveCustom() { File f = LittleFS.open("/custom.txt", "w"); if (!f) return; for (int i = 0; i < numCustom; i++) f.println(customDom[i]); f.close(); }
static bool addCustom(String d) {
  d.trim(); d.toLowerCase(); if (d.startsWith("www.")) d = d.substring(4);
  if (!d.length() || d.indexOf('.') < 0 || numCustom >= MAX_CUSTOM) return false;
  for (int i = 0; i < numCustom; i++) if (customDom[i] == d) return false;
  customDom[numCustom] = d; customHash[numCustom] = fnv40(d.c_str(), d.length()); numCustom++; saveCustom(); return true;
}
static void removeCustom(String d) {
  d.toLowerCase();
  for (int i = 0; i < numCustom; i++) if (customDom[i] == d) {
    for (int j = i; j < numCustom - 1; j++) { customDom[j] = customDom[j+1]; customHash[j] = customHash[j+1]; }
    numCustom--; saveCustom(); return;
  }
}
static bool isBannedIP(uint32_t ip) { for (int i = 0; i < numBanned; i++) if (bannedIP[i] == ip) return true; return false; }
static void loadBanned() {
  numBanned = 0; if (!LittleFS.exists("/banned.txt")) return;
  File f = LittleFS.open("/banned.txt", "r"); if (!f) return;
  while (f.available() && numBanned < MAX_BAN) { String l = f.readStringUntil('\n'); l.trim(); IPAddress ip; if (l.length() && ip.fromString(l)) bannedIP[numBanned++] = (uint32_t)ip; }
  f.close();
}
static void saveBanned() {
  numBanned = 0;
  for (int i = 0; i < numClients && numBanned < MAX_BAN; i++) if (clients[i].banned) bannedIP[numBanned++] = clients[i].ip;
  File f = LittleFS.open("/banned.txt", "w"); if (!f) return;
  for (int i = 0; i < numBanned; i++) { IPAddress ip(bannedIP[i]); f.println(ip.toString()); }
  f.close();
}

// ---------- client table ----------
static void getMac(uint32_t ip, uint8_t* mac) {
  memset(mac, 0, 6); ip4_addr_t ipa; ipa.addr = ip;
  struct eth_addr* eth = nullptr; const ip4_addr_t* ipret = nullptr;
  for (struct netif* nif = netif_list; nif; nif = nif->next)
    if (etharp_find_addr(nif, &ipa, &eth, &ipret) >= 0 && eth) { memcpy(mac, eth->addr, 6); return; }
}
static Dev* getClient(uint32_t ip) {
  for (int i = 0; i < numClients; i++) if (clients[i].ip == ip) { clients[i].lastSeen = millis(); return &clients[i]; }
  if (numClients < MAX_CLIENTS) {
    Dev* c = &clients[numClients++];
    c->ip = ip; c->blocked = c->allowed = 0; c->lastSeen = millis(); c->banned = isBannedIP(ip); c->label = "";
    getMac(ip, c->mac); return c;
  }
  return nullptr;
}

// ---------- DNS ----------
static size_t parseQuery(const uint8_t* pkt, int len, char* out, uint16_t* qtype, int* qend) {
  if (len < 13) return 0; int i = 12; size_t o = 0;
  while (i < len) { uint8_t l = pkt[i++]; if (l == 0) break; if (l & 0xC0) return 0;
    if (o + l + 1 >= 250 || i + l > len) return 0; if (o) out[o++] = '.';
    for (uint8_t k = 0; k < l; k++) out[o++] = tolower(pkt[i++]); }
  out[o] = 0; if (i + 4 > len) return 0; *qtype = (pkt[i] << 8) | pkt[i + 1]; *qend = i + 4;
  return o;
}
static int buildBlocked(int qend, uint16_t qtype) {
  buf[2] = 0x81; buf[3] = 0x80; buf[6] = 0; buf[7] = (qtype == 1) ? 1 : 0; buf[8] = 0; buf[9] = 0; buf[10] = 0; buf[11] = 0;
  if (qtype != 1) return qend;
  const uint8_t ans[] = {0xC0,0x0C, 0,1, 0,1, 0,0,1,0x2C, 0,4, 0,0,0,0};
  memcpy(buf + qend, ans, sizeof(ans)); return qend + sizeof(ans);
}
static int forwardUpstream(int qlen) {
  upstreamCli.beginPacket(UPSTREAM, 53); upstreamCli.write(buf, qlen); upstreamCli.endPacket();
  uint32_t t0 = millis();
  while (millis() - t0 < 1000) { int sz = upstreamCli.parsePacket(); if (sz > 0) return upstreamCli.read(buf, sizeof(buf)); delay(1); }
  return 0;
}
// Drain a whole RX burst per call (capped, so web/OTA still get a turn) instead of
// one packet per loop iteration. Returns true if any query was handled this call.
static bool handleDns() {
  bool did = false;
  const uint32_t startedAt = millis();
  for (int budget = 0; budget < 16; budget++) {
    blocking.tick(millis());
    if (budget && millis() - startedAt >= 10) break;
    int sz = dnsServer.parsePacket(); if (sz <= 0) break;
    did = true;
    IPAddress cip = dnsServer.remoteIP(); uint16_t cport = dnsServer.remotePort();
    int qlen = dnsServer.read(buf, sizeof(buf)); if (qlen < 13) continue;
    char domain[256]; uint16_t qtype = 0; int qend = qlen;
    size_t dl = parseQuery(buf, qlen, domain, &qtype, &qend);
    Dev* c = getClient((uint32_t)cip);
    bool ban = c && c->banned;
    blocked_log::Reason reason = blocked_log::Reason::Client;
    bool blocked = ban || (blocking.active() && dl && (blocklistManager.domains() || numCustom) && isBlocked(domain, &reason));
    int rlen;
    if (blocked) {
      rlen = buildBlocked(qend, qtype); totalBlocked++; if (c) c->blocked++;
      blocked_log::record(dl ? domain : nullptr, static_cast<uint32_t>(cip), qtype, reason);
    }
    else         { rlen = forwardUpstream(qlen);     totalAllowed++; if (c) c->allowed++; }
    if (rlen > 0) { dnsServer.beginPacket(cip, cport); dnsServer.write(buf, rlen); dnsServer.endPacket(); }
  }
  return did;
}

// ---------- web ----------
static String macStr(const uint8_t* m) { char s[18]; snprintf(s, sizeof(s), "%02x:%02x:%02x:%02x:%02x:%02x", m[0],m[1],m[2],m[3],m[4],m[5]); return String(s); }
static String jesc(const String& s) {
  String escaped;
  for (unsigned char ch : s) {
    if (ch >= 0x7f) { escaped += static_cast<char>(ch); continue; }
    char byte[7]; blocked_log::escapeJsonByte(ch, byte); escaped += byte;
  }
  return escaped;
}

#include "page.h"   // dashboard HTML (PROGMEM) — see issue #6

static void handleStats() {
  uint32_t up = millis() / 1000;
  char ut[24]; snprintf(ut, sizeof(ut), "%lud %luh %lum", up/86400, (up%86400)/3600, (up%3600)/60);
  const String listStatus = blocklistManager.status();
  String j = "{\"ip\":\"" + WiFi.localIP().toString() + "\",\"blocked\":" + totalBlocked + ",\"allowed\":" + totalAllowed +
             ",\"domains\":" + blocklistManager.domains() + ",\"rssi\":" + WiFi.RSSI() + ",\"temp\":" + String(temperatureRead(), 1) +
             ",\"heap\":" + ESP.getFreeHeap() + ",\"uptime\":\"" + ut + "\"" +
             ",\"upurl\":\"" + jesc(updateUrl) + "\",\"upiv\":" + updateIntervalH + ",\"upstat\":\"" + jesc(listStatus) + "\"" +
             ",\"blocking\":" + (blocking.active() ? "true" : "false") +
             ",\"fwVersion\":\"" + jesc(String(firmware_identity::VERSION)) + "\",\"fwProfile\":\"" + firmware_identity::PROFILE +
             "\",\"githubStatus\":\"" + jesc(githubUpdater.status()) + "\",\"githubVersion\":\"" + jesc(githubUpdater.availableVersion()) +
             "\",\"githubBusy\":" + String(githubUpdater.busy() ? "true" : "false") +
             ",\"githubCanInstall\":" + String(githubUpdater.canInstall() ? "true" : "false") +
             ",\"githubProgress\":" + githubUpdater.progress() + ",\"githubNonce\":\"" + githubCsrfNonce + "\"" +
             ",\"resumeIn\":" + blocking.remainingSeconds(millis()) +
             ",\"clients\":[";
  for (int i = 0; i < numClients; i++) { Dev& c = clients[i]; IPAddress ip(c.ip);
    j += (i ? "," : ""); j += "{\"ip\":\"" + ip.toString() + "\",\"mac\":\"" + macStr(c.mac) + "\",\"blocked\":" + c.blocked + ",\"allowed\":" + c.allowed + ",\"banned\":" + (c.banned?"true":"false") + "}"; }
  j += "],\"custom\":[";
  for (int i = 0; i < numCustom; i++) { j += (i ? "," : ""); j += "\"" + jesc(customDom[i]) + "\""; }
  j += "]}";
  web.send(200, "application/json", j);
}
static void handleBan() {
  IPAddress ip; if (ip.fromString(web.arg("ip"))) { Dev* c = getClient((uint32_t)ip); if (c) { c->banned = !c->banned; saveBanned(); } }
  web.send(200, "text/plain", "ok");
}

static void handleLists() {
  String j = "{\"selectedProfile\":\"" + blocklistManager.selectedName() +
             "\",\"appliedProfile\":\"" + blocklistManager.appliedName() +
             "\",\"busy\":" + String(blocklistManager.busy() ? "true" : "false") +
             ",\"status\":\"" + jesc(blocklistManager.status()) +
             "\",\"progress\":" + String(blocklistManager.progress()) +
             ",\"nonce\":\"" + jesc(githubCsrfNonce) + "\",\"allowed\":[";
  for (size_t i = 0; i < allowlist.size(); ++i) {
    j += (i ? "," : ""); j += "\""; j += jesc(String(allowlist.at(i))); j += "\"";
  }
  j += "],\"domains\":" + String(blocklistManager.domains()) + "}";
  web.send(200, "application/json", j);
}

// ---------- OTA blocklist update (browser upload) ----------
static bool validGithubOrigin();
static bool upOk = false;
static bool upActive = false;
static bool upComplete = false;
static bool upMultiple = false;
static bool upConflict = false;
static void handleUploadDone() {
  if (!validGithubOrigin() || web.header("X-CSRF-Token") != githubCsrfNonce) {
    if (upActive) blocklistManager.abortUpload();
    upActive = upComplete = false;
    upConflict = false;
    web.send(403, "text/plain", "csrf rejected"); return;
  }
  if (upComplete && !upMultiple) upOk = blocklistManager.completeUpload();
  else if (upActive) { blocklistManager.abortUpload(); upOk = false; }
  const int responseCode = upOk ? 200 : (upConflict ? 409 : 400);
  const bool succeeded = upOk;
  upOk = false;
  upActive = upComplete = upMultiple = upConflict = false;
  web.send(responseCode, "text/plain",
           succeeded ? "ok" : "rejected: empty, unsorted or duplicate hash list");
}
static void handleUpload() {
  if (!validGithubOrigin() || web.header("X-CSRF-Token") != githubCsrfNonce) { upOk = false; return; }
  String contentType = web.header("Content-Type"); contentType.toLowerCase();
  if (!contentType.startsWith("multipart/form-data")) { upOk = false; return; }
  HTTPUpload& u = web.upload();
  switch (u.status) {
    case UPLOAD_FILE_START:
      if (upActive) {
        // A second file part belongs to this request; invalidate only our
        // staged upload and never touch a background profile operation.
        blocklistManager.abortUpload(); upMultiple = true; upOk = false;
      } else {
        upOk = false; upComplete = false; upMultiple = false; upConflict = false;
        if (githubUpdater.busy()) upConflict = true;
        upActive = !upConflict && blocklistManager.beginUpload();
        if (!upActive && !upConflict) upConflict = blocklistManager.busy();
      }
      Serial.printf("[ota] receiving %s\n", u.filename.c_str());
      break;
    case UPLOAD_FILE_WRITE:
      if (upActive && !upMultiple && !blocklistManager.writeUpload(u.buf, u.currentSize)) upOk = false;
      break;
    case UPLOAD_FILE_END:
      if (upActive && !upMultiple) { upOk = blocklistManager.finishUpload(); upComplete = upOk; }
      Serial.printf("[ota] %s -> %u domains\n", upOk ? "OK" : "REJECTED", blocklistManager.domains());
      break;
    case UPLOAD_FILE_ABORTED:
      if (upActive) blocklistManager.abortUpload();
      upActive = upComplete = false; upOk = false; upConflict = false;
      Serial.println("[ota] aborted");
      break;
  }
}

// ---------- remote blocklist auto-update ----------
static void loadUpdateCfg() {
  if (!LittleFS.exists("/update.cfg")) return;
  File f = LittleFS.open("/update.cfg", "r"); if (!f) return;
  updateUrl = f.readStringUntil('\n'); updateUrl.trim();
  String iv = f.readStringUntil('\n'); iv.trim(); if (iv.length()) updateIntervalH = iv.toInt();
  f.close();
  if (updateUrl.length() > 1024 || (updateUrl.length() && !updateUrl.startsWith("https://") && !updateUrl.startsWith("http://"))) updateUrl = "";
  if (updateIntervalH < 1 || updateIntervalH > 720) updateIntervalH = 24;
}
static void saveUpdateCfg() {
  File f = LittleFS.open("/update.cfg", "w"); if (!f) return;
  f.println(updateUrl); f.println(updateIntervalH); f.close();
}
static bool fetchBlocklist(String url) {
  url.trim(); if (!url.length()) { updateStatus = "no url set"; return false; }
  if (githubUpdater.busy()) { updateStatus = "firmware updater busy"; return false; }
  const bool accepted = blocklistManager.requestUrl(url);
  updateStatus = accepted ? "download started" : "busy or unavailable";
  return accepted;
}

// ---------- firmware OTA (browser upload of firmware.bin -> reboot) ----------
static bool fwUploadLocked = false;
static bool fwUploadSucceeded = false;
static bool fwUploadFailed = false;
static bool fwUploadConflict = false;

static void handleFwUpdateDone() {
  const bool ok = fwUploadLocked && fwUploadSucceeded && !fwUploadFailed && Update.end(true);
  web.send(ok ? 200 : (fwUploadConflict ? 409 : 400), "text/plain",
           ok ? "ok, rebooting" : "firmware update failed or another OTA is active");
  if (ok) { delay(300); ESP.restart(); }
  if (fwUploadLocked) {
    Update.abort();
    firmwareUpdateUnlock();
    fwUploadLocked = false;
  }
}

static void handleFwUpload() {
  // WebServer invokes the same callback for raw POST bodies. upload() has
  // no object in that case; accessing it would reboot on an empty/raw request.
  String contentType = web.header("Content-Type");
  contentType.toLowerCase();
  if (!contentType.startsWith("multipart/form-data")) {
    fwUploadFailed = true;
    return;
  }
  HTTPUpload& u = web.upload();
  if (u.status == UPLOAD_FILE_START) {
    // Reject multiple file parts without replacing an already validated app.
    if (fwUploadLocked) {
      fwUploadFailed = true;
      Update.abort();
      return;
    }
    fwUploadSucceeded = fwUploadFailed = fwUploadConflict = false;
    if (githubUpdater.busy() || blocklistManager.busy() || !firmwareUpdateTryLock(0)) {
      fwUploadConflict = true;
      Serial.println("[fw-ota] rejected: another OTA is active");
      return;
    }
    fwUploadLocked = true;
    Serial.printf("[fw-ota] %s\n", u.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
      fwUploadFailed = true;
      Update.printError(Serial);
    }
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (!fwUploadLocked || fwUploadFailed) return;
    if (Update.write(u.buf, u.currentSize) != u.currentSize) {
      fwUploadFailed = true;
      Update.printError(Serial);
    }
  } else if (u.status == UPLOAD_FILE_END) {
    if (!fwUploadLocked || fwUploadFailed) return;
    // Activate only in the completion callback, after the entire multipart
    // request has been accepted. A later malformed file part must not activate.
    fwUploadSucceeded = u.totalSize > 0 && !Update.hasError();
    if (fwUploadSucceeded) Serial.printf("[fw-ota] %u bytes received\n", u.totalSize);
    else { fwUploadFailed = true; Update.printError(Serial); }
  } else if (u.status == UPLOAD_FILE_ABORTED) {
    if (fwUploadLocked) { Update.abort(); firmwareUpdateUnlock(); fwUploadLocked = false; }
    fwUploadSucceeded = false;
    Serial.println("[fw-ota] aborted");
  }
}

static bool validGithubOrigin() {
  const String origin = web.header("Origin");
  if (!origin.length()) return false;
  return origin == "http://c3adblock.local" || origin == (String("http://") + WiFi.localIP().toString());
}

static bool authorizeGithubRequest() {
  if (!validGithubOrigin() || web.header("X-CSRF-Token") != githubCsrfNonce) {
    web.send(403, "text/plain", "csrf rejected"); return false;
  }
  return true;
}

static void loadAllowlist();
static bool saveAllowlist();

static void handleListProfile() {
  if (!authorizeGithubRequest()) return;
  if (githubUpdater.busy()) { web.send(409, "text/plain", "firmware updater busy"); return; }
  BlocklistManager::Profile profile;
  if (!BlocklistManager::profileFromName(web.arg("p").c_str(), profile) || profile == BlocklistManager::Profile::Custom) {
    web.send(400, "text/plain", "invalid profile"); return;
  }
  if (!blocklistManager.requestProfile(profile)) { web.send(409, "text/plain", "list update busy or unavailable"); return; }
  web.send(202, "text/plain", "profile update started");
}

static void handleListCheck() {
  if (!authorizeGithubRequest()) return;
  if (githubUpdater.busy()) { web.send(409, "text/plain", "firmware updater busy"); return; }
  if (!blocklistManager.requestCheck()) { web.send(409, "text/plain", "list refresh busy or custom"); return; }
  web.send(202, "text/plain", "list refresh started");
}

static void handleAllowlistAdd() {
  if (!authorizeGithubRequest()) return;
  const String value = web.arg("d");
  char canonical[domain_rules::kMaxDomainLength + 1];
  if (!domain_rules::normalize(value.c_str(), canonical, sizeof(canonical))) {
    web.send(400, "text/plain", "invalid domain"); return;
  }
  if (!allowlist.addCanonical(canonical)) { web.send(409, "text/plain", "already allowed or full"); return; }
  if (!saveAllowlist()) { loadAllowlist(); web.send(500, "text/plain", "allowlist persistence failed"); return; }
  web.send(200, "text/plain", "ok");
}

static void handleAllowlistRemove() {
  if (!authorizeGithubRequest()) return;
  if (!allowlist.remove(web.arg("d").c_str())) { web.send(404, "text/plain", "not found"); return; }
  if (!saveAllowlist()) { loadAllowlist(); web.send(500, "text/plain", "allowlist persistence failed"); return; }
  web.send(200, "text/plain", "ok");
}

static void loadAllowlist() {
  allowlist.clear();
  if (!LittleFS.exists("/allowlist.txt")) return;
  File f = LittleFS.open("/allowlist.txt", "r"); if (!f) return;
  char line[domain_rules::kMaxDomainLength + 1]; size_t length = 0; bool overflow = false;
  while (f.available() && allowlist.size() < domain_rules::kMaxRules) {
    const int value = f.read();
    if (value == '\n') {
      if (!overflow) { line[length] = '\0'; allowlist.add(line); }
      length = 0; overflow = false;
    } else if (value != '\r') {
      if (length < domain_rules::kMaxDomainLength) line[length++] = static_cast<char>(value);
      else overflow = true;
    }
  }
  if (length && !overflow) { line[length] = '\0'; allowlist.add(line); }
  f.close();
}

static bool saveAllowlist() {
  if (!blocklistFilesystemLock(1000)) return false;
  LittleFS.remove("/allowlist.txt.new");
  File f = LittleFS.open("/allowlist.txt.new", "w");
  if (!f) { blocklistFilesystemUnlock(); return false; }
  bool writeOk = true;
  for (size_t i = 0; i < allowlist.size(); ++i) {
    const String line = allowlist.at(i);
    if (f.println(line) != line.length() + 2) { writeOk = false; break; }
  }
  f.flush();
  f.close();
  if (!writeOk) { LittleFS.remove("/allowlist.txt.new"); blocklistFilesystemUnlock(); return false; }
  const bool renamed = LittleFS.rename("/allowlist.txt.new", "/allowlist.txt");
  if (!renamed) LittleFS.remove("/allowlist.txt.new");
  blocklistFilesystemUnlock();
  return renamed;
}

static void handleGithubCheck() {
  if (!authorizeGithubRequest()) return;
  if (blocklistManager.busy()) { web.send(409, "text/plain", "list update busy"); return; }
  if (!githubUpdater.requestCheck()) { web.send(409, "text/plain", "updater busy or unavailable"); return; }
  web.send(202, "text/plain", "update check started");
}

static void handleGithubInstall() {
  if (!authorizeGithubRequest()) return;
  if (blocklistManager.busy()) { web.send(409, "text/plain", "list update busy"); return; }
  if (!githubUpdater.requestInstall(web.arg("v"))) {
    web.send(409, "text/plain", "check a compatible release first or updater busy"); return;
  }
  web.send(202, "text/plain", "installation started");
}

// Display and web controls share the same volatile pause state.
static void serviceRoundUi(bool portal = false, const char* ap = "") {
#ifdef ROUND_DISPLAY
  const uint32_t now = millis();
  const round_ui::Action action = round_ui::poll(now);
  if (!portal) {
    switch (action) {
      case round_ui::Action::Pause5Minutes: blocking.pause(now, 300); break;
      case round_ui::Action::Pause30Minutes: blocking.pause(now, 1800); break;
      case round_ui::Action::Resume: blocking.resume(); break;
      default: break;
    }
  }
  // Network values only need sampling at the UI refresh rate.
  static round_ui::Snapshot snapshot;
  static uint32_t sampledAt = 0;
  if (sampledAt == 0 || now - sampledAt >= 250 || action != round_ui::Action::None) {
    sampledAt = now;
    snapshot.blocking = blocking.active();
    snapshot.connected = WiFi.status() == WL_CONNECTED;
    snapshot.portal = portal;
    snapshot.blocked = totalBlocked;
    snapshot.allowed = totalAllowed;
    snapshot.domains = blocklistManager.domains();
    snapshot.customDomains = numCustom;
    snapshot.clients = numClients;
    snapshot.rssi = snapshot.connected ? WiFi.RSSI() : 0;
    snapshot.resumeSeconds = blocking.remainingSeconds(now);
    const IPAddress ip = portal ? WiFi.softAPIP() : WiFi.localIP();
    snprintf(snapshot.ip, sizeof(snapshot.ip), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    snprintf(snapshot.ap, sizeof(snapshot.ap), "%s", ap);
  }
  round_ui::update(snapshot, now);
#else
  (void)portal;
  (void)ap;
#endif
}

// ---------- WiFi provisioning (captive portal) ----------
// Try provisioned NVS creds first, then the compile-time secrets.h creds as a
// fallback (so the maintainer's own device + source builders keep working). If
// neither connects, fall through to the config portal.
static bool connectWiFi() {
  prefs.begin("wifi", true);
  String ss = prefs.getString("ssid", "");
  String pw = prefs.getString("pass", "");
  prefs.end();
  const char* ssid = ss.length() ? ss.c_str() : WIFI_SSID;
  const char* pass = ss.length() ? pw.c_str() : WIFI_PASS;
  if (!ssid || !*ssid || strcmp(ssid, "YOUR_WIFI_SSID") == 0) return false;  // unconfigured
  Serial.printf("WiFi: connecting to \"%s\"%s\n", ssid, ss.length() ? " (provisioned)" : " (secrets.h)");
  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.begin(ssid, pass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) { serviceRoundUi(); delay(10); }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

static void handlePortalRoot() {
  String html =
    "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>C3 AdBlock setup</title>"
    "<body style='font:16px system-ui,sans-serif;max-width:420px;margin:36px auto;padding:0 16px;background:#0d1117;color:#c9d1d9'>"
    "<h2>&#128737; C3 AdBlock &mdash; WiFi setup</h2>"
    "<p style='color:#8b949e'>Pick your network and enter its password. The device restarts and joins it.</p>"
    "<form method=POST action=/wifisave>"
    "<input list=nets name=s placeholder='WiFi name' required style='width:100%;box-sizing:border-box;padding:11px;margin:6px 0;border-radius:6px;border:1px solid #30363d;background:#161b22;color:#c9d1d9'>"
    "<datalist id=nets>" + portalOpts + "</datalist>"
    "<input name=p type=password placeholder='Password' style='width:100%;box-sizing:border-box;padding:11px;margin:6px 0;border-radius:6px;border:1px solid #30363d;background:#161b22;color:#c9d1d9'>"
    "<button style='width:100%;padding:12px;margin-top:8px;border-radius:6px;border:0;background:#3fb950;color:#000;font-weight:600;cursor:pointer'>Connect</button>"
    "</form></body>";
  web.send(200, "text/html", html);
}
static void handleWifiSave() {
  String ss = web.arg("s"), pw = web.arg("p");
  if (!ss.length()) { web.send(400, "text/plain", "missing WiFi name"); return; }
  prefs.begin("wifi", false); prefs.putString("ssid", ss); prefs.putString("pass", pw); prefs.end();
  web.send(200, "text/html", "<!doctype html><meta charset=utf-8><body style='font:16px system-ui;text-align:center;margin-top:60px'>"
                             "&#9989; Saved. Restarting and joining <b>" + ss + "</b>&hellip;<br><br>"
                             "Reconnect your phone to your normal WiFi, then find the box at <b>c3adblock.local</b>.</body>");
  delay(900); ESP.restart();
}
// Never returns — blocks in the portal loop until creds are saved (then reboots).
static void startConfigPortal() {
  int n = WiFi.scanNetworks();                 // scan while still in STA mode (no APSTA)
  portalOpts = "";
  for (int i = 0; i < n && i < 15; i++) portalOpts += "<option value='" + jesc(WiFi.SSID(i)) + "'>";
  uint8_t mac[6]; WiFi.macAddress(mac);
  char ap[24]; snprintf(ap, sizeof(ap), "C3-AdBlock-%02X%02X", mac[4], mac[5]);
  WiFi.mode(WIFI_AP); WiFi.softAP(ap);
  IPAddress apIP = WiFi.softAPIP();
  dnsPortal.start(53, "*", apIP);              // catch-all -> phones pop the captive portal
  web.on("/", handlePortalRoot);
  web.on("/wifisave", HTTP_POST, handleWifiSave);
  web.onNotFound(handlePortalRoot);            // any captive-portal probe -> the form
  web.begin();
  Serial.printf("\n[setup] No WiFi. Join open network \"%s\" and a setup page pops up (or http://%s)\n",
                ap, apIP.toString().c_str());
  while (true) { dnsPortal.processNextRequest(); web.handleClient(); serviceRoundUi(true, ap); delay(2); }
}

void setup() {
  Serial.begin(115200); delay(300);
  Serial.printf("\n[adblock] booting chip=%s flash=%lu psram=%lu\n",
      ESP.getChipModel(), static_cast<unsigned long>(ESP.getFlashChipSize()),
      static_cast<unsigned long>(ESP.getPsramSize()));
  Serial.printf("[firmware] %s\n", firmware_identity::MARKER);
  // Never auto-format: an incompatible/missing filesystem must preserve user data.
  if (!LittleFS.begin(false)) Serial.println("[fs] mount failed; data preserved (no format)");
  round_ui::begin();
  blocklistManager.begin();
  loadCustom(); loadAllowlist(); loadBanned(); loadUpdateCfg();
  Serial.printf("blocklist: %u domains\n", blocklistManager.domains());
  Serial.printf("custom: %d, banned: %d\n", numCustom, numBanned);

  // BOOT requests the portal without erasing previously saved credentials.
  pinMode(BOOT_PIN, INPUT_PULLUP);
  bool forcePortal = false;
  if (digitalRead(BOOT_PIN) == LOW) { delay(60);
    if (digitalRead(BOOT_PIN) == LOW) {
      forcePortal = true;
      Serial.println("[setup] BOOT held -> portal (saved WiFi preserved)"); } }

  if (forcePortal || !connectWiFi()) startConfigPortal();
  Serial.printf("WiFi up: %s\n", WiFi.localIP().toString().c_str());
  if (MDNS.begin("c3adblock")) { MDNS.addService("http", "tcp", 80); Serial.println("dashboard: http://c3adblock.local"); }

  dnsServer.begin(DNS_PORT); upstreamCli.begin(0);
  web.on("/", []() { web.send_P(200, "text/html", PAGE); });
  web.on("/stats.json", handleStats);
  web.on("/lists.json", HTTP_GET, handleLists);
  web.on("/lists/profile", HTTP_POST, handleListProfile);
  web.on("/lists/check", HTTP_POST, handleListCheck);
  web.on("/allowlist/add", HTTP_POST, handleAllowlistAdd);
  web.on("/allowlist/remove", HTTP_POST, handleAllowlistRemove);
  web.on("/blocked.json", []() { blocked_log::handleRequest(web); });
  web.on("/ban", handleBan);
  web.on("/addblock", []() { addCustom(web.arg("d")); web.send(200, "text/plain", "ok"); });
  web.on("/unblock", []() { removeCustom(web.arg("d")); web.send(200, "text/plain", "ok"); });
  web.on("/pause", []() {                    // /pause?s=300  (0 or absent = indefinite)
    long s = web.hasArg("s") ? web.arg("s").toInt() : 0;
    blocking.pause(millis(), s > 0 ? (uint32_t)s : 0);
    web.send(200, "text/plain", "paused");
  });
  web.on("/resume", []() { blocking.resume(); web.send(200, "text/plain", "resumed"); });
  web.on("/forgetwifi", []() { web.send(200, "text/plain", "cleared — rebooting into setup portal");
    prefs.begin("wifi", false); prefs.clear(); prefs.end(); delay(500); ESP.restart(); });
  web.on("/upload", HTTP_POST, handleUploadDone, handleUpload);      // blocklist OTA
  web.on("/update", HTTP_POST, handleFwUpdateDone, handleFwUpload);  // firmware OTA
  web.on("/github/install", HTTP_POST, handleGithubInstall);
  web.on("/github/check", HTTP_POST, handleGithubCheck);
  web.on("/fetchnow", []() { const bool accepted = fetchBlocklist(updateUrl); web.send(accepted ? 202 : 409, "text/plain", updateStatus); });
  web.on("/setupdate", []() {
    const String requestedUrl = web.hasArg("u") ? web.arg("u") : updateUrl;
    const long requestedInterval = web.hasArg("h") ? web.arg("h").toInt() : static_cast<long>(updateIntervalH);
    if (requestedUrl.length() > 1024 || (requestedUrl.length() && !requestedUrl.startsWith("https://") && !requestedUrl.startsWith("http://")) || requestedInterval < 1 || requestedInterval > 720) {
      web.send(400, "text/plain", "invalid update settings"); return;
    }
    updateUrl = requestedUrl;
    updateIntervalH = static_cast<uint32_t>(requestedInterval);
    saveUpdateCfg(); web.send(200, "text/plain", "ok");
  });
  web.begin();
  const char* requestHeaders[] = {"Origin", "X-CSRF-Token", "Content-Type"};
  web.collectHeaders(requestHeaders, 3);
  githubCsrfNonce = githubUpdater.nonce();
  githubUpdater.begin();
  ArduinoOTA.setHostname("c3adblock");   // pio run -t upload --upload-port c3adblock.local
  ArduinoOTA.begin();
  Serial.println("DNS :53 + dashboard :80 + OTA up");
}

void loop() {
  blocking.tick(millis());
  blocklistManager.poll();
  // ArduinoOTA calls Update.begin before its onStart callback. Guard the
  // complete handle call, so it can never alter a GitHub upload in progress.
  if (!blocklistManager.busy() && firmwareUpdateTryLock(0)) {
    ArduinoOTA.handle();
    firmwareUpdateUnlock();
  }
  web.handleClient();
  bool busy = handleDns();
  if (updateUrl.length() || blocklistManager.selectedProfile() != BlocklistManager::Profile::Custom) {
    uint32_t now = millis();
    if (lastCheckMs == 0) lastCheckMs = now;   // skip an immediate fetch on boot
    else if (now - lastCheckMs >= updateIntervalH * 3600000UL && !blocklistManager.busy() && !githubUpdater.busy()) {
      lastCheckMs = now;
      if (blocklistManager.selectedProfile() != BlocklistManager::Profile::Custom) {
        updateStatus = blocklistManager.requestCheck() ? "profile refresh started" : "profile refresh unavailable";
      } else {
        fetchBlocklist(updateUrl);
      }
    }
  }
  serviceRoundUi();
  if (!busy) delay(1);   // sleep only when idle: full speed under load, cool when quiet
}
