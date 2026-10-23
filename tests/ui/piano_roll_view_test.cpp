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
#include <utility>

using namespace modal;
using namespace modal::ui;

namespace
{

constexpr CellSize kCell{10, 20};

// Ticks for a note value at a resolution.
struct Ticks
{
    core::Ppqn ppqn;

    core::Tick quarters(core::Tick n) const { return n * ppqn.quarter(); }
    core::Tick bar(std::int64_t n) const { return quarters(4 * (n - 1)); } // in 4/4
    core::Tick sixteenth() const { return ppqn.quarter() / 4; }
};

// A song of 12 bars in 4/4 and two sections, Section 1 of bars 1–2 and Verse of bars 3–12,
// and two tracks: a bass with a chord at the start (G1 then E1, as added), an A2 in bar 2,
// a C2 just after bar 5's start and an A1 in bar 12; and a lead with no notes.
core::Song bassAndLead(core::Ppqn ppqn)
{
    const Ticks t{ppqn};
    core::Song song{ppqn};
    const auto bass = song.addTrack("Bass");
    song.addTrack("Lead");
    song.updateEvents(bass,
                      [&](core::EventList& events)
                      {
                          events.insert({0, core::Note{31, 80, t.quarters(1) / 2}});
                          events.insert({0, core::Note{28, 96, t.quarters(1) / 2}});
                          events.insert({t.bar(2), core::Note{45, 50, t.quarters(1)}});
                          events.insert({t.bar(5) + 7, core::Note{36, 100, t.sixteenth()}});
                          events.insert({t.bar(12), core::Note{33, 70, t.quarters(4)}});
                      });
    song.splitSection(t.bar(3), "Verse");
    song.fitToContent();
    return song;
}

// The song in a workspace of 120 by 30 cells, opened in the piano roll from the arrangement.
struct Rolling
{
    explicit Rolling(core::Ppqn ppqn = core::kPpqn)
        : t{ppqn}
        , document{bassAndLead(ppqn)}
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

    const PianoRollView& view() const { return ws.pianoRoll(); }

    // The text of the regions on a background, such as the inspector's panel.
    std::u32string textOn(Role background) const
    {
        std::u32string text;
        const auto frame = ws.frame();
        for (const auto& region : frame)
        {
            if (region.background == background)
            {
                for (const auto& run : region.text)
                {
                    text += run.text + U" ";
                }
            }
        }
        return text;
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

    Ticks t;
    core::Document document;
    CommandRegistry registry;
    Workspace ws{registry, document};
};

bool contains(const std::u32string& text, std::u32string_view part)
{
    return text.find(part) != std::u32string::npos;
}

} // namespace

TEST_CASE("the piano roll opens on the track at the cursor, centred on its notes")
{
    Rolling r;
    REQUIRE(r.ws.shown() == ViewKind::PianoRoll);
    CHECK(r.view().track() == 0);
    CHECK(r.view().time() == 0);
    CHECK(r.view().pitch() == 36); // the middle of E1 to A2
    // Centred: as many rows above the cursor's pitch as below, give or take one.
    const int above = r.view().topPitch() - r.view().pitch();
    CHECK(std::abs(above - r.view().noteRows() / 2) <= 1);
    CHECK(r.view().page() == PianoRollView::Page{.firstBar = 1,
                                                 .lastBar = 2,
                                                 .start = 0,
                                                 .end = r.t.bar(3),
                                                 .section = 0,
                                                 .number = 1,
                                                 .count = 1});
    CHECK(contains(r.textOn(Role::Bar), U"Bass"));
    CHECK(contains(r.textOn(Role::Bar), U"1–2"));
    CHECK(contains(r.textOn(Role::Bar), U"1/1"));
    CHECK(contains(r.status(), U"1:1:000  C2"));
}

TEST_CASE("Enter on a bar in the arrangement opens it in the piano roll, at its start")
{
    Rolling r;
    r.type(U"ga");
    REQUIRE(r.ws.shown() == ViewKind::Arrangement);
    r.type(U"llll"); // bar 5
    r.press(Key::of(NamedKey::Enter));
    REQUIRE(r.ws.shown() == ViewKind::PianoRoll);
    CHECK(r.view().time() == r.t.bar(5));
    CHECK(r.view().page().firstBar == 3);
    CHECK(r.view().page().lastBar == 6);
    CHECK(r.view().page().number == 1);
    CHECK(r.view().page().count == 3);
    CHECK(contains(r.textOn(Role::Bar), U"Verse"));
}

TEST_CASE("the piano roll's time motions go by the grid, beats and bars")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    r.type(U"l");
    CHECK(r.view().time() == t.sixteenth());
    r.type(U"4l");
    CHECK(r.view().time() == 5 * t.sixteenth());
    r.type(U"h");
    CHECK(r.view().time() == t.quarters(1));
    r.type(U"w");
    CHECK(r.view().time() == t.quarters(2));
    r.type(U"$");
    CHECK(r.view().time() == t.bar(2) - t.sixteenth());
    r.type(U"0");
    CHECK(r.view().time() == 0);
    r.type(U"}l");
    CHECK(r.view().time() == t.bar(2) + t.sixteenth());
    r.type(U"]]");
    CHECK(r.view().time() == t.bar(3));
    r.type(U"G");
    CHECK(r.view().time() == t.bar(13) - t.sixteenth());
    r.type(U"l"); // the end: no further
    CHECK(r.view().time() == t.bar(13) - t.sixteenth());
    r.type(U"gg");
    CHECK(r.view().time() == 0);
    r.type(U"7G");
    CHECK(r.view().time() == t.bar(7));
    r.type(U"[s");
    CHECK(r.view().time() == t.bar(3));

