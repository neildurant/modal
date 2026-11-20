// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace modal::ui
{

// When a recent song was last used, as the recent list shows it (decided 20 November 2026):
// today 17:42, yesterday 09:10, Mon 18 Nov within the year, 2 Mar 2025 before; from local
// times, then from seconds since 1970 in the local time zone (or UTC where there's none).
std::string usedText(std::chrono::local_seconds used, std::chrono::local_seconds now);
std::string usedText(std::int64_t used, std::int64_t now);

} // namespace modal::ui
