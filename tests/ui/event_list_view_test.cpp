// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/event_list_view.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

constexpr CellSize kCell{10, 20};
constexpr core::Tick q = 960;

// A song of two sections and two tracks: a bass with a few events, and a lead in 7/8.
core::Song twoTracks()
{
    core::Song song{core::kPpqn};
    const auto bass = song.addTrack("Bass");
    const auto lead = song.addTrack("Lead");
    song.updateTrack(bass, [](core::Track& t) { t.port = "Pro-800"; });
    song.setLabel(2 * q, "fill");
    song.updateEvents(bass,
                      [&](core::EventList& events)
                      {
                          events.insert({0, core::Note{28, 96, q / 2}});
                          events.insert({0, core::ProgramChange{5}});
                          events.insert({q + 240, core::ControlChange{74, 64}});
                          events.insert({4 * q, core::Note{29, 90, q / 2 + 12, 40}});
                      });
    song.updateMeter(lead,
                     [](core::MeterMap& meter) { meter = core::MeterMap{meter.ppqn(), {7, 8}}; });
    song.updateEvents(lead,
                      [&](core::EventList& events) { events.insert({q, core::Note{72, 100, q}}); });
    song.splitSection(4 * q, "Verse");
    song.fitToContent();
    return song;
}

// A view of a song in a window of 120 by 24 cells (wide enough for the bars, as the minimum size
// is): 22 rows between the bars, the first the headings, so 21 rows of events. With its registry,
// the window's commands described there.
struct Viewing
{
    explicit Viewing(core::Song song = twoTracks())
        : document{std::move(song)}
    {
        registry.add(windowCommands());
        ws.resize(120 * kCell.width, 24 * kCell.height, kCell);
    }

    core::Document document;
    CommandRegistry registry;
    Workspace ws{registry, document};
    EventListView& view = ws.eventList();
};

void type(Workspace& view, std::u32string_view keys)
{
    for (const char32_t key : keys)
    {
        view.handle(Key::typed(key), 0);
    }
}

// A row of a region, as text, from the text runs on that row.
std::u32string rowText(const Region& region, int row);

// The two rows along the bottom: the message row, or the prompt on it, then the status line.
std::u32string bottomText(const Frame& frame)
{
    std::u32string text;
    const auto top = frame.back().bounds.y - frame.back().bounds.height;
    for (const auto& region : frame)
        if (region.bounds.y >= top)
            text += rowText(region, 0) + U'|';
    return text;
}

std::u32string rowText(const Region& region, int row)
{
    std::u32string line(static_cast<std::size_t>(region.bounds.width / kCell.width + 1), U' ');
    for (const auto& run : region.text)
    {
        if (run.y == row * kCell.height)
        {
            line.replace(static_cast<std::size_t>(run.x / kCell.width), run.text.size(), run.text);
        }
    }
    return line;
}

bool contains(const std::u32string& text, std::u32string_view part)
{
    return text.find(part) != std::u32string::npos;
}

// The style of the run covering a cell.
Style styleAt(const Region& region, int column, int row)
{
    for (const auto& run : region.text)
    {
        const int first = run.x / kCell.width;
        if (run.y == row * kCell.height && column >= first &&
            column < first + static_cast<int>(run.text.size()))
        {
            return run.style;
        }
    }
    return {};
}

core::Note bassNote(const Viewing& v, std::size_t index)
{
    return std::get<core::Note>(
        v.document.song().events(v.document.song().tracks()[0]->id())[index].data);
}

void enter(Workspace& view)
{
    view.handle(Key::of(NamedKey::Enter), 0);
}

} // namespace

TEST_CASE("the event list opens on track 1, at its first row")
{
    Viewing v;
    CHECK(v.view.track() == 1);
    CHECK(v.view.row() == 0);
    CHECK(v.view.field() == Column::Position);

    const auto frame = v.ws.frame();
    REQUIRE(frame.size() == 5);
    CHECK(contains(rowText(frame[1], 0), U"Track 1: Bass"));
    CHECK(contains(rowText(frame[1], 0), U"Pro-800"));
    CHECK(contains(rowText(frame[1], 0), U"Section: Section 1"));
    CHECK(contains(rowText(frame[1], 0), U"Position: 1:1:000"));
    // The section's bars too, where there's room for them.
    v.ws.resize(200 * kCell.width, 24 * kCell.height, kCell);
    CHECK(contains(rowText(v.ws.frame()[1], 0), U"Bars: 1–1"));
    v.ws.resize(120 * kCell.width, 24 * kCell.height, kCell);

    // The inspector, on a panel of its own: the kind of row, then headings above their values.
    CHECK(frame[2].background == Role::Panel);
    const auto has = [&](std::u32string_view text, int line)
    {
        return std::ranges::any_of(
            frame[2].text,
            [&](const auto& run) { return run.text == text && run.y == 5 + line * kCell.height; });
    };
    CHECK(has(U"Section", 0));
    CHECK(has(U"Position", 0));
    CHECK(has(U"1:1:000", 1));
    CHECK(has(U"Section 1", 1));

    // Its columns stay put from row to row, whatever the kind of row and its values.
    const auto xOf = [](const Region& region, std::u32string_view text)
    {
        const auto it = std::ranges::find(region.text, text, &TextRun::text);
        return it == region.text.end() ? -1 : it->x;
    };
    CHECK(xOf(frame[2], U"Time") == -1); // which is in the status line
    const int position = xOf(frame[2], U"Position");
    CHECK(position >= 0);
    type(v.ws, U"5j"); // an event
    REQUIRE(v.view.list().rows()[v.view.row()].kind == RowKind::Event);
    CHECK(xOf(v.ws.frame()[2], U"Position") == position);
    type(v.ws, U"gg");

    // The status line names the song's file, with [+] once it has changes not saved, where it's
    // wide enough.
    v.ws.resize(200 * kCell.width, 24 * kCell.height, kCell);
    CHECK(contains(bottomText(v.ws.frame()), U"No file"));
    type(v.ws, U"4jx");
    CHECK(contains(bottomText(v.ws.frame()), U"No file [+]"));
    type(v.ws, U"u");
    CHECK_FALSE(contains(bottomText(v.ws.frame()), U"[+]"));
    const auto list = frame[0];
    CHECK(contains(rowText(list, 0), U"Position"));
    CHECK(contains(rowText(list, 0), U"Release"));
    CHECK(contains(rowText(list, 5), U"note"));
    CHECK(contains(rowText(list, 5), U"E1 (28)"));
    CHECK(contains(rowText(list, 5), U"1/8"));
    CHECK(contains(bottomText(frame), U"NORMAL"));
    // Where the cursor is is in the bar above, not here.
    CHECK_FALSE(contains(bottomText(frame), U"1:1:000"));
    CHECK_FALSE(contains(bottomText(frame), U"Section 1"));

    // The conductor's rows are dimmed; the cursor's field is in the cursor's colours.
    CHECK(styleAt(list, 2, 1).background == Role::Cursor);
    CHECK(styleAt(list, 14, 1).foreground == Role::Dim);
    CHECK(styleAt(list, 14, 4).foreground == Role::Text);

    // A song with no tracks opens on the conductor's list.
    Viewing empty{core::Song{}};
    CHECK(empty.view.track() == 0);
}

TEST_CASE("j and k move between rows, h and l between fields, with counts")
{
    Viewing v;
    type(v.ws, U"4j"); // to the first note
    CHECK(v.view.row() == 4);
    type(v.ws, U"2l");
    CHECK(v.view.field() == Column::Value);
    type(v.ws, U"$");
    CHECK(v.view.field() == Column::Release);
    type(v.ws, U"0");
    CHECK(v.view.field() == Column::Position);

    // The field kept to as rows change, shown on the last field before it where a row has
    // fewer.
    type(v.ws, U"$k");
    CHECK(v.view.row() == 3); // the program change
    CHECK(v.view.field() == Column::Data);
    type(v.ws, U"j");
    CHECK(v.view.field() == Column::Release);

    // Dimmed rows are visited too.
    type(v.ws, U"k3k");
    CHECK(v.view.row() == 0);
    type(v.ws, U"99j");
    CHECK(v.view.row() == v.view.list().rows().size() - 1); // the end row
    type(v.ws, U"99k");
    CHECK(v.view.row() == 0);
}