    // From off the grid, the grid steps either side first.
    r.type(U"5G");
    r.press(Key::of(NamedKey::Tab)); // C2, 7 ticks into bar 5
    REQUIRE(r.view().time() == t.bar(5) + 7);
    r.type(U"l");
    CHECK(r.view().time() == t.bar(5) + t.sixteenth());
    r.press(Key::of(NamedKey::Tab));
    r.press(Key::of(NamedKey::Tab, kShift));
    r.type(U"h");
    CHECK(r.view().time() == t.bar(5));
}

TEST_CASE("the piano roll turns its pages as the cursor moves, and zooms by bars to a page")
{
    Rolling r;
    r.type(U"}}");
    CHECK(r.view().page().firstBar == 3);
    CHECK(r.view().page().lastBar == 6);
    r.type(U"4}");
    CHECK(r.view().page().firstBar == 7);
    CHECK(r.view().page().number == 2);
    r.type(U"zo");
    CHECK(r.view().pageBars() == 8);
    CHECK(r.view().page().firstBar == 3);
    CHECK(r.view().page().lastBar == 10);
    r.type(U"zizi");
    CHECK(r.view().pageBars() == 2);
    CHECK(r.view().page().firstBar == 7);
    CHECK(r.view().page().count == 5);
    r.type(U"z0");
    CHECK(r.view().pageBars() == 4);
    r.type(U"G"); // the section's last page is cut short
    CHECK(r.view().page().firstBar == 11);
    CHECK(r.view().page().lastBar == 12);
    CHECK(r.view().page().end == r.t.bar(13));
}

TEST_CASE("the piano roll's cursor moves by semitones and octaves, keeping its pitch in view")
{
    Rolling r;
    r.type(U"k");
    CHECK(r.view().pitch() == 37);
    r.type(U"3j");
    CHECK(r.view().pitch() == 34);
    r.type(U"K");
    CHECK(r.view().pitch() == 46);
    r.type(U"2J");
    CHECK(r.view().pitch() == 22);
    r.type(U"20K");
    CHECK(r.view().pitch() == 127);
    CHECK(r.view().topPitch() == 127);
    r.type(U"200j");
    CHECK(r.view().pitch() == 0);
    CHECK(r.view().topPitch() == r.view().noteRows() - 1);
    // Scrolling moves the cursor with the rows; zz centres on it.
    r.type(U"60k");
    const auto top = r.view().topPitch();
    r.ws.run("cursor-up-scroll", 2);
    CHECK(r.view().pitch() == 62);
    CHECK(r.view().topPitch() == top + 2);
    r.type(U"zz");
    CHECK(std::abs(r.view().topPitch() - 62 - r.view().noteRows() / 2) <= 1);
}

