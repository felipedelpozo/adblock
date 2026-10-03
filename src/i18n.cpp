#include "i18n.h"
#include <Preferences.h>
#include "i18n_model.h"
#include "i18n_status_model.h"

namespace i18n {
namespace {
i18n_model::Language language = i18n_model::Language::Spanish;
constexpr char kNamespace[] = "ui";
constexpr char kLanguageKey[] = "language";
}

void begin() {
  language = i18n_model::Language::Spanish;
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) return;
  const String saved = prefs.getString(kLanguageKey, "es");
  prefs.end();
  i18n_model::parse(saved.c_str(), language);
}

bool setLanguage(const char* requested) {
  i18n_model::Language selected;
  if (!i18n_model::parse(requested, selected)) return false;
  if (selected == language) return true;
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) return false;
  const size_t written = prefs.putString(kLanguageKey, requested);
  const bool verified = written > 0 && prefs.getString(kLanguageKey, "") == requested;
  prefs.end();
  if (!verified) return false;
  language = selected;
  return true;
}

bool english() { return language == i18n_model::Language::English; }
const char* code() { return i18n_model::code(language); }
const char* text(const char* englishText, const char* spanishText) {
  return english() ? englishText : spanishText;
}
String status(const String& canonical) {
  const std::string translated = i18n_status_model::translate(
      std::string(canonical.c_str(), canonical.length()), english());
  return String(translated.c_str());
}
}  // namespace i18n