TEST_CASE("gg goes to the first row and G to the end, or with a count to that bar")
{
    Viewing v;
    type(v.ws, U"G");
    CHECK(v.view.list().rows()[v.view.row()].kind == RowKind::End);
    type(v.ws, U"gg");
    CHECK(v.view.row() == 0);
    type(v.ws, U"2G");
    CHECK(v.view.list().rows()[v.view.row()].tick == 4 * q);
    type(v.ws, U"1gg");
    CHECK(v.view.row() == 0);
}

TEST_CASE("time motions go to the event at or after where they land, in the track's bars")
{
    // The bass's rows: 0 section, 1 meter, 2 tempo, 3 program and 4 note at 1:1:000, 5 cc at
    // 1:2:240, 6 label at 1:3:000, 7 section and 8 note at 2:1:000, and 9 the end.
    Viewing v;
    type(v.ws, U"6j}"); // a bar on from 1:3:000 is 2:3:000, with nothing after but the end
    CHECK(v.view.row() == 9);
    type(v.ws, U"gg}");
    CHECK(v.view.row() == 7); // a bar on from 1:1:000, the verse's section row

    type(v.ws, U"G[b");
    CHECK(v.view.row() == 7); // the bar line before the end is 2:1:000
    type(v.ws, U"[b");
    CHECK(v.view.row() == 0);
    type(v.ws, U"]b");
    CHECK(v.view.row() == 7);
    type(v.ws, U"{");
    CHECK(v.view.row() == 0);
    type(v.ws, U"[b");
    CHECK(v.view.row() == 0); // nowhere earlier to go
}

TEST_CASE("( and ) go by chords: events starting within the chord window of each other")
{
    Viewing v;
    // The program and the note at 1:1:000 are one chord, then the cc, then the verse's note.
    type(v.ws, U")");
    CHECK(v.view.row() == 3);
    type(v.ws, U")");
    CHECK(v.view.row() == 5);
    type(v.ws, U"2)");
    CHECK(v.view.row() == 9); // past the last, the end
    type(v.ws, U"(");
    CHECK(v.view.row() == 8);
    type(v.ws, U"k(");
    CHECK(v.view.row() == 5); // from inside a chord, its start first
    type(v.ws, U"gg(");
    CHECK(v.view.row() == 0); // nothing before

    // Notes a little apart are one chord, until the window is narrowed.
    core::Song song{core::kPpqn};
    const auto keys = song.addTrack("Keys");
    song.updateEvents(keys,
                      [](core::EventList& events)
                      {
                          events.insert({0, core::Note{60, 100, q}});
                          events.insert({20, core::Note{64, 100, q}}); // about 10 ms later
                          events.insert({40, core::Note{67, 100, q}});
                          events.insert({q, core::Note{72, 100, q}});
                      });
    Viewing chord{song};
    type(chord.ws, U")");
    CHECK(chord.view.list().rows()[chord.view.row()].tick == 0);
    type(chord.ws, U")");
    CHECK(chord.view.list().rows()[chord.view.row()].tick == q);
    chord.view.setChordWindow(std::chrono::milliseconds{5});
    type(chord.ws, U"gg))");
    CHECK(chord.view.list().rows()[chord.view.row()].tick == 20);
}

TEST_CASE("section and label motions go to their rows")
{
    Viewing v;
    type(v.ws, U"]s");
    CHECK(v.view.row() == 7);
    type(v.ws, U"j[s");
    CHECK(v.view.row() == 0); // from the verse's first note, at its start, the section before
    type(v.ws, U"6j[s");
    CHECK(v.view.row() == 0); // from inside a section, its start
    type(v.ws, U"]l");
    CHECK(v.view.row() == 6);
    type(v.ws, U"G[l");
    CHECK(v.view.row() == 6);
    type(v.ws, U"]l");
    CHECK(v.view.row() == 6); // none after: it stays
}

TEST_CASE("x deletes the event under the cursor, and u brings it back where it was")
{
    // The bass's rows: 0 section, 1 meter, 2 tempo, 3 program and 4 note at 1:1:000, 5 cc at
    // 1:2:240, 6 label at 1:3:000, 7 section and 8 note at 2:1:000, and 9 the end.
    Viewing v;
    const auto events = [&]
    { return v.document.song().events(v.document.song().tracks()[0]->id()); };
    REQUIRE(events().size() == 4);

    type(v.ws, U"4jx"); // the note at 1:1:000
    CHECK(events().size() == 3);
    CHECK(v.view.row() == 4); // on what followed, the cc
    CHECK(v.ws.registerEvents().size() == 1);
    CHECK(v.document.modified());

    type(v.ws, U"G");
    type(v.ws, U"u");
    CHECK(events().size() == 4);
    CHECK(v.view.row() == 4); // back on the note, though the section is first at its time
    CHECK(v.ws.message() == "Undid: Delete 1 event in Bass");
    CHECK_FALSE(v.document.modified());
    v.ws.handle(Key::typed(U'r', kCtrl), 0);
    CHECK(events().size() == 3);
    CHECK(v.ws.message() == "Redid: Delete 1 event in Bass");
    type(v.ws, U"uu");
    CHECK(v.ws.message() == "Nothing to undo");

    type(v.ws, U"gg3j2x"); // the program and the note
    CHECK(events().size() == 2);
    type(v.ws, U"u");

    // On a row that isn't the track's, there's nothing to delete.
    type(v.ws, U"ggx");
    CHECK(events().size() == 4);
    CHECK_FALSE(v.document.modified());
}

TEST_CASE("dd and yy take the cursor's bar's events, which p puts from the start of a bar")
{
    // The bass's rows: 0 section, 1 meter, 2 tempo, 3 program and 4 note at 1:1:000, 5 cc at
    // 1:2:240, 6 label at 1:3:000, 7 section and 8 note at 2:1:000, and 9 the end.
    Viewing v;
    const auto events = [&]
    { return v.document.song().events(v.document.song().tracks()[0]->id()); };
    type(v.ws, U"5jdd"); // from the cc, bar 1: the program, the note and the cc
    CHECK(events().size() == 1);
    CHECK(v.ws.registerEvents().size() == 3);
    CHECK(v.ws.message().empty());
    type(v.ws, U"u");
    CHECK(v.ws.message() == "Undid: Delete 3 events in Bass");
    type(v.ws, U"gg5jdib"); // ib is the same
    CHECK(events().size() == 1);
    type(v.ws, U"u");

    // Yanked from the cc's row, they go from bar 2's start, the cc as far into it as it was.
    type(v.ws, U"gg5jyy");
    CHECK(v.ws.message() == "3 events yanked");
    type(v.ws, U"gg8jp");
    REQUIRE(events().size() == 7);
    CHECK(events()[6].tick == 4 * q + q + 240);
    CHECK(std::holds_alternative<core::ControlChange>(events()[6].data));

    type(v.ws, U"dab");
    CHECK(v.ws.message() == "ab isn't in the event list, which is for events rather than time");
}

TEST_CASE("d and y take a motion: whole rows for j, k, gg and G, up to the row for the others")
{
    Viewing v;
    const auto events = [&]
    { return v.document.song().events(v.document.song().tracks()[0]->id()); };

    type(v.ws, U"3jdj"); // the program and the note
    CHECK(events().size() == 2);
    type(v.ws, U"u");

    type(v.ws, U"gg4jd]b"); // from the note up to the verse's section row: the note and the cc
    CHECK(events().size() == 2);
    CHECK(events()[0].tick == 0); // the program is left
    type(v.ws, U"u");

    type(v.ws, U"Gkd[b"); // back from the verse's note to 1:1:000: every event before it
    CHECK(events().size() == 1);
    CHECK(events()[0].tick == 4 * q);
    type(v.ws, U"u");

    // Counts before and after the operator multiply: 2d2j from the program takes 5 rows.
    type(v.ws, U"gg3j2d2j");
    CHECK(events().size() == 1);
    type(v.ws, U"u");

    type(v.ws, U"gg4jy]b");
    CHECK(v.ws.message() == "2 events yanked");
    CHECK(events().size() == 4);
    CHECK(v.ws.registerEvents().size() == 2);
    CHECK(v.ws.registerEvents()[1].tick == q + 240);
}

