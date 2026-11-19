// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Text shared by the song and drum-map formats. Private to the core library.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace modal::core::textFormat
{
struct Token
{
    std::string text;
    bool quoted = false; // the token starts with a quote, so it's text, never a setting
};

std::string quotedText(std::string_view text);
std::string tokenize(std::string_view line, std::vector<Token>& tokens);
std::optional<std::int64_t> parseInteger(std::string_view text, std::int64_t min, std::int64_t max);
} // namespace modal::core::textFormat
