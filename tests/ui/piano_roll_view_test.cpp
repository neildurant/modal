// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/key_notation.hpp>
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
#include <utility>
#include <variant>
#include <vector>

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
    // Four bars, from the section's first, running on into the next section.
    CHECK(r.view().page() ==
          PianoRollView::Page{
              .firstBar = 1, .lastBar = 4, .start = 0, .end = r.t.bar(5), .section = 0});
    CHECK(contains(r.textOn(Role::Bar), U"Bass"));
    CHECK(contains(r.textOn(Role::Bar), U"1–4"));
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
    CHECK(r.view().page().firstBar == 3); // pages restart at the section
    CHECK(r.view().page().lastBar == 6);
    CHECK(contains(r.textOn(Role::Bar), U"Verse"));
}

TEST_CASE("the piano roll's time motions go by the grid and bars")
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
    r.type(U"4l");
    CHECK(r.view().time() == t.quarters(2));
    r.type(U"$");
    CHECK(r.view().time() == t.bar(2) - t.sixteenth());
    r.type(U"0");
    CHECK(r.view().time() == 0);
    r.type(U"}l");
    CHECK(r.view().time() == t.bar(2) + t.sixteenth());
    r.type(U"]b");
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
    // On it, a sixteenth long, l goes off it to the first grid step at or after its end.
    r.type(U"l");
    CHECK(r.view().time() == t.bar(5) + 2 * t.sixteenth());
    r.press(Key::of(NamedKey::Tab));
    r.press(Key::of(NamedKey::Tab, kShift));
    r.type(U"h");
    CHECK(r.view().time() == t.bar(5));
}

TEST_CASE("the piano roll turns by its width as the cursor moves past an edge")
{
    Rolling r;
    const auto& t = r.t;
    r.type(U"3}"); // bar 4, still in view
    CHECK(r.view().page().start == 0);
    r.type(U"}");
    CHECK(r.view().page().start == t.bar(5));
    CHECK(r.view().page().end == t.bar(9));
    r.type(U"G"); // the song's last grid step, in bar 12
    CHECK(r.view().page().start == t.bar(9));
    r.type(U"gg");
    CHECK(r.view().page().start == 0);
}

TEST_CASE("zz centres the piano roll on the cursor in time, and turning keeps the view's place")
{
    Rolling r;
    const auto& t = r.t;
    r.type(U"5G4l"); // bar 5, beat 2
    r.type(U"zz");
    CHECK(r.view().page().start == t.bar(3) + t.quarters(1));
    CHECK(r.view().page().end == t.bar(7) + t.quarters(1));
    CHECK(r.view().page().firstBar == 3);
    CHECK(r.view().page().lastBar == 7);
    CHECK(contains(r.textOn(Role::Bar), U"3–7"));
    r.type(U"2}"); // past the right edge: on by four bars, still at beat 2
    CHECK(r.view().page().start == t.bar(7) + t.quarters(1));
    r.type(U"3{"); // back past the left edge
    CHECK(r.view().page().start == t.bar(3) + t.quarters(1));

    // Near the end, the view may run past the song's end; never before its start.
    r.type(U"Gzz");
    CHECK(r.view().page().end > r.document.song().end());
    r.type(U"ggzz");
    CHECK(r.view().page().start == 0);
}

TEST_CASE("Ctrl with h, l, { and } scroll the piano roll with the cursor")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    r.type(U"5G"); // bar 5: the view is bars 5-8
    REQUIRE(r.view().page().start == t.bar(5));
    r.press(Key::typed(U'l', kCtrl));
    CHECK(r.view().time() == t.bar(5) + t.sixteenth());
    CHECK(r.view().page().start == t.bar(5) + t.sixteenth());
    r.press(Key::typed(U'h', kCtrl));
    r.press(Key::typed(U'h', kCtrl));
    CHECK(r.view().time() == t.bar(5) - t.sixteenth());
    CHECK(r.view().page().start == t.bar(5) - t.sixteenth());
    r.press(Key::typed(U'}', kCtrl));
    CHECK(r.view().time() == t.bar(6) - t.sixteenth());
    CHECK(r.view().page().start == t.bar(6) - t.sixteenth());
    r.ws.run("cursor-bar-previous-scroll", 2);
    CHECK(r.view().time() == t.bar(4) - t.sixteenth());
    CHECK(r.view().page().start == t.bar(4) - t.sixteenth());
    // As far back as bar 1 goes, the view moving as the cursor does.
    r.ws.run("cursor-bar-previous-scroll", 5);
    CHECK(r.view().time() == t.bar(1) + t.quarters(4) - t.sixteenth());
    CHECK(r.view().page().start == r.view().time());
    // At the start the view stops, and the cursor goes on.
    r.type(U"4l");
    r.ws.run("cursor-left-scroll", 16);
    CHECK(r.view().page().start == 0);
    CHECK(r.view().time() == 3 * t.sixteenth());
}

TEST_CASE("Ctrl-Shift-l and Ctrl-Shift-h scroll the piano roll by a page")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    r.type(U"5Gl"); // bar 5 and a step: the view is bars 5-8
    REQUIRE(r.view().page().start == t.bar(5));
    const auto across = [&] { return r.view().time() - r.view().page().start; };
    r.press(Key::typed(U'l', kCtrl | kShift));
    CHECK(r.view().time() == t.bar(9) + t.sixteenth());
    CHECK(r.view().page().start == t.bar(9));
    // Past the last step, only as far as it, the view moving as far.
    r.press(Key::of(NamedKey::Right, kCtrl | kShift));
    CHECK(r.view().time() < t.bar(13) + t.sixteenth());
    CHECK(r.view().time() > t.bar(9) + t.sixteenth());
    CHECK(across() == t.sixteenth());
    r.press(Key::typed(U'h', kCtrl | kShift));
    CHECK(across() == t.sixteenth());
    // At the start the view stops, and the cursor goes on.
    r.ws.run("scroll-page-left", 3);
    CHECK(r.view().page().start == 0);
    CHECK(r.view().time() == 0);
}

