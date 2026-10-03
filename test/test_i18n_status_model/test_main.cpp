#include <string>
#include <unity.h>

#include "i18n_status_model.h"

void test_spanish_is_the_identity_locale() {
  const std::string status = "Lista rechazada: hashes vacíos, duplicados o desordenados";
  TEST_ASSERT_EQUAL_STRING(status.c_str(), i18n_status_model::translate(status, false).c_str());
  TEST_ASSERT_EQUAL_STRING("Nunca comprobado",
                           i18n_status_model::translate("Nunca comprobado", false).c_str());
}

void test_github_and_list_errors_translate() {
  TEST_ASSERT_EQUAL_STRING("OTA coordinator unavailable",
                           i18n_status_model::translate("Coordinador OTA no disponible", true).c_str());
  TEST_ASSERT_EQUAL_STRING("Time not synchronized; cannot validate TLS",
                           i18n_status_model::translate("Hora no sincronizada; no se puede validar TLS", true).c_str());
  TEST_ASSERT_EQUAL_STRING("List rejected: empty, duplicate, or unsorted hashes",
                           i18n_status_model::translate("Lista rechazada: hashes vacíos, duplicados o desordenados", true).c_str());
  TEST_ASSERT_EQUAL_STRING("Could not validate list; keeping active list",
                           i18n_status_model::translate("No se pudo validar la lista; se conserva la activa", true).c_str());
}

void test_dynamic_version_profile_and_count_payloads_are_preserved() {
  TEST_ASSERT_EQUAL_STRING("Installed version is up to date (v0.2.1)",
                           i18n_status_model::translate("La versión instalada está al día (v0.2.1)", true).c_str());
  TEST_ASSERT_EQUAL_STRING("Release ready to install: v0.3.0",
                           i18n_status_model::translate("Release lista para instalar: v0.3.0", true).c_str());
  TEST_ASSERT_EQUAL_STRING("Downloading profile pro++…",
                           i18n_status_model::translate("Descargando perfil pro++…", true).c_str());
  TEST_ASSERT_EQUAL_STRING("OK: 123456 domains",
                           i18n_status_model::translate("ok: 123456 domains", true).c_str());
  TEST_ASSERT_EQUAL_STRING("Correcto: 123456 dominios",
                           i18n_status_model::translate("ok: 123456 domains", false).c_str());
  TEST_ASSERT_EQUAL_STRING("Firmware verified; rebooting in v0.3.0",
                           i18n_status_model::translate("Firmware verificado; reiniciando en v0.3.0", true).c_str());
}

void test_existing_english_update_statuses_remain_stable() {
  TEST_ASSERT_EQUAL_STRING("Never checked",
                           i18n_status_model::translate("never", true).c_str());
  TEST_ASSERT_EQUAL_STRING("Nunca comprobado",
                           i18n_status_model::translate("never", false).c_str());
  TEST_ASSERT_EQUAL_STRING("No URL set",
                           i18n_status_model::translate("no url set", true).c_str());
  TEST_ASSERT_EQUAL_STRING("URL no configurada",
                           i18n_status_model::translate("no url set", false).c_str());
  TEST_ASSERT_EQUAL_STRING("Download started",
                           i18n_status_model::translate("download started", true).c_str());
  TEST_ASSERT_EQUAL_STRING("Descarga iniciada",
                           i18n_status_model::translate("download started", false).c_str());
  TEST_ASSERT_EQUAL_STRING("Profile refresh unavailable",
                           i18n_status_model::translate("profile refresh unavailable", true).c_str());
  TEST_ASSERT_EQUAL_STRING("Actualización del perfil no disponible",
                           i18n_status_model::translate("profile refresh unavailable", false).c_str());
  TEST_ASSERT_EQUAL_STRING("GitHub HTTP 503",
                           i18n_status_model::translate("GitHub HTTP 503", true).c_str());
}

void test_unknown_text_and_non_numeric_count_payloads_are_untouched() {
  const std::string unknown = "SSID cocina-5G / example.com";
  TEST_ASSERT_EQUAL_STRING(unknown.c_str(), i18n_status_model::translate(unknown, true).c_str());
  const std::string malformedCount = "ok: example.com domains";
  TEST_ASSERT_EQUAL_STRING(malformedCount.c_str(), i18n_status_model::translate(malformedCount, true).c_str());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_spanish_is_the_identity_locale);
  RUN_TEST(test_github_and_list_errors_translate);
  RUN_TEST(test_dynamic_version_profile_and_count_payloads_are_preserved);
  RUN_TEST(test_existing_english_update_statuses_remain_stable);
  RUN_TEST(test_unknown_text_and_non_numeric_count_payloads_are_untouched);
  return UNITY_END();
}
