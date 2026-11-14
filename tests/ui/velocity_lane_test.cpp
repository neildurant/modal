// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

constexpr CellSize kCell{10, 20};

// A bass with a chord at the start (G1 at 80, E1 at 96), an A2 at 50 in bar 2, a C2 at 100
// just after bar 5's start, and an A1 at 70 in bar 12, in 4/4.
core::Song bass(core::Ppqn ppqn)
{
    const auto quarter = ppqn.quarter();
    const auto bar = [&](core::Tick n) { return 4 * quarter * (n - 1); };
    core::Song song{ppqn};
    const auto id = song.addTrack("Bass");
    song.updateEvents(id,
                      [&](core::EventList& events)
                      {
                          events.insert({0, core::Note{31, 80, quarter / 2}});
                          events.insert({0, core::Note{28, 96, quarter / 2}});
                          events.insert({bar(2), core::Note{45, 50, quarter}});
                          events.insert({bar(5) + 7, core::Note{36, 100, quarter / 4}});
                          events.insert({bar(12), core::Note{33, 70, 4 * quarter}});
                      });
    song.fitToContent();
    return song;
}

struct Lane
{
    explicit Lane(core::Ppqn ppqn = core::kPpqn)
        : ppqn{ppqn}
        , document{bass(ppqn)}
    {
        registry.add(windowCommands());
        ws.resize(120 * kCell.width, 30 * kCell.height, kCell);
        ws.show(ViewKind::Arrangement);
        type(U"gr");
    }

    void type(std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            ws.handle(Key::typed(key), 0);
        }
    }
    void press(const Key& key) { ws.handle(key, 0); }

    // Into the lane, on the chord's lowest note at the start.
    void intoLane()
    {
        type(U"gggl");
        press(Key::of(NamedKey::Tab, kShift));
        press(Key::of(NamedKey::Tab, kShift));
    }

    core::Tick bar(core::Tick n) const { return 4 * ppqn.quarter() * (n - 1); }

    // The velocity of the note at a time and pitch.
    int velocity(core::Tick tick, std::uint8_t pitch) const
    {
        const auto& song = document.song();
        for (const auto& event : song.events(song.tracks()[0]->id()).all())
        {
            const auto* note = std::get_if<core::Note>(&event.data);
            if (note != nullptr && event.tick == tick && note->pitch == pitch)
            {
                return note->velocity;
            }
        }
        return -1;
    }

    const PianoRollView& view() const { return ws.pianoRoll(); }

    core::Ppqn ppqn;
    core::Document document;
    CommandRegistry registry;
    Workspace ws{registry, document};
};

} // namespace

TEST_CASE("gl goes into the velocity lane, onto the note the cursor is in, and Escape back")
{
    Lane l;
    l.type(U"gg");
    CHECK(l.ws.where() == Where::PianoRoll);
    l.type(U"gl");
    CHECK(l.ws.where() == Where::Lane);
    CHECK(l.view().inLane());
    CHECK(l.view().time() == 0);
    REQUIRE(l.view().laneNote());
    l.press(Key::of(NamedKey::Escape));
    CHECK(l.ws.where() == Where::PianoRoll);
    CHECK_FALSE(l.view().inLane());
}

TEST_CASE("in the lane k and j change the velocity by 1, K and J by 10, with counts")
{
    Lane l(GENERATE(core::kPpqn, core::Ppqn{96}));
    l.intoLane();
    REQUIRE(l.view().pitch() == 28);
    l.type(U"k");
    CHECK(l.velocity(0, 28) == 97);
    l.type(U"3j");
    CHECK(l.velocity(0, 28) == 94);
    l.type(U"K");
    CHECK(l.velocity(0, 28) == 104);
    l.type(U"2J");
    CHECK(l.velocity(0, 28) == 84);
    l.press(Key::of(NamedKey::Up));
    CHECK(l.velocity(0, 28) == 85);
    l.press(Key::of(NamedKey::Down, kShift));
    CHECK(l.velocity(0, 28) == 75);
    // Values stop at the ends of the range.
    l.type(U"9K");
    CHECK(l.velocity(0, 28) == 127);
    // The pitch hasn't moved: k and j are the velocity's here.
    CHECK(l.view().pitch() == 28);
    // Steps one after another on a note undo together.
    l.type(U"u");
    CHECK(l.velocity(0, 28) == 96);
}

TEST_CASE("in the lane a number then Enter sets the velocity, and . repeats it")
{
    Lane l;
    l.intoLane();
    l.type(U"100");
    l.press(Key::of(NamedKey::Enter));
    CHECK(l.velocity(0, 28) == 100);
    l.press(Key::of(NamedKey::Tab));
    CHECK(l.view().pitch() == 31);
    l.type(U".");
    CHECK(l.velocity(0, 31) == 100);

    l.press(Key::of(NamedKey::Enter));
    CHECK(l.ws.message().starts_with("Type the velocity first"));
    l.type(U"200");
    l.press(Key::of(NamedKey::Enter));
    CHECK(l.ws.message() == "A velocity goes from 1 to 127");
    CHECK(l.velocity(0, 31) == 100);
}

