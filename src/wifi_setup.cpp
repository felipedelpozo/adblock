#include "pet/pet_runtime.h"
#include "wifi_setup.h"

#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_system.h>

#include <cstring>
#include <string>

#include "i18n.h"
#include "wifi_setup_model.h"

namespace wifi_setup {
namespace {

constexpr char kStorageNamespace[] = "wifi_setup";
constexpr char kRecordKey[] = "record";
constexpr char kRequestKey[] = "setup_req";
constexpr char kLegacyNamespace[] = "wifi";
constexpr char kLegacySsidKey[] = "ssid";
constexpr char kLegacyPasswordKey[] = "pass";
constexpr uint32_t kConnectDeadlineMs = 20000;
constexpr uint32_t kPortalIdleDeadlineMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kRebootGraceMs = 8000;

enum class PortalState : uint8_t {
  Idle,
  Connecting,
  Failed,
  Connected,
  Cancelled,
};

String defaultSsid;
String defaultPassword;
String apName;
String scannedOptions;
String csrfToken;
String candidateSsid;
String candidatePassword;
wifi_setup_model::Credentials fallback;
bool fallbackAvailable = false;
uint32_t portalStartedAt = 0;
uint32_t candidateStartedAt = 0;
uint32_t rebootAt = 0;
uint32_t stateChangedAt = 0;
uint32_t lastActivityAt = 0;
PortalState portalState = PortalState::Idle;
WebServer* portalWeb = nullptr;
void (*portalService)(bool, const char*) = nullptr;
DNSServer portalDns;

const char* tr(const char* englishText, const char* spanishText) {
  return i18n::text(englishText, spanishText);
}

const char* stateName() {
  switch (portalState) {
    case PortalState::Connecting: return "connecting";
    case PortalState::Failed: return "failed";
    case PortalState::Connected: return "connected";
    case PortalState::Cancelled: return "cancelled";
    case PortalState::Idle: default: return "idle";
  }
}

std::string toStd(const String& value) {
  return std::string(value.c_str(), value.length());
}

bool isPlaceholder(const std::string& value) {
  return value.empty() || value == "YOUR_WIFI_SSID";
}

bool loadRecord(wifi_setup_model::Credentials& credentials) {
  Preferences prefs;
  if (!prefs.begin(kStorageNamespace, true)) return false;
  const std::size_t length = prefs.getBytesLength(kRecordKey);
  if (length != wifi_setup_model::kRecordSize) {
    prefs.end();
    return false;
  }
  uint8_t record[wifi_setup_model::kRecordSize];
  const std::size_t read = prefs.getBytes(kRecordKey, record, sizeof(record));
  prefs.end();
  return read == sizeof(record) && wifi_setup_model::decodeRecord(record, sizeof(record), credentials);
}

bool loadLegacy(wifi_setup_model::Credentials& credentials) {
  Preferences prefs;
  if (!prefs.begin(kLegacyNamespace, true)) return false;
  const String ssid = prefs.getString(kLegacySsidKey, "");
  const String password = prefs.getString(kLegacyPasswordKey, "");
  prefs.end();
  credentials.ssid = toStd(ssid);
  credentials.password = toStd(password);
  if (isPlaceholder(credentials.ssid) || !wifi_setup_model::validCredentials(credentials)) return false;
  return true;
}

bool loadDefault(wifi_setup_model::Credentials& credentials) {
  credentials.ssid = toStd(defaultSsid);
  credentials.password = toStd(defaultPassword);
  if (isPlaceholder(credentials.ssid) || !wifi_setup_model::validCredentials(credentials)) return false;
  return true;
}

bool loadFallback(wifi_setup_model::Credentials& credentials) {
  // The replacement record is authoritative when valid.  An invalid or
  // absent record must leave the old wifi namespace untouched and usable.
  if (loadRecord(credentials)) return true;
  if (loadLegacy(credentials)) return true;
  return loadDefault(credentials);
}

bool persistRecord(const wifi_setup_model::Credentials& credentials) {
  uint8_t record[wifi_setup_model::kRecordSize];
  const std::size_t encoded = wifi_setup_model::encodeRecord(credentials, record, sizeof(record));
  if (encoded != sizeof(record)) return false;

  Preferences prefs;
  if (!prefs.begin(kStorageNamespace, false)) return false;
  // NVS updates one value atomically.  Read it back and validate the complete
  // blob before considering the replacement active.
  const std::size_t written = prefs.putBytes(kRecordKey, record, sizeof(record));
  const std::size_t stored = prefs.getBytesLength(kRecordKey);
  uint8_t verify[wifi_setup_model::kRecordSize];
  const std::size_t read = stored == sizeof(verify) ? prefs.getBytes(kRecordKey, verify, sizeof(verify)) : 0;
  const bool ok = written == sizeof(record) && read == sizeof(verify) &&
                  wifi_setup_model::validRecord(verify, sizeof(verify)) &&
                  std::memcmp(record, verify, sizeof(record)) == 0;
  prefs.end();
  return ok;
}

String htmlEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (std::size_t i = 0; i < value.length(); ++i) {
    switch (value[i]) {
      case '&': escaped += "&amp;"; break;
      case '<': escaped += "&lt;"; break;
      case '>': escaped += "&gt;"; break;
      case '"': escaped += "&quot;"; break;
      case '\'': escaped += "&#39;"; break;
      case '\n': escaped += "&#10;"; break;
      case '\r': escaped += "&#13;"; break;
      case '\t': escaped += "&#9;"; break;
      default:
        if (static_cast<unsigned char>(value[i]) < 0x20 || value[i] == 0x7f) {
          escaped += "&#" + String(static_cast<unsigned char>(value[i])) + ";";
        } else {
          escaped += value[i];
        }
        break;
    }
  }
  return escaped;
}

String randomToken() {
  String token;
  token.reserve(32);
  for (uint8_t i = 0; i < 4; ++i) {
    const uint32_t value = esp_random();
    char part[9];
    snprintf(part, sizeof(part), "%08lx", static_cast<unsigned long>(value));
    token += part;
  }
  return token;
}

bool originAllowed(WebServer& web) {
  const String origin = web.header("Origin");
  if (!origin.length()) return true;
  const String apOrigin = String("http://") + WiFi.softAPIP().toString();
  const String lanOrigin = WiFi.status() == WL_CONNECTED
      ? String("http://") + WiFi.localIP().toString() : String();
  return origin == apOrigin || (lanOrigin.length() && origin == lanOrigin) ||
         origin == "http://c3adblock.local";
}

bool validCsrfAndOrigin(WebServer& web) {
  if (!originAllowed(web) || web.arg("csrf") != csrfToken) {
    web.send(403, "text/plain", tr("CSRF rejected", "CSRF rechazado"));
    return false;
  }
  return true;
}

void sendPortalRedirect(WebServer& web) {
  web.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
  web.send(302, "text/plain", tr("captive portal", "portal cautivo"));
}

String statusJson() {
  String json = String("{\"state\":\"") + stateName() + "\",\"language\":\"" +
                i18n::code() + "\",\"ap\":\"" +
                WiFi.softAPIP().toString() + "\"";
  if (portalState == PortalState::Connected && WiFi.status() == WL_CONNECTED) {
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\",\"mdns\":\"c3adblock.local\"";
  }
  json += "}";
  return json;
}

String portalPage() {
  String status = tr("Choose a network and enter its password.",
                     "Elige una red y escribe su contraseña.");
  if (portalState == PortalState::Connecting) {
    status = tr("Testing the connection; the saved network is still protected.",
                 "Probando la conexión; la red guardada sigue protegida.");
  }
  if (portalState == PortalState::Failed) {
    status = tr("Could not connect. The previous configuration is still active; try again.",
                 "No se pudo conectar. La configuración anterior sigue activa; inténtalo de nuevo.");
  }
  if (portalState == PortalState::Connected) {
    status = String(tr("Connected. New IP: ", "Conectado. IP nueva: ")) + WiFi.localIP().toString() +
             tr(". Reconnect your phone to that WiFi, then open c3adblock.local.",
                ". Vuelve a conectar el teléfono a esa WiFi; después abre c3adblock.local.");
  }
  if (portalState == PortalState::Cancelled) {
    status = tr("Configuration cancelled.", "Configuración cancelada.");
  }
  String html = String("<!doctype html><html lang='") + i18n::code() +
                "'><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
                "<title>C3 AdBlock · WiFi</title><style>body{font:16px system-ui,sans-serif;max-width:460px;margin:30px auto;padding:0 16px;background:#0d1117;color:#c9d1d9}"
                "input,button,select{width:100%;box-sizing:border-box;padding:11px;margin:6px 0;border-radius:6px}input,select{border:1px solid #30363d;background:#161b22;color:#c9d1d9}"
                "button{border:0;background:#3fb950;color:#000;font-weight:600}button.cancel{background:#30363d;color:#c9d1d9}small{color:#8b949e}</style>"
                "</head><body><h2>🛡️ C3 AdBlock · WiFi</h2><p>" + status + "</p>";
  if (portalState == PortalState::Idle || portalState == PortalState::Failed) {
    html += "<form method=POST action=/wifisave><input type=hidden name=csrf value='" + htmlEscape(csrfToken) +
            "'><label>" + tr("WiFi network", "Red WiFi") +
            "<input list=nets name=s maxlength=32 required autocomplete=off></label><datalist id=nets>" +
            scannedOptions + "</datalist><label>" + tr("Password", "Contraseña") +
            "<input name=p type=password maxlength=63 autocomplete=new-password></label><button>" +
            tr("Try and save", "Probar y guardar") +
            "</button></form><form method=POST action=/wifi/cancel><input type=hidden name=csrf value='" +
            htmlEscape(csrfToken) + "'><button class=cancel>" + tr("Cancel", "Cancelar") +
            "</button></form>";
    const char* selectedSpanish = i18n::english() ? "" : " selected";
    const char* selectedEnglish = i18n::english() ? " selected" : "";
    html += String("<form method=POST action=/wifi/language><input type=hidden name=csrf value='") +
            htmlEscape(csrfToken) + "'><label for=portal-language>" +
            tr("Language", "Idioma") + "<select id=portal-language name=lang>" +
            String("<option value=es") + selectedSpanish + ">" +
            tr("Spanish", "Español") + "</option><option value=en" + selectedEnglish + ">" +
            tr("English", "English") + "</option></select></label><button>" +
            tr("Apply language", "Aplicar idioma") + "</button></form>";
  } else if (portalState == PortalState::Connecting) {
    html += "<form method=POST action=/wifi/cancel><input type=hidden name=csrf value='" +
            htmlEscape(csrfToken) + "'><button class=cancel>" + tr("Cancel", "Cancelar") +
            "</button></form>";
  }
  html += String("<p><small>") +
          tr("The device keeps the previous configuration until the new network is verified.",
             "El dispositivo conserva la configuración anterior hasta comprobar la nueva red.") +
          "</small></p>"
          "<script>const initial='" + String(stateName()) + "',initialLanguage='" +
          i18n::code() + "';setInterval(()=>fetch('/wifi/status').then(r=>r.json()).then(s=>{if(s.state!==initial||s.language!==initialLanguage)location.reload()}).catch(()=>{}),1000)</script>";
  return html + "</body></html>";
}

void handlePortalRoot() {
  if (!portalWeb) return;
  lastActivityAt = millis();
  portalWeb->send(200, "text/html; charset=utf-8", portalPage());
}

void handleStatus() {
  if (!portalWeb) return;
  portalWeb->sendHeader("Cache-Control", "no-store");
  portalWeb->send(200, "application/json", statusJson());
}

void startOldConnection() {
  if (!fallbackAvailable) return;
  WiFi.begin(fallback.ssid.c_str(), fallback.password.c_str());
}

void failCandidate() {
  candidateSsid = "";
  candidatePassword = "";
  portalState = PortalState::Failed;
  stateChangedAt = millis();
  startOldConnection();
}

void handleWifiSave() {
  if (!portalWeb || !validCsrfAndOrigin(*portalWeb)) return;
  lastActivityAt = millis();
  if (portalState != PortalState::Idle && portalState != PortalState::Failed) {
    portalWeb->send(409, "text/plain", tr("connection already in progress", "conexión ya en curso"));
    return;
  }
  const String ssid = portalWeb->arg("s");
  const String password = portalWeb->arg("p");
  const wifi_setup_model::Credentials candidate{toStd(ssid), toStd(password)};
  if (!wifi_setup_model::validCredentials(candidate)) {
    portalWeb->send(400, "text/plain", tr("invalid WiFi credentials", "credenciales WiFi no válidas"));
    return;
  }
  candidateSsid = ssid;
  candidatePassword = password;
  candidateStartedAt = millis();
  portalState = PortalState::Connecting;
  stateChangedAt = candidateStartedAt;
  // Drop any still-connected old station before judging the candidate.  This
  // prevents a same-SSID attempt from being mistaken for a successful new
  // password while retaining the AP interface.
  WiFi.disconnect(false, false);
  WiFi.begin(candidateSsid.c_str(), candidatePassword.c_str());
  // Return to the live portal page after a native form submission.
  portalWeb->sendHeader("Location", "/", true);
  portalWeb->send(303, "text/plain", "connecting");
}

void handleLanguage() {
  if (!portalWeb || !validCsrfAndOrigin(*portalWeb)) return;
  lastActivityAt = millis();
  if (portalState != PortalState::Idle && portalState != PortalState::Failed) {
    portalWeb->send(409, "text/plain",
                    tr("language change unavailable while WiFi setup is active",
                       "cambio de idioma no disponible mientras la configuración WiFi está activa"));
    return;
  }
  const String requested = portalWeb->arg("lang");
  if (requested != "es" && requested != "en") {
    portalWeb->send(400, "text/plain", tr("invalid language", "idioma no válido"));
    return;
  }
  if (!i18n::setLanguage(requested.c_str())) {
    portalWeb->send(500, "text/plain", tr("could not save language", "no se pudo guardar el idioma"));
    return;
  }
  portalWeb->sendHeader("Location", "/", true);
  portalWeb->send(303, "text/plain", tr("language updated", "idioma actualizado"));
}

void handleCancel() {
  if (!portalWeb || !validCsrfAndOrigin(*portalWeb)) return;
  if (portalState == PortalState::Connected) {
    portalWeb->send(409, "text/plain", tr("network already saved", "red ya guardada"));
    return;
  }
  lastActivityAt = millis();
  candidateSsid = "";
  candidatePassword = "";
  portalState = PortalState::Cancelled;
  stateChangedAt = millis();
  if (fallbackAvailable) rebootAt = stateChangedAt + 1000;
  portalWeb->sendHeader("Location", "/", true);
  portalWeb->send(303, "text/plain", "cancelled");
}

void handleNotFound() {
  if (!portalWeb) return;
  const String uri = portalWeb->uri();
  if (uri == "/generate_204" || uri == "/hotspot-detect.html" || uri == "/connecttest.txt" ||
      uri == "/ncsi.txt" || uri == "/canonical.html") {
    sendPortalRedirect(*portalWeb);
    return;
  }
  handlePortalRoot();
}

void checkPortalState() {
  const uint32_t now = millis();
  if (portalState == PortalState::Connecting) {
    if (WiFi.status() == WL_CONNECTED && WiFi.SSID() == candidateSsid) {
      const wifi_setup_model::Credentials candidate{toStd(candidateSsid), toStd(candidatePassword)};
      if (persistRecord(candidate)) {
        portalState = PortalState::Connected;
        stateChangedAt = now;
        rebootAt = now + kRebootGraceMs;
        candidatePassword = "";
      } else {
        failCandidate();
      }
    } else if (now - candidateStartedAt >= kConnectDeadlineMs) {
      failCandidate();
    }
  }
  if (portalState == PortalState::Cancelled && !fallbackAvailable &&
      now - stateChangedAt >= 1000) {
    portalState = PortalState::Idle;
  }
  if (rebootAt != 0 && static_cast<int32_t>(now - rebootAt) >= 0) {
    pet_runtime::checkpoint();
    ESP.restart();
  }
  if (rebootAt == 0 && fallbackAvailable && now - lastActivityAt >= kPortalIdleDeadlineMs &&
      portalState != PortalState::Connecting) {
    pet_runtime::checkpoint();
    ESP.restart();
  }
}

void collectNetworks() {
  scannedOptions = "";
  const int count = WiFi.scanNetworks();
  for (int i = 0; i < count && i < 15; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length()) scannedOptions += "<option value=\"" + htmlEscape(ssid) + "\">";
  }
  WiFi.scanDelete();
}

}  // namespace

