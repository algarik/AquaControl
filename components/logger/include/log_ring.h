// AquaControl — In-RAM log ring (Phase 3.5 Block E15).
//
// Tees ESP-IDF log output into a 50-line ring buffer that the UI ("System
// Info → Logs") and the future web dashboard can dump.
//
// Usage:
//   aqua::log_ring::init();          // install vprintf hook once at boot
//   auto lines = aqua::log_ring::snapshot();   // newest-first vector
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace aqua::log_ring {

void init();
std::vector<std::string> snapshot();
void clear();

}  // namespace aqua::log_ring
