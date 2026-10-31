// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

constexpr CellSize kCell{10, 20};

// Eight bars of 4/4 in sections A (bars 1-2), B (3-5) and C (6-8), and two tracks, Bass and
// Lead, each with a note starting each bar.
core::Song sectioned(core::Ppqn ppqn)
{
    const auto bar = ppqn.whole();
    core::Song song{ppqn};
    for (const auto& [name, low] : {std::pair{"Bass", 40}, std::pair{"Lead", 60}})
    {
        const auto id = song.addTrack(name);
        song.updateEvents(id,
                          [&](core::EventList& events)
                          {
                              for (int i = 0; i < 8; ++i)
                              {
                                  events.insert(
                                      {i * bar, core::Note{static_cast<std::uint8_t>(low + i), 100,
                                                           ppqn.quarter()}});
                              }
                          });
    }
    song.renameSection(0, "A");
    song.splitSection(2 * bar, "B");
    song.splitSection(5 * bar, "C");
    return song;
}

// The song in a workspace of 120 by 30 cells, in the arrangement.
struct Selecting
{
    explicit Selecting(core::Ppqn ppqn = core::kPpqn)
        : bar{ppqn.whole()}
        , document{sectioned(ppqn)}
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
    void press(NamedKey key) { ws.handle(Key::of(key), 0); }

    // The selection as the status line has it, or empty if there's none.
    std::string selected() const { return ws.arrangement().selectionStatus().value_or(""); }
    std::string bars(int first, int last) const
    {
        return barsText(document.song().meter(), (first - 1) * bar, last * bar) + " on every track";
    }

    core::Tick bar;
    core::Document document;
    CommandRegistry registry;
    Workspace ws{registry, document};
};

} // namespace

TEST_CASE("v in the arrangement selects bars on every track, following the cursor")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    s.type(U"lv");
    CHECK(s.ws.mode() == Mode::Visual);
    CHECK(s.selected() == s.bars(2, 2));
    s.type(U"2l");
    CHECK(s.selected() == s.bars(2, 4));
    s.type(U"j"); // onto the lead: still every track
    CHECK(s.selected() == s.bars(2, 4));
    s.type(U"3h"); // back past where it started
    CHECK(s.selected() == s.bars(1, 2));

    // o goes to the other end.
    s.type(U"o");
    CHECK(s.ws.arrangement().page().bar() == 2);
    CHECK(s.selected() == s.bars(1, 2));

    // The status line says so.
    const auto frame = s.ws.frame();
    std::u32string status;
    for (const auto& run : frame.back().text)
    {
        status += run.text;
    }
    CHECK(status.find(U" VISUAL ") != std::u32string::npos);

    // Escape, or v again, ends it; gv selects it again, the cursor where it was.
    s.press(NamedKey::Escape);
    CHECK(s.ws.mode() == Mode::Normal);
    CHECK(s.selected().empty());
    s.type(U"G");
    s.type(U"gv");
    CHECK(s.ws.mode() == Mode::Visual);
    CHECK(s.selected() == s.bars(1, 2));
    CHECK(s.ws.arrangement().page().bar() == 2);
    s.type(U"v");
    CHECK(s.ws.mode() == Mode::Normal);
}

TEST_CASE("an object in Visual mode selects what it takes")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    s.type(U"3lv"); // bar 4, in B
    s.type(U"is");
    CHECK(s.selected() == s.bars(3, 5));
    s.type(U"l"); // and on from its end
    CHECK(s.selected() == s.bars(3, 6));
    s.type(U"ib");
    CHECK(s.selected() == s.bars(6, 6));
    s.type(U"in");
    CHECK(s.ws.message() == "There's no single note or chord here to select");
    CHECK(s.selected() == s.bars(6, 6));
}

TEST_CASE("the selection is drawn across every track, in place of the cursor's bar")
{
    Selecting s;
    const auto widest = [&]
    {
        int width = 0;
        int height = 0;
        for (const auto& region : s.ws.frame())
        {
            for (const auto& shape : region.under)
            {
                if (shape.role == Role::Selection && shape.rect.width > width)
                {
                    width = shape.rect.width;
                    height = shape.rect.height;
                }
            }
        }
        return std::pair{width, height};
    };
    const auto [barWidth, tracksHeight] = widest(); // the cursor's bar
    s.type(U"v2l");
    const auto [width, height] = widest();
    CHECK(width > 2 * barWidth);
    CHECK(height == tracksHeight);
}

