#include <unity.h>
#include "i18n_model.h"

void test_language_codes_are_explicit_and_round_trip() {
  i18n_model::Language language;
  TEST_ASSERT_TRUE(i18n_model::parse("en", language));
  TEST_ASSERT_EQUAL_STRING("en", i18n_model::code(language));
  TEST_ASSERT_TRUE(i18n_model::parse("es", language));
  TEST_ASSERT_EQUAL_STRING("es", i18n_model::code(language));
}
void test_invalid_codes_do_not_replace_current_preference() {
  auto language = i18n_model::Language::Spanish;
  const char* codes[] = {nullptr, "", "EN", "en-US", "fr", " es", "es<script>"};
  for (const char* code : codes) {
    TEST_ASSERT_FALSE(i18n_model::parse(code, language));
    TEST_ASSERT_EQUAL_STRING("es", i18n_model::code(language));
  }
}
int main() {
  UNITY_BEGIN();
  RUN_TEST(test_language_codes_are_explicit_and_round_trip);
  RUN_TEST(test_invalid_codes_do_not_replace_current_preference);
  return UNITY_END();
}