TEST_CASE("scrolling the piano roll in time keeps its scale over bars of another length")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    r.document.edit("3/4 from bar 7", {},
                    [&](core::Song& song)
                    {
                        song.updateMeter([&](core::MeterMap& meter)
                                         { meter.set(t.bar(7), core::TimeSignature{3, 4}); });
                    });
    const auto& m = r.document.song().meter();
    const auto length = [&] { return r.view().page().end - r.view().page().start; };
    const auto across = [&] { return r.view().time() - r.view().page().start; };
    r.type(U"5G"); // bars 5-8: two of 4/4, then two of 3/4
    REQUIRE(r.view().page().start == t.bar(5));
    REQUIRE(length() == t.quarters(14));
    r.press(Key::typed(U'l', kCtrl));
    r.press(Key::typed(U'}', kCtrl));
    CHECK(r.view().time() == t.bar(6) + t.sixteenth());
    CHECK(across() == 0); // still at the left edge
    CHECK(length() == t.quarters(14));
    r.ws.run("cursor-bar-next-scroll", 2); // into the 3/4 bars
    CHECK(r.view().time() == m.barStart(8) + t.sixteenth());
    CHECK(across() == 0); // still at the left edge
    CHECK(length() == t.quarters(14));

    // Turning goes on from the view's end, four bars wide again.
    const auto end = r.view().page().end;
    r.type(U"5}");
    CHECK(r.view().time() == m.barStart(13) + t.sixteenth());
    CHECK(r.view().page().start == end);
    CHECK(length() == t.quarters(12));
}

TEST_CASE("zooming the piano roll keeps the cursor in its place on screen")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    r.type(U"5G"); // bars 5-8 in view
    r.type(U"2}"); // the start of bar 7, halfway across
    r.type(U"zo"); // eight bars, bar 7 still halfway
    CHECK(r.view().pageBars() == 8);
    CHECK(r.view().page().firstBar == 3);
    CHECK(r.view().page().lastBar == 10);
    r.type(U"zi");
    CHECK(r.view().page().firstBar == 5);
    r.type(U"zi"); // two bars
    CHECK(r.view().page().firstBar == 6);
    CHECK(r.view().page().lastBar == 7);

    // z0 goes back to whole bars, the section's page holding the cursor.
    r.type(U"z0");
    CHECK(r.view().pageBars() == 4);
    CHECK(r.view().page().start == t.bar(7));

    // Off the bar lines too, zooming out and back in comes back to the same view.
    r.type(U"12l"); // beat 4, three quarters of sixteen across
    r.type(U"zo");
    CHECK(r.view().page().start == t.bar(7) - t.quarters(3));
    r.type(U"zozizi");
    CHECK(r.view().page().start == t.bar(7));

    // Near the start the view stops at bar 1; and the cursor always stays in view.
    r.type(U"gg8lzo");
    CHECK(r.view().page().start == 0);
    CHECK(r.view().time() == t.quarters(2));
    r.type(U"G4zi");
    CHECK(r.view().pageBars() == 1);
    CHECK(r.view().time() >= r.view().page().start);
    CHECK(r.view().time() < r.view().page().end);
}

namespace
{

// The runs on a row of the ruler above the notes, as x and text, in order.
std::vector<std::pair<int, std::u32string>> rulerRow(const Rolling& r, int row)
{
    const auto frame = r.ws.frame();
    std::vector<std::pair<int, std::u32string>> runs;
    for (const auto& region : frame)
    {
        if (region.bounds.height == PianoRollView::kRulerRows * kCell.height && region.bounds.x > 0)
        {
            for (const auto& run : region.text)
            {
                if (run.y == row * kCell.height)
                {
                    runs.emplace_back(region.bounds.x + run.x, run.text);
                }
            }
        }
    }
    std::ranges::sort(runs);
    return runs;
}

std::vector<std::u32string> texts(const std::vector<std::pair<int, std::u32string>>& runs)
{
    std::vector<std::u32string> result;
    for (const auto& run : runs)
    {
        result.push_back(run.second);
    }
    return result;
}

} // namespace

TEST_CASE("the piano roll's ruler names the sections and markers and numbers the bars")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    r.document.edit("Marker", {}, [&](core::Song& song) { song.setMarker(t.bar(4), "Fill"); });
    using Texts = std::vector<std::u32string>;
    // Bars 1-4: Section 1, then the Verse from bar 3, with the marker in it.
    CHECK(texts(rulerRow(r, 0)) == Texts{U"Section 1", U"Verse", U"Fill"});
    CHECK(texts(rulerRow(r, 1)) == Texts{U"1", U"2", U"3", U"4"});
    // The section's start a line down through the notes, stronger than the marker's.
    std::vector<std::uint8_t> lines;
    const auto frame = r.ws.frame();
    for (const auto& region : frame)
    {
        if (region.bounds.y > 0 && region.bounds.height > 10 * kCell.height)
        {
            for (const auto& shape : region.under)
            {
                if (shape.role == Role::Marker)
                {
                    lines.push_back(shape.alpha);
                }
            }
        }
    }
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] > lines[1]);

    // Scrolled into the Verse, its name stays at the left edge.
    r.type(U"5G4l");
    r.press(Key::typed(U'l', kCtrl));
    const auto top = rulerRow(r, 0);
    REQUIRE(!top.empty());
    CHECK(top.front().second == U"Verse");
    CHECK(top.front().first < (PianoRollView::kKeyColumns + 1) * kCell.width);
    CHECK(texts(rulerRow(r, 1)) == Texts{U"6", U"7", U"8", U"9"});
}

