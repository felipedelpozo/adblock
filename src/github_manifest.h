#pragma once

#include <string>
#include <ArduinoJson.h>
#include "updater_model.h"

namespace github_manifest {

struct Build {
  std::string version, asset, url, sha256;
  uint32_t size = 0;
};

inline bool parseRelease(const std::string& text, std::string& tag, std::string& manifestUrl) {
  // Ignore release body/author metadata so large descriptions do not consume
  // the C3 heap. The HTTP transport separately limits the complete response.
  StaticJsonDocument<512> filter;
  filter["draft"] = true;
  filter["prerelease"] = true;
  filter["tag_name"] = true;
  filter["assets"][0]["name"] = true;
  filter["assets"][0]["browser_download_url"] = true;
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, text, DeserializationOption::Filter(filter)) || !doc.is<JsonObject>()) return false;
  const JsonObject release = doc.as<JsonObject>();
  if (!release["draft"].is<bool>() || !release["prerelease"].is<bool>() ||
      release["draft"].as<bool>() || release["prerelease"].as<bool>()) return false;
  tag = release["tag_name"] | "";
  if (tag.empty() || tag[0] != 'v' || !updater_model::strictSemver(tag)) return false;
  const JsonArray assets = release["assets"].as<JsonArray>();
  unsigned matches = 0;
  for (const JsonObject asset : assets) {
    if (std::string(asset["name"] | "") == "manifest.json") {
      manifestUrl = asset["browser_download_url"] | "";
      ++matches;
    }
  }
  return matches == 1 && updater_model::canonicalAssetUrl(manifestUrl, tag, "manifest.json");
}

inline bool parseBuild(const std::string& text, const std::string& tag,
                       const char* profile, const char* chip, const char* board, Build& out) {
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, text) || !doc.is<JsonObject>()) return false;
  const JsonObject root = doc.as<JsonObject>();
  const std::string version = root["version"] | "";
  if (!root["schema"].is<unsigned>() || root["schema"].as<unsigned>() != 1 ||
      std::string(root["repository"] | "") != "felipedelpozo/adblock" ||
      version.empty() || version[0] == 'v' || !updater_model::strictSemver(version) ||
      tag != "v" + version) return false;
  const JsonArray builds = root["builds"].as<JsonArray>();
  if (builds.isNull()) return false;
  unsigned matches = 0;
  for (const JsonObject build : builds) {
    if (std::string(build["profile"] | "") != profile) continue;
    if (++matches != 1 || std::string(build["version"] | "") != version ||
        std::string(build["chip"] | "") != chip || std::string(build["board"] | "") != board ||
        !build["size"].is<uint32_t>()) return false;
    out.version = version;
    out.asset = build["asset"] | "";
    out.url = build["url"] | "";
    out.sha256 = build["sha256"] | "";
    out.size = build["size"].as<uint32_t>();
    if (out.asset != std::string("firmware-") + profile + ".bin" || out.size == 0 ||
        !updater_model::canonicalAssetUrl(out.url, tag, out.asset) ||
        !updater_model::strictSha256(out.sha256)) return false;
  }
  return matches == 1;
}

}  // namespace github_manifest
