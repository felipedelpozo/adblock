#pragma once

#include "query_log.h"

class WebServer;

namespace blocked_log {
void record(const char* domain, uint32_t client, uint16_t type, Reason reason);
void handleRequest(WebServer& web);
}  // namespace blocked_log
