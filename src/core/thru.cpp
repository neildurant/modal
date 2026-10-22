// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/thru.hpp>

#include <algorithm>
#include <array>

namespace modal::core
{

namespace
{

using std::chrono::nanoseconds;

constexpr std::uint8_t kNoteOff = 0x80;
constexpr std::uint8_t kNoteOn = 0x90;
constexpr std::uint8_t kPolyPressure = 0xA0;
constexpr std::uint8_t kControlChange = 0xB0;

constexpr std::uint8_t kPedalDown = 64; // values from this up hold a pedal down

bool isPedal(std::uint8_t controller)
{
    return controller == 64 || controller == 66 || controller == 67; // sustain, sostenuto, soft
}

std::optional<std::uint8_t> transposed(std::uint8_t pitch, int transpose)
{
    const int result = pitch + transpose;
    if (result < 0 || result > 127)
    {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(result);
}

void send(MessageSink& sink, DestinationId destination, std::uint8_t status, std::uint8_t channel,
          std::span<const std::uint8_t> data, nanoseconds time) noexcept
{
    std::array<std::uint8_t, 3> bytes{static_cast<std::uint8_t>(status | channel)};
    std::ranges::copy(data, bytes.begin() + 1);
    sink.send(destination, std::span{bytes}.first(1 + data.size()), time);
}

} // namespace

Thru::Thru(MessageSink& sink)
    : sink_(sink)
{
    notes_.reserve(kMaxHeldNotes);
    pedals_.reserve(kMaxHeldPedals);
}

void Thru::receive(InputId input, std::span<const std::uint8_t> message, nanoseconds time) noexcept
{
    if (message.empty() || message[0] < 0x80 || message[0] >= 0xF0)
    {
        return;
    }
    const auto status = static_cast<std::uint8_t>(message[0] & 0xF0);
    const auto inChannel = static_cast<std::uint8_t>(message[0] & 0x0F);
    const auto data = message.subspan(1);

    if (status == kNoteOn && data.size() == 2 && data[1] > 0)
    {
        noteOn(input, inChannel, data[0], data[1], time);
    }
    else if ((status == kNoteOn || status == kNoteOff) && data.size() == 2)
    {
        noteOff(input, inChannel, data[0], status == kNoteOff ? data[1] : 64, time);
    }
    else if (status == kControlChange && data.size() == 2 && isPedal(data[0]))
    {
        pedal(input, inChannel, data[0], data[1], time);
    }
    else if (status == kPolyPressure && data.size() == 2)
    {
        for (const auto& target : targets_)
        {
            if (const auto pitch = transposed(data[0], target.transpose))
            {
                const std::array<std::uint8_t, 2> moved{*pitch, data[1]};
                send(sink_, target.destination, status, target.channel, moved, time);
            }
        }
    }
    else
    {
        for (const auto& target : targets_)
        {
            send(sink_, target.destination, status, target.channel, data, time);
        }
    }
}

void Thru::noteOn(InputId input, std::uint8_t inChannel, std::uint8_t pitch, std::uint8_t velocity,
                  nanoseconds time) noexcept
{
    for (const auto& target : targets_)
    {
        const auto outPitch = transposed(pitch, target.transpose);
        if (!outPitch)
        {
            continue;
        }
        if (notes_.size() == kMaxHeldNotes)
        {
            ++droppedNotes_;
            continue;
        }
        const auto outVelocity =
            static_cast<std::uint8_t>(std::clamp(velocity + target.velocityOffset, 1, 127));
        const std::array<std::uint8_t, 2> data{*outPitch, outVelocity};
        send(sink_, target.destination, kNoteOn, target.channel, data, time);
        notes_.push_back({input, inChannel, pitch, target.destination, target.channel, *outPitch});
    }
}

void Thru::noteOff(InputId input, std::uint8_t inChannel, std::uint8_t pitch, std::uint8_t velocity,
                   nanoseconds time) noexcept
{
    std::erase_if(notes_,
                  [&](const HeldNote& note)
                  {
                      if (note.input != input || note.inChannel != inChannel ||
                          note.inPitch != pitch)
                      {
                          return false;
                      }
                      const std::array<std::uint8_t, 2> data{note.pitch, velocity};
                      send(sink_, note.destination, kNoteOff, note.channel, data, time);
                      return true;
                  });
}

void Thru::pedal(InputId input, std::uint8_t inChannel, std::uint8_t controller, std::uint8_t value,
                 nanoseconds time) noexcept
{
    const std::array<std::uint8_t, 2> data{controller, value};
    const auto matches = [&](const HeldPedal& held)
    { return held.input == input && held.inChannel == inChannel && held.controller == controller; };

    if (value >= kPedalDown)
    {
        for (const auto& target : targets_)
        {
            send(sink_, target.destination, kControlChange, target.channel, data, time);
            const HeldPedal held{input, inChannel, controller, target.destination, target.channel};
            const bool known = std::ranges::any_of(pedals_,
                                                   [&](const HeldPedal& p)
                                                   {
                                                       return matches(p) &&
                                                              p.destination == held.destination &&
                                                              p.channel == held.channel;
                                                   });
            if (!known && pedals_.size() < kMaxHeldPedals)
            {
                pedals_.push_back(held);
            }
        }
        return;
    }

    // Up: to everywhere it went down, and to the current targets too.
    for (const auto& held : pedals_)
    {
        if (matches(held))
        {
            send(sink_, held.destination, kControlChange, held.channel, data, time);
        }
    }
    for (const auto& target : targets_)
    {
        const bool sent = std::ranges::any_of(pedals_,
                                              [&](const HeldPedal& p)
                                              {
                                                  return matches(p) &&
                                                         p.destination == target.destination &&
                                                         p.channel == target.channel;
                                              });
        if (!sent)
        {
            send(sink_, target.destination, kControlChange, target.channel, data, time);
        }
    }
    std::erase_if(pedals_, matches);
}

void Thru::endAll(nanoseconds now) noexcept
{
    for (const auto& note : notes_)
    {
        const std::array<std::uint8_t, 2> data{note.pitch, 64};
        send(sink_, note.destination, kNoteOff, note.channel, data, now);
    }
    for (const auto& held : pedals_)
    {
        const std::array<std::uint8_t, 2> data{held.controller, 0};
        send(sink_, held.destination, kControlChange, held.channel, data, now);
    }
    notes_.clear();
    pedals_.clear();
}

} // namespace modal::core