void begin(const char* configuredSsid, const char* configuredPassword) {
  defaultSsid = configuredSsid ? configuredSsid : "";
  defaultPassword = configuredPassword ? configuredPassword : "";
  apName = "";
  csrfToken = "";
  portalState = PortalState::Idle;
  rebootAt = 0;
}

bool connect(void (*service)()) {
  wifi_setup_model::Credentials credentials;
  if (!loadFallback(credentials)) return false;
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.begin(credentials.ssid.c_str(), credentials.password.c_str());
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < kConnectDeadlineMs) {
    if (service) service();
    delay(10);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool consumeSetupRequest() {
  Preferences prefs;
  if (!prefs.begin(kStorageNamespace, false)) return false;
  const bool requested = prefs.getBool(kRequestKey, false);
  if (requested) {
    const std::size_t written = prefs.putBool(kRequestKey, false);
    const bool consumed = written == sizeof(bool) && !prefs.getBool(kRequestKey, true);
    if (!consumed) {
      // Enter the portal even when clearing the flag failed.  The next boot
      // will retry the one-shot clear instead of silently skipping a request.
    }
  }
  prefs.end();
  return requested;
}

bool requestSetup() {
  Preferences prefs;
  if (!prefs.begin(kStorageNamespace, false)) return false;
  const std::size_t written = prefs.putBool(kRequestKey, true);
  const bool verified = written == sizeof(bool) && prefs.getBool(kRequestKey, false);
  prefs.end();
  return verified;
}

String accessPointName() {
  if (apName.length()) return apName;
  uint8_t mac[6] = {};
  WiFi.macAddress(mac);
  char name[24];
  snprintf(name, sizeof(name), "C3-AdBlock-%02X%02X", mac[4], mac[5]);
  apName = name;
  return apName;
}

void runPortal(WebServer& web, void (*service)(bool, const char*)) {
  portalWeb = &web;
  portalService = service;
  portalStartedAt = millis();
  stateChangedAt = portalStartedAt;
  lastActivityAt = portalStartedAt;
  rebootAt = 0;
  portalState = PortalState::Idle;
  csrfToken = randomToken();
  fallbackAvailable = loadFallback(fallback);

  WiFi.mode(WIFI_AP_STA);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  const String ap = accessPointName();
  WiFi.softAP(ap.c_str());
  collectNetworks();
  startOldConnection();
  portalDns.start(53, "*", WiFi.softAPIP());

  static const char* headers[] = {"Origin"};
  web.collectHeaders(headers, 1);
  web.on("/", HTTP_GET, handlePortalRoot);
  web.on("/wifisave", HTTP_POST, handleWifiSave);
  web.on("/wifi/language", HTTP_POST, handleLanguage);
  web.on("/wifi/cancel", HTTP_POST, handleCancel);
  web.on("/wificancel", HTTP_POST, handleCancel);
  web.on("/wifi/status", HTTP_GET, handleStatus);
  web.onNotFound(handleNotFound);
  web.begin();

  Serial.printf("[setup] portal AP %s at http://%s\n", ap.c_str(), WiFi.softAPIP().toString().c_str());
  while (true) {
    portalDns.processNextRequest();
    web.handleClient();
    checkPortalState();
    if (portalService) portalService(true, ap.c_str());
    delay(2);
  }
}

}  // namespace wifi_setup
