// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string_view>

namespace modal::core
{

// The project version, e.g. "0.1.0".
std::string_view version() noexcept;

} // namespace modal::core
