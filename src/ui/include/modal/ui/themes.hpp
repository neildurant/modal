#pragma once

#include <modal/ui/style.hpp>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// The themes built into the program: dark, the default, and light. Each is a command file
// of highlight lines, assets/themes/NAME, built in as text.
std::span<const std::string_view> builtInThemeNames();
std::optional<std::string_view> builtInThemeText(std::string_view name);

// A built-in theme, if there's one of that name.
std::optional<Theme> builtInTheme(std::string_view name);

// The highlight command, highlight ROLE #rrggbb, which sets a role's colour. Returns why
// not if the arguments aren't a role and a colour, or else nothing.
std::string highlight(Theme& theme, const std::vector<std::string>& arguments);

} // namespace modal::ui
