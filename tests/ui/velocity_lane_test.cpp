// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <chrono>
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

TEST_CASE("in the lane Ctrl-a and Ctrl-x change the velocity by 1, with counts")
{
    Lane l(GENERATE(core::kPpqn, core::Ppqn{96}));
    const auto ctrl = [&](char32_t key) { l.press(Key::typed(key, kCtrl)); };
    l.intoLane();
    REQUIRE(l.view().pitch() == 28);
    ctrl(U'a');
    CHECK(l.velocity(0, 28) == 97);
    l.type(U"3");
    ctrl(U'x');
    CHECK(l.velocity(0, 28) == 94);
    // Values stop at the ends of the range.
    l.type(U"50");
    ctrl(U'a');
    CHECK(l.velocity(0, 28) == 127);
    // k and j size a box, so on a note they're refused, the pitch staying put.
    l.type(U"k");
    CHECK(l.ws.message() == "k and j size a Ctrl-v box: Ctrl-a and Ctrl-x change the velocity");
    CHECK(l.velocity(0, 28) == 127);
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

    // Enter alone asks, as below.
    l.press(Key::of(NamedKey::Enter));
    CHECK(l.ws.mode() == Mode::CommandLine);
    l.press(Key::of(NamedKey::Escape));
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

TEST_CASE("h and l in the velocity lane go note to note, as Tab does")
{
    Lane l;
    l.intoLane();
    l.type(U"l");
    CHECK((l.view().time() == 0 && l.view().pitch() == 31));
    l.press(Key::of(NamedKey::Right));
    CHECK((l.view().time() == l.bar(2) && l.view().pitch() == 45));
    // A note a little off the grid is reached as any other.
    l.type(U"l");
    CHECK((l.view().time() == l.bar(5) + 7 && l.view().pitch() == 36));
    l.press(Key::typed(U'a', kCtrl));
    CHECK(l.velocity(l.bar(5) + 7, 36) == 101);
    l.type(U"3h");
    CHECK((l.view().time() == 0 && l.view().pitch() == 28));
    // Past the first, nothing moves.
    l.press(Key::of(NamedKey::Left));
    CHECK((l.view().time() == 0 && l.view().pitch() == 28));
}

TEST_CASE("o in a Ctrl-v box in the lane goes to its other corner, its values too")
{
    Lane l;
    l.intoLane();
    // From E1's 96 to A2's 50: E1, G1 and A2 taken.
    l.press(Key::typed(U'v', kCtrl));
    l.type(U"2l");
    REQUIRE(l.view().laneTaken().size() == 3);
    l.type(U"o");
    CHECK((l.view().time() == 0 && l.view().pitch() == 28));
    // k now moves the edge that was the anchor's, 96, so lowering it drops E1 and G1.
    l.type(U"k");
    CHECK(l.view().laneTaken().size() == 3);
    l.type(U"2J");
    CHECK(l.view().laneTaken().size() == 1);
    CHECK(l.view().selectionStatus()->starts_with("1 note, velocities 50–77"));
}

TEST_CASE("v in the lane takes the notes starting in its time, Ctrl-a raising them all")
{
    Lane l;
    l.intoLane();
    l.type(U"v2l");
    CHECK(l.ws.mode() == Mode::Visual);
    CHECK(l.view().laneTaken().size() == 3);
    l.press(Key::typed(U'a', kCtrl));
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
    // From E1's 96, its corners on the notes it's started and ended on.
    l.press(Key::typed(U'v', kCtrl));
    CHECK(l.view().laneTaken().size() == 1);
    l.type(U"l"); // G1's 80
    CHECK(l.view().laneTaken().size() == 2);
    l.type(U"l"); // A2's 50
    CHECK(l.view().laneTaken().size() == 3);
    // k and K move its moving edge from there, as moves size any selection, not the
    // velocities: up to 80 leaves A2 out.
    l.type(U"3K");
    CHECK(l.velocity(l.bar(2), 45) == 50);
    CHECK(l.view().laneTaken().size() == 2);
    l.press(Key::of(NamedKey::Up));
    CHECK(l.view().laneTaken().size() == 1); // 81 leaves G1's 80 out
    l.press(Key::of(NamedKey::Down));
    CHECK(l.view().laneTaken().size() == 2);
    // Ctrl-a changes the velocities it takes, which it keeps though E1's leaves it.
    l.press(Key::typed(U'a', kCtrl));
    CHECK(l.velocity(0, 28) == 97);
    CHECK(l.velocity(0, 31) == 81);
    l.press(Key::typed(U'a', kCtrl));
    CHECK(l.velocity(0, 28) == 98);
    CHECK(l.velocity(0, 31) == 82);
    l.type(U"110");
    l.press(Key::of(NamedKey::Enter));
    CHECK(l.velocity(0, 28) == 110);
    CHECK(l.velocity(0, 31) == 110);
    CHECK(l.velocity(l.bar(2), 45) == 50);
}

TEST_CASE("a lane selection runs from note to note, not on to the next grid step")
{
    // v on E1 takes E1 alone, though G1 starts with it, and l takes G1 too, but not A2.
    Lane l;
    l.intoLane();
    l.type(U"v");
    CHECK(l.view().laneTaken().size() == 1);
    l.type(U"l");
    CHECK(l.view().laneTaken().size() == 2);
    CHECK(l.view().selectionStatus()->starts_with("2 notes"));
}

TEST_CASE("Alt and the section strip's keys don't reach into the lane")
{
    // Alt-h moves a section on the strip; in the lane it isn't a key, and nothing moves.
    Lane l;
    l.intoLane();
    l.press(Key::typed(U'h', kAlt));
    CHECK(l.velocity(0, 28) == 96);
    l.press(Key::typed(U'k', kAlt));
    CHECK(l.velocity(0, 28) == 96);
    l.type(U"=");
    CHECK(l.velocity(0, 28) == 96);
    l.type(U"-");
    CHECK(l.velocity(0, 28) == 96);
    CHECK(l.document.song().events(l.document.song().tracks()[0]->id())[0].tick == 0);
    CHECK(l.document.song().sections().size() == 1);
}

TEST_CASE("Enter alone in the lane asks for a velocity, or a change to it")
{
    Lane l;
    const auto answer = [&](std::u32string_view text)
    {
        l.press(Key::of(NamedKey::Enter));
        REQUIRE(l.ws.mode() == Mode::CommandLine);
        l.type(text);
        l.press(Key::of(NamedKey::Enter));
    };
    l.intoLane();
    answer(U"+30");
    CHECK(l.velocity(0, 28) == 126);
    answer(U"-26");
    CHECK(l.velocity(0, 28) == 100);
    answer(U"50%");
    CHECK(l.velocity(0, 28) == 50);
    answer(U"90");
    CHECK(l.velocity(0, 28) == 90);
    answer(U"+x");
    CHECK(l.ws.message().starts_with("A velocity is 1 to 127"));
    CHECK(l.velocity(0, 28) == 90);

    // On a selection, every note it takes, stopping at the ends of the range.
    l.type(U"v2l");
    answer(U"+40");
    CHECK(l.velocity(0, 28) == 127);
    CHECK(l.velocity(0, 31) == 120);
    CHECK(l.velocity(l.bar(2), 45) == 90);
    l.type(U"u");
    CHECK(l.velocity(0, 31) == 80);
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
    const auto name = std::ranges::find_if(
        frame, [](const Region& region)
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

TEST_CASE("a chord's late note's bar is at its own time in the lane, not with the others")
{
    Lane l;
    // A third note a 32nd after the chord, within a chordwindow of 100 ms, zoomed in to a bar.
    l.ws.pianoRoll().setChordWindow(std::chrono::milliseconds{100});
    const auto late = l.ppqn.quarter() / 8;
    l.document.edit("Late", {},
                    [&](core::Song& song)
                    {
                        song.updateEvents(song.tracks()[0]->id(), [&](core::EventList& events)
                                          { events.insert({late, core::Note{40, 60, 100}}); });
                    });
    l.type(U"gg");
    l.type(U"zizi");
    const auto frame = l.ws.frame();
    const auto name = std::ranges::find_if(
        frame, [](const Region& region)
        { return !region.text.empty() && region.text.front().text == U"vel"; });
    REQUIRE(name != frame.end());
    std::vector<Rect> bars;
    for (const auto& region : frame)
    {
        if (region.bounds.y == name->bounds.y &&
            region.bounds.x == name->bounds.x + PianoRollView::kKeyColumns * kCell.width)
        {
            for (const auto& shape : region.under)
            {
                if ((shape.role == Role::Note || shape.role == Role::NoteSelected) &&
                    shape.rect.x < region.bounds.width / 4)
                {
                    bars.push_back(shape.rect);
                }
            }
        }
    }
    REQUIRE(bars.size() == 3);
    std::ranges::sort(bars, {}, &Rect::x);
    // The two at the start side by side, the late one well after them.
    CHECK(bars[1].x - bars[0].x < 6);
    CHECK(bars[2].x - bars[1].x > 6);
}

TEST_CASE("a lane selection by an object or to a boundary stays inside it")
{
    // vib, v]b and Ctrl-v ib in bar 1 take its chord, not bar 2's A2.
    for (const auto* keys : {U"vib", U"v]b"})
    {
        Lane l;
        l.intoLane();
        l.type(keys);
        CHECK(l.view().laneTaken().size() == 2);
        CHECK((l.view().time() == 0 && l.view().pitch() == 31));
    }
    Lane box;
    box.intoLane();
    box.press(Key::typed(U'v', kCtrl));
    box.type(U"ib");
    CHECK(box.view().laneTaken().size() == 2);
    CHECK(box.view().selectionStatus()->starts_with("2 notes, velocities 80–96"));

    // Not selecting, ]b goes on to the next bar's first note.
    Lane l;
    l.intoLane();
    l.type(U"]b");
    CHECK((l.view().time() == l.bar(2) && l.view().pitch() == 45));
}
