#include <modal/core/document.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/event_list_view.hpp>
#include <modal/ui/window_commands.hpp>

#include <catch2/catch_test_macros.hpp>

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
    song.setMarker(2 * q, "fill");
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

// A view of a song in a window of 80 by 24 cells: 22 rows between the bars, the first the
// headings, so 21 rows of events. With its registry, the window's commands described there.
struct View
{
    explicit View(core::Song song = twoTracks())
        : document{std::move(song)}
    {
        registry.add(windowCommands());
        view.resize(80 * kCell.width, 24 * kCell.height, kCell);
    }

    core::Document document;
    CommandRegistry registry;
    EventListView view{registry, document};
};

void type(EventListView& view, std::u32string_view keys)
{
    for (const char32_t key : keys)
    {
        view.handle(Key::typed(key), 0);
    }
}

// A row of a region, as text, from the text runs on that row.
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

core::Note bassNote(const View& v, std::size_t index)
{
    return std::get<core::Note>(
        v.document.song().events(v.document.song().tracks()[0]->id())[index].data);
}

void enter(EventListView& view)
{
    view.handle(Key::of(NamedKey::Enter), 0);
}

} // namespace

TEST_CASE("the event list opens on track 1, at its first row")
{
    View v;
    CHECK(v.view.track() == 1);
    CHECK(v.view.row() == 0);
    CHECK(v.view.field() == Column::Position);

    const auto frame = v.view.frame();
    REQUIRE(frame.size() == 3);
    CHECK(contains(rowText(frame[1], 0), U"1 Bass"));
    CHECK(contains(rowText(frame[1], 0), U"Pro-800"));
    CHECK(contains(rowText(frame[1], 0), U"Song · bars 1–1"));
    const auto list = frame[0];
    CHECK(contains(rowText(list, 0), U"Position"));
    CHECK(contains(rowText(list, 0), U"Release"));
    CHECK(contains(rowText(list, 5), U"note"));
    CHECK(contains(rowText(list, 5), U"E1 (28)"));
    CHECK(contains(rowText(list, 5), U"1/8"));
    CHECK(contains(rowText(frame[2], 0), U"NORMAL"));
    CHECK(contains(rowText(frame[2], 0), U"1:1:000  4/4  Song"));

    // The conductor's rows are dimmed; the cursor's field is in the cursor's colours.
    CHECK(styleAt(list, 2, 1).background == Role::Cursor);
    CHECK(styleAt(list, 14, 1).foreground == Role::Dim);
    CHECK(styleAt(list, 14, 4).foreground == Role::Text);

    // A song with no tracks opens on the conductor's list.
    View empty{core::Song{}};
    CHECK(empty.view.track() == 0);
}

TEST_CASE("j and k move between rows, h and l between fields, with counts")
{
    View v;
    type(v.view, U"4j"); // to the first note
    CHECK(v.view.row() == 4);
    type(v.view, U"2l");
    CHECK(v.view.field() == Column::Value);
    type(v.view, U"$");
    CHECK(v.view.field() == Column::Release);
    type(v.view, U"0");
    CHECK(v.view.field() == Column::Position);

    // The field kept to as rows change, shown on the last field before it where a row has
    // fewer.
    type(v.view, U"$k");
    CHECK(v.view.row() == 3); // the program change
    CHECK(v.view.field() == Column::Data);
    type(v.view, U"j");
    CHECK(v.view.field() == Column::Release);

    // Dimmed rows are visited too.
    type(v.view, U"k3k");
    CHECK(v.view.row() == 0);
    type(v.view, U"99j");
    CHECK(v.view.row() == v.view.list().rows().size() - 1); // the end row
    type(v.view, U"99k");
    CHECK(v.view.row() == 0);
}

TEST_CASE("gg goes to the first row and G to the end, or with a count to that bar")
{
    View v;
    type(v.view, U"G");
    CHECK(v.view.list().rows()[v.view.row()].kind == RowKind::End);
    type(v.view, U"gg");
    CHECK(v.view.row() == 0);
    type(v.view, U"2G");
    CHECK(v.view.list().rows()[v.view.row()].tick == 4 * q);
    type(v.view, U"1gg");
    CHECK(v.view.row() == 0);
}

