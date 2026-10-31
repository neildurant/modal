// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <map>
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

// A song of 8 bars in 4/4 and two tracks: a bass with a chord at the start (G1 and E1), an A2
// in bar 2, a C2 a little after bar 5's start with the sustain pedal down just after it, and
// an A1 in bar 8; and a lead with no notes.
core::Song bassAndLead(core::Ppqn ppqn)
{
    const auto q = ppqn.quarter();
    core::Song song{ppqn};
    const auto bass = song.addTrack("Bass");
    song.addTrack("Lead");
    song.updateEvents(bass,
                      [&](core::EventList& events)
                      {
                          events.insert({0, core::Note{31, 80, q / 2}});
                          events.insert({0, core::Note{28, 96, q / 2}});
                          events.insert({4 * q, core::Note{45, 50, q}});
                          events.insert({16 * q + q / 16, core::Note{36, 100, q / 4}});
                          events.insert({16 * q + q / 8, core::ControlChange{64, 127}});
                          events.insert({28 * q, core::Note{33, 70, 4 * q}});
                      });
    song.fitToContent();
    return song;
}

// The song in a workspace of 120 by 30 cells, in the arrangement.
struct Operating
{
    explicit Operating(core::Ppqn ppqn = core::kPpqn)
        : document{bassAndLead(ppqn)}
        , q{ppqn.quarter()}
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

    // The bass's notes, as start and pitch, and its pedals' times.
    std::vector<std::pair<core::Tick, int>> notes() const
    {
        std::vector<std::pair<core::Tick, int>> found;
        const auto& song = document.song();
        for (const auto& event : song.events(song.tracks()[0]->id()).all())
        {
            if (const auto* note = std::get_if<core::Note>(&event.data))
            {
                found.emplace_back(event.tick, note->pitch);
            }
        }
        return found;
    }
    std::vector<int> velocities() const
    {
        std::vector<int> found;
        const auto& song = document.song();
        for (const auto& event : song.events(song.tracks()[0]->id()).all())
        {
            if (const auto* note = std::get_if<core::Note>(&event.data))
            {
                found.push_back(note->velocity);
            }
        }
        return found;
    }
    std::vector<core::Tick> pedals() const
    {
        std::vector<core::Tick> found;
        const auto& song = document.song();
        for (const auto& event : song.events(song.tracks()[0]->id()).all())
        {
            if (std::holds_alternative<core::ControlChange>(event.data))
            {
                found.push_back(event.tick);
            }
        }
        return found;
    }

    core::Document document;
    core::Tick q;
    CommandRegistry registry;
    Workspace ws{registry, document};
};

using Notes = std::vector<std::pair<core::Tick, int>>;

} // namespace

TEST_CASE("> and < transpose the notes a range or object takes, the count before saying how far")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.type(U"gr"); // the piano roll, at the start
    o.type(U">>");
    CHECK(o.notes() == Notes{{0, 32}, {0, 29}, {4 * q, 45}, {16 * q + q / 16, 36}, {28 * q, 33}});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Transpose 2 notes in Bass up a semitone");
    o.type(U"3<<");
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 28});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Transpose 2 notes in Bass down 3 semitones");

    // The count after the operator is the range's: two bars.
    o.type(U">2ib");
    CHECK(o.notes() == Notes{{0, 32}, {0, 29}, {4 * q, 46}, {16 * q + q / 16, 36}, {28 * q, 33}});
    o.type(U"u");

    // Past the highest note: refused, naming the note.
    o.type(U"100>>");
    CHECK(o.ws.message().starts_with("The G1 at 1:1:0"));
    CHECK(o.ws.message().ends_with(" would go above G9, the highest note"));
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 31});

    // The operator and its count wait at the status line.
    o.type(U"3<");
    CHECK(o.ws.pending() == "3<");
    o.press(NamedKey::Escape);
}