TEST_CASE("p puts the events at the cursor's time, never overlapping notes")
{
    Viewing v;
    const auto events = [&]
    { return v.document.song().events(v.document.song().tracks()[0]->id()); };
    type(v.ws, U"p");
    CHECK(v.ws.message() == "Nothing yanked or deleted to put");

    type(v.ws, U"4jY"); // the E1 at 1:1:000
    type(v.ws, U"4jp"); // at 2:1:000, beside the F1 there
    CHECK(events().size() == 5);
    CHECK(events()[4].tick == 4 * q);
    CHECK(std::get<core::Note>(events()[4].data).pitch == 28);

    // The F1 put where an F1 starts replaces it, rather than doubling it.
    type(v.ws, U"u");
    type(v.ws, U"GkYp");
    CHECK(events().size() == 4);
}

TEST_CASE("an operator waits for its motion, and Escape or another key gives it up")
{
    Viewing v;
    type(v.ws, U"2d3");
    CHECK(v.ws.mode() == Mode::OperatorPending);
    CHECK(v.ws.pending() == "2d3");
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    CHECK(v.ws.mode() == Mode::Normal);
    CHECK(v.ws.pending().empty());
    type(v.ws, U"dQ"); // no motion
    CHECK(v.ws.mode() == Mode::Normal);
    CHECK(v.document.song().events(v.document.song().tracks()[0]->id()).size() == 4);
}

