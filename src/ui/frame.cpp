#include <modal/ui/frame.hpp>

#include <cstddef>

namespace modal::ui
{

std::u32string decodeUtf8(std::string_view text)
{
    constexpr char32_t kReplacement = 0xFFFD;
    std::u32string result;
    result.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size())
    {
        const auto lead = static_cast<unsigned char>(text[i]);
        int length = 0;
        char32_t character = 0;
        char32_t minimum = 0; // the smallest character this length may encode
        if (lead < 0x80)
        {
            result.push_back(lead);
            ++i;
            continue;
        }
        if ((lead & 0xE0) == 0xC0)
        {
            length = 2;
            character = lead & 0x1F;
            minimum = 0x80;
        }
        else if ((lead & 0xF0) == 0xE0)
        {
            length = 3;
            character = lead & 0x0F;
            minimum = 0x800;
        }
        else if ((lead & 0xF8) == 0xF0)
        {
            length = 4;
            character = lead & 0x07;
            minimum = 0x10000;
        }
        else
        {
            result.push_back(kReplacement); // a stray continuation byte or an invalid lead
            ++i;
            continue;
        }

        std::size_t next = i + 1;
        while (next < text.size() && next < i + static_cast<std::size_t>(length) &&
               (static_cast<unsigned char>(text[next]) & 0xC0) == 0x80)
        {
            character = (character << 6) | (static_cast<unsigned char>(text[next]) & 0x3F);
            ++next;
        }
        const bool complete = next == i + static_cast<std::size_t>(length);
        const bool valid = complete && character >= minimum && character <= 0x10FFFF &&
                           (character < 0xD800 || character > 0xDFFF);
        result.push_back(valid ? character : kReplacement);
        i = next; // a broken sequence is replaced as far as it went
    }
    return result;
}

} // namespace modal::ui