TEST_CASE("t and T move events later and earlier by the grid step")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.type(U"gr");
    o.type(U"tt"); // this bar's notes, a sixteenth later
    CHECK(o.notes().front() == std::pair{q / 4, 31});
    o.type(U"2TT");
    CHECK(o.ws.message().starts_with("The G1 at 1:1:"));
    CHECK(o.ws.message().ends_with(" would go before the song's start"));
    o.type(U"TT");
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 31});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Move 2 notes in Bass earlier by a grid step");

    // In the arrangement, controllers move with the notes; in the piano roll only notes move.
    o.type(U"ga4l"); // bar 5
    o.type(U"tt");
    CHECK(o.notes()[3].first == 16 * q + q / 16 + q / 4);
    CHECK(o.pedals() == std::vector{16 * q + q / 8 + q / 4});
    o.type(U"u");
    o.press(NamedKey::Enter); // bar 5 in the piano roll
    o.type(U"tt");
    CHECK(o.notes()[3].first == 16 * q + q / 16 + q / 4);
    CHECK(o.pedals() == std::vector{16 * q + q / 8});
}

TEST_CASE("q quantises notes' starts to the grid, and the event list and arrangement take rows "
          "and areas")
{
    Operating o;
    const auto q = o.q;
    o.type(U"4l"); // bar 5, in the arrangement, with the sustain pedal down after the C2
    o.type(U"qq");
    CHECK(o.ws.message() == "Move the pedalling with its notes? (y/n)");
    o.type(U"n"); // only the notes
    CHECK(o.notes()[3] == std::pair{16 * q, 36});
    CHECK(o.pedals() == std::vector{16 * q + q / 8});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Quantise bar 5 of Bass");
    o.type(U"qqy"); // the pedal too, as far from the note as it was
    CHECK(o.notes()[3] == std::pair{16 * q, 36});
    CHECK(o.pedals() == std::vector{16 * q + q / 16});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Quantise bar 5 of Bass, with its pedalling");
    o.type(U"qq");
    o.press(NamedKey::Escape);
    CHECK(o.ws.message() == "Nothing done");
    CHECK(o.notes()[3] == std::pair{16 * q + q / 16, 36});
    o.type(U"qqn");
    o.type(U"qqn");
    CHECK(o.ws.message() == "Nothing changed in bar 5 of Bass");
    o.type(U"u");

    // The event list's rows: this one and the next, from the first of the track's own (after
    // the conductor's).
    o.type(U"gegg");
    const auto& list = o.ws.eventList();
    while (list.list().rows()[list.row()].kind != RowKind::Event)
    {
        o.type(U"j");
    }
    o.type(U">j");
    CHECK(o.notes().front() == std::pair{core::Tick{0}, 32});
    CHECK(o.notes()[1] == std::pair{core::Tick{0}, 29});
    CHECK(o.notes()[2] == std::pair{4 * q, 45});
    o.type(U"u");

    // Not on the conductor's list, nor on the section strip, nor over a single note in the
    // arrangement.
    o.type(U"gT>>");
    CHECK(o.ws.message() == "The conductor's tempo and meter changes aren't changed this way");
    o.type(U"gags>>");
    CHECK(o.ws.message() ==
          "On the section strip, operators take is, this section's events on every track");
    o.type(U"j>in");
    CHECK(o.ws.message() == "There's no single note here; ib takes this bar's events");
}

TEST_CASE("after > the hint menu lists its ranges, but not ab, which is ib to it")
{
    Operating o;
    o.ws.setHints(true, 0);
    const auto title = [&](std::string_view keys) -> std::string
    {
        for (const auto& hint : o.ws.hints().value_or(std::vector<Workspace::Hint>{}))
        {
            if (hint.keys == keys)
            {
                return hint.title;
            }
        }
        return "(none)";
    };
    o.type(U"gr>");
    REQUIRE(o.ws.hints());
    CHECK(o.ws.hints()->front() == Workspace::Hint{">", "this bar's notes", false});
    CHECK(title("ib") == "this bar's notes");
    CHECK(title("ab") == "(none)");
    CHECK(title("]b") == "to the next bar line");
    o.press(NamedKey::Escape);
    o.type(U"<"); // shown as typed, not as <lt>
    CHECK(o.ws.hints()->front() == Workspace::Hint{"<", "this bar's notes", false});
    o.press(NamedKey::Escape);
    o.type(U"tab"); // still taken, as ib
    CHECK(o.notes().front().first == o.q / 4);
}

