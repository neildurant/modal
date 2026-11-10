// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/fake_clock.hpp"
#include "support/playback.hpp"

#include <modal/core/player.hpp>
#include <modal/platform/midi_input_session.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace modal;
using namespace std::chrono_literals;

namespace
{

struct FakeInputs
{
    struct Port final : platform::MidiInputSession::Port
    {
        Port(FakeInputs& owner, std::string name)
            : owner(owner)
            , name(std::move(name))
        {
        }
        ~Port() override
        {
            if (owner.finalMessage)
            {
                const auto bytes = *owner.finalMessage;
                owner.callbacks.at(name)(bytes, test::kStart);
            }
            owner.callbacks.erase(name);
            owner.log.push_back("close " + name);
        }
        FakeInputs& owner;
        std::string name;
    };

    platform::MidiInputSession::Backend backend()
    {
        return {.ports = [&] { return available; },
                .open = [&](std::string_view name, platform::MidiInput::Callback callback)
                    -> platform::MidiInputSession::Opened
                {
                    const std::string key{name};
                    log.push_back("open " + key);
                    if (key == fail)
                        return {nullptr, "can't open " + key};
                    callbacks[key] = std::move(callback);
                    return {std::make_unique<Port>(*this, key), {}};
                }};
    }

    std::vector<platform::MidiPort> available{{"Keyboard A"}, {"Keyboard B"}, {"Keyboard C"}};
    std::map<std::string, platform::MidiInput::Callback> callbacks;
    std::vector<std::string> log;
    std::string fail;
    std::optional<std::array<std::uint8_t, 3>> finalMessage;
};

struct Listening
{
    Listening()
    {
        core::Song song;
        const auto id = song.addTrack("Armed");
        song.updateTrack(id,
                         [](core::Track& track)
                         {
                             track.arm = true;
                             track.channel = 2;
                         });
        REQUIRE(engine.setPlan(std::make_shared<const core::PlaybackPlan>(
            core::compilePlan(song, [](const core::Track&) { return core::DestinationId{1}; }))));
        engine.step();
        session.setWake([&] { ++wakes; });
    }
    void note(std::string_view port, std::uint8_t pitch)
    {
        const std::array<std::uint8_t, 3> bytes{0x90, pitch, 100};
        inputs.callbacks.at(std::string{port})(bytes, clock.time);
    }

    test::RecordingSink sink;
    test::FakeClock clock;
    core::Engine engine{sink, clock};
    FakeInputs inputs;
    int wakes = 0;
    platform::MidiInputSession session{engine, inputs.backend()};
};

} // namespace

TEST_CASE("an input session matches and deduplicates names in stable input ID order")
{
    Listening l;
    CHECK(l.session.setInputs({"Keyboard B", "keyboard a", "Keyboard B"}, 0).empty());
    CHECK(l.inputs.log == std::vector<std::string>{"open Keyboard B", "open Keyboard A"});
    CHECK(l.session.inputId("Keyboard B") == core::InputId{0});
    CHECK(l.session.inputId("keyboard a") == core::InputId{1});
    CHECK_FALSE(l.session.inputId("Keyboard"));   // ambiguous
    CHECK_FALSE(l.session.inputId("Keyboard C")); // available but not listened to
    CHECK_FALSE(l.session.deadline());
    l.note("Keyboard A", 60);
    l.engine.step();
    CHECK(l.wakes == 1);
    const auto received = l.engine.nextReceived();
    REQUIRE(received);
    CHECK(received->input == core::InputId{1});
    CHECK(l.session.setInputs({"Keyboard B", "keyboard a", "Keyboard B"}, 1).empty());
    CHECK(l.inputs.log.size() == 2); // unchanged settings don't close or reset anything
}

TEST_CASE("an input session closes callbacks and releases held state before reusing IDs")
{
    Listening l;
    REQUIRE(l.session.setInputs({"Keyboard A"}, 0).empty());
    l.note("Keyboard A", 60);
    l.engine.step();
    // The callback's final message is allowed to arrive while close is joining it.
    l.inputs.finalMessage = std::array<std::uint8_t, 3>{0xB0, 64, 100};
    CHECK(l.session.setInputs({"Keyboard B"}, 1).empty());
    l.inputs.finalMessage.reset();
    CHECK(l.inputs.callbacks.empty());
    CHECK(l.session.ports().empty());
    CHECK_FALSE(l.session.inputId("Keyboard A"));
    CHECK_FALSE(l.session.inputId("Keyboard B"));
    CHECK(l.inputs.log == std::vector<std::string>{"open Keyboard A", "close Keyboard A"});
    REQUIRE(l.session.deadline());
    const auto due = *l.session.deadline();
    l.engine.step();
    CHECK(l.sink.messages() ==
          std::vector<std::vector<std::uint8_t>>{
              {0x91, 60, 100}, {0xB1, 64, 100}, {0x81, 60, 64}, {0xB1, 64, 0}});
    CHECK(l.session.poll(due - 1).empty());
    CHECK(l.inputs.callbacks.empty()); // an early wake doesn't open ports
    CHECK(l.session.poll(due).empty());
    CHECK_FALSE(l.session.deadline());
    CHECK(l.session.inputId("Keyboard B") == core::InputId{0});
    CHECK(l.inputs.log.back() == "open Keyboard B");
    l.note("Keyboard B", 64);
    l.engine.step();
    CHECK(l.sink.messages().back() == std::vector<std::uint8_t>{0x91, 64, 100});
}

