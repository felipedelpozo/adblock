#pragma once

#include <Arduino.h>

class WebServer;

namespace wifi_setup {

// Load the optional replacement record and retain the legacy wifi/ssid and
// wifi/pass fallback.  No filesystem or NVS namespace is erased here.
void begin(const char* defaultSsid, const char* defaultPass);

// Start a normal station connection.  The callback is called while waiting
// so the display and other lightweight service work continue to run.
bool connect(void (*service)());

// Consume the one-shot NVS request written by requestSetup().
bool consumeSetupRequest();

// Persist a one-shot request for entering the portal on the next boot.  The
// value is read back before this reports success.
bool requestSetup();

// Stable C3-AdBlock-XXXX AP name, derived from the station MAC.
String accessPointName();

// Run the captive portal until a successful replacement is given a short
// visible grace period and the firmware reboots.  The callback is called on
// every portal iteration with the AP name.
void runPortal(WebServer& web, void (*service)(bool, const char*));

}  // namespace wifi_setup