TEST_CASE("keys bound to nothing in Visual mode say so, and it goes on")
{
    Selecting s;
    s.type(U"vQ");
    CHECK(s.ws.message() == "Q isn't a key here");
    CHECK(s.ws.mode() == Mode::Visual);
}

namespace
{

// A track's notes' pitches, in order, counting tracks from 0.
std::vector<int> pitches(const core::Document& document, std::size_t track)
{
    std::vector<int> found;
    const auto& song = document.song();
    for (const auto& event : song.events(song.tracks()[track]->id()).all())
    {
        if (const auto* note = std::get_if<core::Note>(&event.data))
        {
            found.push_back(note->pitch);
        }
    }
    return found;
}

std::vector<std::string> sections(const core::Document& document)
{
    std::vector<std::string> names;
    for (const auto& section : document.song().sections())
    {
        names.push_back(section->name());
    }
    return names;
}

using Pitches = std::vector<int>;

} // namespace

TEST_CASE("d, y and the other operators act on the selection at once, ending Visual mode")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    s.type(U"lvld"); // bars 2-3
    CHECK(s.ws.mode() == Mode::Normal);
    CHECK(pitches(s.document, 0) == Pitches{40, 43, 44, 45, 46, 47});
    CHECK(pitches(s.document, 1) == Pitches{60, 63, 64, 65, 66, 67});
    CHECK(s.document.song().end() == 8 * s.bar); // contents only
    s.type(U"u");
    CHECK(s.ws.message() == "Undid: Delete " + s.bars(2, 3));
    s.type(U"gglvlx"); // x as d
    CHECK(pitches(s.document, 0) == Pitches{40, 43, 44, 45, 46, 47});
    s.type(U"u");

    // The count before an operator is how far, as in Normal mode.
    s.type(U"ggv2l2>");
    CHECK(pitches(s.document, 0) == Pitches{42, 43, 44, 43, 44, 45, 46, 47});
    CHECK(pitches(s.document, 1) == Pitches{62, 63, 64, 63, 64, 65, 66, 67});
    CHECK(s.ws.mode() == Mode::Normal);
    s.type(U"u");

    // y yanks it, as bars on every track.
    s.type(U"ggvly");
    CHECK(s.ws.registerArea().tracks.size() == 2);
    CHECK(s.ws.mode() == Mode::Normal);
}

TEST_CASE("D takes a selection on every track out of the song, as :bar-delete does")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    s.type(U"lvlD"); // bars 2-3, across A's end and B's start
    CHECK(pitches(s.document, 0) == Pitches{40, 43, 44, 45, 46, 47});
    CHECK(s.document.song().end() == 6 * s.bar);
    CHECK(sections(s.document) == std::vector<std::string>{"A", "B", "C"});
    s.type(U"u");
    CHECK(s.document.song().end() == 8 * s.bar);

    // A whole section goes.
    s.type(U"gg3lvisD"); // bar 4, in B
    CHECK(sections(s.document) == std::vector<std::string>{"A", "C"});
    CHECK(pitches(s.document, 1) == Pitches{60, 61, 65, 66, 67});
}

TEST_CASE("p puts the register in the selection's place, as one undo step")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    s.type(U"yy");    // bar 1 of the bass
    s.type(U"3lvlp"); // over bars 4-5
    CHECK(pitches(s.document, 0) == Pitches{40, 41, 42, 40, 45, 46, 47});
    CHECK(pitches(s.document, 1) == Pitches{60, 61, 62, 65, 66, 67});
    CHECK(s.ws.mode() == Mode::Normal);
    CHECK(s.ws.registerArea().tracks.front().size() == 1); // it keeps what it had
    s.type(U"u");
    CHECK(s.ws.message() == "Undid: Put over " + s.bars(4, 5));
    CHECK(pitches(s.document, 0) == Pitches{40, 41, 42, 43, 44, 45, 46, 47});
}

