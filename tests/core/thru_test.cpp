#include "support/playback.hpp"

#include <modal/core/thru.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <vector>

using namespace modal::core;
using namespace modal::test;
using namespace std::chrono_literals;
using Bytes = std::vector<std::uint8_t>;

namespace
{

constexpr InputId kInput{0};

ThruTarget target(std::uint32_t destination, std::uint8_t channel, int transpose = 0,
                  int velocityOffset = 0)
{
    return {TrackId{destination}, DestinationId{destination}, channel, transpose, velocityOffset};
}

} // namespace

TEST_CASE("thru sends channel messages to every target, on its channel, at once")
{
    RecordingSink sink;
    Thru thru{sink};
    const std::vector targets{target(1, 0), target(2, 9)};
    thru.setTargets(targets);

    thru.receive(kInput, Bytes{0xB3, 7, 100}, 5ms); // CC on input channel 4
    thru.receive(kInput, Bytes{0xC0, 12}, 6ms);
    thru.receive(kInput, Bytes{0xE5, 0, 64}, 7ms);
    CHECK(sink.sent == std::vector<Sent>{
                           {DestinationId{1}, {0xB0, 7, 100}, 5ms},
                           {DestinationId{2}, {0xB9, 7, 100}, 5ms},
                           {DestinationId{1}, {0xC0, 12}, 6ms},
                           {DestinationId{2}, {0xC9, 12}, 6ms},
                           {DestinationId{1}, {0xE0, 0, 64}, 7ms},
                           {DestinationId{2}, {0xE9, 0, 64}, 7ms},
                       });
}

TEST_CASE("thru applies each target's transpose and velocity offset")
{
    RecordingSink sink;
    Thru thru{sink};
    const std::vector targets{target(1, 0, 12, 30), target(2, 0, -70, -120)};
    thru.setTargets(targets);

    thru.receive(kInput, Bytes{0x90, 60, 100}, 1ms);
    thru.receive(kInput, Bytes{0xA0, 60, 50}, 2ms); // poly pressure follows the pitch
    thru.receive(kInput, Bytes{0x80, 60, 40}, 3ms);
    CHECK(sink.messages() == std::vector<Bytes>{
                                 {0x90, 72, 127}, // velocity clamped to 127
                                 {0xA0, 72, 50},
                                 {0x80, 72, 40},
                             }); // 60 - 70 is below 0, so target 2 gets nothing
    CHECK(thru.heldNotes() == 0);

    sink.sent.clear();
    thru.receive(kInput, Bytes{0x90, 100, 10}, 4ms);
    CHECK(sink.messages() == std::vector<Bytes>{{0x90, 112, 40}, {0x90, 30, 1}}); // clamped to 1
}

TEST_CASE("a thru note ends where it was sent, even if the targets change")
{
    RecordingSink sink;
    Thru thru{sink};
    const std::vector before{target(1, 0, 12)};
    const std::vector after{target(2, 3)};
    thru.setTargets(before);
    thru.receive(kInput, Bytes{0x90, 60, 100}, 1ms);
    thru.setTargets(after);
    thru.receive(kInput, Bytes{0x90, 60, 0}, 2ms); // a note-on at velocity 0 is a note-off
    CHECK(sink.sent == std::vector<Sent>{
                           {DestinationId{1}, {0x90, 72, 100}, 1ms},
                           {DestinationId{1}, {0x80, 72, 64}, 2ms},
                       });
    CHECK(thru.heldNotes() == 0);
}

TEST_CASE("the same key held on two inputs or channels is two notes")
{
    RecordingSink sink;
    Thru thru{sink};
    const std::vector targets{target(1, 0)};
    thru.setTargets(targets);
    thru.receive(InputId{0}, Bytes{0x90, 60, 100}, 1ms);
    thru.receive(InputId{1}, Bytes{0x90, 60, 100}, 1ms);
    thru.receive(InputId{0}, Bytes{0x91, 60, 100}, 1ms);
    CHECK(thru.heldNotes() == 3);
    thru.receive(InputId{1}, Bytes{0x80, 60, 0}, 2ms);
    CHECK(thru.heldNotes() == 2);
}

TEST_CASE("a thru pedal comes up wherever it went down, and wherever thru now goes")
{
    RecordingSink sink;
    Thru thru{sink};
    const std::vector before{target(1, 0), target(2, 0)};
    const std::vector after{target(2, 0), target(3, 5)};
    thru.setTargets(before);
    thru.receive(kInput, Bytes{0xB0, 64, 127}, 1ms);
    thru.receive(kInput, Bytes{0xB0, 64, 100}, 2ms); // a half-pedal value is still down
    CHECK(thru.heldPedals() == 2);

    thru.setTargets(after);
    sink.sent.clear();
    thru.receive(kInput, Bytes{0xB0, 64, 0}, 3ms);
    CHECK(sink.sent == std::vector<Sent>{
                           {DestinationId{1}, {0xB0, 64, 0}, 3ms},
                           {DestinationId{2}, {0xB0, 64, 0}, 3ms},
                           {DestinationId{3}, {0xB5, 64, 0}, 3ms},
                       });
    CHECK(thru.heldPedals() == 0);
}

TEST_CASE("ending everything sends note-offs and pedal-ups for what thru holds")
{
    RecordingSink sink;
    Thru thru{sink};
    const std::vector targets{target(1, 2)};
    thru.setTargets(targets);
    thru.receive(kInput, Bytes{0x90, 60, 100}, 1ms);
    thru.receive(kInput, Bytes{0xB0, 66, 127}, 1ms);
    thru.receive(kInput, Bytes{0xB0, 67, 127}, 1ms);
    thru.setTargets({});
    sink.sent.clear();

    thru.endAll(5ms);
    CHECK(sink.sent == std::vector<Sent>{
                           {DestinationId{1}, {0x82, 60, 64}, 5ms},
                           {DestinationId{1}, {0xB2, 66, 0}, 5ms},
                           {DestinationId{1}, {0xB2, 67, 0}, 5ms},
                       });
    CHECK(thru.heldNotes() == 0);
    CHECK(thru.heldPedals() == 0);
}

TEST_CASE("thru drops note-ons it can't remember, so none can hang")
{
    RecordingSink sink;
    Thru thru{sink};
    const std::vector targets{target(1, 0)};
    thru.setTargets(targets);
    for (std::size_t i = 0; i < Thru::kMaxHeldNotes; ++i)
    {
        thru.receive(InputId{static_cast<std::uint32_t>(i / 128)},
                     Bytes{0x90, static_cast<std::uint8_t>(i % 128), 100}, 1ms);
    }
    sink.sent.clear();
    thru.receive(InputId{99}, Bytes{0x90, 60, 100}, 2ms);
    CHECK(sink.sent.empty());
    CHECK(thru.droppedNotes() == 1);
    thru.receive(InputId{99}, Bytes{0x80, 60, 0}, 3ms);
    CHECK(sink.sent.empty());
}

TEST_CASE("with nothing armed, thru sends nothing")
{
    RecordingSink sink;
    Thru thru{sink};
    thru.receive(kInput, Bytes{0x90, 60, 100}, 1ms);
    thru.receive(kInput, Bytes{0xB0, 64, 127}, 1ms);
    CHECK(sink.sent.empty());
    CHECK(thru.heldNotes() == 0);
    CHECK(thru.heldPedals() == 0);
}