TEST_CASE("time motions go to the event at or after where they land, in the track's bars")
{
    // The bass's rows: 0 section, 1 meter, 2 tempo, 3 program and 4 note at 1:1:000, 5 cc at
    // 1:2:240, 6 marker at 1:3:000, 7 section and 8 note at 2:1:000, and 9 the end.
    View v;
    type(v.view, U"4j");
    type(v.view, U"w"); // the beat after is 1:2:000; the next event, the cc
    CHECK(v.view.row() == 5);
    type(v.view, U"w");
    CHECK(v.view.row() == 6);
    type(v.view, U"gg4j2w");
    CHECK(v.view.row() == 6); // a count of beats, then the event at or after
    type(v.view, U"}");       // a bar on from 1:3:000 is 2:3:000, with nothing after but the end
    CHECK(v.view.row() == 9);
    type(v.view, U"gg}");
    CHECK(v.view.row() == 7); // a bar on from 1:1:000, the verse's section row

    // Going back keeps going until it's somewhere earlier: from the note at 2:1:000, a beat
    // back is 1:4:000, whose event at or after is no earlier, so another beat, and another.
    type(v.view, U"j");
    REQUIRE(v.view.row() == 8);
    type(v.view, U"b");
    CHECK(v.view.row() == 6);
    type(v.view, U"G[[");
    CHECK(v.view.row() == 7); // the bar line before the end is 2:1:000
    type(v.view, U"[[");
    CHECK(v.view.row() == 0);
    type(v.view, U"]]");
    CHECK(v.view.row() == 7);
    type(v.view, U"{");
    CHECK(v.view.row() == 0);
    type(v.view, U"b");
    CHECK(v.view.row() == 0); // nowhere earlier to go
}

TEST_CASE("( and ) go by chords: events starting within the chord window of each other")
{
    View v;
    // The program and the note at 1:1:000 are one chord, then the cc, then the verse's note.
    type(v.view, U")");
    CHECK(v.view.row() == 3);
    type(v.view, U")");
    CHECK(v.view.row() == 5);
    type(v.view, U"2)");
    CHECK(v.view.row() == 9); // past the last, the end
    type(v.view, U"(");
    CHECK(v.view.row() == 8);
    type(v.view, U"k(");
    CHECK(v.view.row() == 5); // from inside a chord, its start first
    type(v.view, U"gg(");
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
    View chord{song};
    type(chord.view, U")");
    CHECK(chord.view.list().rows()[chord.view.row()].tick == 0);
    type(chord.view, U")");
    CHECK(chord.view.list().rows()[chord.view.row()].tick == q);
    chord.view.setChordWindow(std::chrono::milliseconds{5});
    type(chord.view, U"gg))");
    CHECK(chord.view.list().rows()[chord.view.row()].tick == 20);
}

TEST_CASE("section and marker motions go to their rows")
{
    View v;
    type(v.view, U"]s");
    CHECK(v.view.row() == 7);
    type(v.view, U"j[s");
    CHECK(v.view.row() == 0); // from the verse's first note, at its start, the section before
    type(v.view, U"6j[s");
    CHECK(v.view.row() == 0); // from inside a section, its start
    type(v.view, U"]m");
    CHECK(v.view.row() == 6);
    type(v.view, U"G[m");
    CHECK(v.view.row() == 6);
    type(v.view, U"]m");
    CHECK(v.view.row() == 6); // none after: it stays
}