TEST_CASE("v in the piano roll selects time at every pitch, on the cursor's track")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    s.type(U"grv}"); // bar 1's start to bar 2's first grid step
    REQUIRE(s.ws.mode() == Mode::Visual);
    CHECK(s.ws.pianoRoll().selectionStatus().value_or("").ends_with(" in Bass"));
    s.type(U"d");
    CHECK(pitches(s.document, 0) == Pitches{42, 43, 44, 45, 46, 47});
    CHECK(pitches(s.document, 1).size() == 8); // only the cursor's track
    s.type(U"u");

    // The other operators.
    s.type(U"ggv}>");
    CHECK(pitches(s.document, 0) == Pitches{41, 42, 42, 43, 44, 45, 46, 47});
    s.type(U"u");

    // An object selects exactly what it takes: vin the note, vis the section.
    s.ws.pianoRoll().setCursor(
        {.track = s.document.song().tracks()[0]->id(), .tick = 0, .pitch = 40});
    s.type(U"vin");
    CHECK(s.ws.pianoRoll().selectionSpan() == std::pair{core::Tick{0}, ppqn.quarter()});
    s.press(NamedKey::Escape);

    // D takes the time out of this track, closing the gap as far as the section's end.
    s.type(U"3}vis");
    CHECK(s.ws.pianoRoll().selectionSpan() == std::pair{2 * s.bar, 5 * s.bar});
    s.type(U"D");
    CHECK(pitches(s.document, 0) == Pitches{40, 41, 45, 46, 47});
    CHECK(pitches(s.document, 1).size() == 8);
    CHECK(s.document.song().end() == 8 * s.bar);
}

TEST_CASE("v in the event list selects rows of events")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    const auto bass = s.document.song().tracks()[0]->id();
    const auto at = [&](core::Tick tick)
    { s.ws.eventList().setCursor({.track = bass, .tick = tick, .pitch = std::nullopt}); };
    s.type(U"ge");
    const auto& list = s.ws.eventList();
    while (list.list().rows()[list.row()].kind != RowKind::Event) // past the section's rows
    {
        s.type(U"j");
    }
    s.type(U"vj");
    CHECK(s.ws.eventList().selectionStatus() == "2 events in Bass");
    s.type(U"d");
    CHECK(pitches(s.document, 0) == Pitches{42, 43, 44, 45, 46, 47});
    s.type(U"u");

    // An object selects its rows; D, taking time out, isn't for the event list.
    at(3 * s.bar); // the note in bar 4, in B
    s.type(U"vis");
    CHECK(s.ws.eventList().selectionStatus() == "3 events in Bass");
    s.type(U"D");
    CHECK(s.ws.message().starts_with("D takes time out"));
    CHECK(pitches(s.document, 0).size() == 8);
    at(3 * s.bar);
    s.type(U"vis>");
    CHECK(pitches(s.document, 0) == Pitches{40, 41, 43, 44, 45, 45, 46, 47});
}

TEST_CASE("v on the section strip selects sections, and the operators take them")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    s.type(U"gsvl");
    CHECK(s.ws.arrangement().selectionStatus() == "2 sections from A");
    s.type(U"d");
    CHECK(sections(s.document) == std::vector<std::string>{"C"});
    s.type(U"u");

    // The other operators change their events on every track.
    s.type(U"ggvl>");
    CHECK(pitches(s.document, 0) == Pitches{41, 42, 43, 44, 45, 45, 46, 47});
    CHECK(pitches(s.document, 1) == Pitches{61, 62, 63, 64, 65, 65, 66, 67});
    s.type(U"u");

    // p puts the register's sections in their place.
    s.type(U"$Y"); // C
    s.type(U"ggvlp");
    CHECK(sections(s.document).size() == 2);
    CHECK(s.document.song().end() == 6 * s.bar);
    CHECK(pitches(s.document, 0) == Pitches{45, 46, 47, 45, 46, 47});
    s.type(U"u");
    CHECK(sections(s.document) == std::vector<std::string>{"A", "B", "C"});
}

TEST_CASE("^ on a selection asks how to change the velocity, then changes it")
{
    Selecting s;
    s.type(U"grv}^");
    REQUIRE(s.ws.mode() == Mode::CommandLine);
    CHECK(s.ws.pianoRoll().selectionSpan()); // still shown while it asks
    s.type(U"80");
    s.press(NamedKey::Enter);
    const auto& song = s.document.song();
    const auto& events = song.events(song.tracks()[0]->id());
    CHECK(std::get<core::Note>(events[0].data).velocity == 80);
    CHECK(std::get<core::Note>(events[1].data).velocity == 80);
    CHECK(std::get<core::Note>(events[2].data).velocity == 100);
    CHECK(s.ws.mode() == Mode::Normal);
    CHECK_FALSE(s.ws.pianoRoll().selectionSpan());

    // Escape at the prompt ends it too, changing nothing.
    s.type(U"ggv}^");
    CHECK(s.ws.pianoRoll().selectionSpan());
    s.press(NamedKey::Escape);
    CHECK_FALSE(s.ws.pianoRoll().selectionSpan());
    CHECK(s.ws.mode() == Mode::Normal);
    CHECK(std::get<core::Note>(events[0].data).velocity == 80);
}