TEST_CASE("Tab and Shift-Tab go to each note's start in turn, low to high at the same time")
{
    Rolling r;
    const auto& t = r.t;
    r.type(U"G");
    const auto back = [&]
    {
        r.press(Key::of(NamedKey::Tab, kShift));
        return std::pair{r.view().time(), r.view().pitch()};
    };
    CHECK(back() == std::pair{t.bar(12), std::uint8_t{33}});
    CHECK(back() == std::pair{t.bar(5) + 7, std::uint8_t{36}});
    CHECK(back() == std::pair{t.bar(2), std::uint8_t{45}});
    CHECK(back() == std::pair{core::Tick{0}, std::uint8_t{31}});
    CHECK(back() == std::pair{core::Tick{0}, std::uint8_t{28}});
    CHECK(back() == std::pair{core::Tick{0}, std::uint8_t{28}}); // the first: no further
    r.press(Key::of(NamedKey::Tab));
    CHECK(r.view().pitch() == 31);
    r.ws.run("cursor-note-next", 2);
    CHECK(r.view().time() == t.bar(5) + 7);
}

TEST_CASE("( and ) go to the starts of chords, on their lowest notes")
{
    Rolling r;
    r.type(U")");
    CHECK(r.view().time() == r.t.bar(2));
    CHECK(r.view().pitch() == 45);
    r.type(U"(");
    CHECK(r.view().time() == 0);
    CHECK(r.view().pitch() == 28);
}

TEST_CASE("the note under the piano roll's cursor is the current note, which the inspector shows")
{
    Rolling r;
    r.type(U"8j"); // E1, at the start
    REQUIRE(r.view().pitch() == 28);
    REQUIRE(r.view().currentNote());
    CHECK(r.document.song()
              .events(r.document.song().tracks()[0]->id())[*r.view().currentNote()]
              .tick == 0);
    auto inspector = r.textOn(Role::Panel);
    CHECK(contains(inspector, U"Note"));
    CHECK(contains(inspector, U"E1 (28)"));
    CHECK(contains(inspector, U"96"));
    r.type(U"l"); // inside it still
    CHECK(r.view().currentNote());
    r.type(U"l"); // at its end
    CHECK_FALSE(r.view().currentNote());
    inspector = r.textOn(Role::Panel);
    CHECK(contains(inspector, U"No note"));
    CHECK(contains(inspector, U"1:1:480"));
    CHECK(contains(inspector, U"E1 (28)"));

    // The current note is drawn in the accent colour.
    r.type(U"0");
    const auto frame = r.ws.frame();
    CHECK(std::ranges::any_of(frame,
                              [](const Region& region)
                              {
                                  return std::ranges::any_of(
                                      region.under, [](const Shape& shape)
                                      { return shape.role == Role::Accent; });
                              }));
}

TEST_CASE("the piano roll shares the cursor with the other views")
{
    Rolling r;
    r.type(U")"); // A2 in bar 2
    r.type(U"ga");
    CHECK(r.ws.arrangement().page().bar() == 2);
    r.type(U"gr");
    CHECK(r.view().time() == r.t.bar(2));
    CHECK(r.view().pitch() == 45);
    r.type(U"ge");
    REQUIRE(r.ws.shown() == ViewKind::EventList);
    CHECK(r.ws.eventList().cursor().pitch == 45); // on the note's own row
    r.press(Key::of(NamedKey::Backspace));
    CHECK(r.ws.shown() == ViewKind::PianoRoll);
    CHECK(r.view().pitch() == 45);
}

TEST_CASE("gt and gT go between the tracks in the piano roll, each keeping its pitch")
{
    Rolling r;
    r.type(U"K");
    r.type(U"gt");
    CHECK(r.view().track() == 1);
    CHECK(r.view().pitch() == 60); // no notes: middle C
    CHECK(contains(r.textOn(Role::Bar), U"Lead"));
    r.type(U"gt"); // round to the first
    CHECK(r.view().track() == 0);
    CHECK(r.view().pitch() == 48);
    r.type(U"2gt");
    CHECK(r.view().track() == 1);
    r.type(U"gT");
    CHECK(r.view().track() == 0);
}

TEST_CASE("the piano roll follows the playhead from page to page")
{
    Rolling r;
    r.ws.pianoRoll().showPlayhead(r.t.bar(8), true);
    CHECK(r.view().page().firstBar == 7);
    r.ws.pianoRoll().showPlayhead(r.t.bar(9), false); // not following: the page stays
    CHECK(r.view().page().firstBar == 7);
    r.ws.pianoRoll().stopPlayhead(r.t.bar(1));
    CHECK(r.view().time() == 0);
}

