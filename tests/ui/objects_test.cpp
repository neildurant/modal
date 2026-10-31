// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
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
// Lead, each with a note starting each bar: the bass's pitch 40 and up, the lead's 60 and up.
core::Song sectioned(core::Ppqn ppqn)
{
    const auto bar = ppqn.whole();
    core::Song song{ppqn};
    const auto bass = song.addTrack("Bass");
    const auto lead = song.addTrack("Lead");
    for (const auto& [id, low] : {std::pair{bass, 40}, std::pair{lead, 60}})
    {
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
struct Objects
{
    explicit Objects(core::Ppqn ppqn = core::kPpqn)
        : Objects{sectioned(ppqn)}
    {
    }
    explicit Objects(core::Song song)
        : bar{song.ppqn().whole()}
        , document{std::move(song)}
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

    // A track's notes' pitches, in order, counting tracks from 0.
    std::vector<int> pitches(std::size_t track) const
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

    // The same, lowest first.
    std::vector<int> sorted(std::size_t track) const
    {
        auto found = pitches(track);
        std::ranges::sort(found);
        return found;
    }

    std::vector<std::string> sections() const
    {
        std::vector<std::string> names;
        for (const auto& section : document.song().sections())
        {
            names.push_back(section->name());
        }
        return names;
    }

    // The hint menu's title for keys after an operator, or "(none)".
    std::string hint(std::string_view keys) const
    {
        for (const auto& shown : ws.hints().value_or(std::vector<Workspace::Hint>{}))
        {
            if (shown.keys == keys)
            {
                return shown.title;
            }
        }
        return "(none)";
    }

    core::Tick bar;
    core::Document document;
    CommandRegistry registry;
    Workspace ws{registry, document};
};

using Pitches = std::vector<int>;

// One track, Keys: a chord of C4, E4 and G4 at the start, the E4 played a 128th note late, at
// 120 a minute about 16 ms, with the sustain pedal down there too; a chord of D4 and F4 in bar
// 2; and an A4 alone in bar 3.
core::Song chords(core::Ppqn ppqn)
{
    const auto q = ppqn.quarter();
    const auto bar = ppqn.whole();
    core::Song song{ppqn};
    const auto keys = song.addTrack("Keys");
    song.updateEvents(keys,
                      [&](core::EventList& events)
                      {
                          events.insert({0, core::Note{60, 100, q}});
                          events.insert({0, core::ControlChange{64, 127}});
                          events.insert({q / 32, core::Note{64, 100, q}});
                          events.insert({0, core::Note{67, 100, q}});
                          events.insert({bar, core::Note{62, 100, q}});
                          events.insert({bar, core::Note{65, 100, q}});
                          events.insert({2 * bar, core::Note{69, 100, q}});
                      });
    song.fitToContent();
    return song;
}

} // namespace

TEST_CASE("is takes this section's events on the cursor's track, in every view")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Objects o{ppqn};
    const Pitches bass{40, 41, 42, 43, 44, 45, 46, 47};
    const Pitches lead{60, 61, 62, 63, 64, 65, 66, 67};

    // The arrangement: bar 4, in B.
    o.type(U"3ldis");
    CHECK(o.pitches(0) == Pitches{40, 41, 45, 46, 47});
    CHECK(o.pitches(1) == lead);
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Delete B of Bass");
    o.type(U"d2is"); // B and C
    CHECK(o.pitches(0) == Pitches{40, 41});
    o.type(U"u");
    o.type(U"j>is"); // on the lead
    CHECK(o.pitches(1) == Pitches{60, 61, 63, 64, 65, 65, 66, 67});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Transpose B of Lead up a semitone");

    // The piano roll and the event list, on the bass.
    o.type(U"kgr");
    o.type(U"2<is");
    CHECK(o.pitches(0) == Pitches{40, 41, 40, 41, 42, 45, 46, 47});
    o.type(U"u");
    o.type(U"ge");
    o.type(U"dis");
    CHECK(o.pitches(0) == Pitches{40, 41, 45, 46, 47});
    o.type(U"u");
    CHECK(o.pitches(0) == bass);
    CHECK(o.sections() == std::vector<std::string>{"A", "B", "C"});
}