TEST_CASE("the piano roll keeps its view when it's shown again, but Enter opens whole bars")
{
    Rolling r;
    r.type(U"5G4lzz");
    const auto scrolled = r.view().page().start;
    r.type(U"gagr");
    CHECK(r.view().page().start == scrolled);
    r.type(U"ga");
    r.press(Key::of(NamedKey::Enter));
    CHECK(r.view().page().start == r.t.bar(3)); // the Verse's page holding bar 5
    // Coming back to a time outside the view starts again from whole bars too.
    r.type(U"zz");
    r.type(U"ga2Ggr");
    CHECK(r.view().page().start == 0);
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
    r.type(U"l"); // off it in one step, to its end, rather than a grid step inside it
    CHECK_FALSE(r.view().currentNote());
    inspector = r.textOn(Role::Panel);
    CHECK(contains(inspector, U"No note"));
    CHECK(contains(inspector, U"1:1:480"));
    CHECK(contains(inspector, U"E1 (28)"));

    // The current note is drawn in its own colour.
    r.type(U"0");
    const auto frame = r.ws.frame();
    CHECK(std::ranges::any_of(frame,
                              [](const Region& region)
                              {
                                  return std::ranges::any_of(
                                      region.under, [](const Shape& shape)
                                      { return shape.role == Role::NoteSelected; });
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
    r.ws.pianoRoll().showPlayhead(r.t.bar(8), Follow::Cursor);
    CHECK(r.view().page().firstBar == 5);
    r.ws.pianoRoll().stopPlayhead(r.t.bar(1));
    CHECK(r.view().time() == 0);
    CHECK(r.view().page().firstBar == 1);

    // Not following, the pages turn with the playhead, the cursor staying where it is, and
    // come back to it when playback stops.
    r.ws.pianoRoll().showPlayhead(r.t.bar(4), Follow::View);
    CHECK(r.view().page().firstBar == 1);
    r.ws.pianoRoll().showPlayhead(r.t.bar(6), Follow::View);
    r.ws.pianoRoll().showPlayhead(r.t.bar(10), Follow::None); // moved away from it
    CHECK(r.view().page().firstBar == 5);
    CHECK(r.view().page().firstBar == 5);
    CHECK(r.view().time() == 0);
    r.ws.pianoRoll().stopPlayhead(std::nullopt);
    CHECK(r.view().page().firstBar == 1);
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

TEST_CASE("zl, zh and Ctrl-T change the track's grid, which h and l step by")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    CHECK(r.view().grid() == core::NoteFraction{1, 16});
    r.type(U"zh");
    CHECK(r.view().grid() == core::NoteFraction{1, 32});
    r.type(U"2zl");
    CHECK(r.view().grid() == core::NoteFraction{1, 8});
    CHECK(contains(r.textOn(Role::Bar), U"1/8"));
    r.press(Key::typed(U't', kCtrl));
    CHECK(r.view().grid() == core::NoteFraction{1, 12});
    CHECK(contains(r.textOn(Role::Bar), U"1/8t"));
    r.type(U"l");
    CHECK(r.view().time() == t.quarters(1) / 3);
    r.press(Key::typed(U't', kCtrl));
    CHECK(r.view().grid() == core::NoteFraction{1, 8});
    r.type(U"9zl"); // no coarser than a whole note
    CHECK(r.view().grid() == core::NoteFraction{1, 1});
    r.type(U"9zh"); // nor finer than a 1/64
    CHECK(r.view().grid() == core::NoteFraction{1, 64});
    CHECK(r.document.modified());

    // The changes one after another undo together.
    r.type(U"u");
    CHECK(r.view().grid() == core::NoteFraction{1, 16});
    CHECK_FALSE(r.document.canUndo());

    // Each track has its own.
    r.type(U"zl");
    r.type(U"gt");
    CHECK(r.view().grid() == core::NoteFraction{1, 16});
    r.type(U"gT");
    CHECK(r.view().grid() == core::NoteFraction{1, 8});
}

TEST_CASE("the default note key takes the current note's length and velocity, as , n")
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
    CHECK(r.ws.message() == "Grid is for the piano roll");
    CHECK(bass().grid == core::NoteFraction{1, 12});
}

TEST_CASE("the piano roll's cursor covers the current note, or the default note it would add")
{
    Rolling r;
    const auto& t = r.t;
    r.type(U"8j"); // E1, an eighth note at the start
    CHECK(r.view().cursorSpan() == std::pair{core::Tick{0}, t.quarters(1) / 2});
    r.type(U"l"); // past it in one: the default note, the grid's length
    CHECK(r.view().cursorSpan() == std::pair{t.quarters(1) / 2, t.quarters(1) / 2 + t.sixteenth()});
    r.registry.runLine("len 1/2");
    CHECK(r.view().cursorSpan() == std::pair{t.quarters(1) / 2, t.quarters(1) / 2 + t.quarters(2)});
    r.type(U"h"); // back onto its last grid step
    CHECK(r.view().cursorSpan() == std::pair{core::Tick{0}, t.quarters(1) / 2});
    CHECK(r.view().time() == t.quarters(1) / 2 - t.sixteenth());
}

TEST_CASE("h and l go off a long note in one step, to the grid steps either side of it")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    // A half note on E1 at bar 3's second beat, the grid a sixteenth.
    const auto start = t.bar(3) + t.quarters(1);
    r.document.edit("Add", {},
                    [&](core::Song& song)
                    {
                        song.updateEvents(
                            song.tracks()[0]->id(), [&](core::EventList& events)
                            { events.insert({start, core::Note{28, 90, t.quarters(2)}}); });
                    });
    r.ws.pianoRoll().setCursor(
        {.track = r.document.song().tracks()[0]->id(), .tick = start + t.quarters(1), .pitch = 28});
    REQUIRE(r.view().currentNote());
    r.type(U"l");
    CHECK(r.view().time() == start + t.quarters(2)); // its end
    CHECK_FALSE(r.view().currentNote());
    r.type(U"h"); // back onto its last step
    CHECK(r.view().time() == start + t.quarters(2) - t.sixteenth());
    r.type(U"h"); // and off before its start
    CHECK(r.view().time() == start - t.sixteenth());
    r.type(U"2l"); // onto its start, then past its end
    CHECK(r.view().time() == start + t.quarters(2));
}

TEST_CASE("the piano roll draws at any window size, even none")
{
    Rolling r;
    r.type(U"8j");
    for (const auto& [width, height] : {std::pair{0, 0}, std::pair{1, 1}, std::pair{50, 30},
                                        std::pair{120 * kCell.width, 3 * kCell.height},
                                        std::pair{3 * kCell.width, 30 * kCell.height},
                                        std::pair{120 * kCell.width, 30 * kCell.height}})
    {
        r.ws.resize(width, height, kCell);
        CHECK_FALSE(r.ws.frame().empty());
        r.type(U"kjlhG");
        r.press(Key::of(NamedKey::Tab));
        CHECK_FALSE(r.ws.frame().empty());
    }
}

namespace
{

// The bass's notes, as tick and pitch, in order.
std::vector<std::pair<core::Tick, int>> bassNotes(const Rolling& r)
{
    std::vector<std::pair<core::Tick, int>> notes;
    const auto& song = r.document.song();
    for (const auto& event : song.events(song.tracks()[0]->id()).all())
    {
        if (const auto* note = std::get_if<core::Note>(&event.data))
        {
            notes.emplace_back(event.tick, note->pitch);
        }
    }
    std::ranges::sort(notes);
    return notes;
}

} // namespace

TEST_CASE("o adds the track's default note at the piano roll's cursor, and x deletes it")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    const auto& song = r.document.song();
    const auto before = bassNotes(r);
    r.type(U"o"); // C2 at the start
    const auto index = r.view().currentNote();
    REQUIRE(index);
    const auto& event = song.events(song.tracks()[0]->id())[*index];
    CHECK(event.tick == 0);
    CHECK(std::get<core::Note>(event.data) == core::Note{36, 100, t.sixteenth()});
    r.type(U"o");
    CHECK(r.ws.message() == "There's a note here already");

    // Not into another of its pitch: the C2 just after bar 5's start.
    r.type(U"5Go");
    CHECK(r.ws.message().starts_with("Would run into the C2 at 5:1"));
    CHECK(bassNotes(r).size() == before.size() + 1);

    r.type(U"ggx");
    CHECK(bassNotes(r) == before);
    r.type(U"x");
    CHECK(r.ws.message() == "No note here to delete");
    r.type(U"u"); // back, with the cursor on it
    CHECK(r.view().currentNote());
    CHECK(r.view().time() == 0);
    CHECK(bassNotes(r).size() == before.size() + 1);
}