TEST_CASE("an input session retries a full command queue and stays closed until acknowledged")
{
    Listening l;
    REQUIRE(l.session.setInputs({"Keyboard A"}, 0).empty());
    while (l.engine.stop())
    {
    }
    CHECK(l.session.setInputs({"Keyboard B"}, 1).empty());
    REQUIRE(l.session.deadline());
    auto due = *l.session.deadline();
    CHECK(l.session.poll(due).empty());
    CHECK(l.inputs.callbacks.empty());
    l.engine.step(); // makes room, but there was no reset request to acknowledge yet
    due = *l.session.deadline();
    CHECK(l.session.poll(due).empty()); // reset queued now
    CHECK(l.inputs.callbacks.empty());
    REQUIRE(l.session.deadline());
    l.engine.step();
    CHECK(l.session.poll(*l.session.deadline()).empty());
    CHECK(l.session.inputId("Keyboard B") == core::InputId{0});
    CHECK_FALSE(l.session.deadline());
}

TEST_CASE("an input session opens the latest request after a delayed reset")
{
    Listening l;
    REQUIRE(l.session.setInputs({"Keyboard A"}, 0).empty());
    CHECK(l.session.setInputs({"Keyboard B"}, 1).empty());
    CHECK(l.session.setInputs({"Keyboard C"}, 2).empty());
    CHECK(l.session.setInputs({"Keyboard A", "Keyboard C"}, 3).empty());
    CHECK(l.inputs.callbacks.empty());
    l.engine.step();
    CHECK(l.session.poll(*l.session.deadline()).empty());
    CHECK(l.inputs.log == std::vector<std::string>{"open Keyboard A", "close Keyboard A",
                                                   "open Keyboard A", "open Keyboard C"});
    CHECK(l.session.inputId("Keyboard A") == core::InputId{0});
    CHECK(l.session.inputId("Keyboard C") == core::InputId{1});
}

TEST_CASE("input sessions report missing, ambiguous and failed ports without leaving ID gaps")
{
    Listening l;
    l.inputs.fail = "Keyboard B";
    const auto message =
        l.session.setInputs({"missing", "Keyboard", "Keyboard A", "Keyboard B", "Keyboard C"}, 0);
    CHECK(message.find("missing not found") != std::string::npos);
    CHECK(message.find("Keyboard matches several") != std::string::npos);
    CHECK(message.find("can't open Keyboard B") != std::string::npos);
    CHECK(l.session.inputId("Keyboard A") == core::InputId{0});
    CHECK(l.session.inputId("Keyboard C") == core::InputId{1});
    CHECK_FALSE(l.session.inputId("Keyboard B"));
    l.note("Keyboard C", 60);
    l.engine.step();
    const auto received = l.engine.nextReceived();
    REQUIRE(received);
    CHECK(received->input == core::InputId{1});
}

TEST_CASE("input sessions listen to none when given none")
{
    Listening l;
    CHECK(l.session.setInputs({}, 0).empty());
    CHECK(l.session.ports().empty());
    CHECK(l.inputs.callbacks.empty());
}

TEST_CASE("input sessions open the ports given up to the engine limit")
{
    Listening l;
    l.inputs.available.clear();
    std::vector<std::string> all;
    for (std::size_t i = 0; i < core::Engine::kMaxInputs + 1; ++i)
    {
        l.inputs.available.push_back({"Input " + std::to_string(i)});
        all.push_back("Input " + std::to_string(i));
    }
    const auto message = l.session.setInputs(all, 0);
    CHECK(l.session.ports().size() == core::Engine::kMaxInputs);
    CHECK(l.inputs.callbacks.size() == core::Engine::kMaxInputs);
    CHECK(message.find("only " + std::to_string(core::Engine::kMaxInputs) +
                       " inputs can be open") != std::string::npos);
    CHECK_FALSE(l.session.inputId("Input " + std::to_string(core::Engine::kMaxInputs)));
}