TEST_CASE("dd deletes the event under the cursor, x too, and u brings it back where it was")
{
    // The bass's rows: 0 section, 1 meter, 2 tempo, 3 program and 4 note at 1:1:000, 5 cc at
    // 1:2:240, 6 marker at 1:3:000, 7 section and 8 note at 2:1:000, and 9 the end.
    View v;
    const auto events = [&]
    { return v.document.song().events(v.document.song().tracks()[0]->id()); };
    REQUIRE(events().size() == 4);

    type(v.view, U"4jdd"); // the note at 1:1:000
    CHECK(events().size() == 3);
    CHECK(v.view.row() == 4); // on what followed, the cc
    CHECK(v.view.registerEvents().size() == 1);
    CHECK(v.document.modified());

    type(v.view, U"G");
    type(v.view, U"u");
    CHECK(events().size() == 4);
    CHECK(v.view.row() == 4); // back on the note, though the section is first at its time
    CHECK(v.view.message() == "Undid: Delete 1 event in Bass");
    CHECK_FALSE(v.document.modified());
    v.view.handle(Key::typed(U'r', kCtrl), 0);
    CHECK(events().size() == 3);
    CHECK(v.view.message() == "Redid: Delete 1 event in Bass");
    type(v.view, U"uu");
    CHECK(v.view.message() == "Nothing to undo");

    type(v.view, U"gg3j2x"); // the program and the note
    CHECK(events().size() == 2);
    type(v.view, U"u");

    // On a row that isn't the track's, there's nothing to delete.
    type(v.view, U"ggdd");
    CHECK(events().size() == 4);
    CHECK_FALSE(v.document.modified());
}

TEST_CASE("d and y take a motion: whole rows for j, k, gg and G, up to the row for the others")
{
    View v;
    const auto events = [&]
    { return v.document.song().events(v.document.song().tracks()[0]->id()); };

    type(v.view, U"3jdj"); // the program and the note
    CHECK(events().size() == 2);
    type(v.view, U"u");

    type(v.view, U"gg4jd}"); // from the note up to the verse's section row: the note and the cc
    CHECK(events().size() == 2);
    CHECK(events()[0].tick == 0); // the program is left
    type(v.view, U"u");

    type(v.view, U"Gkd{"); // back from the verse's note to 1:1:000: every event before it
    CHECK(events().size() == 1);
    CHECK(events()[0].tick == 4 * q);
    type(v.view, U"u");

    // Counts before and after the operator multiply: 2d2j from the program takes 5 rows.
    type(v.view, U"gg3j2d2j");
    CHECK(events().size() == 1);
    type(v.view, U"u");

    type(v.view, U"gg4jy}");
    CHECK(v.view.message() == "2 events yanked");
    CHECK(events().size() == 4);
    CHECK(v.view.registerEvents().size() == 2);
    CHECK(v.view.registerEvents()[1].tick == q + 240);
}

TEST_CASE("p puts the events at the cursor's time, never overlapping notes")
{
    View v;
    const auto events = [&]
    { return v.document.song().events(v.document.song().tracks()[0]->id()); };
    type(v.view, U"p");
    CHECK(v.view.message() == "Nothing yanked or deleted to put");

    type(v.view, U"4jyy"); // the E1 at 1:1:000
    type(v.view, U"4jp");  // at 2:1:000, beside the F1 there
    CHECK(events().size() == 5);
    CHECK(events()[4].tick == 4 * q);
    CHECK(std::get<core::Note>(events()[4].data).pitch == 28);

    // The F1 put where an F1 starts replaces it, rather than doubling it.
    type(v.view, U"u");
    type(v.view, U"Gkyyp");
    CHECK(events().size() == 4);
}

TEST_CASE("an operator waits for its motion, and Escape or another key gives it up")
{
    View v;
    type(v.view, U"2d3");
    CHECK(v.view.mode() == Mode::OperatorPending);
    CHECK(v.view.pending() == "2d3");
    v.view.handle(Key::of(NamedKey::Escape), 0);
    CHECK(v.view.mode() == Mode::Normal);
    CHECK(v.view.pending().empty());
    type(v.view, U"dQ"); // no motion
    CHECK(v.view.mode() == Mode::Normal);
    CHECK(v.document.song().events(v.document.song().tracks()[0]->id()).size() == 4);
}