TEST_CASE("x and Y take the piano roll's current note, which p puts at the cursor's time")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    const auto before = bassNotes(r);
    r.type(U"8jY"); // E1 at the start
    CHECK(r.ws.message() == "E1 yanked");
    r.type(U"8k}p"); // at C2, a bar on: the E1 at its own pitch
    auto expected = before;
    expected.emplace_back(t.bar(2), 28);
    std::ranges::sort(expected);
    CHECK(bassNotes(r) == expected);
    r.type(U"p");
    CHECK(r.ws.message().starts_with("Would overlap the E1 at 2:1"));

    r.type(U"8jx");
    CHECK(bassNotes(r) == before);
    r.type(U"din"); // as x
    CHECK(r.ws.message() == "No note here to delete");
    r.type(U"ggp"); // the deleted E1 back at the start overlaps the one there
    CHECK(r.ws.message().starts_with("Would overlap the E1 at 1:1"));
}

TEST_CASE("dd and yy take the bar's notes in the piano roll, which p puts from a bar's start")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    const auto before = bassNotes(r);
    r.type(U"8ldd"); // from mid-bar, bar 1's two notes
    CHECK(bassNotes(r) ==
          std::vector<std::pair<core::Tick, int>>(before.begin() + 2, before.end()));
    r.type(U"u");
    r.type(U"gg8ldib"); // ib is the same
    CHECK(bassNotes(r).size() == before.size() - 2);
    r.type(U"u");

    r.type(U"yy");
    CHECK(r.ws.message() == "2 notes yanked");
    r.type(U"3G8lp"); // from mid-bar 3, at its start
    CHECK(std::ranges::count(bassNotes(r), std::pair<core::Tick, int>{t.bar(3), 28}) == 1);
    CHECK(std::ranges::count(bassNotes(r), std::pair<core::Tick, int>{t.bar(3), 31}) == 1);
}

TEST_CASE("d and y with a motion in the piano roll take the notes starting in that time")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    const auto before = bassNotes(r);
    r.type(U"d]b"); // bar 1, at every pitch, but not the A2 at bar 2's start
    CHECK(bassNotes(r) ==
          std::vector<std::pair<core::Tick, int>>(before.begin() + 2, before.end()));
    r.type(U"u");
    CHECK(bassNotes(r) == before);

    r.type(U"5GdG"); // from bar 5 to the end, the last grid step too
    CHECK(bassNotes(r) ==
          std::vector<std::pair<core::Tick, int>>(before.begin(), before.begin() + 3));
    r.type(U"uggy]b");
    CHECK(r.ws.message() == "2 notes yanked");
    r.type(U"3G}p"); // a bar later at the same pitches
    CHECK(bassNotes(r).size() == before.size() + 2);
    CHECK(std::ranges::count(bassNotes(r), std::pair<core::Tick, int>{t.bar(4), 31}) == 1);

    r.type(U"dj");
    CHECK(r.ws.message() == "Ranges across pitches come with Visual mode");
}

