#pragma once
#include <Arduino.h>

namespace i18n {
// One appliance-wide preference for dashboards, display and portal.
void begin();
bool setLanguage(const char* code);
bool english();
const char* code();
const char* text(const char* englishText, const char* spanishText);
String status(const String& canonical);
}  // namespace i18n