TEST_CASE("the conductor's list edits its names, tempos and meters, and deletes changes")
{
    // The conductor's rows: 0 section Song, 1 meter, 2 tempo, 3 label at 1:3:000, 4 section
    // Verse at 2:1:000, and 5 the end.
    Viewing v;
    const auto& song = [&]() -> const core::Song& { return v.document.song(); };
    v.ws.handle(Key::of(NamedKey::PageUp, kCtrl), 0);
    REQUIRE(v.view.track() == 0);

    type(v.ws, U"3jl"); // the label's name
    enter(v.ws);
    CHECK(v.view.fieldLine().text() == U"fill");
    type(v.ws, U"fix");
    enter(v.ws);
    CHECK(song().labels()[0].name == "fix");
    type(v.ws, U"u");
    CHECK(v.ws.message() == "Undid: Rename label fill to fix");

    type(v.ws, U"j"); // the verse's name
    enter(v.ws);
    type(v.ws, U"Chorus");
    enter(v.ws);
    CHECK(song().sections()[1]->name() == "Chorus");

    type(v.ws, U"gg2j"); // the tempo at the start
    enter(v.ws);
    CHECK(v.view.fieldLine().text() == U"120");
    type(v.ws, U"140");
    enter(v.ws);
    CHECK(song().tempo().tempoAt(0) == core::Tempo::fromBpm(140));
    type(v.ws, U"+");
    CHECK(song().tempo().tempoAt(0) == core::Tempo::fromBpm(141));
    enter(v.ws);
    type(v.ws, U"1001");
    enter(v.ws);
    CHECK(v.ws.message().starts_with("A tempo must be from 1 to 1000 BPM"));
    v.ws.handle(Key::of(NamedKey::Escape), 0);

    type(v.ws, U"k"); // the meter at the start
    enter(v.ws);
    type(v.ws, U"3/4");
    enter(v.ws);
    CHECK(song().meter().signatureAt(0) == core::TimeSignature{3, 4});
    CHECK(v.ws.message().empty());
    // The intro's four beats are now a 3/4 bar and a 1/4 remainder bar, which leaves no room
    // for a whole 4/4 bar at the start: it says so, and changes nothing.
    type(v.ws, U"+");
    CHECK(v.ws.message() == "The meter change at 2:1:000 leaves no room for a whole 4/4 bar "
                            "here; change or delete it first");
    CHECK(song().meter().signatureAt(0) == core::TimeSignature{3, 4});
    type(v.ws, U"u");
    CHECK(song().meter().signatureAt(0) == core::TimeSignature{4, 4});
    // A meter chosen that isn't built in is remembered, to be offered from now on; 3/4 is
    // built in.
    CHECK(v.ws.remembered().meters().empty());
    enter(v.ws);
    type(v.ws, U"2/2");
    enter(v.ws);
    REQUIRE(song().meter().signatureAt(0) == core::TimeSignature{2, 2});
    REQUIRE(v.ws.remembered().meters().size() == 1);
    CHECK(v.ws.remembered().meters().front() == core::TimeSignature{2, 2});
    type(v.ws, U"u");

    // A section's own tempo and meter, and the section, can't be deleted; a label can.
    type(v.ws, U"gg3x");
    CHECK(v.ws.message().starts_with("Every section keeps its tempo and meter at its start"));
    type(v.ws, U"gg]l");
    REQUIRE(v.view.list().rows()[v.view.row()].kind == RowKind::Label);
    type(v.ws, U"x");
    CHECK(song().labels().empty());

    // A tempo change inside a section can.
    v.document.edit("Add a tempo change", {}, [](core::Song& s)
                    { s.updateTempo([](core::TempoMap& t) { t.set(q, core::Tempo{400'000}); }); });
    v.view.refresh();
    type(v.ws, U"gg3j");
    REQUIRE(v.view.list().rows()[v.view.row()].kind == RowKind::Tempo);
    type(v.ws, U"x");
    CHECK(song().tempo().changes().size() == 1);
    CHECK(v.ws.message().empty());

    // Its positions, and yanking and putting there, wait.
    type(v.ws, U"0");
    enter(v.ws);
    CHECK(v.ws.message() == "Moving the conductor's changes comes later");
    type(v.ws, U"Y");
    CHECK(v.ws.message() == "Only a track's events can be yanked");
    type(v.ws, U"p");
    CHECK(v.ws.message() == "Only a track's events can be put");
}

TEST_CASE("Enter edits a field in place: typing replaces it, and Enter sets it")
{
    Viewing v;
    type(v.ws, U"4j2l"); // the E1's velocity, 96
    const auto before = rowText(v.ws.frame()[0], 5).find(U"96");
    enter(v.ws);
    CHECK(v.view.editingField());
    CHECK(rowText(v.ws.frame()[0], 5).find(U"96") == before); // right-justified as it was
    CHECK(v.ws.mode() == Mode::CommandLine);
    CHECK(v.view.fieldLine().text() == U"96");
    CHECK(contains(bottomText(v.ws.frame()), U"EDIT"));
    // Highlighted whole while typing would replace it, then with a cursor once it's moved.
    const auto column = static_cast<int>(before);
    CHECK(styleAt(v.ws.frame()[0], column, 5).background == Role::Cursor);
    CHECK(styleAt(v.ws.frame()[0], column + 1, 5).background == Role::Cursor);
    v.ws.handle(Key::of(NamedKey::Left), 0);
    CHECK(v.view.fieldLine().text() == U"96");
    CHECK(styleAt(v.ws.frame()[0], column, 5).background == Role::Selection);
    CHECK(styleAt(v.ws.frame()[0], column + 1, 5).background == Role::Cursor);
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    enter(v.ws);
    v.ws.handle(Key::of(NamedKey::Backspace), 0); // takes it all while it's highlighted
    CHECK(v.view.fieldLine().text().empty());
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    CHECK(bassNote(v, 1).velocity == 96);
    enter(v.ws);
    type(v.ws, U"80");
    CHECK(v.view.fieldLine().text() == U"80");
    enter(v.ws);
    CHECK_FALSE(v.view.editingField());
    CHECK(bassNote(v, 1).velocity == 80);
    type(v.ws, U"u");
    CHECK(v.ws.message() == "Undid: Set velocity in Bass");

    // Long text stays in the field's box, scrolling to keep the cursor in it, rather than
    // spilling over the columns beside it.
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    enter(v.ws);
    type(v.ws, U"1234567890123456789");
    const auto editing = rowText(v.ws.frame()[0], 5);
    CHECK(contains(editing, U"0:0:480 (1/8)"));     // the length beside it, still there
    CHECK(contains(editing, U"6789"));              // the end, where the cursor is
    CHECK_FALSE(contains(editing, U"12345678901")); // more than fits
    CHECK(contains(editing, U"64"));                // the release beyond, untouched
    v.ws.handle(Key::of(NamedKey::Escape), 0);

    // A change; keys that edit keep the value.
    enter(v.ws);
    type(v.ws, U"+10");
    enter(v.ws);
    CHECK(bassNote(v, 1).velocity == 106);
    enter(v.ws);
    v.ws.handle(Key::of(NamedKey::End), 0);
    v.ws.handle(Key::of(NamedKey::Backspace), 0);
    type(v.ws, U"0");
    CHECK(v.view.fieldLine().text() == U"100");
    enter(v.ws);
    CHECK(bassNote(v, 1).velocity == 100);

    // A value out of range keeps the prompt open to put right; Escape leaves it as it was.
    enter(v.ws);
    type(v.ws, U"200");
    enter(v.ws);
    CHECK(v.view.editingField());
    CHECK(v.ws.message().starts_with("A velocity must be from 1 to 127"));
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    CHECK_FALSE(v.view.editingField());
    CHECK(bassNote(v, 1).velocity == 100);
}

TEST_CASE("editing a note's pitch, length and position, never into another of its pitch")
{
    Viewing v;
    type(v.ws, U"4jl"); // the E1's pitch, shown as E1 (28), edited as E1
    enter(v.ws);
    CHECK(v.view.fieldLine().text() == U"E1");
    type(v.ws, U"F1");
    enter(v.ws);
    CHECK(bassNote(v, 1).pitch == 29);

    type(v.ws, U"2l"); // its length, 0:0:480 (1/8), edited as 0:0:480
    enter(v.ws);
    CHECK(v.view.fieldLine().text() == U"0:0:480");
    type(v.ws, U"+1/16");
    enter(v.ws);
    CHECK(bassNote(v, 1).duration == 720);

    // Moved to where the other F1 starts: refused, naming it.
    type(v.ws, U"0");
    enter(v.ws);
    type(v.ws, U"2:1:000");
    enter(v.ws);
    CHECK(v.view.editingField());
    CHECK(v.ws.message() == "Would overlap the F1 at 2:1:000");
    v.ws.handle(Key::of(NamedKey::Escape), 0);

    // Moved a beat on instead, the cursor following it.
    enter(v.ws);
    type(v.ws, U"+0:1:000");
    enter(v.ws);
    CHECK(v.view.list().rows()[v.view.row()].tick == q);
    CHECK(v.view.list().text(v.view.row())[Column::Data] == "F1 (29)");
}

TEST_CASE("+ and - step the field, with counts, as Ctrl-A and Ctrl-X do")
{
    Viewing v;
    type(v.ws, U"4j2l");
    type(v.ws, U"+");
    CHECK(bassNote(v, 1).velocity == 97);
    type(v.ws, U"5-");
    CHECK(bassNote(v, 1).velocity == 92);
    v.ws.handle(Key::typed(U'a', kCtrl), 0);
    CHECK(bassNote(v, 1).velocity == 93);
    type(v.ws, U"200");
    v.ws.handle(Key::typed(U'x', kCtrl), 0);
    CHECK(bassNote(v, 1).velocity == 1); // kept in range
    type(v.ws, U"h-");                   // the pitch, a semitone down
    CHECK(bassNote(v, 1).pitch == 27);
}

TEST_CASE("o adds a note at the cursor's time once its pitch is typed")
{
    Viewing v;
    const auto events = [&]
    { return v.document.song().events(v.document.song().tracks()[0]->id()); };
    type(v.ws, U"5jo"); // on the cc at 1:2:240
    CHECK(v.view.editingField());
    CHECK(v.view.fieldLine().text().empty());
    // The new row shows after the cursor's, with the velocity and length of the note before.
    const auto list = v.ws.frame()[0];
    CHECK(contains(rowText(list, 7), U"1:2:240"));
    CHECK(contains(rowText(list, 7), U"note"));
    CHECK(contains(rowText(list, 7), U"96"));
    CHECK(events().size() == 4);

    enter(v.ws); // no pitch yet
    CHECK(v.ws.message() == "Type the note's pitch, such as C4 or 60");
    type(v.ws, U"G2");
    enter(v.ws);
    CHECK_FALSE(v.view.editingField());
    REQUIRE(events().size() == 5);
    CHECK(events()[3].tick == q + 240);
    CHECK(std::get<core::Note>(events()[3].data) == core::Note{43, 96, q / 2, 64});
    CHECK(v.view.list().text(v.view.row())[Column::Data] == "G2 (43)"); // on it
    CHECK(v.view.field() == Column::Data);
    type(v.ws, U"u");
    CHECK(v.ws.message() == "Undid: Add a note in Bass");
    CHECK(events().size() == 4);

    // Escape leaves nothing behind, and a pitch already sounding there is refused.
    type(v.ws, U"o");
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    CHECK(events().size() == 4);
    type(v.ws, U"ggo");
    type(v.ws, U"E1");
    enter(v.ws);
    CHECK(v.ws.message() == "Would overlap the E1 at 1:1:000");
    CHECK(v.view.editingField());
}

TEST_CASE("only the track's own events are edited in its list")
{
    Viewing v;
    enter(v.ws); // the section row
    CHECK_FALSE(v.view.editingField());
    CHECK(v.ws.message() == "That's the conductor's: edit it in its list (gc)");
    type(v.ws, U"G");
    enter(v.ws);
    CHECK(v.ws.message() == "Nothing to edit here");
    type(v.ws, U"+");
    CHECK(v.ws.message() == "Nothing to change here");
}

TEST_CASE("Ctrl-PageDown and Ctrl-PageUp go through the tracks, the conductor as track 0, keeping "
          "the time")
{
    Viewing v;
    type(v.ws, U"7j"); // the verse's section row, at 2:1:000
    REQUIRE(v.view.list().rows()[v.view.row()].tick == 4 * q);
    v.ws.handle(Key::of(NamedKey::PageDown, kCtrl), 0);
    CHECK(v.view.track() == 2);
    CHECK(v.view.list().rows()[v.view.row()].tick == 4 * q);
    CHECK(contains(rowText(v.ws.frame()[1], 0), U"Track 2: Lead"));
    v.ws.handle(Key::of(NamedKey::PageDown, kCtrl), 0);
    CHECK(v.view.track() == 0); // round to the conductor
    CHECK(contains(rowText(v.ws.frame()[1], 0), U"Track 0: Conductor"));
    v.ws.handle(Key::of(NamedKey::PageDown, kCtrl), 0);
    CHECK(v.view.track() == 1);
    v.ws.handle(Key::of(NamedKey::PageUp, kCtrl), 0);
    CHECK(v.view.track() == 0);
    v.ws.handle(Key::of(NamedKey::PageUp, kCtrl), 0);
    CHECK(v.view.track() == 2);
    type(v.ws, U"1");
    v.ws.handle(Key::of(NamedKey::PageDown, kCtrl), 0); // one track on, wrapping to the conductor
    CHECK(v.view.track() == 0);
    type(v.ws, U"0");
    v.ws.handle(Key::of(NamedKey::PageDown, kCtrl), 0);
    CHECK(v.view.track() == 1); // 0 is a field motion, then the next track
}

TEST_CASE("the event list scrolls by half and whole pages, and Ctrl with j and k keep place")
{
    Viewing v{demoSong()};
    v.ws.resize(120 * kCell.width, 25 * kCell.height, kCell); // a row for the message row
    REQUIRE(v.view.viewRows() == 15); // below the inspector, the track's bar and the headings
    v.ws.handle(Key::typed(U'd', kCtrl), 0);
    CHECK(v.view.row() == 7);
    CHECK(v.view.top() == 7);
    v.ws.handle(Key::typed(U'f', kCtrl), 0);
    CHECK(v.view.row() == 20); // a page less two rows, as in Vim
    CHECK(v.view.top() == 20);
    v.ws.handle(Key::typed(U'u', kCtrl), 0);
    CHECK(v.view.top() == 13);

    v.ws.handle(Key::typed(U'j', kCtrl), 0);
    CHECK(v.view.row() == 14);
    CHECK(v.view.top() == 14);
    type(v.ws, U"zz");
    CHECK(v.view.top() == 7); // row 14 in the middle of 15
    type(v.ws, U"gg");
    v.ws.handle(Key::typed(U'k', kCtrl), 0); // the view stops at the top
    CHECK(v.view.top() == 0);
    CHECK(v.view.row() == 0);

    // The cursor stays in view as it moves.
    type(v.ws, U"30j");
    CHECK(v.view.top() == 16);
}

TEST_CASE("a note's length shows as bars, beats and ticks, with the value it's close to")
{
    Viewing v;
    type(v.ws, U"4j");
    CHECK(v.view.list().text(v.view.row())[Column::Length] == "0:0:480 (1/8)");
    type(v.ws, U"4j"); // the verse's note
    CHECK(v.view.list().text(v.view.row())[Column::Length] == "0:0:492 (≈1/8)");
}

TEST_CASE("the list shows the song as it is after a change, at the same time")
{
    Viewing v;
    type(v.ws, U"8j"); // the verse's note
    v.document.edit("Add a note", {},
                    [&](core::Song& song)
                    {
                        song.updateEvents(song.tracks()[0]->id(), [](core::EventList& events)
                                          { events.insert({0, core::Note{30, 1, 1}}); });
                    });
    v.view.refresh();
    CHECK(v.view.list().rows()[v.view.row()].tick == 4 * q);
    CHECK(v.view.list().rows().size() == 11);
}

TEST_CASE("the built-in modalrc binds keys only to the event list's and the window's commands")
{
    Config config;
    CommandRegistry commands;
    commands.add(Workspace::commands());
    commands.add(windowCommands());
    ConfigReader reader{config, commands};
    reader.runText(builtInModalrc(), "built-in modalrc", {});
    CHECK(reader.errors().empty());

    // And every one of its commands is one the view runs.
    Viewing v;
    for (const auto& command : Workspace::commands())
    {
        CAPTURE(command.name);
        const auto result = v.ws.run(command.name, 0);
        // Commands only for the : line, such as port, may want what this view lacks; and the
        // bars' and sections' commands what this song does: a one-bar song at its end, a
        // section starting there and no labels. Freeze also requires a whole track rather
        // than a partial selection left by an earlier command in this run. A meter goes in
        // before the song's end, and rebarring takes a selection.
        const bool songs = command.name == "bar-delete" || command.name == "bar-insert" ||
                           command.name == "section-split" || command.name == "label-to-section" ||
                           command.name == "section-join" || command.name == "track-freeze" ||
                           command.name == "meter-insert" || command.name == "rebar";
        CHECK((!result.warning || result.message.find("works only in") != std::string::npos ||
               !command.keys || songs));
    }
}

TEST_CASE("a count shows until a key uses it or Escape gives it up")
{
    Viewing v;
    CHECK(v.ws.handle(Key::typed(U'4'), 0));
    CHECK(v.ws.handle(Key::typed(U'0'), 0));
    CHECK(v.ws.pending() == "40");
    CHECK(contains(bottomText(v.ws.frame()), U"40"));
    CHECK(v.ws.handle(Key::of(NamedKey::Escape), 0));
    CHECK(v.ws.pending().empty());
    type(v.ws, U"5x"); // a key with no binding gives the count up too
    CHECK(v.ws.pending().empty());
    type(v.ws, U"j");
    CHECK(v.view.row() == 1);
}

TEST_CASE("a message shows in the status line until the next key")
{
    Viewing v;
    v.ws.resize(200 * kCell.width, 24 * kCell.height, kCell);
    v.ws.setMessage("modalrc:3: unknown setting", MessageKind::Warning);
    CHECK(contains(bottomText(v.ws.frame()), U"modalrc:3: unknown setting"));
    CHECK(v.ws.handle(Key::typed(U'x'), 0)); // unbound, but the message goes
    CHECK(v.ws.message().empty());
}

TEST_CASE("keys run the registry's other commands too, such as the window's, showing messages")
{
    Viewing v;
    std::vector<int> counts;
    v.registry.add(windowCommands()[0],
                   [&](const CommandCall& call)
                   {
                       counts.push_back(call.count);
                       return CommandResult{true, "bigger", false};
                   });
    CHECK(v.ws.handle(Key::typed(U'=', kCtrl), 0));
    CHECK(v.ws.message() == "bigger");
    v.ws.handle(Key::typed(U'3'), 0);
    v.ws.handle(Key::of(NamedKey::KeypadPlus, kCtrl), 0);
    CHECK(counts == std::vector<int>{0, 3});
    v.ws.handle(Key::typed(U'-', kCtrl), 0);
    CHECK(v.ws.message() == "font-smaller can't be run here");
}

TEST_CASE("colon opens a prompt where keys are typed, and Enter hands the line on")
{
    Viewing v;
    std::vector<std::string> lines;
    v.ws.setLineHandler([&](const std::string& line) { lines.push_back(line); });

    type(v.ws, U":set fontsize=14j"); // digits and j are typed, not counted or moved
    CHECK(v.ws.mode() == Mode::CommandLine);
    CHECK(v.ws.commandLine().text() == U"set fontsize=14j");
    CHECK(v.view.row() == 0);
    v.ws.handle(Key::of(NamedKey::Backspace), 0);
    CHECK(contains(bottomText(v.ws.frame()), U":set fontsize=14"));
    v.ws.handle(Key::of(NamedKey::Enter), 0);
    CHECK(v.ws.mode() == Mode::Normal);
    CHECK(lines == std::vector<std::string>{"set fontsize=14"});

    type(v.ws, U":");
    v.ws.handle(Key::of(NamedKey::Up), 0);
    CHECK(v.ws.commandLine().text() == U"set fontsize=14");
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    CHECK(v.ws.mode() == Mode::Normal);
    CHECK(lines.size() == 1);
}

TEST_CASE(":port sets the track's output port by its name or part of it")
{
    Viewing v;
    const std::vector<std::string> outputs{"Microsoft GS Wavetable Synth", "loopMIDI Port",
                                           "loopMIDI Port 2"};
    // As the platform's matching does: the whole name, or else the one port containing it.
    v.ws.setPortFinder(
        [&](std::string_view name)
        {
            Workspace::PortLookup lookup{.port = {}, .candidates = {}, .ports = outputs};
            for (const auto& port : outputs)
            {
                if (port == name)
                {
                    lookup.port = port;
                    return lookup;
                }
                if (!name.empty() && port.find(name) != std::string::npos)
                {
                    lookup.candidates.push_back(port);
                }
            }
            if (lookup.candidates.size() == 1)
            {
                lookup.port = lookup.candidates.front();
                lookup.candidates.clear();
            }
            return lookup;
        });
    const auto port = [&] { return v.document.song().tracks()[0]->port; };

    auto result = v.registry.runLine("port");
    CHECK(result.message == "Bass plays to Pro-800; outputs: Microsoft GS Wavetable Synth, "
                            "loopMIDI Port, loopMIDI Port 2");
    CHECK_FALSE(result.warning);

    result = v.registry.runLine("port GS");
    CHECK(result.message == "Bass plays to Microsoft GS Wavetable Synth");
    CHECK(port() == "Microsoft GS Wavetable Synth");
    CHECK(v.document.modified());

    result = v.registry.runLine("port loop");
    CHECK(result.warning);
    CHECK(result.message == "\"loop\" could be loopMIDI Port, loopMIDI Port 2");
    result = v.registry.runLine("port Pro-800");
    CHECK(result.message == "No output port matches \"Pro-800\"; outputs: Microsoft GS Wavetable "
                            "Synth, loopMIDI Port, loopMIDI Port 2");
    CHECK(port() == "Microsoft GS Wavetable Synth"); // unchanged

    // One undo step, back to the port it had.
    type(v.ws, U"u");
    CHECK(port() == "Pro-800");

    // The conductor sends nothing, so has no port.
    v.ws.handle(Key::of(NamedKey::PageUp, kCtrl), 0);
    result = v.registry.runLine("port GS");
    CHECK(result.warning);
    CHECK(result.message == "The conductor has no port; its tempo and meter aren't sent");
}

TEST_CASE(":port without MIDI says so")
{
    Viewing v;
    const auto result = v.registry.runLine("port GS");
    CHECK(result.warning);
    CHECK(result.message == "There are no MIDI ports here");
}

TEST_CASE(":section-split starts a new section at the bar line of the cursor's bar")
{
    core::Song song{core::kPpqn}; // one section of eight bars of 4/4
    const auto bass = song.addTrack("Bass");
    const auto lead = song.addTrack("Lead");
    song.updateEvents(bass, [](core::EventList& events)
                      { events.insert({2 * 4 * q + q, core::Note{40, 100, q}}); }); // 3:2:000
    song.updateMeter(lead,
                     [](core::MeterMap& meter) { meter = core::MeterMap{meter.ppqn(), {2, 4}}; });
    song.updateEvents(lead, [](core::EventList& events)
                      { events.insert({2 * q + q / 2, core::Note{60, 100, q}}); }); // its 2:1:480
    Viewing v{std::move(song)};
    const auto names = [&]
    {
        std::vector<std::string> result;
        for (const auto& s : v.document.song().sections())
        {
            result.push_back(s->name());
        }
        return result;
    };

    // At the first section's start there's nothing to split.
    CHECK(v.registry.runLine("section-split Intro") ==
          CommandResult::error("Section 1 starts at 1:1:000 already"));

    type(v.ws, U"G"); // the end
    CHECK(v.registry.runLine("section-split Outro").warning);

    // On the bass's note in bar 3, the section starts at 3:1:000.
    type(v.ws, U"gg");
    while (v.view.list().rows()[v.view.row()].kind != RowKind::Event)
    {
        type(v.ws, U"j");
    }
    CHECK(v.registry.runLine("section-split Chorus") ==
          CommandResult{true, "Chorus starts at 3:1:000", false});
    CHECK(names() == std::vector<std::string>{"Section 1", "Chorus"});
    CHECK(v.document.song().sectionStart(1) == 2 * 4 * q);

    // Without a name, Section and a number; one named again gets a number of its own.
    type(v.ws, U"u");
    CHECK(names() == std::vector<std::string>{"Section 1"});
    // Asking for its name first, with that one to edit (decided November 2026, after use):
    // Escape starts none, Enter starts it named, as one step.
    CHECK(v.registry.runLine("section-split").changed);
    REQUIRE(v.ws.mode() == Mode::CommandLine);
    CHECK(v.ws.commandLine().text() == U"Section 2");
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    CHECK(names() == std::vector<std::string>{"Section 1"});
    v.registry.runLine("section-split");
    v.ws.handle(Key::of(NamedKey::Enter), 0);
    CHECK(v.ws.message() == "Section 2 starts at 3:1:000");
    CHECK(names() == std::vector<std::string>{"Section 1", "Section 2"});
    // As renaming with no name asks, with the section's name to edit.
    CHECK(v.registry.runLine("section-rename").changed);
    CHECK(v.ws.commandLine().text() == U"section-rename Section 2");
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    REQUIRE(v.ws.mode() == Mode::Normal);

    // From the lead, in 2/4, its second bar starts in the middle of the conductor's first,
    // which ends short.
    v.ws.handle(Key::of(NamedKey::PageDown, kCtrl), 0);
    REQUIRE(v.view.track() == 2);
    type(v.ws, U"gg");
    while (v.view.list().rows()[v.view.row()].kind != RowKind::Event)
    {
        type(v.ws, U"j");
    }
    const auto result = v.registry.runLine("section-split Bridge");
    CHECK(result.warning);
    CHECK(result.message ==
          "Bridge starts at 2:1:000; this cuts a bar short in the conductor and the tracks "
          "following it");
    CHECK(v.document.song().sectionStart(1) == 2 * q);
}

TEST_CASE("Enter edits the cell under the cursor, and Tab goes on along the row")
{
    // The bass's rows: 0 section, 1 meter, 2 tempo, 3 program and 4 note at 1:1:000, 5 cc at
    // 1:2:240, 6 label at 1:3:000, 7 section and 8 note at 2:1:000, and 9 the end.
    Viewing v;
    const auto events = [&]
    { return v.document.song().events(v.document.song().tracks()[0]->id()); };
    const auto note = [&] { return std::get<core::Note>(events()[1].data); };
    type(v.ws, U"4j");
    v.ws.handle(Key::of(NamedKey::Enter), 0); // the note's position
    REQUIRE(v.view.editingField());
    CHECK(v.view.field() == Column::Position);
    v.ws.handle(Key::of(NamedKey::Tab), 0); // past its type, to its pitch
    CHECK(v.view.field() == Column::Data);
    CHECK(v.view.fieldLine().text() == U"E1");
    type(v.ws, U"F1");
    v.ws.handle(Key::of(NamedKey::Tab), 0);
    CHECK(note().pitch == 29);
    CHECK(v.view.field() == Column::Value);
    v.ws.handle(Key::of(NamedKey::Up), 0); // a step, straight away
    CHECK(note().velocity == 97);
    CHECK(v.view.fieldLine().text() == U"97");
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    CHECK_FALSE(v.view.editingField());

    // Round from the first field to the last.
    type(v.ws, U"0");
    v.ws.handle(Key::of(NamedKey::Enter), 0);
    v.ws.handle(Key::of(NamedKey::Tab, kShift), 0);
    CHECK(v.view.field() == Column::Release);
    v.ws.handle(Key::of(NamedKey::Tab), 0);
    CHECK(v.view.field() == Column::Position);
}

TEST_CASE("the hint menu shows what can follow keys that start longer bindings, after a pause")
{
    Viewing v;
    v.ws.setHints(true, 400);
    const auto find = [&](std::string_view keys) -> std::optional<Workspace::Hint>
    {
        for (const auto& hint : v.ws.hints().value_or(std::vector<Workspace::Hint>{}))
        {
            if (hint.keys == keys)
            {
                return hint;
            }
        }
        return std::nullopt;
    };
    v.ws.handle(Key::typed(U'g'), 1000);
    CHECK_FALSE(v.ws.hints());
    CHECK(v.ws.deadline() == 1400);
    CHECK(v.ws.wake(1400));
    REQUIRE(v.ws.hints());
    CHECK(find("a") == Workspace::Hint{"a", "Arrangement", false});
    CHECK(find("g") == Workspace::Hint{"g", "First event, or bar N", false});
    const auto frame = v.ws.frame();
    CHECK(std::ranges::any_of(frame, [](const Region& r) { return r.background == Role::Panel; }));
    v.ws.handle(Key::typed(U'g'), 1500); // gg, and the menu goes
    CHECK_FALSE(v.ws.hints());

    // The leader's, groups with their titles; going on into a group keeps it showing.
    v.ws.handle(Key::typed(U','), 2000);
    v.ws.wake(2400);
    CHECK(find("T") == Workspace::Hint{"T", "Toggles", true});
    CHECK(find("v") == Workspace::Hint{"v", "Views", true});
    CHECK_FALSE(find("n")); // the default note is the piano roll's
    v.ws.handle(Key::typed(U'T'), 2500);
    CHECK(find("<Space>") == Workspace::Hint{"<Space>", "Rewind: on", false});
    v.ws.handle(Key::of(NamedKey::Escape), 2600);
    CHECK_FALSE(v.ws.hints());

    // Off, or straight away.
    v.ws.setHints(false, 400);
    v.ws.handle(Key::typed(U'g'), 3000);
    v.ws.wake(3500);
    CHECK_FALSE(v.ws.hints());
    v.ws.handle(Key::of(NamedKey::Escape), 3600);
    v.ws.setHints(true, 0);
    v.ws.handle(Key::typed(U'g'), 4000);
    CHECK(v.ws.hints());
}

TEST_CASE("Tab at the prompt completes as zsh does: as far as the matches agree, then round them")
{
    Viewing v;
    const auto line = [&] { return encodeUtf8(v.ws.commandLine().text()); };
    const auto tab = [&] { v.ws.handle(Key::of(NamedKey::Tab), 0); };
    type(v.ws, U":ba");
    tab();
    CHECK(line() == "bar-");
    REQUIRE(v.ws.completions());
    CHECK(v.ws.completions()->matches == std::vector<std::string>{"bar-delete", "bar-insert"});
    CHECK_FALSE(v.ws.completions()->chosen);
    tab();
    CHECK(line() == "bar-delete");
    CHECK(v.ws.completions()->chosen == 0u);
    tab();
    CHECK(line() == "bar-insert");
    v.ws.handle(Key::of(NamedKey::Tab, kShift), 0);
    CHECK(line() == "bar-delete");
    v.ws.handle(Key::of(NamedKey::Up), 0); // Up and Down go through them as they look
    CHECK(line() == "bar-insert");
    v.ws.handle(Key::of(NamedKey::Down), 0);
    CHECK(line() == "bar-delete");
    v.ws.handle(Key::of(NamedKey::Down), 0); // stopping at the first, by the line
    CHECK(line() == "bar-delete");
    type(v.ws, U" "); // typing goes on from there
    CHECK_FALSE(v.ws.completions());
    v.ws.handle(Key::of(NamedKey::Escape), 0);

    // One match, with a space after it; the start of a word in a name.
    type(v.ws, U":chan");
    tab();
    CHECK(line() == "chan"); // only what does something here: change is the strip's
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    type(v.ws, U":grid 1/");
    tab();
    CHECK(line() == "grid 1/"); // and nor are its arguments, the grid being the piano roll's
    v.ws.handle(Key::of(NamedKey::Escape), 0);

    // An argument, by the kind the command says it is (the config commands, as the program
    // adds them).
    v.registry.add(configCommands());
    type(v.ws, U":highlight hint-k");
    tab();
    CHECK(line() == "highlight hint-key ");
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    type(v.ws, U":section-rename V");
    tab();
    CHECK(line() == "section-rename Verse ");
    v.ws.handle(Key::of(NamedKey::Escape), 0);

    // A source the program gives, as for settings; a setting's name goes on without a space,
    // so ? or = can follow, and so does NAME=.
    v.ws.setChoices(Choices::Setting,
                    [](std::string_view word)
                    {
                        return std::string_view{word}.find('=') == std::string_view::npos
                                   ? std::vector<std::string>{"theme", "timeoutlen"}
                                   : std::vector<std::string>{"theme=dark", "theme=light"};
                    });
    type(v.ws, U":set th");
    tab();
    CHECK(line() == "set theme");
    type(v.ws, U"=l");
    tab();
    CHECK(line() == "set theme=light ");
}

TEST_CASE("the matches of completion follow what's typed, and the arrows and pages go round them")
{
    Viewing v;
    const auto line = [&] { return encodeUtf8(v.ws.commandLine().text()); };
    const auto press = [&](NamedKey key) { v.ws.handle(Key::of(key), 0); };

    // The first nearest the line, so Up goes on through them, as Tab does, and Down back.
    type(v.ws, U":bar-insert ");
    press(NamedKey::Tab); // shown, none chosen
    CHECK(line() == "bar-insert ");
    press(NamedKey::Tab);
    CHECK(line() == "bar-insert 2/4");
    press(NamedKey::Up);
    const auto second = line();
    CHECK(second != "bar-insert 2/4");
    press(NamedKey::Down);
    CHECK(line() == "bar-insert 2/4");
    press(NamedKey::Escape);

    // Typing and deleting while they show narrows and widens them, none chosen.
    type(v.ws, U":ba");
    press(NamedKey::Tab); // bar-, as far as they agree
    REQUIRE(v.ws.completions());
    press(NamedKey::Backspace);
    REQUIRE(v.ws.completions());
    CHECK(line() == "bar");
    CHECK(v.ws.completions()->matches == std::vector<std::string>{"bar-delete", "bar-insert"});
    type(v.ws, U"-i"); // one left: the list goes, and Tab completes it
    CHECK_FALSE(v.ws.completions());
    press(NamedKey::Tab);
    CHECK(line() == "bar-insert ");
    press(NamedKey::Escape);

    // Tab on nothing shows every command, none chosen, and typing narrows them.
    type(v.ws, U":");
    press(NamedKey::Tab);
    REQUIRE(v.ws.completions());
    CHECK(line().empty());
    CHECK_FALSE(v.ws.completions()->chosen);
    CHECK(v.ws.completions()->matches.size() > 10);
    type(v.ws, U"bar-");
    REQUIRE(v.ws.completions());
    CHECK(v.ws.completions()->matches == std::vector<std::string>{"bar-delete", "bar-insert"});
    press(NamedKey::Escape);

    // With more than the popup holds, Down from none chosen starts at the top one shown,
    // where the eye starts, and goes down towards the line, stopping at the first; Up from
    // none takes the first, and stops at the last.
    type(v.ws, U":");
    press(NamedKey::Tab);
    REQUIRE(v.ws.completions());
    const auto count = v.ws.completions()->matches.size();
    REQUIRE(count > 10);
    press(NamedKey::Down);
    CHECK(v.ws.completions()->chosen == 9u);
    CHECK(v.ws.completions()->scroll == 0u); // nothing moved
    press(NamedKey::Down);
    CHECK(v.ws.completions()->chosen == 8u);
    for (int i = 0; i < 12; ++i)
    {
        press(NamedKey::Down);
    }
    CHECK(v.ws.completions()->chosen == 0u);
    press(NamedKey::Escape);
    type(v.ws, U":");
    press(NamedKey::Tab);
    press(NamedKey::Up);
    CHECK(v.ws.completions()->chosen == 0u);
    for (std::size_t i = 0; i < count + 2; ++i)
    {
        press(NamedKey::Up);
    }
    CHECK(v.ws.completions()->chosen == count - 1);
    press(NamedKey::Escape);
    // Shift-Tab from none starts at the top one shown too; Tab goes round.
    type(v.ws, U":");
    press(NamedKey::Tab);
    v.ws.handle(Key::of(NamedKey::Tab, kShift), 0);
    CHECK(v.ws.completions()->chosen == 9u);
    press(NamedKey::Escape);

    // More than the popup holds scroll, Page Up a popup of them on, to keep the one chosen
    // in view, with a ▼ for those passed.
    type(v.ws, U":");
    press(NamedKey::Tab);
    REQUIRE(v.ws.completions());
    CHECK(v.ws.completions()->scroll == 0u);
    for (int i = 0; i < 2; ++i)
    {
        press(NamedKey::PageUp);
    }
    REQUIRE(v.ws.completions());
    CHECK(v.ws.completions()->scroll > 0u);
    const auto shown = v.ws.frame();
    CHECK(std::ranges::any_of(shown,
                              [](const Region& r)
                              {
                                  return std::ranges::any_of(r.text, [](const TextRun& run)
                                                             { return run.text == U"▼"; });
                              }));
}

TEST_CASE("completion's matches are a popup above the word, the first nearest the line")
{
    Viewing v;
    type(v.ws, U":bar-insert ");
    v.ws.handle(Key::of(NamedKey::Tab), 0);
    const auto shown = v.ws.completions();
    REQUIRE(shown);
    const auto frame = v.ws.frame();
    const auto& status = frame.back().bounds;
    const auto& line = frame[frame.size() - 2];
    // The line on a row of its own, above the status line, which it leaves alone.
    CHECK(line.bounds.y + line.bounds.height == status.y);
    CHECK(encodeUtf8(line.text.front().text) == ":");
    // The popup on the line, over the word, as wide as the longest match and a cell either side.
    const auto popup = std::ranges::find_if(
        frame, [&](const Region& r)
        { return r.background == Role::Panel && r.bounds.y + r.bounds.height == line.bounds.y; });
    REQUIRE(popup != frame.end());
    const auto word = std::u32string_view{U":bar-insert "}.size();
    CHECK(popup->bounds.x == line.text.front().x + static_cast<int>(word - 1) * kCell.width);
    std::size_t widest = 0;
    for (const auto& match : shown->matches)
    {
        widest = std::max(widest, decodeUtf8(match).size());
    }
    CHECK(popup->bounds.width ==
          static_cast<int>(widest + (shown->matches.size() > 10 ? 4 : 2)) * kCell.width);
    // The first match at the bottom, the next above it.
    const auto at = [&](const std::string& text)
    {
        const auto run = std::ranges::find(popup->text, decodeUtf8(text), &TextRun::text);
        return run == popup->text.end() ? -1 : run->y;
    };
    CHECK(at(shown->matches[0]) > at(shown->matches[1]));
    CHECK(at(shown->matches[1]) >= 0);
}

TEST_CASE("a message too long for the status line is shown whole above it until the next key")
{
    Viewing v;
    v.ws.resize(70 * kCell.width, 24 * kCell.height, kCell); // room for a short one
    const auto panel = [&]() -> std::u32string
    {
        std::u32string text;
        for (const auto& region : v.ws.frame())
        {
            if (region.background == Role::Panel && region.bounds.y > 12 * kCell.height)
            {
                for (const auto& run : region.text)
                {
                    text += run.text + U"|";
                }
            }
        }
        return text;
    };
    v.ws.setMessage("Short", MessageKind::Information);
    CHECK(panel().empty());
    v.ws.setMessage("Bass's bars don't line up with bar 2; delete them on each track instead, "
                    "or give the bass the conductor's meter first",
                    MessageKind::Warning);
    const auto shown = panel();
    CHECK(shown.find(U"conductor's meter first|") != std::u32string::npos);
    CHECK(std::ranges::count(shown, U'|') >= 2); // wrapped onto lines
    // And only there, not cut short on the message row as well.
    CHECK(bottomText(v.ws.frame()).find(U"Bass's bars") == std::u32string::npos);
    v.ws.handle(Key::typed(U'j'), 0);
    CHECK(panel().empty());
}

TEST_CASE("the meters offered: built in, the song's own and those typed, in order")
{
    // One track, following the conductor's 4/4, so bars of any meter go in.
    core::Song song{core::kPpqn};
    const auto bass = song.addTrack("Bass");
    song.updateEvents(bass, [](core::EventList& events)
                      { events.insert({0, core::Note{40, 100, core::kPpqn.quarter()}}); });
    song.fitToContent();
    Viewing v{std::move(song)};
    v.ws.setRemembered({}, {});
    const auto offered = [&]
    {
        type(v.ws, U":bar-insert ");
        v.ws.handle(Key::of(NamedKey::Tab), 0);
        const auto shown = v.ws.completions();
        v.ws.handle(Key::of(NamedKey::Escape), 0);
        return shown ? shown->matches : std::vector<std::string>{};
    };
    const auto before = offered();
    CHECK(std::ranges::find(before, "13/16") == before.end());
    // Chosen, and so offered from now on, by denominator then numerator.
    REQUIRE(v.registry.runLine("bar-insert 13/16").message.empty());
    v.document.undo(); // the song without it: offered as typed, not as the song's
    const auto after = offered();
    REQUIRE(std::ranges::find(after, "13/16") != after.end());
    CHECK(after.back() == "13/16");
    CHECK(v.ws.remembered().meters().front() == core::TimeSignature{13, 16});
    // One built in is offered anyway, so isn't remembered.
    REQUIRE(v.registry.runLine("bar-insert 7/8").message.empty());
    CHECK(v.ws.remembered().meters().size() == 1);
}

TEST_CASE("any kind of event but a note can be inserted at the cursor, as one undo step")
{
    Viewing v;
    const auto id = v.document.song().tracks()[0]->id();
    const auto count = [&] { return v.document.song().events(id).size(); };
    const auto before = count();

    struct Inserted
    {
        std::string_view line;
        core::EventData data;
    };
    const std::vector<Inserted> cases{
        {"event-insert program 5", core::ProgramChange{5}},
        {"event-insert cc 7 100", core::ControlChange{7, 100}},
        {"event-insert bend -200", core::PitchBend{-200}},
        {"event-insert pressure 60", core::ChannelPressure{60}},
        {"event-insert polypressure C4 60", core::PolyPressure{60, 60}},
        {"event-insert sysex F0 7D 01 F7", core::makeSysEx({0xF0, 0x7D, 0x01, 0xF7})},
        {"event-insert lyric \"la\"", core::makeMetaText(core::MetaText::Kind::Lyric, "la")},
    };
    for (const auto& inserted : cases)
    {
        CAPTURE(inserted.line);
        const auto result = v.registry.runLine(inserted.line);
        CHECK_FALSE(result.warning);
        const auto all = v.document.song().events(id).all();
        CHECK(std::ranges::any_of(all, [&](const core::Event& event)
                                  { return event.data == inserted.data; }));
    }
    CHECK(count() == before + cases.size());
    type(v.ws, U"u");
    CHECK(count() == before + cases.size() - 1);

    for (const auto* bad :
         {"event-insert program 128", "event-insert cc 7", "event-insert nonsense 1",
          "event-insert note C4", "event-insert sysex 7D 01"})
    {
        CAPTURE(bad);
        const auto result = v.registry.runLine(bad);
        CHECK(result.warning);
        CHECK_FALSE(result.message.empty());
        CHECK(result.message.find("line ") == std::string::npos);
        CHECK(count() == before + cases.size() - 1);
    }

    // The conductor has no such events.
    v.ws.handle(Key::of(NamedKey::PageUp, kCtrl), 0);
    CHECK(v.registry.runLine("event-insert program 5").warning);
}

TEST_CASE("the key for inserting an event asks which, completing the kind")
{
    Viewing v;
    const auto id = v.document.song().tracks()[0]->id();
    const auto before = v.document.song().events(id).size();
    v.ws.setLineHandler([&](const std::string& line) { v.registry.runLine(line); });
    type(v.ws, U",e");
    CHECK(v.ws.mode() == Mode::CommandLine);
    CHECK(v.ws.choices(Choices::EventKind, "") ==
          std::vector<std::string>{"bend", "cc", "copyright", "cue", "instrument", "lyric",
                                   "polypressure", "pressure", "program", "sysex", "tempo",
                                   "text"});
    type(v.ws, U"program 7");
    v.ws.handle(Key::of(NamedKey::Enter), 0);
    CHECK(v.ws.mode() == Mode::Normal);
    REQUIRE(v.document.song().events(id).size() == before + 1);

    // Asked and left without an answer, nothing changes.
    type(v.ws, U",e");
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    CHECK(v.ws.mode() == Mode::Normal);
    CHECK(v.document.song().events(id).size() == before + 1);
}

TEST_CASE("a tempo change can be inserted at the cursor, from any track or the conductor")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    Viewing v{[&]
              {
                  core::Song song{ppqn};
                  const auto id = song.addTrack("Bass");
                  song.updateEvents(
                      id,
                      [&](core::EventList& events)
                      {
                          events.insert({0, core::Note{40, 100, ppqn.quarter()}});
                          events.insert({4 * ppqn.quarter(), core::Note{42, 100, ppqn.quarter()}});
                      });
                  return song;
              }()};
    const auto tempoAt = [&](core::Tick tick) { return v.document.song().tempo().tempoAt(tick); };
    const auto before = tempoAt(0);

    for (int i = 0; i < 8 && v.view.cursor().tick == 0; ++i)
    {
        type(v.ws, U"j"); // to the second note
    }
    const auto tick = v.view.cursor().tick;
    REQUIRE(tick == 4 * ppqn.quarter());
    const auto result = v.registry.runLine("event-insert tempo 96.5");
    CHECK_FALSE(result.warning);
    CHECK(tempoAt(tick) == core::Tempo::fromBpm(96.5));
    CHECK(tempoAt(tick - 1) == before);

    // Another at the same time replaces it, each an undo step.
    CHECK_FALSE(v.registry.runLine("event-insert tempo 100").warning);
    CHECK(tempoAt(tick) == core::Tempo::fromBpm(100));
    type(v.ws, U"u");
    CHECK(tempoAt(tick) == core::Tempo::fromBpm(96.5));
    type(v.ws, U"u");
    CHECK(tempoAt(tick) == before);

    for (const auto* bad : {"event-insert tempo", "event-insert tempo 0", "event-insert tempo 1001",
                            "event-insert tempo fast", "event-insert tempo 96 bpm"})
    {
        CAPTURE(bad);
        CHECK(v.registry.runLine(bad).message == "A tempo must be from 1 to 1000 BPM");
        CHECK(tempoAt(tick) == before);
    }

    // The conductor takes it, though nothing else.
    v.ws.handle(Key::of(NamedKey::PageUp, kCtrl), 0);
    REQUIRE_FALSE(v.view.cursor().track);
    CHECK_FALSE(v.registry.runLine("event-insert tempo 80").warning);
    CHECK(tempoAt(v.view.cursor().tick) == core::Tempo::fromBpm(80));
    CHECK(v.registry.runLine("event-insert program 5").warning);
}

TEST_CASE("the hint menu says what each toggle is set to now")
{
    Viewing v;
    v.ws.setHints(true, 0);
    const auto titles = [&]
    {
        std::vector<std::string> found;
        for (const auto& hint : v.ws.hints().value_or(std::vector<Workspace::Hint>{}))
        {
            found.push_back(hint.title);
        }
        return found;
    };
    const auto has = [&](std::string_view title)
    {
        const auto all = titles();
        return std::ranges::find(all, title) != all.end();
    };
    type(v.ws, U",T");
    CHECK(has("Rewind: on"));
    CHECK(has("Quantise marks: shown"));
    CHECK(has("O moves on by: note length"));
    type(v.ws, U" ");
    type(v.ws, U",T");
    CHECK(has("Rewind: off"));
    v.ws.handle(Key::of(NamedKey::Escape), 0);

    type(v.ws, U",t");
    CHECK(has("Mute this track: off"));
    CHECK(has("Quantise this track: off"));
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    type(v.ws, U",tm,t");
    CHECK(has("Mute this track: on"));
    v.ws.handle(Key::of(NamedKey::Escape), 0);

    // Recording's, saying they switch (decided after use).
    type(v.ws, U",r");
    CHECK(has("Toggle arming this track: off"));
    CHECK(has("Toggle Thru to this track: on"));
    CHECK(has("Toggle replace/overdub: replace"));
    v.ws.handle(Key::of(NamedKey::Escape), 0);
    type(v.ws, U",rm,r");
    CHECK(has("Toggle replace/overdub: overdub"));
    v.ws.handle(Key::of(NamedKey::Escape), 0);
}