TEST_CASE("Ctrl-A and Ctrl-X step the piano roll's current note's velocity, undoing together")
{
    Rolling r;
    const auto velocity = [&]
    {
        const auto& song = r.document.song();
        return std::get<core::Note>(
                   song.events(song.tracks()[0]->id())[*r.view().currentNote()].data)
            .velocity;
    };
    r.press(Key::typed(U'a', kCtrl));
    CHECK(r.ws.message() == "No note here to step");
    r.type(U"8j"); // E1, at 96
    r.press(Key::typed(U'a', kCtrl));
    CHECK(velocity() == 97);
    r.type(U"10");
    r.press(Key::typed(U'x', kCtrl));
    CHECK(velocity() == 87);
    r.type(U"200");
    r.press(Key::typed(U'a', kCtrl));
    CHECK(velocity() == 127);
    r.type(U"u");
    CHECK(velocity() == 96);
}

namespace
{

// The bass's note at a pitch nearest after a tick, as its tick and length; or none.
std::optional<std::pair<core::Tick, core::Tick>> bassNote(const Rolling& r, int pitch,
                                                          core::Tick from = 0)
{
    const auto& song = r.document.song();
    for (const auto& event : song.events(song.tracks()[0]->id()).all())
    {
        const auto* note = std::get_if<core::Note>(&event.data);
        if (note != nullptr && note->pitch == pitch && event.tick >= from)
        {
            return std::pair{event.tick, note->duration};
        }
    }
    return std::nullopt;
}

} // namespace

TEST_CASE("Alt with h and l moves the piano roll's current note to the grid, the cursor with it")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    const auto bar5 = t.bar(5);
    const auto alt = [&](char32_t key) { r.press(Key::typed(key, kAlt)); };
    r.type(U"5G");
    r.press(Key::of(NamedKey::Tab)); // the C2 just after bar 5's start, off the grid
    CHECK(r.view().time() == bar5 + 7);
    alt(U'l'); // onto the grid, the next step
    CHECK(bassNote(r, 36)->first == bar5 + t.sixteenth());
    CHECK(r.view().time() == bar5 + t.sixteenth());
    CHECK(r.view().currentNote());
    alt(U'h');
    alt(U'h'); // over the bar line
    CHECK(bassNote(r, 36)->first == bar5 - t.sixteenth());
    r.type(U"3");
    alt(U'l');
    CHECK(bassNote(r, 36)->first == bar5 + 2 * t.sixteenth());
    r.type(U"u");
    CHECK(bassNote(r, 36)->first == bar5 - t.sixteenth());
    CHECK(r.view().time() == bar5 - t.sixteenth());

    // A bar on and back, at the same place in the bar; not past the song's end.
    alt(U'}');
    CHECK(bassNote(r, 36)->first == t.bar(6) - t.sixteenth());
    r.type(U"2");
    alt(U'{');
    CHECK(bassNote(r, 36)->first == t.bar(4) - t.sixteenth());
    r.type(U"20");
    alt(U'}');
    CHECK(bassNote(r, 36)->first == t.bar(4) - t.sixteenth());

    // Nothing to move on an empty spot, and not before the start.
    r.type(U"l");
    alt(U'l');
    CHECK(r.ws.message() == "No note here to move");
    r.type(U"gg");
    r.type(U"8j"); // E1 at the start
    alt(U'h');
    CHECK(bassNote(r, 28)->first == 0);
}

TEST_CASE("Alt with k, j, K and J move the piano roll's current note in pitch")
{
    Rolling r;
    const auto alt = [&](char32_t key, Modifiers more = kNoModifiers)
    { r.press(Key::typed(key, kAlt | more)); };
    r.type(U"8j"); // E1 at the start
    alt(U'k');
    CHECK(bassNote(r, 29));
    CHECK(r.view().pitch() == 29);
    r.type(U"2");
    alt(U'k'); // onto the G1 there
    CHECK(r.ws.message().starts_with("Would overlap the G1 at 1:1"));
    CHECK(bassNote(r, 29));
    alt(U'k', kShift);
    CHECK(bassNote(r, 41));
    CHECK(r.view().pitch() == 41);
    alt(U'j', kShift);
    alt(U'j');
    CHECK(bassNote(r, 28));
    r.type(U"200");
    alt(U'j'); // below the lowest pitch
    CHECK(bassNote(r, 28));
}

TEST_CASE("Alt and Alt-Shift with the arrow keys move notes as with h, j, k and l")
{
    Rolling r;
    const auto& t = r.t;
    const auto arrow = [&](NamedKey key, Modifiers more = kNoModifiers)
    { r.press(Key::of(key, kAlt | more)); };
    r.type(U"8j"); // E1 at the start
    arrow(NamedKey::Right);
    CHECK(bassNote(r, 28)->first == t.sixteenth());
    arrow(NamedKey::Left);
    CHECK(bassNote(r, 28)->first == 0);
    arrow(NamedKey::Right, kShift);
    CHECK(bassNote(r, 28)->first > 0);
    CHECK(bassNote(r, 28)->first < t.sixteenth());
    arrow(NamedKey::Left, kShift);
    CHECK(bassNote(r, 28)->first == 0);
    arrow(NamedKey::Up);
    CHECK(bassNote(r, 29));
    arrow(NamedKey::Up, kShift);
    CHECK(bassNote(r, 41));
    arrow(NamedKey::Down, kShift);
    arrow(NamedKey::Down);
    CHECK(bassNote(r, 28));
}