TEST_CASE("the piano roll shows a song with no tracks")
{
    core::Document document{core::Song{core::kPpqn}};
    CommandRegistry registry;
    registry.add(windowCommands());
    Workspace ws{registry, document};
    ws.resize(120 * kCell.width, 30 * kCell.height, kCell);
    ws.show(ViewKind::PianoRoll);
    CHECK(ws.shown() == ViewKind::PianoRoll);
    for (const char32_t key : std::u32string_view{U"lkG)"})
    {
        ws.handle(Key::typed(key), 0);
    }
    ws.handle(Key::of(NamedKey::Tab), 0);
    CHECK_FALSE(ws.frame().empty());
}

TEST_CASE("zh, zl and Ctrl-T change the track's grid, which h and l step by")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    CHECK(r.view().grid() == core::NoteFraction{1, 16});
    r.type(U"zl");
    CHECK(r.view().grid() == core::NoteFraction{1, 32});
    r.type(U"2zh");
    CHECK(r.view().grid() == core::NoteFraction{1, 8});
    CHECK(contains(r.textOn(Role::Bar), U"1/8"));
    r.press(Key::typed(U't', kCtrl));
    CHECK(r.view().grid() == core::NoteFraction{1, 12});
    CHECK(contains(r.textOn(Role::Bar), U"1/8t"));
    r.type(U"l");
    CHECK(r.view().time() == t.quarters(1) / 3);
    r.press(Key::typed(U't', kCtrl));
    CHECK(r.view().grid() == core::NoteFraction{1, 8});
    r.type(U"9zh"); // no coarser than a whole note
    CHECK(r.view().grid() == core::NoteFraction{1, 1});
    r.type(U"9zl"); // nor finer than a 1/64
    CHECK(r.view().grid() == core::NoteFraction{1, 64});
    CHECK(r.document.modified());

    // The changes one after another undo together.
    r.type(U"u");
    CHECK(r.view().grid() == core::NoteFraction{1, 16});
    CHECK_FALSE(r.document.canUndo());

    // Each track has its own.
    r.type(U"zh");
    r.type(U"gt");
    CHECK(r.view().grid() == core::NoteFraction{1, 16});
    r.type(U"gT");
    CHECK(r.view().grid() == core::NoteFraction{1, 8});
}

TEST_CASE(", n takes the current note's length and velocity as the track's default note")
{
    Rolling r;
    const auto& bass = [&]() -> const core::Track& { return *r.document.song().tracks()[0]; };
    CHECK(contains(r.textOn(Role::Bar), U"grid v100"));
    r.type(U",n"); // on no note
    CHECK(r.ws.message() == "No note here to take as the default");
    CHECK_FALSE(bass().noteLength);

    r.type(U"8j"); // E1, an eighth note at velocity 96
    r.type(U",n");
    CHECK(bass().noteLength == core::NoteFraction{1, 8});
    CHECK(bass().noteVelocity == 96);
    CHECK(r.ws.message() == "Default note: 1/8, velocity 96");
    CHECK(contains(r.textOn(Role::Bar), U"1/8 v96"));
    r.type(U"u");
    CHECK_FALSE(bass().noteLength);
    CHECK(bass().noteVelocity == 100);
}

TEST_CASE(":grid and :len set the track's grid and default length")
{
    Rolling r;
    const auto& bass = [&]() -> const core::Track& { return *r.document.song().tracks()[0]; };
    const auto line = [&](std::string_view text) { return r.registry.runLine(text); };
    CHECK(line("grid 1/8t") == CommandResult{true});
    CHECK(bass().grid == core::NoteFraction{1, 12});
    CHECK(line("grid") == CommandResult{false, "Grid 1/8t", false});
    CHECK(line("grid 1/256") == CommandResult::error("The grid can be from 1/64 to 1/1"));
    CHECK(line("grid long") ==
          CommandResult::error("long isn't a note length, such as 1/16, 1/8. or 1/8t"));
    CHECK(line("len 1/4.") == CommandResult{true});
    CHECK(bass().noteLength == core::NoteFraction{3, 8});
    CHECK(line("len") == CommandResult{false, "Default note 1/4. v100", false});
    CHECK(line("len grid") == CommandResult{true});
    CHECK_FALSE(bass().noteLength);
    CHECK(bass().defaultLength() == core::NoteFraction{1, 12});

    r.type(U"ge"); // not in the event list
    line("grid 1/4");
    CHECK(r.ws.message() == "Grid isn't in the event list");
    CHECK(bass().grid == core::NoteFraction{1, 12});
}
