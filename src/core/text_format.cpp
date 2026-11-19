// SPDX-License-Identifier: GPL-3.0-or-later

#include "text_format.hpp"

#include <charconv>
#include <utility>

namespace modal::core::textFormat
{
namespace
{
constexpr std::string_view kHexDigits = "0123456789ABCDEF";

// The length of the UTF-8 sequence starting at `i`, or 0 if there isn't a valid one.
std::size_t utf8Length(std::string_view text, std::size_t i)
{
    const auto lead = static_cast<unsigned char>(text[i]);
    std::size_t length = 0;
    if (lead < 0x80)
    {
        length = 1;
    }
    else if (lead >= 0xC2 && lead <= 0xDF)
    {
        length = 2;
    }
    else if (lead >= 0xE0 && lead <= 0xEF)
    {
        length = 3;
    }
    else if (lead >= 0xF0 && lead <= 0xF4)
    {
        length = 4;
    }
    if (length == 0 || i + length > text.size())
    {
        return 0;
    }
    for (std::size_t k = 1; k < length; ++k)
    {
        if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80)
        {
            return 0;
        }
    }
    return length;
}

} // namespace

// Text in double quotes, with \" and \\, and \xNN for control characters and for bytes that
// aren't UTF-8, as an imported file's raw text may have.
std::string quotedText(std::string_view text)
{
    std::string out = "\"";
    for (std::size_t i = 0; i < text.size();)
    {
        const auto byte = static_cast<unsigned char>(text[i]);
        const auto length = utf8Length(text, i);
        if (byte == '"' || byte == '\\')
        {
            out += '\\';
            out += text[i++];
        }
        else if (length == 0 || byte < 0x20 || byte == 0x7F)
        {
            out += "\\x";
            out += kHexDigits[byte >> 4];
            out += kHexDigits[byte & 0xF];
            ++i;
        }
        else
        {
            out += text.substr(i, length);
            i += length;
        }
    }
    out += '"';
    return out;
}

// Splits a line into tokens at spaces and tabs, reading quoted text with its escapes, also
// as part of a token (port="My Port"). Returns an error message for a broken quote.
std::string tokenize(std::string_view line, std::vector<Token>& tokens)
{
    const auto hex = [](char c) -> int
    {
        const auto at = kHexDigits.find(static_cast<char>(c >= 'a' ? c - 'a' + 'A' : c));
        return at == std::string_view::npos ? -1 : static_cast<int>(at);
    };
    std::size_t i = 0;
    for (;;)
    {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
        {
            ++i;
        }
        if (i == line.size())
        {
            return {};
        }
        Token token;
        token.quoted = line[i] == '"';
        while (i < line.size() && line[i] != ' ' && line[i] != '\t')
        {
            if (line[i] != '"')
            {
                token.text += line[i++];
                continue;
            }
            ++i;
            for (;;)
            {
                if (i == line.size())
                {
                    return "a quote isn't closed";
                }
                const char c = line[i++];
                if (c == '"')
                {
                    break;
                }
                if (c != '\\')
                {
                    token.text += c;
                    continue;
                }
                const char escaped = i < line.size() ? line[i++] : '\0';
                if (escaped == '"' || escaped == '\\')
                {
                    token.text += escaped;
                }
                else if (escaped == 'x' && i + 2 <= line.size() && hex(line[i]) >= 0 &&
                         hex(line[i + 1]) >= 0)
                {
                    token.text += static_cast<char>(hex(line[i]) * 16 + hex(line[i + 1]));
                    i += 2;
                }
                else
                {
                    return "unknown escape in quoted text";
                }
            }
        }
        tokens.push_back(std::move(token));
    }
}

std::optional<std::int64_t> parseInteger(std::string_view text, std::int64_t min, std::int64_t max)
{
    if (!text.empty() && text[0] == '+')
    {
        text.remove_prefix(1);
    }
    std::int64_t value = 0;
    const auto* const end = text.data() + text.size();
    const auto [ptr, error] = std::from_chars(text.data(), end, value);
    if (text.empty() || error != std::errc{} || ptr != end || value < min || value > max)
    {
        return std::nullopt;
    }
    return value;
}

} // namespace modal::core::textFormat