TEST_CASE("= and - move the end of the piano roll's current note to the grid")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    const auto bar5 = t.bar(5);
    r.type(U"5G");
    r.press(Key::of(NamedKey::Tab)); // C2 at 7 ticks in, a sixteenth long
    r.type(U"=");
    CHECK(bassNote(r, 36)->second == 2 * t.sixteenth() - 7);
    r.type(U"2=");
    CHECK(bassNote(r, 36)->second == 4 * t.sixteenth() - 7);
    r.type(U"5-"); // no shorter than a grid step
    CHECK(bassNote(r, 36)->second == t.sixteenth());
    r.type(U"-");
    CHECK(r.ws.message() == "A note can't be shortened below a grid step");
    CHECK(bassNote(r, 36)->second == t.sixteenth());
    CHECK(bassNote(r, 36)->first == bar5 + 7);

    // Not into the next note of its pitch.
    r.type(U"gg");
    r.type(U"8j");
    r.type(U"o"); // a second E1 after the first, which then can't reach it
    r.type(U"8l");
    r.type(U"o");
    r.type(U"8h");
    r.type(U"8=");
    CHECK(r.ws.message().starts_with("Would run into the E1 at 1:"));
}

TEST_CASE("Alt-H and Alt-L nudge the piano roll's current note, + and _ its end, by the nudge")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    const auto& tempo = r.document.song().tempo();
    r.ws.pianoRoll().setNudge(std::chrono::milliseconds{50}); // a tenth of a quarter at 120 BPM
    const auto nudge = t.quarters(1) / 10;
    const auto alt = [&](char32_t key) { r.press(Key::typed(key, kAlt | kShift)); };
    r.type(U"5G");
    r.press(Key::of(NamedKey::Tab));
    const auto start = t.bar(5) + 7;
    alt(U'l');
    CHECK(bassNote(r, 36)->first ==
          tempo.toTick(tempo.toTime(start) + std::chrono::milliseconds{50}));
    CHECK(bassNote(r, 36)->first - start >= nudge);
    CHECK(r.view().currentNote());
    r.type(U"2");
    alt(U'h');
    const auto back = bassNote(r, 36)->first;
    CHECK(back < start);
    CHECK(start - back <= nudge + 2); // rounding to whole ticks

    const auto length = bassNote(r, 36)->second;
    r.type(U"+");
    CHECK(bassNote(r, 36)->second > length);
    r.type(U"_");
    r.type(U"_");
    CHECK(bassNote(r, 36)->second < length);
    r.type(U"20_"); // no shorter than a nudge
    CHECK(bassNote(r, 36)->second >= nudge);
    CHECK(bassNote(r, 36)->second <= nudge + 1);
    r.type(U"_");
    CHECK(r.ws.message() == "A note can't be shortened below a nudge");

    // A nudge is at least a tick, however short.
    r.ws.pianoRoll().setNudge(std::chrono::milliseconds{0});
    const auto at = bassNote(r, 36)->first;
    alt(U'l');
    CHECK(bassNote(r, 36)->first == at + 1);
}

TEST_CASE("O adds notes one after another, moving on by the note's length or the grid step")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    const auto before = bassNotes(r).size();
    r.registry.runLine("len 1/8");
    r.type(U"kO"); // C#2 at the start, the cursor after it
    CHECK(bassNote(r, 37)->first == 0);
    CHECK(r.view().time() == t.quarters(1) / 2);
    r.type(U"3O");
    CHECK(bassNotes(r).size() == before + 4);
    CHECK(r.view().time() == t.quarters(2));
    r.type(U"u"); // the three as one step
    CHECK(bassNotes(r).size() == before + 1);

    // By the grid step, the notes overlapping a little: another pitch each time.
    r.type(U",ta");
    CHECK(r.ws.message() == "O moves on by the grid step");
    r.type(U"5GkO");
    CHECK(r.view().time() == t.bar(5) + t.sixteenth());
    r.type(U"kO");
    CHECK(bassNote(r, 39, t.bar(5))->first == t.bar(5) + t.sixteenth());
    // At the same pitch, the cursor is still on the last, which is in the way.
    r.type(U"3O");
    CHECK(r.ws.message() == "There's a note here already");
    CHECK(bassNotes(r).size() == before + 3);
    r.type(U",ta");
    CHECK(r.ws.message() == "O moves on by the note's length");

    // o stays put.
    r.type(U"ggjjo");
    CHECK(r.view().time() == 0);
}

TEST_CASE("dab in the piano roll takes a bar out of the track, controllers too, and P puts it in")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    const auto& song = r.document.song();
    const auto id = song.tracks()[0]->id();
    r.document.edit("CC", {},
                    [&](core::Song& s)
                    {
                        s.updateEvents(id, [&](core::EventList& events)
                                       { events.insert({t.bar(4), core::ControlChange{1, 64}}); });
                    });
    const auto ccAt = [&]
    {
        for (const auto& event : song.events(id).all())
        {
            if (std::holds_alternative<core::ControlChange>(event.data))
            {
                return event.tick;
            }
        }
        return core::Tick{-1};
    };

    r.type(U"3G8ldab"); // bar 3, empty: what's after it in the Verse moves up a bar
    CHECK(ccAt() == t.bar(3));
    CHECK(bassNote(r, 36)->first == t.bar(4) + 7);
    CHECK(bassNote(r, 33)->first == t.bar(11));
    CHECK(bassNote(r, 45)->first == t.bar(2)); // before it, in Section 1
    CHECK(r.ws.registerArea().span == t.bar(2));
    r.type(U"u");
    CHECK(ccAt() == t.bar(4));

    // Bar 5 yanked as time, then put in before bar 4 from mid-bar: the rest of the Verse moves
    // on a bar, the A1 past its end, so it asks to grow.
    r.type(U"5Gyab4G8lP");
    CHECK(r.ws.message() == "Lengthen Verse by a bar to make room? (y/n)");
    r.type(U"y");
    CHECK(bassNote(r, 36)->first == t.bar(4) + 7);
    CHECK(bassNote(r, 36, t.bar(4) + 8)->first == t.bar(6) + 7);
    CHECK(ccAt() == t.bar(5));
    CHECK(bassNote(r, 33)->first == t.bar(13));
    r.type(U"u");
    CHECK(bassNote(r, 33)->first == t.bar(12));

    // The C2 yanked, then inserted where it is: it and what follows move on by its length.
    r.type(U"5G");
    r.press(Key::of(NamedKey::Tab));
    r.type(U"YP");
    CHECK(bassNote(r, 36)->first == t.bar(5) + 7);
    CHECK(bassNote(r, 36, t.bar(5) + 8)->first == t.bar(5) + 7 + t.sixteenth());
    CHECK(bassNote(r, 33)->first == t.bar(12) + t.sixteenth());

    r.type(U"gedab");
    CHECK(r.ws.message() == "ab isn't in the event list, which is for events rather than time");
}

