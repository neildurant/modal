// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

using namespace modal;
using namespace modal::ui;

namespace
{

constexpr CellSize kCell{10, 20};

// The demo song in a workspace of 120 by 30 cells, in the arrangement.
struct Arranging
{
    explicit Arranging(core::Song song = demoSong())
        : document{std::move(song)}
    {
        registry.add(windowCommands());
        ws.resize(120 * kCell.width, 30 * kCell.height, kCell);
        ws.show(ViewKind::Arrangement);
    }

    void type(std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            ws.handle(Key::typed(key), 0);
        }
    }
    void press(const Key& key) { ws.handle(key, 0); }

    const ArrangePage& page() const { return ws.arrangement().page(); }
    const core::MeterMap& meterOf(std::size_t track) const
    {
        return document.song().meter(document.song().tracks()[track]->id());
    }

    std::u32string status() const
    {
        std::u32string text;
        const auto frame = ws.frame();
        for (const auto& run : frame.back().text)
        {
            text += run.text;
        }
        return text;
    }

    core::Document document;
    CommandRegistry registry;
    Workspace ws{registry, document};
};

bool contains(const std::u32string& text, std::u32string_view part)
{
    return text.find(part) != std::u32string::npos;
}

} // namespace

TEST_CASE("the arrangement's cursor is a bar of a track, moved by bars and tracks")
{
    Arranging a;
    REQUIRE(a.ws.shown() == ViewKind::Arrangement);
    CHECK(a.page().track() == 0);
    CHECK(a.page().bar() == 1);
    CHECK(contains(a.status(), U"1:1:000"));

    a.type(U"3l");
    CHECK(a.page().bar() == 4);
    a.type(U"h");
    CHECK(a.page().bar() == 3);
    a.type(U"2j");
    CHECK(a.page().track() == 2);
    CHECK(a.page().time() == a.meterOf(0).barStart(3)); // keeping the time
    a.type(U"k");
    CHECK(a.page().track() == 1);
    a.type(U"G");
    CHECK(a.page().bar() == a.meterOf(1).toBarBeatTick(a.document.song().end() - 1).bar);
    a.type(U"gg");
    CHECK(a.page().bar() == 1);
    a.type(U"5gg");
    CHECK(a.page().bar() == 5);
    a.type(U"$");
    CHECK(a.page().bar() == a.meterOf(1).toBarBeatTick(a.document.song().end() - 1).bar);
    a.type(U"0");
    CHECK(a.page().bar() == 1);

    // The view's bar names the cursor's track, and the inspector its bar.
    const auto frame = a.ws.frame();
    std::u32string bar;
    for (const auto& run : frame[frame.size() - 3].text)
    {
        bar += run.text;
    }
    CHECK(contains(bar, U"Track 2:"));
    std::u32string inspector;
    for (const auto& run : frame[frame.size() - 2].text)
    {
        inspector += run.text + U" ";
    }
    CHECK(contains(inspector, U"Bar"));
    CHECK(contains(inspector, U"1 of"));
}

TEST_CASE("zi, zo and z0 zoom the arrangement")
{
    Arranging a;
    const auto start = a.page().zoom();
    a.type(U"zo");
    CHECK(a.page().zoom() == start + 1);
    a.type(U"2zo");
    CHECK(a.page().zoom() == start + 3);
    a.type(U"zi");
    CHECK(a.page().zoom() == start + 2);
    a.type(U"z0");
    CHECK(a.page().zoom() == start);
}

TEST_CASE("ga, ge and Backspace go between the views, keeping the cursor")
{
    Arranging a;
    a.type(U"j4l"); // track 2, bar 5
    const auto tick = a.page().time();
    const auto id = a.document.song().tracks()[1]->id();

    a.type(U"ge");
    REQUIRE(a.ws.shown() == ViewKind::EventList);
    const auto& list = a.ws.eventList();
    CHECK(list.track() == 2);
    CHECK(list.list().rows()[list.row()].tick >= tick);

    // Moving in the event list moves the shared cursor; the arrangement shows its bar.
    a.type(U"}");
    const auto moved = list.list().rows()[list.row()].tick;
    a.press(Key::of(NamedKey::Backspace));
    REQUIRE(a.ws.shown() == ViewKind::Arrangement);
    CHECK(a.page().track() == 1);
    CHECK(a.page().bar() == a.meterOf(1).toBarBeatTick(moved).bar);
    CHECK(a.ws.arrangement().cursor().track == id);

    // Back again: the history went arrangement, event list; Backspace from the arrangement now
    // has nowhere to go but the view before it, which it left.
    a.press(Key::of(NamedKey::Backspace));
    CHECK(a.ws.shown() == ViewKind::EventList);
    a.press(Key::of(NamedKey::Backspace));
    CHECK(a.ws.shown() == ViewKind::EventList);
    CHECK(a.ws.message() == "No view to go back to");
}

TEST_CASE("coming back to the event list lands on the note it left")
{
    // A chord: three notes at one time, so only the pitch says which row the cursor was on.
    core::Song song{core::kPpqn};
    const auto keys = song.addTrack("Keys");
    song.updateEvents(keys,
                      [](core::EventList& events)
                      {
                          for (const int pitch : {60, 64, 67})
                          {
                              events.insert(
                                  {core::kPpqn.quarter(),
                                   core::Note{static_cast<std::uint8_t>(pitch), 100, 100}});
                          }
                      });
    Arranging a{std::move(song)};
    a.type(U"ge");
    const auto& list = a.ws.eventList();
    a.type(U"gg");
    while (list.list().rows()[list.row()].kind != RowKind::Event)
    {
        a.type(U"j");
    }
    a.type(U"j"); // the second note of the chord, E4
    const auto second = list.row();
    a.type(U"ga");
    REQUIRE(a.ws.shown() == ViewKind::Arrangement);
    a.type(U"ge");
    CHECK(list.row() == second);
}

TEST_CASE("Enter on a bar opens it, in the event list until the piano roll comes")
{
    Arranging a;
    a.type(U"2j3l");
    const auto tick = a.page().time();
    a.press(Key::of(NamedKey::Enter));
    REQUIRE(a.ws.shown() == ViewKind::EventList);
    CHECK(a.ws.eventList().track() == 3);
    const auto& list = a.ws.eventList();
    CHECK(list.list().rows()[list.row()].tick >= tick);
}

TEST_CASE("a command a view doesn't have says so")
{
    Arranging a;
    a.type(U"+");
    CHECK(a.ws.message() == "Field increase isn't in the arrangement");
    a.type(U"gr");
    CHECK(a.ws.message() == "The piano roll comes later in milestone 5");
    a.type(U"ge");
    a.type(U"zi");
    CHECK(a.ws.message() == "Zoom in isn't in the event list");
}