TEST_CASE("in the piano roll the notes a selection takes, and the current one, stand out")
{
    Selecting s;
    const auto standing = [&]
    {
        int count = 0;
        for (const auto& region : s.ws.frame())
        {
            for (const auto& shape : region.under)
            {
                count += shape.role == Role::NoteSelected ? 1 : 0;
            }
        }
        return count;
    };
    s.type(U"gr");
    s.ws.pianoRoll().setCursor(
        {.track = s.document.song().tracks()[0]->id(), .tick = 0, .pitch = 40});
    const auto one = standing(); // the current note: its start, body and lane bar
    CHECK(one > 0);
    s.type(U"v}");
    CHECK(standing() == 2 * one); // and the note in bar 2
    s.press(NamedKey::Escape);

    // Selecting leftwards, the cursor on a note starting before the selection: only the
    // notes it takes stand out, not that one.
    s.ws.pianoRoll().setCursor(
        {.track = s.document.song().tracks()[0]->id(), .tick = s.bar + s.bar / 4, .pitch = 41});
    s.type(U"v");
    s.ws.pianoRoll().setCursor(
        {.track = s.document.song().tracks()[0]->id(), .tick = s.bar / 16, .pitch = 40});
    REQUIRE(s.ws.pianoRoll().currentNote()); // on bar 1's note, which starts before it
    CHECK(standing() == one);                // only bar 2's note
}

TEST_CASE("a selection stays shown while an operator asks about it, as q does about pedals")
{
    Selecting s;
    const auto bass = s.document.song().tracks()[0]->id();
    s.document.edit("Pedal", {},
                    [&](core::Song& song)
                    {
                        song.updateEvents(
                            bass, [&](core::EventList& events)
                            { events.insert({s.bar / 8, core::ControlChange{64, 127}}); });
                    });
    s.type(U"grv}q");
    CHECK(s.ws.message().starts_with("Move the pedalling with its notes?"));
    CHECK(s.ws.pianoRoll().selectionSpan());
    s.type(U"n");
    CHECK_FALSE(s.ws.pianoRoll().selectionSpan());
    CHECK(s.ws.mode() == Mode::Normal);
}

TEST_CASE("V in the arrangement selects whole tracks, an object setting their time")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    const Pitches lead{60, 61, 62, 63, 64, 65, 66, 67};
    s.type(U"3lV"); // bar 4 of the bass
    CHECK(s.selected() == barsText(s.document.song().meter(), 0, 8 * s.bar) + " of Bass");
    s.type(U"l"); // moving in time changes nothing
    CHECK(s.selected() == barsText(s.document.song().meter(), 0, 8 * s.bar) + " of Bass");
    s.type(U"d");
    CHECK(pitches(s.document, 0).empty());
    CHECK(pitches(s.document, 1) == lead);
    s.type(U"u");

    // j takes the next track too; an object sets the time.
    s.type(U"gg3lVjis");
    CHECK(s.selected() ==
          barsText(s.document.song().meter(), 2 * s.bar, 5 * s.bar) + " of Bass and Lead");
    s.type(U">");
    CHECK(pitches(s.document, 0) == Pitches{40, 41, 43, 44, 45, 45, 46, 47});
    CHECK(pitches(s.document, 1) == Pitches{60, 61, 63, 64, 65, 65, 66, 67});
    s.type(U"u");

    // v and V change one to the other.
    s.type(U"ggVv");
    CHECK(s.selected() == s.bars(1, 1));
    s.press(NamedKey::Escape);
}