TEST_CASE("e edits the current note's fields in the inspector, Tab and Shift-Tab going round")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Rolling r{ppqn};
    const auto& t = r.t;
    const auto& editor = r.view().inspectorEditor();
    const auto tab = [&] { r.press(Key::of(NamedKey::Tab)); };
    r.type(U"e");
    CHECK(r.ws.message() == "No note here to edit");
    CHECK_FALSE(editor.active());

    r.type(U"8je"); // the E1 at the start, on its position first
    REQUIRE(editor.active());
    CHECK(editor.heading() == "Position");
    CHECK(editor.line().text().starts_with(U"1:1:0"));
    r.type(U"2:1");
    tab(); // moved to bar 2, the cursor with it
    CHECK(bassNote(r, 28)->first == t.bar(2));
    CHECK(r.view().time() == t.bar(2));
    CHECK(editor.heading() == "Pitch");
    CHECK(editor.line().text() == U"E1");
    r.type(U"A2"); // onto the A2 there
    tab();
    CHECK(r.ws.message().starts_with("Would overlap the A2 at 2:1"));
    CHECK(editor.heading() == "Pitch"); // to put right
    r.press(Key::of(NamedKey::Backspace));
    r.press(Key::of(NamedKey::Backspace));
    r.type(U"F1");
    tab();
    CHECK(bassNote(r, 29)->first == t.bar(2));
    CHECK(editor.heading() == "Velocity");
    r.type(U"+10");
    r.press(Key::of(NamedKey::Enter));
    CHECK_FALSE(editor.active());
    REQUIRE(r.view().currentNote());
    const auto velocity = [&]
    {
        const auto& song = r.document.song();
        return std::get<core::Note>(
                   song.events(song.tracks()[0]->id())[*r.view().currentNote()].data)
            .velocity;
    };
    CHECK(velocity() == 106);
    r.type(U"u");
    CHECK(r.ws.message() == "Undid: Set velocity of F1 in Bass");
    CHECK(velocity() == 96);

    // Again on the field last edited; Up steps it straight away; Escape leaves the rest.
    r.type(U"e");
    CHECK(editor.heading() == "Velocity");
    r.press(Key::of(NamedKey::Up));
    CHECK(velocity() == 97);
    CHECK(editor.line().text() == U"97");
    r.type(U"50");
    r.press(Key::of(NamedKey::Escape));
    CHECK(velocity() == 97);

    // Its end, which sets its length; round from the first field to the last.
    r.type(U"e");
    tab();
    tab();
    CHECK(editor.heading() == "Ends");
    r.type(U"2:2");
    tab();
    CHECK(bassNote(r, 29)->second == t.quarters(1));
    CHECK(editor.heading() == "Release");
    tab();
    CHECK(editor.heading() == "Position");
    r.press(Key::of(NamedKey::Tab, kShift));
    CHECK(editor.heading() == "Release");
    r.press(Key::of(NamedKey::Escape));
    r.type(U"e"); // where it was left, the release
    CHECK(editor.heading() == "Release");
    r.press(Key::of(NamedKey::Tab, kShift));
    CHECK(editor.heading() == "Ends");
    r.type(U"1:4");
    tab();
    CHECK(r.ws.message() == "A note has to end after it starts");
}

TEST_CASE("after an operator the hint menu lists its ranges and objects, with what each takes")
{
    Rolling r;
    r.ws.setHints(true, 0);
    const auto title = [&](std::string_view keys) -> std::string
    {
        for (const auto& hint : r.ws.hints().value_or(std::vector<Workspace::Hint>{}))
        {
            if (hint.keys == keys)
            {
                return hint.title;
            }
        }
        return "(none)";
    };
    r.type(U"d");
    REQUIRE(r.ws.hints());
    CHECK(r.ws.hints()->front() == Workspace::Hint{"d", "this bar's notes", false});
    CHECK(title("ib") == "this bar's notes");
    CHECK(title("ab") == "this bar, closing the gap");
    CHECK(title("in") == "this note");
    CHECK(title("]b") == "to the next bar line");
    CHECK(title("l/→") == "to the next grid step"); // the arrow too
    CHECK(title("G") == "to the end of the song");
    for (const auto* moves : {"}", "$", "0", "<Tab>", ")", "y", "j"})
    {
        CAPTURE(moves);
        CHECK(title(moves) == "(none)");
    }
    r.press(Key::of(NamedKey::Escape));
    CHECK_FALSE(r.ws.hints());
    r.type(U"y");
    CHECK(title("yy") == "(none)");
    CHECK(title("y") == "this bar's notes");
    CHECK(title("ab") == "this bar as time, controllers too");
    r.press(Key::of(NamedKey::Escape));

    // Each view its own: the event list has no time to take out; the strip takes sections.
    r.type(U"ged");
    CHECK(title("ab") == "(none)");
    CHECK(title("in") == "this event");
    CHECK(title("j/↓") == "this event and the next");
    r.press(Key::of(NamedKey::Escape));
    r.type(U"gagsd");
    CHECK(title("l/→") == "this section");
    CHECK(title("d") == "(none)");
    CHECK(title("ib") == "(none)");
}

