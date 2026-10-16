#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <vector>

namespace modal::test
{

// Builds Standard MIDI File bytes for tests. Deltas are in the file's own ticks.
class SmfTrack
{
public:
    SmfTrack& event(std::uint32_t delta, std::initializer_list<std::uint8_t> bytes)
    {
        vlq(delta);
        bytes_.insert(bytes_.end(), bytes);
        return *this;
    }

    SmfTrack& meta(std::uint32_t delta, std::uint8_t type, const std::vector<std::uint8_t>& data)
    {
        vlq(delta);
        bytes_.push_back(0xFF);
        bytes_.push_back(type);
        vlq(static_cast<std::uint32_t>(data.size()));
        bytes_.insert(bytes_.end(), data.begin(), data.end());
        return *this;
    }

    SmfTrack& text(std::uint32_t delta, std::uint8_t type, std::string_view text)
    {
        return meta(delta, type, {text.begin(), text.end()});
    }

    SmfTrack& tempo(std::uint32_t delta, std::uint32_t microsPerQuarter)
    {
        return meta(delta, 0x51,
                    {static_cast<std::uint8_t>(microsPerQuarter >> 16),
                     static_cast<std::uint8_t>(microsPerQuarter >> 8),
                     static_cast<std::uint8_t>(microsPerQuarter)});
    }

    SmfTrack& timeSignature(std::uint32_t delta, std::uint8_t numerator,
                            std::uint8_t denominatorPower)
    {
        return meta(delta, 0x58, {numerator, denominatorPower, 24, 8});
    }

    // A SysEx or escape event: status F0 or F7, then the length and data.
    SmfTrack& sysex(std::uint32_t delta, std::uint8_t status, const std::vector<std::uint8_t>& data)
    {
        vlq(delta);
        bytes_.push_back(status);
        vlq(static_cast<std::uint32_t>(data.size()));
        bytes_.insert(bytes_.end(), data.begin(), data.end());
        return *this;
    }

    SmfTrack& end(std::uint32_t delta = 0) { return meta(delta, 0x2F, {}); }

    // Arbitrary bytes, for malformed data.
    SmfTrack& raw(std::initializer_list<std::uint8_t> bytes)
    {
        bytes_.insert(bytes_.end(), bytes);
        return *this;
    }

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }

private:
    void vlq(std::uint32_t value)
    {
        std::uint8_t groups[4];
        int count = 0;
        do
        {
            groups[count++] = static_cast<std::uint8_t>(value & 0x7F);
            value >>= 7;
        } while (value != 0 && count < 4);
        while (count > 0)
        {
            --count;
            bytes_.push_back(static_cast<std::uint8_t>(groups[count] | (count > 0 ? 0x80 : 0)));
        }
    }

    std::vector<std::uint8_t> bytes_;
};

inline void appendBigEndian(std::vector<std::uint8_t>& out, std::uint32_t value, int size)
{
    for (int i = size - 1; i >= 0; --i)
    {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

// A whole file. The header's track count defaults to the number of tracks given.
inline std::vector<std::uint8_t> makeSmf(std::uint16_t format, std::uint16_t division,
                                         const std::vector<SmfTrack>& tracks,
                                         std::optional<std::uint16_t> trackCount = {})
{
    std::vector<std::uint8_t> out{'M', 'T', 'h', 'd'};
    appendBigEndian(out, 6, 4);
    appendBigEndian(out, format, 2);
    appendBigEndian(out, trackCount.value_or(static_cast<std::uint16_t>(tracks.size())), 2);
    appendBigEndian(out, division, 2);
    for (const auto& track : tracks)
    {
        out.insert(out.end(), {'M', 'T', 'r', 'k'});
        appendBigEndian(out, static_cast<std::uint32_t>(track.bytes().size()), 4);
        out.insert(out.end(), track.bytes().begin(), track.bytes().end());
    }
    return out;
}

} // namespace modal::test
