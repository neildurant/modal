// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/env.hpp"

#include <modal/platform/clock.hpp>
#include <modal/platform/midi_input.hpp>
#include <modal/platform/midi_output.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <utility>
#include <vector>

using namespace modal::platform;
using namespace std::chrono_literals;
using modal::core::DestinationId;

TEST_CASE("opening a port that doesn't exist fails, saying why")
{
    MidiOutputs outputs;
    const auto output = outputs.open("No Such Port 7b1f");
    CHECK_FALSE(output.ok());
    CHECK(output.portName.empty());

    MidiInput input{[](auto, auto) {}};
    CHECK_FALSE(input.open("No Such Port 7b1f").ok());
}

TEST_CASE("sending to a destination that isn't open counts as a failure")
{
    MidiOutputs outputs;
    const std::uint8_t noteOn[] = {0x90, 60, 100};
    outputs.send(DestinationId{0}, noteOn, 0ns);
    outputs.send(DestinationId{3}, noteOn, 0ns);
    CHECK(outputs.failedSends() == 2);
}

// Needs the "Midi Through" ports from snd-seq-dummy; see the port listing test.
TEST_CASE("messages sent to Midi Through come back with their arrival times")
{
    if (!modal::test::envFlagSet("MODAL_TEST_ALSA_SEQ"))
    {
        SKIP("set MODAL_TEST_ALSA_SEQ=1 on a Linux machine with snd-seq-dummy loaded");
    }

    using Received = std::pair<std::vector<std::uint8_t>, std::chrono::nanoseconds>;
    std::mutex mutex;
    std::condition_variable arrived;
    std::vector<Received> received;
    MidiInput input{[&](std::span<const std::uint8_t> message, std::chrono::nanoseconds time)
                    {
                        const std::lock_guard lock{mutex};
                        received.emplace_back(std::vector(message.begin(), message.end()), time);
                        arrived.notify_one();
                    }};
    REQUIRE(input.open("Midi Through").ok());

    MidiOutputs outputs;
    const auto output = outputs.open("Midi Through");
    REQUIRE(output.ok());
    CHECK(outputs.open("midi through").destination == output.destination);

    const std::uint8_t noteOn[] = {0x90, 60, 100};
    const std::uint8_t sysex[] = {0xF0, 0x7D, 0x01, 0x02, 0xF7};
    const auto sent = now();
    outputs.send(output.destination, noteOn, sent);
    outputs.send(output.destination, sysex, sent);

    std::unique_lock lock{mutex};
    REQUIRE(arrived.wait_for(lock, 2s, [&] { return received.size() == 2; }));
    CHECK(received[0].first == std::vector<std::uint8_t>{0x90, 60, 100});
    CHECK(received[1].first == std::vector<std::uint8_t>(std::begin(sysex), std::end(sysex)));
    CHECK(received[0].second >= sent);
    CHECK(received[0].second <= now());
    CHECK(outputs.failedSends() == 0);
}
