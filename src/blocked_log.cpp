#include "blocked_log.h"

#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_timer.h>

namespace {
blocked_log::Ring history;
constexpr int kPageSize = 16;

uint32_t uptimeSeconds() {
  // Unlike millis(), this does not wrap after 49 days.
  return static_cast<uint32_t>(esp_timer_get_time() / 1000000ULL);
}

void appendQuoted(String& json, const char* value) {
  json += '"';
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
    char escaped[7];
    blocked_log::escapeJsonByte(*p, escaped);
    json += escaped;
  }
  json += '"';
}

bool matches(const blocked_log::Entry& entry, const String& query) {
  if (blocked_log::containsIgnoreCase(entry.domain, query.c_str())) return true;
  const String ip = IPAddress(entry.client).toString();
  return blocked_log::containsIgnoreCase(ip.c_str(), query.c_str());
}

const char* reasonName(blocked_log::Reason reason) {
  switch (reason) {
    case blocked_log::Reason::Custom: return "custom";
    case blocked_log::Reason::Client: return "client";
    default: return "blocklist";
  }
}
}  // namespace

namespace blocked_log {

void record(const char* domain, uint32_t client, uint16_t type, Reason reason) {
  history.record(domain, client, type, reason, uptimeSeconds());
}

void handleRequest(WebServer& web) {
  const long requestedOffset = web.arg("offset").toInt();
  const size_t offset = requestedOffset > 0
      ? (static_cast<size_t>(requestedOffset) > Ring::kCapacity ? Ring::kCapacity : requestedOffset) : 0;
  const long requestedLimit = web.arg("limit").toInt();
  const int limit = requestedLimit > 0 && requestedLimit < kPageSize ? requestedLimit : kPageSize;
  String query = web.arg("q").substring(0, 63);
  query.trim();
  size_t count = 0;
  for (size_t i = 0; i < history.size(); ++i) if (matches(*history.newest(i), query)) ++count;

  String json;
  json.reserve(6144);
  json = "{\"capacity\":64,\"count\":";
  json += count;
  json += ",\"total\":"; json += history.size();
  json += ",\"offset\":"; json += offset;
  json += ",\"limit\":"; json += limit;
  json += ",\"more\":"; json += (count > offset + limit ? "true" : "false");
  json += ",\"entries\":[";
  size_t matched = 0;
  size_t written = 0;
  const uint32_t now = uptimeSeconds();
  for (size_t i = 0; i < history.size() && written < static_cast<size_t>(limit); ++i) {
    const Entry& entry = *history.newest(i);
    if (!matches(entry, query)) continue;
    if (matched++ < offset) continue;
    if (written++) json += ',';
    json += "{\"id\":"; json += entry.id;
    json += ",\"domain\":"; appendQuoted(json, entry.domain);
    json += ",\"client\":"; appendQuoted(json, IPAddress(entry.client).toString().c_str());
    json += ",\"type\":"; json += entry.type;
    json += ",\"reason\":"; appendQuoted(json, reasonName(entry.reason));
    json += ",\"ageSeconds\":"; json += now - entry.uptimeSeconds;
    json += '}';
  }
  json += "]}";
  web.sendHeader("Cache-Control", "no-store");
  web.send(200, "application/json", json);
}
}  // namespace blocked_log
