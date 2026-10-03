#pragma once

#include <cstddef>
#include <string>

namespace i18n_status_model {

namespace detail {

struct Translation {
  const char* source;
  const char* english;
  const char* spanish;

  constexpr Translation(const char* sourceValue, const char* englishValue,
              const char* spanishValue = nullptr)
      : source(sourceValue),
        english(englishValue),
        spanish(spanishValue ? spanishValue : sourceValue) {}
};

inline bool startsWith(const std::string& value, const char* prefix) {
  const std::string candidate(prefix);
  return value.size() >= candidate.size() && value.compare(0, candidate.size(), candidate) == 0;
}

inline bool endsWith(const std::string& value, const char* suffix) {
  const std::string candidate(suffix);
  return value.size() >= candidate.size() &&
         value.compare(value.size() - candidate.size(), candidate.size(), candidate) == 0;
}

inline bool unsignedDecimal(const std::string& value) {
  if (value.empty()) return false;
  for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
    if (*it < '0' || *it > '9') return false;
  }
  return true;
}

inline bool dynamicTranslation(const std::string& raw, bool english, std::string& translated) {
  // Keep the payload opaque: versions, profile names and HTTP values are
  // produced elsewhere and must not be altered by locale translation.
  if (english) {
    const char* const versionPrefix = "La versión instalada está al día (";
    if (startsWith(raw, versionPrefix) && endsWith(raw, ")")) {
      translated = "Installed version is up to date (" +
                   raw.substr(std::string(versionPrefix).size());
      return true;
    }

    const char* const releasePrefix = "Release lista para instalar: ";
    if (startsWith(raw, releasePrefix)) {
      translated = "Release ready to install: " + raw.substr(std::string(releasePrefix).size());
      return true;
    }

    const char* const rebootPrefix = "Firmware verificado; reiniciando en ";
    if (startsWith(raw, rebootPrefix)) {
      translated = "Firmware verified; rebooting in " + raw.substr(std::string(rebootPrefix).size());
      return true;
    }

    const char* const profilePrefix = "Descargando perfil ";
    if (startsWith(raw, profilePrefix) && endsWith(raw, "…")) {
      translated = "Downloading profile " + raw.substr(std::string(profilePrefix).size());
      return true;
    }
  }

  const char* const countPrefix = "ok: ";
  const char* const countSuffix = " domains";
  const char* const uppercaseCountPrefix = "OK: ";
  const char* countMatch = startsWith(raw, countPrefix) ? countPrefix :
                           (startsWith(raw, uppercaseCountPrefix) ? uppercaseCountPrefix : nullptr);
  if (countMatch && endsWith(raw, countSuffix)) {
    const size_t payloadStart = std::string(countMatch).size();
    const size_t payloadLength = raw.size() - payloadStart - std::string(countSuffix).size();
    const std::string count = raw.substr(payloadStart, payloadLength);
    if (unsignedDecimal(count)) {
      translated = english ? "OK: " + count + countSuffix : "Correcto: " + count + " dominios";
      return true;
    }
  }

  return false;
}

}  // namespace detail

// Translate status values emitted by github_updater.cpp, blocklist_manager.cpp
// and main.cpp. Spanish source strings remain byte-for-byte stable, while
// recognized legacy English values are localized in either direction.
inline std::string translate(const std::string& raw, bool english) {
  static const detail::Translation kTranslations[] = {
      // GitHub updater and blocklist manager statuses.
      {"Coordinador OTA no disponible", "OTA coordinator unavailable"},
      {"No se pudo iniciar el updater", "Updater could not start"},
      {"Estado ocupado", "State busy"},
      {"Comprobando releases de GitHub…", "Checking GitHub releases…"},
      {"Hardware incompatible", "Incompatible hardware"},
      {"Hora no sincronizada; no se puede validar TLS", "Time not synchronized; cannot validate TLS"},
      {"No hay releases publicadas", "No published releases found"},
      {"GitHub limita las consultas; inténtalo más tarde", "GitHub is rate-limiting requests; try again later"},
      {"No se pudo validar TLS o conectar a GitHub", "Could not validate TLS or connect to GitHub"},
      {"Release sin manifest compatible", "Release has no compatible manifest"},
      {"Manifest inaccesible", "Manifest inaccessible"},
      {"Manifest incompatible con este perfil", "Manifest incompatible with this profile"},
      {"Firmware demasiado grande para el slot OTA", "Firmware too large for OTA slot"},
      {"Descargando firmware…", "Downloading firmware…"},
      {"Otra actualización OTA está en curso", "Another OTA update is in progress"},
      {"No se pudo abrir el firmware", "Could not open firmware"},
      {"Tamaño o respuesta del firmware no coincide", "Firmware size or response does not match"},
      {"No hay espacio para el firmware", "Not enough space for firmware"},
      {"Descarga incompleta", "Download incomplete"},
      {"SHA-256 del firmware no coincide", "Firmware SHA-256 does not match"},
      {"El binario no corresponde a este perfil o versión", "Binary does not match this profile or version"},
      {"La activación OTA falló", "OTA activation failed"},
      {"Coordinador de listas no disponible", "List coordinator unavailable"},
      {"No se pudo guardar el estado de listas", "Could not save list state"},
      {"Espacio insuficiente para conservar la lista activa", "Not enough space to keep the active list"},
      {"Lista aplicada; no se pudo guardar el perfil", "List applied; could not save profile"},
      {"Lista rechazada: hashes vacíos, duplicados o desordenados", "List rejected: empty, duplicate, or unsorted hashes"},
      {"No se pudo guardar el perfil seleccionado", "Could not save selected profile"},
      {"Actualizando perfil seleccionado…", "Refreshing selected profile…"},
      {"Descargando URL personalizada…", "Downloading custom URL…"},
      {"WiFi no conectado", "Wi-Fi not connected"},
      {"Hora TLS no disponible", "TLS time unavailable"},
      {"Manifest inválido o no disponible", "Manifest invalid or unavailable"},
      {"Descarga, SHA-256 o lista ordenada inválida", "Download, SHA-256, or sorted list invalid"},
      {"URL no disponible o lista inválida", "URL unavailable or list invalid"},
      {"No se pudo validar la lista; se conserva la activa", "Could not validate list; keeping active list"},
      {"No se pudo aplicar la lista; se conserva la activa", "Could not apply list; keeping active list"},
      {"Nunca comprobado", "Never checked"},

      // Existing English updateStatus values are listed explicitly so the
      // complete status contract remains visible in one locale table.
      {"never", "Never checked", "Nunca comprobado"},
      {"no url set", "No URL set", "URL no configurada"},
      {"firmware updater busy", "Firmware updater busy", "Actualizador de firmware ocupado"},
      {"busy or unavailable", "Busy or unavailable", "Ocupado o no disponible"},
      {"download started", "Download started", "Descarga iniciada"},
      {"profile refresh started", "Profile refresh started", "Actualización del perfil iniciada"},
      {"profile refresh unavailable", "Profile refresh unavailable", "Actualización del perfil no disponible"},
  };

  for (size_t index = 0; index < sizeof(kTranslations) / sizeof(kTranslations[0]); ++index) {
    if (raw == kTranslations[index].source) {
      return english ? kTranslations[index].english : kTranslations[index].spanish;
    }
  }

  std::string dynamic;
  return detail::dynamicTranslation(raw, english, dynamic) ? dynamic : raw;
}

}  // namespace i18n_status_model
