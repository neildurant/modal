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
    s.type(U"80");
    s.press(NamedKey::Enter);
    const auto& song = s.document.song();
    const auto& events = song.events(song.tracks()[0]->id());
    CHECK(std::get<core::Note>(events[0].data).velocity == 80);
    CHECK(std::get<core::Note>(events[1].data).velocity == 80);
    CHECK(std::get<core::Note>(events[2].data).velocity == 100);
    CHECK(s.ws.mode() == Mode::Normal);
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
}
