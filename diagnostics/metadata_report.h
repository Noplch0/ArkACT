#pragma once

#include <string>

namespace ark::stronghold::diagnostics {

// Enumerates metadata without invoking gameplay methods. Optional runtime text
// is copied on the game thread, so exporters never read live player state.
// All output stays below the framework root (diagnostics/).
std::string write_metadata_report(const char* framework_root, const char* reason,
                                  const std::string& runtime_snapshot = {});

} // namespace ark::stronghold::diagnostics
