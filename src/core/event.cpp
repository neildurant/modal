#include <modal/core/event.hpp>

#include <algorithm>
#include <utility>

namespace modal::core
{

namespace
{

constexpr bool is7Bit(std::uint8_t value)
{
    return value <= 127;
}

struct DataValidator
{
    bool operator()(const Note& note) const
    {
        return is7Bit(note.pitch) && note.velocity >= 1 && is7Bit(note.velocity) &&
               note.duration >= 0 && is7Bit(note.releaseVelocity);
    }
    bool operator()(const ControlChange& cc) const
    {
        return is7Bit(cc.controller) && is7Bit(cc.value);
    }
    bool operator()(const ProgramChange& pc) const { return is7Bit(pc.program); }
    bool operator()(const PitchBend& bend) const
    {
        return bend.value >= PitchBend::kMin && bend.value <= PitchBend::kMax;
    }
    bool operator()(const ChannelPressure& pressure) const { return is7Bit(pressure.value); }
    bool operator()(const PolyPressure& pressure) const
    {
        return is7Bit(pressure.pitch) && is7Bit(pressure.value);
    }
    bool operator()(const SysEx& sysex) const
    {
        if (!sysex.bytes || sysex.bytes->size() < 2)
        {
            return false;
        }
        const auto& bytes = *sysex.bytes;
        return bytes.front() == 0xF0 && bytes.back() == 0xF7 &&
               std::all_of(bytes.begin() + 1, bytes.end() - 1, is7Bit);
    }
    bool operator()(const MetaText& meta) const { return meta.text != nullptr; }
};

} // namespace

bool isValid(const Event& event)
{
    return event.tick >= 0 && std::visit(DataValidator{}, event.data);
}

SysEx makeSysEx(std::vector<std::uint8_t> bytes)
{
    return SysEx{std::make_shared<const std::vector<std::uint8_t>>(std::move(bytes))};
}

MetaText makeMetaText(MetaText::Kind kind, std::string text)
{
    return MetaText{kind, std::make_shared<const std::string>(std::move(text))};
}

} // namespace modal::core