TEST_CASE("D on whole rows closes up within each section the selection crosses")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    s.type(U"lV3ib"); // bars 2-4 of the bass, across A's end and B's start
    s.type(U"D");
    // Out of A, bar 2, A closing up: silence at its end. Out of B, bars 3-4, bar 5's note
    // moving up to bar 3: silence at B's end. C as it was, and the song as long.
    CHECK(pitches(s.document, 0) == Pitches{40, 44, 45, 46, 47});
    const auto& song = s.document.song();
    const auto& events = song.events(song.tracks()[0]->id());
    CHECK(events[1].tick == 2 * s.bar);
    CHECK(pitches(s.document, 1).size() == 8);
    CHECK(song.end() == 8 * s.bar);
    CHECK(s.ws.registerArea().tracks.front().size() == 3);
    s.type(U"u");
    CHECK(pitches(s.document, 0).size() == 8);
}

TEST_CASE("V in the piano roll selects pitch rows across the page it started on")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    const auto bass = s.document.song().tracks()[0]->id();
    s.type(U"gr");
    s.ws.pianoRoll().setCursor({.track = bass, .tick = 0, .pitch = 40});
    const auto page = s.ws.pianoRoll().page();
    s.type(U"V");
    CHECK(s.ws.pianoRoll().selectionStatus().value_or("").starts_with("E2, "));
    CHECK(s.ws.pianoRoll().selectionSpan() == std::pair{page.start, page.end});
    s.type(U"k"); // and F2
    CHECK(s.ws.pianoRoll().selectionStatus().value_or("").starts_with("E2 to F2, "));
    // The time stays the page's as the cursor moves on, the view turning.
    s.type(U"4}");
    CHECK(s.ws.pianoRoll().selectionSpan() == std::pair{page.start, page.end});
    s.type(U"d");
    CHECK(pitches(s.document, 0) == Pitches{42, 43, 44, 45, 46, 47});
    s.type(U"u");

    // An object sets the time: these rows across the whole track.
    s.ws.pianoRoll().setCursor({.track = bass, .tick = 0, .pitch = 40});
    s.type(U"Vkkit>");
    CHECK(pitches(s.document, 0) == Pitches{41, 42, 43, 43, 44, 45, 46, 47});
    s.type(U"u");

    // D takes time out of whole tracks, not some pitches.
    s.ws.pianoRoll().setCursor({.track = bass, .tick = 0, .pitch = 40});
    s.type(U"VD");
    CHECK(s.ws.message().starts_with("D takes time out of the whole track"));
    CHECK(pitches(s.document, 0).size() == 8);
}

TEST_CASE("V in the event list and on the strip selects as v does there")
{
    Selecting s;
    s.type(U"ge");
    const auto& list = s.ws.eventList();
    while (list.list().rows()[list.row()].kind != RowKind::Event)
    {
        s.type(U"j");
    }
    s.type(U"Vj");
    CHECK(s.ws.eventList().selectionStatus() == "2 events in Bass");
    s.type(U"d");
    CHECK(pitches(s.document, 0) == Pitches{42, 43, 44, 45, 46, 47});
    s.type(U"u");

    s.type(U"gagsVl");
    CHECK(s.ws.arrangement().selectionStatus() == "2 sections from A");
    s.type(U"d");
    CHECK(sections(s.document) == std::vector<std::string>{"C"});
}

TEST_CASE("ie takes the whole song: every track in the arrangement, the track elsewhere")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    s.type(U"3ldie");
    CHECK(pitches(s.document, 0).empty());
    CHECK(pitches(s.document, 1).empty());
    s.type(U"u");
    CHECK(s.ws.message() == "Undid: Delete the whole song");

    // vie selects it all.
    s.type(U"vie");
    CHECK(s.selected() == s.bars(1, 8));
    s.press(NamedKey::Escape);

    // In the piano roll, the track's notes.
    s.type(U"gr>ie");
    CHECK(pitches(s.document, 0) == Pitches{41, 42, 43, 44, 45, 46, 47, 48});
    CHECK(pitches(s.document, 1).front() == 60);
}

namespace
{

std::u32string statusLine(const Selecting& s)
{
    std::u32string text;
    const auto frame = s.ws.frame();
    for (const auto& run : frame.back().text)
    {
        text += run.text;
    }
    return text;
}

} // namespace