TEST_CASE("das and yas take the section itself, from any view; to the other operators as is is")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Objects o{ppqn};
    o.type(U"3ldas");
    CHECK(o.sections() == std::vector<std::string>{"A", "C"});
    CHECK(o.pitches(0) == Pitches{40, 41, 45, 46, 47});
    CHECK(o.pitches(1) == Pitches{60, 61, 65, 66, 67});
    CHECK(o.document.song().end() == 5 * o.bar);
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Delete section B");
    CHECK(o.sections() == std::vector<std::string>{"A", "B", "C"});

    // Yanked in the piano roll, and put back on the strip.
    o.type(U"gryas");
    CHECK(o.ws.message() == "B yanked; p on the section strip puts it back");
    o.type(U"gagsp");
    CHECK(o.sections().size() == 4);
    o.type(U"u");

    // To the other operators it's is.
    o.type(U"j");
    o.ws.setHints(true, 0);
    o.type(U"d");
    CHECK(o.hint("is") == "this section's events");
    CHECK(o.hint("as") == "this section, taken out of the song");
    o.ws.handle(Key::of(NamedKey::Escape), 0);
    o.type(U">");
    CHECK(o.hint("is") == "this section's events");
    CHECK(o.hint("as") == "(none)");
    o.type(U"as");
    CHECK(o.pitches(0) == Pitches{40, 41, 43, 44, 45, 45, 46, 47});
    CHECK(o.sections() == std::vector<std::string>{"A", "B", "C"});
}

TEST_CASE("a song keeps at least one section")
{
    Objects o;
    o.type(U"gs");
    o.type(U"d2l"); // A and B, leaving C
    REQUIRE(o.sections() == std::vector<std::string>{"C"});
    o.type(U"jdas");
    CHECK(o.ws.message() == "A song keeps at least one section");
    CHECK(o.sections() == std::vector<std::string>{"C"});
}

TEST_CASE("on the section strip, is takes the section's events on every track, as the section")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Objects o{ppqn};
    o.type(U"gsl"); // B
    o.type(U"dis");
    CHECK(o.pitches(0) == Pitches{40, 41, 45, 46, 47});
    CHECK(o.pitches(1) == Pitches{60, 61, 65, 66, 67});
    CHECK(o.sections() == std::vector<std::string>{"A", "B", "C"});
    CHECK(o.ws.arrangement().page().stripCursor() == 1u); // still on the strip, on B
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Delete B on every track");

    // The other operators take is there too, on every track, and only is or as.
    o.type(U">is");
    CHECK(o.pitches(0) == Pitches{40, 41, 43, 44, 45, 45, 46, 47});
    CHECK(o.pitches(1) == Pitches{60, 61, 63, 64, 65, 65, 66, 67});
    o.type(U"u");
    o.type(U">l");
    CHECK(o.ws.message() == "On the section strip, operators take is, this section's events on "
                            "every track");
    o.type(U"q");
    o.type(U"q");
    CHECK(o.ws.message() == "On the section strip, operators take is, this section's events on "
                            "every track");

    // as is the section, as x takes it.
    o.type(U"das");
    CHECK(o.sections() == std::vector<std::string>{"A", "C"});
}

TEST_CASE("ic takes the chord holding the note under the cursor, as ( and ) find chords")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Objects o{chords(ppqn)};
    // The piano roll, on the late E4.
    o.type(U"gr");
    const auto keys = o.document.song().tracks()[0]->id();
    o.ws.pianoRoll().setCursor({.track = keys, .tick = ppqn.quarter() / 32, .pitch = 64});
    o.type(U">ic");
    CHECK(o.sorted(0) == Pitches{61, 62, 65, 65, 68, 69});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Transpose 3 notes in Keys up a semitone");
    o.type(U">2ic"); // and the next chord
    CHECK(o.sorted(0) == Pitches{61, 63, 65, 66, 68, 69});
    o.type(U"u");
    o.type(U"k"); // F4, where no note is
    o.type(U"dic");
    CHECK(o.ws.message() == "No chord here");

    // The event list, on the E4's row: its notes, not the pedal among them.
    o.type(U"j");
    o.type(U"ge");
    o.type(U"dic");
    CHECK(o.sorted(0) == Pitches{62, 65, 69});
    const auto& events = o.document.song().events(o.document.song().tracks()[0]->id());
    CHECK(std::ranges::count_if(events.all(),
                                [](const core::Event& event) { return !event.isNote(); }) == 1);
    o.type(U"u");

    // Not in the arrangement, where there's no single chord.
    o.type(U"ga");
    o.type(U"dic");
    CHECK(o.ws.message() == "There's no single chord here; dd takes this bar's events");
    CHECK(o.sorted(0) == Pitches{60, 62, 64, 65, 67, 69});
}
