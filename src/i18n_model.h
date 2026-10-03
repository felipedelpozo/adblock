#pragma once
#include <cstring>

namespace i18n_model {
enum class Language { Spanish, English };
inline bool parse(const char* code, Language& language) {
  if (!code) return false;
  if (std::strcmp(code, "es") == 0) language = Language::Spanish;
  else if (std::strcmp(code, "en") == 0) language = Language::English;
  else return false;
  return true;
}
inline const char* code(Language language) {
  return language == Language::English ? "en" : "es";
}
}  // namespace i18n_model