TEST_CASE("the hint menu offers only the commands that do something where the cursor is")
{
    // Every binding offered in each place, run there afresh, does something there rather than
    // saying it's for somewhere else; and gs, say, isn't offered in the piano roll.
    const std::array<std::pair<std::u32string_view, Where>, 4> places{{
        {U"", Where::PianoRoll},
        {U"ga", Where::Tracks},
        {U"gags", Where::Strip},
        {U"ge", Where::EventList},
    }};
    const auto keymap = Workspace::defaultKeymap();
    for (const auto& [to, where] : places)
    {
        Rolling place;
        place.type(to);
        REQUIRE(place.ws.where() == where);
        for (const auto& [keys, binding] : keymap.bindings(Mode::Normal))
        {
            if (!place.ws.offered(binding.command))
            {
                continue;
            }
            Rolling r;
            r.type(to);
            for (const auto& key : keys)
            {
                r.press(key.named == NamedKey::Leader ? Key::typed(U',') : key);
            }
            CAPTURE(static_cast<int>(where), binding.command, formatKeys(keys));
            CHECK(r.ws.message().find(" is for ") == std::string::npos);
            CHECK(r.ws.message().find(" does nothing here") == std::string::npos);
        }
    }
    Rolling r;
    CHECK_FALSE(r.ws.offered("go-section-strip"));
    r.type(U"ga");
    CHECK(r.ws.offered("go-section-strip"));
}

TEST_CASE("the hint menu shows after an operator once it has waited, its keys ignoring case")
{
    Rolling r;
    r.ws.setHints(true, 400);
    r.ws.handle(Key::typed(U'd'), 1000);
    CHECK_FALSE(r.ws.hints());
    CHECK(r.ws.wake(1400));
    REQUIRE(r.ws.hints());
    CHECK(r.ws.hints()->front().keys == "d");
    r.ws.handle(Key::of(NamedKey::Escape), 1500);

    // After g, its keys in order, t and T together; gs, for the arrangement, isn't offered.
    r.ws.handle(Key::typed(U'g'), 2000);
    r.ws.wake(2400);
    const auto shown = r.ws.hints();
    REQUIRE(shown);
    std::vector<std::string> keys;
    for (const auto& hint : *shown)
    {
        keys.push_back(hint.keys);
    }
    CHECK(keys == std::vector<std::string>{"a", "e", "g", "r", "t", "T", "v"});
}

TEST_CASE("keys bound to one command show together in the hint menu, arrows as arrows")
{
    Rolling r;
    r.ws.setHints(true, 0);
    r.type(U"z");
    const auto shown = r.ws.hints();
    REQUIRE(shown);
    CHECK(std::ranges::count(*shown, Workspace::Hint{"h/←", "Finer grid", false}) == 1);
    CHECK(std::ranges::count(*shown, Workspace::Hint{"l/→", "Coarser grid", false}) == 1);
}

TEST_CASE("the list of keys, , ?, shows those that do something here, in modalrc's groups")
{
    Rolling r;
    const auto before = bassNotes(r);
    r.type(U",?");
    REQUIRE(r.ws.showingKeys());
    const auto groups = r.ws.keyList();
    REQUIRE(!groups.empty());
    CHECK(groups.front().title == "Moving the cursor");
    const auto has = [&](std::string_view group, const Workspace::Hint& key)
    {
        const auto it = std::ranges::find(groups, group, &Workspace::KeyGroup::title);
        return it != groups.end() && std::ranges::find(it->keys, key) != it->keys.end();
    };
    CHECK(has("Moving the cursor", {"j/↓", "Down", false}));
    CHECK(has("Moving the cursor", {"G", "End, or bar N", false}));
    CHECK(has("Pitch in the piano roll", {"K", "Up an octave", false}));
    CHECK(has("After an operator", {"ib", "This bar's events", false}));
    CHECK(has("The leader", {",?", "The keys here", false}));
    CHECK(has("Moving notes", {"<A-h>/<A-Left>", "Move earlier", false}));
    CHECK(has("Pitch in the piano roll", {"J", "Down an octave", false}));
    // What the step keys do here, each once; and one key for bigger text, the others unlisted.
    CHECK(has("Changing events", {"=", "Lengthen to the grid", false}));
    CHECK(has("Changing events", {"<C-a>", "Velocity up", false}));
    CHECK(has("Changing events", {"<", "Transpose down", false})); // not as <lt>
    CHECK(has("Text size", {"<C-+>", "Bigger text", false}));
    CHECK(
        std::ranges::none_of(groups, [](const auto& g) { return g.title == "The section strip"; }));
    const auto frame = r.ws.frame();
    CHECK(std::ranges::any_of(
        frame, [](const Region& region)
        { return region.background == Role::Panel && region.bounds.height > 10 * kCell.height; }));

    // While it shows, keys turn its pages or close it, and change nothing.
    r.type(U"xdd");
    CHECK(r.ws.showingKeys());
    CHECK(bassNotes(r) == before);
    r.type(U"q");
    CHECK_FALSE(r.ws.showingKeys());

    // Elsewhere, what's there: the strip's keys on the strip, and no pitch keys.
    r.type(U"gags,?");
    const auto strip = r.ws.keyList();
    CHECK(std::ranges::any_of(strip, [](const auto& g) { return g.title == "The section strip"; }));
    CHECK(std::ranges::none_of(strip,
                               [](const auto& g) { return g.title == "Pitch in the piano roll"; }));
    r.press(Key::of(NamedKey::Escape));
    CHECK_FALSE(r.ws.showingKeys());

    // In the event list, the keys that step a field do the same, so show as one.
    r.type(U"ge,?");
    const auto list = r.ws.keyList();
    const auto changing = std::ranges::find(list, "Changing events", &Workspace::KeyGroup::title);
    REQUIRE(changing != list.end());
    CHECK(std::ranges::count(changing->keys, Workspace::Hint{"+/=/<C-a>", "Step up", false}) == 1);
}