TEST_CASE("Ctrl-v in the arrangement selects a box of tracks by bars")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    const auto box = [&] { s.ws.handle(Key::typed(U'v', kCtrl), 0); };
    s.type(U"l");
    box();
    s.type(U"l"); // bars 2-3 of the bass, across B's start
    CHECK(s.selected() == barsText(s.document.song().meter(), s.bar, 3 * s.bar) + " of Bass");
    CHECK(statusLine(s).find(U"D closes up within each section") != std::u32string::npos);
    s.type(U"d");
    CHECK(pitches(s.document, 0) == Pitches{40, 43, 44, 45, 46, 47});
    CHECK(pitches(s.document, 1).size() == 8);
    s.type(U"u");

    // Within a section, no warning; j takes the next track.
    s.type(U"gg");
    box();
    s.type(U"lj");
    CHECK(s.selected() == barsText(s.document.song().meter(), 0, 2 * s.bar) + " of Bass and Lead");
    CHECK(statusLine(s).find(U"D closes up") == std::u32string::npos);
    s.press(NamedKey::Escape);

    // An object sets its time, keeping its tracks.
    s.type(U"gg3lk"); // bar 4 of the bass
    box();
    s.type(U"jis");
    CHECK(s.selected() ==
          barsText(s.document.song().meter(), 2 * s.bar, 5 * s.bar) + " of Bass and Lead");
    s.type(U"D");
    CHECK(pitches(s.document, 0) == Pitches{40, 41, 45, 46, 47});
    CHECK(pitches(s.document, 1) == Pitches{60, 61, 65, 66, 67});
    CHECK(s.document.song().end() == 8 * s.bar);
}

TEST_CASE("Ctrl-v in the piano roll selects a box of pitches by grid steps")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    const auto bass = s.document.song().tracks()[0]->id();
    s.type(U"gr");
    s.ws.pianoRoll().setCursor({.track = bass, .tick = 0, .pitch = 41});
    s.ws.handle(Key::typed(U'v', kCtrl), 0);
    s.type(U"j}"); // E2 to F2, bar 1 to bar 2's first step
    CHECK(s.ws.pianoRoll().selectionStatus().value_or("").starts_with("E2 to F2, "));
    s.type(U">");
    CHECK(pitches(s.document, 0) == Pitches{41, 42, 42, 43, 44, 45, 46, 47});
    s.type(U"u");

    // Not in the event list.
    s.type(U"ge");
    s.ws.handle(Key::typed(U'v', kCtrl), 0);
    CHECK(s.ws.mode() == Mode::Normal);
    CHECK(s.ws.message().find("is for") != std::string::npos);
}

namespace
{

// A three-bar C3 on the bass from the start, a D3 a quarter long in bar 2, and nothing else.
core::Song sustained(core::Ppqn ppqn)
{
    const auto bar = ppqn.whole();
    core::Song song{ppqn};
    const auto bass = song.addTrack("Bass");
    song.addTrack("Lead");
    song.updateEvents(bass,
                      [&](core::EventList& events)
                      {
                          events.insert({0, core::Note{48, 100, 3 * bar, 90}});
                          events.insert({bar, core::Note{50, 100, ppqn.quarter()}});
                      });
    song.fitToContent();
    return song;
}

// The bass's notes as start, pitch and length.
std::vector<std::tuple<core::Tick, int, core::Tick>> notesOf(const core::Document& document)
{
    std::vector<std::tuple<core::Tick, int, core::Tick>> found;
    const auto& song = document.song();
    for (const auto& event : song.events(song.tracks()[0]->id()).all())
    {
        if (const auto* note = std::get_if<core::Note>(&event.data))
        {
            found.emplace_back(event.tick, note->pitch, note->duration);
        }
    }
    return found;
}

using Notes = std::vector<std::tuple<core::Tick, int, core::Tick>>;

} // namespace

