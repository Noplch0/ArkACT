#pragma once

#include <string>
#include <vector>

#include <ark/plugin.h>

#include "match_intel.h"
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
// Copied on the game thread; safe to read from the render thread while the
// plugin remains loaded. The latest snapshot is retained after mode exit.
MatchIntelSnapshot capture_match_intel();
// Full AllStateSync snapshot copied from the read-only protocol parser hook.
// Unlike MatchIntelSnapshot, this includes every player's bannedUnits and full
// preparation store when the current client exposes them.
HiddenSyncSnapshot capture_hidden_sync();
struct ShopObservationBatch {
    std::vector<ShopObservation> observations;
    bool incomplete = false;
};
ShopObservationBatch drain_shop_observations();
void request_manual_reset();

} // namespace ark::stronghold
