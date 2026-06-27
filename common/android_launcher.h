#pragma once

#include <string>

namespace bo2 {

// Call this to request a restart of the Android activity with a new app and mode.
void RequestAndroidRestart(const std::string& app, const std::string& mode);

} // namespace bo2
