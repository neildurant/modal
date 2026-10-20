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
