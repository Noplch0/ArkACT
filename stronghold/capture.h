#pragma once

#include <string>

#include <ark/plugin.h>

#include "types.h"

namespace ark::stronghold {

EventQueue& event_queue();
bool initialize_capture(const ArkPluginApiV1* host);
bool shutdown_capture();
bool capture_ready();
bool capture_incompatible();
std::string capture_report_path();
std::string capture_error();
// Copied on the game thread; safe to export from the render thread.
std::string capture_diagnostics();
void request_manual_reset();

} // namespace ark::stronghold