TEST_CASE("^ asks how to change the velocity: a value, a change or a percentage")
{
    Operating o;
    const auto prompt = [&]
    {
        // The bottom line's text.
        const auto frame = o.ws.frame();
        const auto* bottom = &frame.front();
        for (const auto& region : frame)
        {
            if (region.bounds.y > bottom->bounds.y)
            {
                bottom = &region;
            }
        }
        std::u32string text;
        for (const auto& run : bottom->text)
        {
            text += run.text;
        }
        return text;
    };
    o.type(U"gr^^"); // bar 1's chord, G1 at 80 and E1 at 96
    REQUIRE(o.ws.mode() == Mode::CommandLine);
    CHECK(prompt().starts_with(U"Velocity of 2 notes in Bass (80, +10, 80%): "));
    o.type(U"+10");
    o.press(NamedKey::Enter);
    CHECK(o.velocities() == std::vector{90, 106, 50, 100, 70});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Change the velocity of 2 notes in Bass by +10");
    o.type(U"^^50%");
    o.press(NamedKey::Enter);
    CHECK(o.velocities() == std::vector{40, 48, 50, 100, 70});
    o.type(U"ga^ib120"); // in the arrangement, the bar's events
    o.press(NamedKey::Enter);
    CHECK(o.velocities() == std::vector{120, 120, 50, 100, 70});
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Set the velocity of bar 1 of Bass to 120");
    o.type(U"u");

    // Not a velocity: said so, and nothing changed; Escape leaves it; Tab doesn't complete.
    o.type(U"^^200");
    o.press(NamedKey::Enter);
    CHECK(o.ws.message() ==
          "Not a velocity: 200; type a value (80), a change (+10, -10) or a percentage (80%)");
    o.type(U"^^8");
    o.press(NamedKey::Tab);
    CHECK(o.ws.commandLine().text() == U"8");
    o.press(NamedKey::Escape);
    CHECK(o.ws.mode() == Mode::Normal);
    CHECK(o.velocities() == std::vector{80, 96, 50, 100, 70});

    // The : prompt afterwards is the command line again.
    o.type(U":");
    CHECK(prompt().starts_with(U":"));
    o.press(NamedKey::Escape);
}

TEST_CASE(
    "humanising with ~ moves notes a little at random in time and velocity, up to the settings")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Operating o{ppqn};
    const auto q = o.q;
    o.ws.setHumanize(std::chrono::milliseconds{20}, 5);
    // Each note's start and velocity, by its pitch, since the chord's two may change places.
    const auto byPitch = [&]
    {
        std::map<int, std::pair<core::Tick, int>> found;
        const auto notes = o.notes();
        const auto velocities = o.velocities();
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            found[notes[i].second] = {notes[i].first, velocities[i]};
        }
        return found;
    };
    const auto before = byPitch();
    o.type(U"gr~2ib"); // the first two bars' notes, at 120 a minute: 20 ms is q/25
    const auto after = byPitch();
    REQUIRE(after.size() == before.size());
    for (const int pitch : {31, 28, 45})
    {
        CAPTURE(pitch);
        REQUIRE(after.contains(pitch));
        CHECK(std::abs(after.at(pitch).first - before.at(pitch).first) <= q / 25 + 1);
        CHECK(std::abs(after.at(pitch).second - before.at(pitch).second) <= 5);
    }
    CHECK(after.at(36) == before.at(36)); // the C2 in bar 5 is outside it
    o.type(U"u");
    CHECK(o.ws.message() == "Undid: Humanise 3 notes in Bass");

    // With nothing to change by, nothing changes.
    o.ws.setHumanize(std::chrono::milliseconds{0}, 0);
    o.type(U"~~");
    CHECK(o.ws.message() == "Nothing changed in 2 notes in Bass");
}