TEST_CASE("| cuts the notes crossing a selection's edges, so operators take what's inside")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    const auto bar = ppqn.whole();
    core::Document document{sustained(ppqn)};
    CommandRegistry registry;
    registry.add(windowCommands());
    Workspace ws{registry, document};
    ws.resize(120 * kCell.width, 30 * kCell.height, kCell);
    ws.show(ViewKind::Arrangement);
    const auto type = [&](std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            ws.handle(Key::typed(key), 0);
        }
    };
    const Notes before{{0, 48, 3 * bar}, {bar, 50, ppqn.quarter()}};

    // Without cutting, bar 2 takes the D3 alone, the C3 starting before it.
    type(U"lvd");
    CHECK(notesOf(document) == Notes{{0, 48, 3 * bar}});
    type(U"u");

    // Cutting, the C3's part in bar 2 goes too, as one step.
    type(U"v|");
    CHECK(ws.mode() == Mode::Visual);
    type(U"d");
    CHECK(notesOf(document) == Notes{{0, 48, bar}, {2 * bar, 48, bar}});
    const auto& events = document.song().events(document.song().tracks()[0]->id());
    CHECK(std::get<core::Note>(events[0].data).releaseVelocity == core::Note{}.releaseVelocity);
    CHECK(std::get<core::Note>(events[1].data).releaseVelocity == 90);
    type(U"u");
    CHECK(ws.message() == "Undid: Delete bar 2 on every track");
    CHECK(notesOf(document) == before);

    // The other operators change only the part inside.
    type(U"v|>");
    CHECK(notesOf(document) ==
          Notes{{0, 48, bar}, {bar, 49, bar}, {bar, 51, ppqn.quarter()}, {2 * bar, 48, bar}});
    type(U"u");
    CHECK(notesOf(document) == before);

    // y takes the part inside, leaving the song as it is.
    type(U"v|y");
    CHECK(notesOf(document) == before);
    const auto& taken = ws.registerArea().tracks.front();
    REQUIRE(taken.size() == 2);
    CHECK(taken[0].tick == 0);
    CHECK(std::get<core::Note>(taken[0].data).duration == bar);

    // Before an operator, for it alone: |dd is bar 2's, cut.
    type(U"|");
    CHECK(ws.pending() == "|");
    type(U"dd");
    CHECK(notesOf(document) == Notes{{0, 48, bar}, {2 * bar, 48, bar}});
    type(U"u");
    type(U"dd"); // and then not
    CHECK(notesOf(document) == Notes{{0, 48, 3 * bar}});
    type(U"u");

    // ^ cuts in its own change: cancelled, nothing's cut.
    type(U"v|^");
    REQUIRE(ws.mode() == Mode::CommandLine);
    ws.handle(Key::of(NamedKey::Escape), 0);
    CHECK(notesOf(document) == before);

    // Not in the event list, whose ranges are rows.
    type(U"ge|");
    CHECK(ws.message() == "Cut at edges is for the arrangement's tracks and the piano roll");
    CHECK(ws.pending().empty());
}

TEST_CASE("cutting in the piano roll, the parts of notes outside the selection are dimmed")
{
    const auto ppqn = core::kPpqn;
    core::Document document{sustained(ppqn)};
    CommandRegistry registry;
    registry.add(windowCommands());
    Workspace ws{registry, document};
    ws.resize(120 * kCell.width, 30 * kCell.height, kCell);
    ws.show(ViewKind::PianoRoll);
    const auto dimmed = [&]
    {
        int count = 0;
        for (const auto& region : ws.frame())
        {
            for (const auto& shape : region.under)
            {
                count += shape.role == Role::Background && shape.alpha == 150 ? 1 : 0;
            }
        }
        return count;
    };
    ws.pianoRoll().setCursor(
        {.track = document.song().tracks()[0]->id(), .tick = ppqn.whole(), .pitch = 48});
    // Bar 2's first step: the C3 sounds across both its edges, and the D3 starts in it and
    // runs on past its end, taken whole, its part past the end dimmed.
    ws.handle(Key::typed(U'v'), 0);
    CHECK(dimmed() == 1);
    ws.handle(Key::typed(U'|'), 0);
    CHECK(dimmed() == 3); // and the C3's parts before and after
}

TEST_CASE("Enter on a selection of bars opens exactly those in the piano roll")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Selecting s{ppqn};
    s.type(U"lvl"); // bars 2-3
    s.press(NamedKey::Enter);
    REQUIRE(s.ws.mode() == Mode::Normal);
    const auto& roll = s.ws.pianoRoll();
    CHECK(roll.page().start == s.bar);
    CHECK(roll.page().end == 3 * s.bar);
    CHECK(roll.time() == s.bar);
    // Turning moves on by as many bars.
    s.type(U"2}");
    CHECK(roll.page().start == 3 * s.bar);
    CHECK(roll.page().end == 5 * s.bar);
    // Until zooming, when pages fall back into line.
    s.type(U"zo"); // the default 4 bars' next, 8
    CHECK(roll.pageBars() == 8);

    // Whole tracks open as Enter does.
    s.type(U"ga");
    s.type(U"ggV");
    s.press(NamedKey::Enter);
    CHECK(s.ws.mode() == Mode::Normal);
    CHECK(roll.page().start == 0);
    CHECK(roll.pageBars() == 8); // the track's own page size, as it was left
}