TEST_CASE("the conductor's list edits its names, tempos and meters, and deletes changes")
{
    // The conductor's rows: 0 section Song, 1 meter, 2 tempo, 3 marker at 1:3:000, 4 section
    // Verse at 2:1:000, and 5 the end.
    View v;
    const auto& song = [&]() -> const core::Song& { return v.document.song(); };
    type(v.view, U"gT");
    REQUIRE(v.view.track() == 0);

    type(v.view, U"3jl"); // the marker's name
    enter(v.view);
    CHECK(v.view.fieldLine().text() == U"fill");
    type(v.view, U"fix");
    enter(v.view);
    CHECK(song().markers()[0].name == "fix");
    type(v.view, U"u");
    CHECK(v.view.message() == "Undid: Rename marker fill to fix");

    type(v.view, U"j"); // the verse's name
    enter(v.view);
    type(v.view, U"Chorus");
    enter(v.view);
    CHECK(song().sections()[1]->name() == "Chorus");

    type(v.view, U"gg2j"); // the tempo at the start
    enter(v.view);
    CHECK(v.view.fieldLine().text() == U"120");
    type(v.view, U"140");
    enter(v.view);
    CHECK(song().tempo().tempoAt(0) == core::Tempo::fromBpm(140));
    type(v.view, U"+");
    CHECK(song().tempo().tempoAt(0) == core::Tempo::fromBpm(141));
    enter(v.view);
    type(v.view, U"1001");
    enter(v.view);
    CHECK(v.view.message().starts_with("A tempo must be from 1 to 1000 BPM"));
    v.view.handle(Key::of(NamedKey::Escape), 0);

    type(v.view, U"k"); // the meter at the start
    enter(v.view);
    type(v.view, U"3/4");
    enter(v.view);
    CHECK(song().meter().signatureAt(0) == core::TimeSignature{3, 4});
    CHECK(v.view.message().empty());
    // The intro's four beats are now a 3/4 bar and a 1/4 remainder bar, which leaves no room
    // for a whole 4/4 bar at the start: it says so, and changes nothing.
    type(v.view, U"+");
    CHECK(v.view.message() == "The meter change at 2:1:000 leaves no room for a whole 4/4 bar "
                              "here; change or delete it first");
    CHECK(song().meter().signatureAt(0) == core::TimeSignature{3, 4});
    type(v.view, U"u");
    CHECK(song().meter().signatureAt(0) == core::TimeSignature{4, 4});

    // A section's own tempo and meter, and the section, can't be deleted; a marker can.
    type(v.view, U"gg3dd");
    CHECK(v.view.message().starts_with("Every section keeps its tempo and meter at its start"));
    type(v.view, U"gg]m");
    REQUIRE(v.view.list().rows()[v.view.row()].kind == RowKind::Marker);
    type(v.view, U"dd");
    CHECK(song().markers().empty());

    // A tempo change inside a section can.
    v.document.edit("Add a tempo change", {}, [](core::Song& s)
                    { s.updateTempo([](core::TempoMap& t) { t.set(q, core::Tempo{400'000}); }); });
    v.view.refresh();
    type(v.view, U"gg3j");
    REQUIRE(v.view.list().rows()[v.view.row()].kind == RowKind::Tempo);
    type(v.view, U"x");
    CHECK(song().tempo().changes().size() == 1);
    CHECK(v.view.message().empty());

    // Its positions, and yanking and putting there, wait.
    type(v.view, U"0");
    enter(v.view);
    CHECK(v.view.message() == "Moving the conductor's changes comes later");
    type(v.view, U"yy");
    CHECK(v.view.message() == "Only a track's events can be yanked");
    type(v.view, U"p");
    CHECK(v.view.message() == "Only a track's events can be put");
}

TEST_CASE("Enter edits a field in place: typing replaces it, and Enter sets it")
{
    View v;
    type(v.view, U"4j2l"); // the E1's velocity, 96
    enter(v.view);
    CHECK(v.view.editingField());
    CHECK(v.view.mode() == Mode::CommandLine);
    CHECK(v.view.fieldLine().text() == U"96");
    CHECK(contains(rowText(v.view.frame()[2], 0), U"EDIT"));
    type(v.view, U"80");
    CHECK(v.view.fieldLine().text() == U"80");
    enter(v.view);
    CHECK_FALSE(v.view.editingField());
    CHECK(bassNote(v, 1).velocity == 80);
    type(v.view, U"u");
    CHECK(v.view.message() == "Undid: Set velocity in Bass");

    // A change; keys that edit keep the value.
    enter(v.view);
    type(v.view, U"+10");
    enter(v.view);
    CHECK(bassNote(v, 1).velocity == 106);
    enter(v.view);
    v.view.handle(Key::of(NamedKey::Backspace), 0);
    type(v.view, U"0");
    CHECK(v.view.fieldLine().text() == U"100");
    enter(v.view);
    CHECK(bassNote(v, 1).velocity == 100);

    // A value out of range keeps the prompt open to put right; Escape leaves it as it was.
    enter(v.view);
    type(v.view, U"200");
    enter(v.view);
    CHECK(v.view.editingField());
    CHECK(v.view.message().starts_with("A velocity must be from 1 to 127"));
    v.view.handle(Key::of(NamedKey::Escape), 0);
    CHECK_FALSE(v.view.editingField());
    CHECK(bassNote(v, 1).velocity == 100);
}

TEST_CASE("editing a note's pitch, length and position, never into another of its pitch")
{
    View v;
    type(v.view, U"4jl"); // the E1's pitch, shown as E1 (28), edited as E1
    enter(v.view);
    CHECK(v.view.fieldLine().text() == U"E1");
    type(v.view, U"F1");
    enter(v.view);
    CHECK(bassNote(v, 1).pitch == 29);

    type(v.view, U"2l"); // its length, 0:0:480 (1/8), edited as 0:0:480
    enter(v.view);
    CHECK(v.view.fieldLine().text() == U"0:0:480");
    type(v.view, U"+1/16");
    enter(v.view);
    CHECK(bassNote(v, 1).duration == 720);

    // Moved to where the other F1 starts: refused, naming it.
    type(v.view, U"0");
    enter(v.view);
    type(v.view, U"2:1:000");
    enter(v.view);
    CHECK(v.view.editingField());
    CHECK(v.view.message() == "Would overlap the F1 at 2:1:000");
    v.view.handle(Key::of(NamedKey::Escape), 0);

    // Moved a beat on instead, the cursor following it.
    enter(v.view);
    type(v.view, U"+0:1:000");
    enter(v.view);
    CHECK(v.view.list().rows()[v.view.row()].tick == q);
    CHECK(v.view.list().text(v.view.row())[Column::Data] == "F1 (29)");
}

TEST_CASE("+ and - step the field, with counts, as Ctrl-A and Ctrl-X do")
{
    View v;
    type(v.view, U"4j2l");
    type(v.view, U"+");
    CHECK(bassNote(v, 1).velocity == 97);
    type(v.view, U"5-");
    CHECK(bassNote(v, 1).velocity == 92);
    v.view.handle(Key::typed(U'a', kCtrl), 0);
    CHECK(bassNote(v, 1).velocity == 93);
    type(v.view, U"200");
    v.view.handle(Key::typed(U'x', kCtrl), 0);
    CHECK(bassNote(v, 1).velocity == 1); // kept in range
    type(v.view, U"h-");                 // the pitch, a semitone down
    CHECK(bassNote(v, 1).pitch == 27);
}

TEST_CASE("o adds a note at the cursor's time once its pitch is typed")
{
    View v;
    const auto events = [&]
    { return v.document.song().events(v.document.song().tracks()[0]->id()); };
    type(v.view, U"5jo"); // on the cc at 1:2:240
    CHECK(v.view.editingField());
    CHECK(v.view.fieldLine().text().empty());
    // The new row shows after the cursor's, with the velocity and length of the note before.
    const auto list = v.view.frame()[0];
    CHECK(contains(rowText(list, 7), U"1:2:240"));
    CHECK(contains(rowText(list, 7), U"note"));
    CHECK(contains(rowText(list, 7), U"96"));
    CHECK(events().size() == 4);

    enter(v.view); // no pitch yet
    CHECK(v.view.message() == "Type the note's pitch, such as C4 or 60");
    type(v.view, U"G2");
    enter(v.view);
    CHECK_FALSE(v.view.editingField());
    REQUIRE(events().size() == 5);
    CHECK(events()[3].tick == q + 240);
    CHECK(std::get<core::Note>(events()[3].data) == core::Note{43, 96, q / 2, 64});
    CHECK(v.view.list().text(v.view.row())[Column::Data] == "G2 (43)"); // on it
    CHECK(v.view.field() == Column::Data);
    type(v.view, U"u");
    CHECK(v.view.message() == "Undid: Add a note in Bass");
    CHECK(events().size() == 4);

    // Escape leaves nothing behind, and a pitch already sounding there is refused.
    type(v.view, U"o");
    v.view.handle(Key::of(NamedKey::Escape), 0);
    CHECK(events().size() == 4);
    type(v.view, U"ggo");
    type(v.view, U"E1");
    enter(v.view);
    CHECK(v.view.message() == "Would overlap the E1 at 1:1:000");
    CHECK(v.view.editingField());
}

TEST_CASE("only the track's own events are edited in its list")
{
    View v;
    enter(v.view); // the section row
    CHECK_FALSE(v.view.editingField());
    CHECK(v.view.message() == "That's the conductor's: edit it in its list (gT)");
    type(v.view, U"G");
    enter(v.view);
    CHECK(v.view.message() == "Nothing to edit here");
    type(v.view, U"+");
    CHECK(v.view.message() == "Nothing to change here");
}

TEST_CASE("gt and gT go through the tracks, the conductor as track 0, keeping the time")
{
    View v;
    type(v.view, U"7j"); // the verse's section row, at 2:1:000
    REQUIRE(v.view.list().rows()[v.view.row()].tick == 4 * q);
    type(v.view, U"gt");
    CHECK(v.view.track() == 2);
    CHECK(v.view.list().rows()[v.view.row()].tick == 4 * q);
    CHECK(contains(rowText(v.view.frame()[1], 0), U"2 Lead"));
    type(v.view, U"gt");
    CHECK(v.view.track() == 0); // round to the conductor
    CHECK(contains(rowText(v.view.frame()[1], 0), U"0 Conductor"));
    type(v.view, U"gt");
    CHECK(v.view.track() == 1);
    type(v.view, U"gT");
    CHECK(v.view.track() == 0);
    type(v.view, U"gT");
    CHECK(v.view.track() == 2);
    type(v.view, U"1gt"); // a count goes to that track
    CHECK(v.view.track() == 1);
    type(v.view, U"0gt");
    CHECK(v.view.track() == 2); // 0 is a motion, then gt
}

TEST_CASE("the event list scrolls by half and whole pages, and Ctrl with j and k keep place")
{
    View v{demoSong()};
    REQUIRE(v.view.viewRows() == 21);
    v.view.handle(Key::typed(U'd', kCtrl), 0);
    CHECK(v.view.row() == 10);
    CHECK(v.view.top() == 10);
    v.view.handle(Key::typed(U'f', kCtrl), 0);
    CHECK(v.view.row() == 29); // a page less two rows, as in Vim
    CHECK(v.view.top() == 29);
    v.view.handle(Key::typed(U'u', kCtrl), 0);
    CHECK(v.view.top() == 19);

    v.view.handle(Key::typed(U'j', kCtrl), 0);
    CHECK(v.view.row() == 20);
    CHECK(v.view.top() == 20);
    type(v.view, U"zz");
    CHECK(v.view.top() == 10); // row 20 in the middle of 21
    type(v.view, U"gg");
    v.view.handle(Key::typed(U'k', kCtrl), 0); // the view stops at the top
    CHECK(v.view.top() == 0);
    CHECK(v.view.row() == 0);

    // The cursor stays in view as it moves.
    type(v.view, U"30j");
    CHECK(v.view.top() == 10);
}

TEST_CASE("a note's length shows as bars, beats and ticks, with the value it's close to")
{
    View v;
    type(v.view, U"4j");
    CHECK(v.view.list().text(v.view.row())[Column::Length] == "0:0:480 (1/8)");
    type(v.view, U"4j"); // the verse's note
    CHECK(v.view.list().text(v.view.row())[Column::Length] == "0:0:492 (≈1/8)");
}

TEST_CASE("the list shows the song as it is after a change, at the same time")
{
    View v;
    type(v.view, U"8j"); // the verse's note
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
    commands.add(EventListView::commands());
    commands.add(windowCommands());
    ConfigReader reader{config, commands};
    reader.runText(builtInModalrc(), "built-in modalrc", {});
    CHECK(reader.errors().empty());

    // And every one of its commands is one the view runs.
    View v;
    for (const auto& command : EventListView::commands())
    {
        CAPTURE(command.name);
        const auto result = v.view.run(command.name, 0);
        CHECK((!result.warning || result.message.find("works only in") != std::string::npos));
    }
}

TEST_CASE("a count shows until a key uses it or Escape gives it up")
{
    View v;
    CHECK(v.view.handle(Key::typed(U'4'), 0));
    CHECK(v.view.handle(Key::typed(U'0'), 0));
    CHECK(v.view.pending() == "40");
    CHECK(contains(rowText(v.view.frame()[2], 0), U"40"));
    CHECK(v.view.handle(Key::of(NamedKey::Escape), 0));
    CHECK(v.view.pending().empty());
    type(v.view, U"5x"); // a key with no binding gives the count up too
    CHECK(v.view.pending().empty());
    type(v.view, U"j");
    CHECK(v.view.row() == 1);
}

TEST_CASE("a message shows in the status line until the next key")
{
    View v;
    v.view.setMessage("modalrc:3: unknown setting", EventListView::MessageKind::Warning);
    CHECK(contains(rowText(v.view.frame()[2], 0), U"modalrc:3: unknown setting"));
    CHECK(v.view.handle(Key::typed(U'x'), 0)); // unbound, but the message goes
    CHECK(v.view.message().empty());
}

TEST_CASE("keys run the registry's other commands too, such as the window's, showing messages")
{
    View v;
    std::vector<int> counts;
    v.registry.add(windowCommands()[0],
                   [&](const CommandCall& call)
                   {
                       counts.push_back(call.count);
                       return CommandResult{true, "bigger", false};
                   });
    CHECK(v.view.handle(Key::typed(U'=', kCtrl), 0));
    CHECK(v.view.message() == "bigger");
    v.view.handle(Key::typed(U'3'), 0);
    v.view.handle(Key::of(NamedKey::KeypadPlus, kCtrl), 0);
    CHECK(counts == std::vector<int>{0, 3});
    v.view.handle(Key::typed(U'-', kCtrl), 0);
    CHECK(v.view.message() == "font-smaller can't be run here");
}

TEST_CASE("colon opens a prompt where keys are typed, and Enter hands the line on")
{
    View v;
    std::vector<std::string> lines;
    v.view.setLineHandler([&](const std::string& line) { lines.push_back(line); });

    type(v.view, U":set fontsize=14j"); // digits and j are typed, not counted or moved
    CHECK(v.view.mode() == Mode::CommandLine);
    CHECK(v.view.commandLine().text() == U"set fontsize=14j");
    CHECK(v.view.row() == 0);
    v.view.handle(Key::of(NamedKey::Backspace), 0);
    CHECK(contains(rowText(v.view.frame()[2], 0), U":set fontsize=14"));
    v.view.handle(Key::of(NamedKey::Enter), 0);
    CHECK(v.view.mode() == Mode::Normal);
    CHECK(lines == std::vector<std::string>{"set fontsize=14"});

    type(v.view, U":");
    v.view.handle(Key::of(NamedKey::Up), 0);
    CHECK(v.view.commandLine().text() == U"set fontsize=14");
    v.view.handle(Key::of(NamedKey::Escape), 0);
    CHECK(v.view.mode() == Mode::Normal);
    CHECK(lines.size() == 1);
}
