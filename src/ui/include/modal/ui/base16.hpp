// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/ui/style.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// A base16 or base24 colour scheme (decided 24 November 2026), as tinted-theming's collection
// and many others give them: sixteen colours, base00 to base0F, the background through the
// foreground and then eight accents (red, orange, yellow, green, cyan, blue, magenta, brown);
// base24 adds eight more, base10 to base17, darker backgrounds and brighter accents.
struct Base16Scheme
{
    std::string name;
    std::string author;
    std::vector<Colour> colours; // 16, or 24 for base24
};

// Reads a scheme's YAML, in tinted-theming's form (name, author and a palette block of
// "#rrggbb" colours) or the older one (scheme, author and the colours at the top level,
// without #). Says why not, naming the line, if it can't.
struct Base16Read
{
    std::optional<Base16Scheme> scheme;
    std::string error;
};
Base16Read readBase16(std::string_view text);

// The scheme as a Modal theme: a command file of highlight lines, one for every role, the
// roles given the scheme's colours by what each is for, a few blended between two.
std::string base16ThemeText(const Base16Scheme& scheme);

} // namespace modal::ui
