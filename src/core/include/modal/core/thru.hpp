// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/input_queue.hpp>
#include <modal/core/playback_plan.hpp>
#include <modal/core/scheduler.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace modal::core
{

// MIDI thru: sends what arrives on the inputs on to the armed tracks, on each track's channel
// and with its transpose and velocity offset, whether or not the song is playing. It
// remembers where each held note and pedal went, so its note-off or pedal-up goes to the same
// place even if the targets change in between. Like the scheduler, it does no timing itself:
// it sends at once, with the arrival time as the due time.
class Thru
{
public:
    static constexpr std::size_t kMaxHeldNotes = 1024;
    static constexpr std::size_t kMaxHeldPedals = 256;

    explicit Thru(MessageSink& sink);

    // The tracks to send to. The span must stay valid until the next call (the engine keeps
    // the plan it comes from alive). Held notes and pedals are unaffected.
    void setTargets(std::span<const ThruTarget> targets) noexcept { targets_ = targets; }

    // Sends a channel message on. Anything else is ignored.
    void receive(InputId input, std::span<const std::uint8_t> message,
                 std::chrono::nanoseconds time) noexcept;

    // Ends every held note and pedal, as panic does.
    void endAll(std::chrono::nanoseconds now) noexcept;

    // Note-ons not sent because kMaxHeldNotes notes were already held.
    std::size_t droppedNotes() const noexcept { return droppedNotes_; }

    std::size_t heldNotes() const noexcept { return notes_.size(); }
    std::size_t heldPedals() const noexcept { return pedals_.size(); }

private:
    // Where one held note went.
    struct HeldNote
    {
        InputId input;
        std::uint8_t inChannel;
        std::uint8_t inPitch;
        DestinationId destination;
        std::uint8_t channel;
        std::uint8_t pitch;
    };

    // Where one held pedal went.
    struct HeldPedal
    {
        InputId input;
        std::uint8_t inChannel;
        std::uint8_t controller;
        DestinationId destination;
        std::uint8_t channel;
    };

    void noteOn(InputId input, std::uint8_t inChannel, std::uint8_t pitch, std::uint8_t velocity,
                std::chrono::nanoseconds time) noexcept;
    void noteOff(InputId input, std::uint8_t inChannel, std::uint8_t pitch, std::uint8_t velocity,
                 std::chrono::nanoseconds time) noexcept;
    void pedal(InputId input, std::uint8_t inChannel, std::uint8_t controller, std::uint8_t value,
               std::chrono::nanoseconds time) noexcept;

    MessageSink& sink_;
    std::span<const ThruTarget> targets_;
    std::vector<HeldNote> notes_;   // reserved up front
    std::vector<HeldPedal> pedals_; // reserved up front
    std::size_t droppedNotes_ = 0;
};

} // namespace modal::core