TEST_CASE("Tab in the lane goes through a chord's notes low to high, then on")
{
    Lane l;
    l.intoLane();
    CHECK(l.view().time() == 0);
    CHECK(l.view().pitch() == 28);
    l.press(Key::of(NamedKey::Tab));
    CHECK((l.view().time() == 0 && l.view().pitch() == 31));
    l.press(Key::of(NamedKey::Tab));
    CHECK((l.view().time() == l.bar(2) && l.view().pitch() == 45));
    l.press(Key::of(NamedKey::Tab));
    CHECK((l.view().time() == l.bar(5) + 7 && l.view().pitch() == 36));
    l.press(Key::of(NamedKey::Tab, kShift));
    CHECK((l.view().time() == l.bar(2) && l.view().pitch() == 45));
    l.type(U"2");
    l.press(Key::of(NamedKey::Tab, kShift));
    CHECK((l.view().time() == 0 && l.view().pitch() == 28));
}

TEST_CASE("h and l in the lane move a grid step, onto a note starting in it")
{
    Lane l;
    l.intoLane();
    l.type(U"l");
    CHECK(l.view().time() == l.ppqn.quarter() / 4);
    CHECK_FALSE(l.view().laneNote());
    l.type(U"k");
    CHECK(l.ws.message() == "No note starts here: Tab goes to the next");
    l.type(U"h");
    CHECK(l.view().time() == 0);
    CHECK(l.view().laneNote());

    // A note a little off the grid is in the grid step it starts in.
    l.type(U"3");
    l.press(Key::of(NamedKey::Tab));
    REQUIRE(l.view().time() == l.bar(5) + 7);
    l.type(U"lh");
    CHECK(l.view().time() == l.bar(5));
    CHECK(l.view().pitch() == 36);
    l.type(U"k");
    CHECK(l.velocity(l.bar(5) + 7, 36) == 101);
}

TEST_CASE("v in the lane takes the notes starting in its time, k raising them all")
{
    Lane l;
    l.intoLane();
    l.type(U"v16l");
    CHECK(l.ws.mode() == Mode::Visual);
    CHECK(l.view().laneTaken().size() == 3);
    l.type(U"k");
    CHECK(l.velocity(0, 28) == 97);
    CHECK(l.velocity(0, 31) == 81);
    CHECK(l.velocity(l.bar(2), 45) == 51);
    CHECK(l.velocity(l.bar(5) + 7, 36) == 100);
    l.type(U"80");
    l.press(Key::of(NamedKey::Enter));
    CHECK(l.velocity(0, 28) == 80);
    CHECK(l.velocity(0, 31) == 80);
    CHECK(l.velocity(l.bar(2), 45) == 80);
    l.type(U"u");
    CHECK(l.velocity(0, 28) == 97);
    l.type(U"u");
    CHECK(l.velocity(0, 28) == 96);
}

TEST_CASE("Ctrl-v in the lane takes only the notes with velocities inside its values")
{
    Lane l;
    l.intoLane();
    // From E1's 96, over the chord and A2.
    l.press(Key::typed(U'v', kCtrl));
    l.type(U"16l");
    CHECK(l.view().laneTaken().size() == 1);
    // k and j move the box's other edge, not the velocities.
    l.type(U"J");
    CHECK(l.velocity(0, 28) == 96);
    CHECK(l.view().laneTaken().size() == 1);
    l.type(U"J");
    CHECK(l.view().laneTaken().size() == 2); // and G1's 80
    l.type(U"110");
    l.press(Key::of(NamedKey::Enter));
    CHECK(l.velocity(0, 28) == 110);
    CHECK(l.velocity(0, 31) == 110);
    CHECK(l.velocity(l.bar(2), 45) == 50);
}

TEST_CASE("the lane refuses what takes notes or pitches, and V")
{
    Lane l;
    l.intoLane();
    const auto before = l.document.song().events(l.document.song().tracks()[0]->id()).size();
    l.type(U"dd");
    CHECK(l.ws.message().starts_with("Velocities change in the lane"));
    l.type(U"x");
    l.type(U"V");
    CHECK(l.ws.mode() == Mode::Normal);
    CHECK(l.document.song().events(l.document.song().tracks()[0]->id()).size() == before);
    // gl in a selection of notes is refused, as the selection is the notes'.
    l.press(Key::of(NamedKey::Escape));
    l.type(U"v");
    l.press(Key::of(NamedKey::Escape));
    l.press(Key::of(NamedKey::Escape));
    REQUIRE(l.ws.where() == Where::PianoRoll);
    l.type(U"vgl");
    CHECK(l.ws.where() == Where::PianoRoll);
}

TEST_CASE("a chord's velocity bars fan out side by side in the lane, low to high")
{
    Lane l;
    const auto frame = l.ws.frame();
    // The lane: right of its name, vel, under the keyboard.
    const auto name = std::ranges::find_if(frame, [](const Region& region)
                                           { return !region.text.empty() && region.text.front().text == U"vel"; });
    REQUIRE(name != frame.end());
    const Region* lane = nullptr;
    for (const auto& region : frame)
    {
        if (region.bounds.y == name->bounds.y &&
            region.bounds.x == name->bounds.x + PianoRollView::kKeyColumns * kCell.width)
        {
            lane = &region;
        }
    }
    REQUIRE(lane != nullptr);
    std::vector<Rect> bars;
    for (const auto& shape : lane->under)
    {
        if ((shape.role == Role::Note || shape.role == Role::NoteSelected) && shape.rect.x < 20)
        {
            bars.push_back(shape.rect);
        }
    }
    REQUIRE(bars.size() == 2);
    std::ranges::sort(bars, {}, &Rect::x);
    CHECK(bars[0].x < bars[1].x);
    // E1, the lower, first, and the taller, at 96 to G1's 80.
    CHECK(bars[0].height > bars[1].height);
}
