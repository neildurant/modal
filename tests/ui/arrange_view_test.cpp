// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/demo_song.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>

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

// The demo song in a workspace of 120 by 30 cells, in the arrangement.
struct Arranging
{
    explicit Arranging(core::Song song = demoSong())
        : document{std::move(song)}
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
    void press(const Key& key) { ws.handle(key, 0); }

    const ArrangePage& page() const { return ws.arrangement().page(); }
    const core::MeterMap& meterOf(std::size_t track) const
    {
        return document.song().meter(document.song().tracks()[track]->id());
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

    core::Document document;
    CommandRegistry registry;
    Workspace ws{registry, document};
};

bool contains(const std::u32string& text, std::u32string_view part)
{
    return text.find(part) != std::u32string::npos;
}

} // namespace

TEST_CASE("the arrangement's cursor is a bar of a track, moved by bars and tracks")
{
    Arranging a;
    REQUIRE(a.ws.shown() == ViewKind::Arrangement);
    CHECK(a.page().track() == 0);
    CHECK(a.page().bar() == 1);
    CHECK(contains(a.status(), U"1:1:000"));

    a.type(U"3l");
    CHECK(a.page().bar() == 4);
    a.type(U"h");
    CHECK(a.page().bar() == 3);
    a.type(U"2j");
    CHECK(a.page().track() == 2);
    CHECK(a.page().time() == a.meterOf(0).barStart(3)); // keeping the time
    a.type(U"k");
    CHECK(a.page().track() == 1);
    a.type(U"G");
    CHECK(a.page().bar() == a.meterOf(1).toBarBeatTick(a.document.song().end() - 1).bar);
    a.type(U"gg");
    CHECK(a.page().bar() == 1);
    a.type(U"5gg");
    CHECK(a.page().bar() == 5);
    a.type(U"$");
    CHECK(a.page().bar() == a.meterOf(1).toBarBeatTick(a.document.song().end() - 1).bar);
    a.type(U"0");
    CHECK(a.page().bar() == 1);

    // The view's bar names the cursor's track, and the inspector its bar.
    const auto frame = a.ws.frame();
    std::u32string bar;
    for (const auto& run : frame[frame.size() - 3].text)
    {
        bar += run.text;
    }
    CHECK(contains(bar, U"Track 2:"));
    std::u32string inspector;
    for (const auto& run : frame[frame.size() - 2].text)
    {
        inspector += run.text + U" ";
    }
    CHECK(contains(inspector, U"Bar"));
    CHECK(contains(inspector, U"1 of"));
}

TEST_CASE("zi, zo and z0 zoom the arrangement")
{
    Arranging a;
    const auto start = a.page().zoom();
    a.type(U"zo");
    CHECK(a.page().zoom() == start + 1);
    a.type(U"2zo");
    CHECK(a.page().zoom() == start + 3);
    a.type(U"zi");
    CHECK(a.page().zoom() == start + 2);
    a.type(U"z0");
    CHECK(a.page().zoom() == start);
}

TEST_CASE("ga, ge and Backspace go between the views, keeping the cursor")
{
    Arranging a;
    a.type(U"j4l"); // track 2, bar 5
    const auto tick = a.page().time();
    const auto id = a.document.song().tracks()[1]->id();

    a.type(U"ge");
    REQUIRE(a.ws.shown() == ViewKind::EventList);
    const auto& list = a.ws.eventList();
    CHECK(list.track() == 2);
    CHECK(list.list().rows()[list.row()].tick >= tick);

    // Moving in the event list moves the shared cursor; the arrangement shows its bar.
    a.type(U"}");
    const auto moved = list.list().rows()[list.row()].tick;
    a.press(Key::of(NamedKey::Backspace));
    REQUIRE(a.ws.shown() == ViewKind::Arrangement);
    CHECK(a.page().track() == 1);
    CHECK(a.page().bar() == a.meterOf(1).toBarBeatTick(moved).bar);
    CHECK(a.ws.arrangement().cursor().track == id);

    // Back again: the history went arrangement, event list; Backspace from the arrangement now
    // has nowhere to go but the view before it, which it left.
    a.press(Key::of(NamedKey::Backspace));
    CHECK(a.ws.shown() == ViewKind::EventList);
    a.press(Key::of(NamedKey::Backspace));
    CHECK(a.ws.shown() == ViewKind::EventList);
    CHECK(a.ws.message() == "No view to go back to");
}

TEST_CASE("coming back to the event list lands on the note it left")
{
    // A chord: three notes at one time, so only the pitch says which row the cursor was on.
    core::Song song{core::kPpqn};
    const auto keys = song.addTrack("Keys");
    song.updateEvents(keys,
                      [](core::EventList& events)
                      {
                          for (const int pitch : {60, 64, 67})
                          {
                              events.insert(
                                  {core::kPpqn.quarter(),
                                   core::Note{static_cast<std::uint8_t>(pitch), 100, 100}});
                          }
                      });
    Arranging a{std::move(song)};
    a.type(U"ge");
    const auto& list = a.ws.eventList();
    a.type(U"gg");
    while (list.list().rows()[list.row()].kind != RowKind::Event)
    {
        a.type(U"j");
    }
    a.type(U"j"); // the second note of the chord, E4
    const auto second = list.row();
    a.type(U"ga");
    REQUIRE(a.ws.shown() == ViewKind::Arrangement);
    a.type(U"ge");
    CHECK(list.row() == second);
}

TEST_CASE("Enter on a bar opens it in the piano roll")
{
    Arranging a;
    a.type(U"2j3l");
    const auto tick = a.page().time();
    a.press(Key::of(NamedKey::Enter));
    REQUIRE(a.ws.shown() == ViewKind::PianoRoll);
    CHECK(a.ws.pianoRoll().track() == 2);
    CHECK(a.ws.pianoRoll().time() == tick);
}

TEST_CASE("a command a view doesn't have says so")
{
    Arranging a;
    a.type(U"=");
    CHECK(a.ws.message() == "Field increase isn't in the arrangement");
    a.type(U"ge");
    a.type(U"zi");
    CHECK(a.ws.message() == "Zoom in isn't in the event list");
}

TEST_CASE(":section starts a section at the cursor's bar in the arrangement too")
{
    core::Song song{core::kPpqn}; // one section of eight bars of 4/4
    song.addTrack("Bass");
    Arranging a{std::move(song)};
    a.type(U"2l"); // bar 3
    CHECK(a.registry.runLine("section Chorus") ==
          CommandResult{true, "Chorus starts at 3:1:000", false});
    REQUIRE(a.document.song().sections().size() == 2);
    CHECK(a.document.song().sections()[1]->name() == "Chorus");
    CHECK(a.document.song().sectionStart(1) == 2 * core::kPpqn.whole());
    CHECK(a.ws.shown() == ViewKind::Arrangement);
    CHECK(a.page().bar() == 3); // the cursor stays
}

namespace
{

// Two tracks of eight bars of 4/4: Bass with a note starting in each of bars 1 to 4, the one
// in bar 2 held into bar 3, and Lead with a note in bar 2.
core::Song operatorSong()
{
    core::Song song{core::kPpqn};
    const auto bass = song.addTrack("Bass");
    const auto lead = song.addTrack("Lead");
    const auto bar = core::kPpqn.whole();
    const auto q = core::kPpqn.quarter();
    song.updateEvents(bass,
                      [&](core::EventList& events)
                      {
                          events.insert({0, core::Note{40, 100, q}});
                          events.insert({bar + 2 * q, core::Note{41, 100, bar}}); // into bar 3
                          events.insert({2 * bar, core::Note{42, 100, q}});
                          events.insert({3 * bar + q, core::Note{43, 100, q}});
                      });
    song.updateEvents(lead, [&](core::EventList& events)
                      { events.insert({bar, core::Note{72, 90, q}}); });
    return song;
}

std::vector<core::Tick> ticksOf(const core::Document& document, std::size_t track)
{
    std::vector<core::Tick> ticks;
    for (const auto& event : document.song().events(document.song().tracks()[track]->id()).all())
    {
        ticks.push_back(event.tick);
    }
    return ticks;
}

constexpr core::Tick kBar = core::kPpqn.whole();
constexpr core::Tick kQuarter = core::kPpqn.quarter();

} // namespace

TEST_CASE("dd deletes the events starting in the cursor's bar, and u brings them back")
{
    Arranging a{operatorSong()};
    a.type(U"2l"); // bar 3
    a.type(U"dd");
    // The note held into bar 3 from bar 2 starts outside it, so it stays.
    CHECK(ticksOf(a.document, 0) ==
          std::vector<core::Tick>{0, kBar + 2 * kQuarter, 3 * kBar + kQuarter});
    CHECK(a.page().bar() == 3);
    CHECK(a.ws.registerArea().tracks.size() == 1);
    CHECK(a.ws.registerEvents().size() == 1);
    CHECK(a.ws.registerEvents()[0].tick == 0); // from the bar's start
    a.type(U"u");
    CHECK(a.ws.message() == "Undid: Delete bar 3 of Bass");
    CHECK(ticksOf(a.document, 0).size() == 4);
}

TEST_CASE("operators take bars across to where a motion goes, or tracks down")
{
    SECTION("2dd and d2l: this bar and the next")
    {
        for (const auto* keys : {U"2dd", U"d2l"})
        {
            Arranging a{operatorSong()};
            a.type(U"l");
            a.type(keys);
            CHECK(ticksOf(a.document, 0) == std::vector<core::Tick>{0, 3 * kBar + kQuarter});
        }
    }
    SECTION("dh: the bar before")
    {
        Arranging a{operatorSong()};
        a.type(U"2ldh");
        CHECK(ticksOf(a.document, 0) == std::vector<core::Tick>{0, 2 * kBar, 3 * kBar + kQuarter});
        CHECK(a.page().bar() == 2); // at the area's start
    }
    SECTION("dG: to the end, the last bar too")
    {
        Arranging a{operatorSong()};
        a.type(U"2ldG");
        CHECK(ticksOf(a.document, 0) == std::vector<core::Tick>{0, kBar + 2 * kQuarter});
    }
    SECTION("keys that only move aren't ranges, and say what is")
    {
        Arranging a{operatorSong()};
        const auto before = ticksOf(a.document, 0);
        for (const auto* keys : {U"d$", U"d}", U"y0", U"d)"})
        {
            a.type(keys);
            CHECK(a.ws.message().ends_with(
                "only moves the cursor; for a range, use ]b or ]s, or an object such as ib"));
            CHECK(ticksOf(a.document, 0) == before);
        }
        a.type(U"d}");
        CHECK(a.ws.message() ==
              "} only moves the cursor; for a range, use ]b or ]s, or an object such as ib");
    }
    SECTION("dgg: from the first bar, this one too")
    {
        Arranging a{operatorSong()};
        a.type(U"ldgg");
        CHECK(ticksOf(a.document, 0) == std::vector<core::Tick>{2 * kBar, 3 * kBar + kQuarter});
    }
    SECTION("dj: this bar on this track and the next")
    {
        Arranging a{operatorSong()};
        a.type(U"ldj");
        CHECK(ticksOf(a.document, 0) == std::vector<core::Tick>{0, 2 * kBar, 3 * kBar + kQuarter});
        CHECK(ticksOf(a.document, 1).empty());
        CHECK(a.ws.message().empty());
        a.type(U"u");
        CHECK(a.ws.message() == "Undid: Delete bar 2 of Bass and Lead");
    }
}

TEST_CASE("y yanks an area of several tracks, and p puts it at the cursor's bar")
{
    Arranging a{operatorSong()};
    a.type(U"lyj");
    CHECK(a.ws.message() == "Bar 2 of Bass and Lead yanked (2 events)");
    REQUIRE(a.ws.registerArea().tracks.size() == 2);
    a.type(U"4lp"); // bar 6
    CHECK(ticksOf(a.document, 0).back() == 5 * kBar + 2 * kQuarter);
    CHECK(ticksOf(a.document, 1).back() == 5 * kBar);
    a.type(U"u");
    CHECK(a.ws.message() == "Undid: Put 2 events in Bass and the tracks after it");
}

TEST_CASE("dd deletes the cursor's bar, saying when there's nothing in it; x and Y want a note")
{
    Arranging a{operatorSong()};
    a.type(U"6l"); // bar 7, empty
    a.type(U"dd");
    CHECK(a.ws.message() == "Nothing to delete in bar 7 of Bass");
    for (const auto* keys : {U"x", U"Y", U"din"})
    {
        a.type(keys);
        CHECK(a.ws.message() == "There's no single note here; dd takes this bar's events");
    }
    a.type(U"gg3dd");
    CHECK(ticksOf(a.document, 0) == std::vector<core::Tick>{3 * kBar + kQuarter});
    a.type(U"u2dib"); // ib is the same
    CHECK(ticksOf(a.document, 0) == std::vector<core::Tick>{2 * kBar, 3 * kBar + kQuarter});
}

TEST_CASE("events yanked in the event list are put in the arrangement at the cursor's bar")
{
    Arranging a{operatorSong()};
    a.type(U"ge");
    a.type(U"gg");
    const auto& list = a.ws.eventList();
    while (list.list().rows()[list.row()].kind != RowKind::Event)
    {
        a.type(U"j");
    }
    a.type(U"yy");
    a.type(U"ga");
    a.type(U"4lp"); // bar 5
    CHECK(ticksOf(a.document, 0).back() == 4 * kBar);
}

namespace
{

// Eight bars of 4/4 in sections A (bars 1-2), B (3-5) and C (6-8), and Bass with a note
// starting each bar; the : prompt runs lines through the registry, as the program's does.
struct Strip : Arranging
{
    Strip()
        : Arranging{song()}
    {
        ws.setLineHandler(
            [this](const std::string& line)
            {
                const auto result = registry.runLine(line);
                ws.setMessage(result.message,
                              result.warning ? MessageKind::Warning : MessageKind::Information);
            });
    }

    static core::Song song()
    {
        core::Song song{core::kPpqn};
        const auto bass = song.addTrack("Bass");
        song.updateEvents(
            bass,
            [](core::EventList& events)
            {
                for (int i = 0; i < 8; ++i)
                {
                    events.insert(
                        {i * kBar, core::Note{static_cast<std::uint8_t>(40 + i), 100, kQuarter}});
                }
            });
        song.renameSection(0, "A");
        song.splitSection(2 * kBar, "B");
        song.splitSection(5 * kBar, "C");
        return song;
    }

    std::vector<std::string> names() const
    {
        std::vector<std::string> result;
        for (const auto& section : document.song().sections())
        {
            result.push_back(section->name());
        }
        return result;
    }

    std::optional<std::size_t> strip() const { return page().stripCursor(); }
};

} // namespace

TEST_CASE("k from the first track, or gs, goes onto the section strip, and j back")
{
    Strip s;
    s.type(U"3l"); // bar 4, in B
    s.type(U"k");
    REQUIRE(s.strip() == 1u);
    std::u32string inspector;
    const auto frame = s.ws.frame();
    for (const auto& run : frame[frame.size() - 2].text)
    {
        inspector += run.text + U" ";
    }
    CHECK(contains(inspector, U"Section"));
    CHECK(contains(inspector, U"3–5"));

    // Back on the same section, the cursor goes back to where it was.
    s.type(U"j");
    CHECK_FALSE(s.strip());
    CHECK(s.page().bar() == 4);

    // From a section moved to, to its start.
    s.type(U"gsl");
    CHECK(s.strip() == 2u);
    s.type(U"j");
    CHECK(s.page().bar() == 6);
    s.type(U"gs0");
    CHECK(s.strip() == 0u);
    s.type(U"$");
    CHECK(s.strip() == 2u);
    s.type(U"2gg");
    CHECK(s.strip() == 1u);
}

TEST_CASE("sections are deleted, yanked and put like lines")
{
    Strip s;
    s.type(U"gsl"); // B
    s.type(U"x");
    CHECK(s.names() == std::vector<std::string>{"A", "C"});
    CHECK(s.strip() == 1u); // on what followed
    s.type(U"u");
    CHECK(s.ws.message() == "Undid: Delete section B");
    CHECK(s.names() == std::vector<std::string>{"A", "B", "C"});

    // dd, yy and the objects are bars and notes, which the strip doesn't hold.
    for (const auto* keys : {U"dd", U"yy", U"dib", U"yab", U"din"})
    {
        s.type(keys);
        CHECK(s.ws.message() == "The strip holds sections, not bars or notes: x deletes this "
                                "section and Y yanks it");
        CHECK(s.names() == std::vector<std::string>{"A", "B", "C"});
    }

    // Yank B, put it after C, and a copy before A.
    s.type(U"Y");
    CHECK(s.ws.message() == "B yanked");
    s.type(U"$p");
    CHECK(s.names() == std::vector<std::string>{"A", "B", "C", "B (2)"});
    CHECK(s.strip() == 3u);
    s.type(U"0P");
    CHECK(s.names() == std::vector<std::string>{"B (3)", "A", "B", "C", "B (2)"});

    // d with a motion: this section and the next.
    s.type(U"d2l");
    CHECK(s.names() == std::vector<std::string>{"B", "C", "B (2)"});
    // A song keeps at least one section.
    s.type(U"dG");
    CHECK(s.ws.message() == "A song keeps at least one section");
    CHECK(s.names().size() == 3);

    // Sections don't go on tracks, nor bars on the strip.
    s.type(U"jp");
    CHECK(s.ws.message() == "The register holds sections: put them on the section strip");
    s.type(U"yy");
    s.type(U"gsp");
    CHECK(s.ws.message() == "The register holds bars of tracks: put them on the tracks");
}

TEST_CASE("Alt-h and Alt-l move a section, J joins, and + and - resize")
{
    Strip s;
    s.type(U"gs");
    s.press(Key::typed(U'l', kAlt));
    CHECK(s.names() == std::vector<std::string>{"B", "A", "C"});
    CHECK(s.strip() == 1u); // with it
    s.press(Key::typed(U'h', kAlt));
    CHECK(s.names() == std::vector<std::string>{"A", "B", "C"});

    s.type(U"J");
    CHECK(s.names() == std::vector<std::string>{"A", "C"});
    CHECK(s.document.song().sectionStart(1) == 5 * kBar);
    s.type(U"$J");
    CHECK(s.ws.message() == "C is the last section, with none after to join");

    s.type(U"0+");
    CHECK(s.document.song().sectionStart(1) == 6 * kBar);
    s.type(U"6-"); // can't go below a bar
    CHECK(s.ws.message() == "A can't be shorter than a bar");
}

TEST_CASE("i and a insert a new section, which the prompt then names, as c renames one")
{
    Strip s;
    s.type(U"gsl"); // B
    s.type(U"a");
    CHECK(s.names() == std::vector<std::string>{"A", "B", "Section 1", "C"});
    CHECK(s.document.song().sectionStart(3) == 9 * kBar); // four bars
    REQUIRE(s.ws.mode() == Mode::CommandLine);
    CHECK(s.ws.commandLine().text() == U"section-rename Section 1");
    for (int i = 0; i < 9; ++i) // over the name given
    {
        s.press(Key::of(NamedKey::Backspace));
    }
    s.type(U"Bridge");
    s.press(Key::of(NamedKey::Enter));
    CHECK(s.names() == std::vector<std::string>{"A", "B", "Bridge", "C"});
    CHECK(s.ws.message() == "Renamed Section 1 to Bridge");

    s.type(U"2i"); // two bars, before it; the numbering starts again with no Section N left
    CHECK(s.names()[2] == "Section 1");
    CHECK(s.document.song().sectionStart(3) - s.document.song().sectionStart(2) == 2 * kBar);
    s.press(Key::of(NamedKey::Escape)); // keeps the name

    s.type(U"c");
    CHECK(s.ws.commandLine().text() == U"section-rename Section 1");
    s.press(Key::of(NamedKey::Escape));
}

TEST_CASE("Enter on a section plays from its start")
{
    Strip s;
    s.type(U"gsl");
    s.press(Key::of(NamedKey::Enter));
    CHECK(s.ws.message() == "Nothing to play to"); // there's no player here
}

namespace
{

// Eight bars of 4/4 in two sections, Section 1 of bars 1–4 and B of bars 5–8, with a note on
// each bar's start on the first track, pitch 60 plus the bar's number, and one on the second
// track's first bar.
core::Song barNotes()
{
    core::Song song{core::kPpqn};
    const auto first = song.addTrack("One");
    const auto second = song.addTrack("Two");
    const auto bar = [&](std::int64_t n) { return song.meter().barStart(n); };
    song.updateEvents(first,
                      [&](core::EventList& events)
                      {
                          for (std::int64_t n = 1; n <= 8; ++n)
                          {
                              events.insert({bar(n), core::Note{static_cast<std::uint8_t>(60 + n),
                                                                100, core::kPpqn.quarter()}});
                          }
                      });
    song.updateEvents(second, [&](core::EventList& events)
                      { events.insert({0, core::Note{40, 100, core::kPpqn.quarter()}}); });
    song.splitSection(bar(5), "B");
    return song;
}

// The bar each note of a track starts in, as bar and pitch.
std::vector<std::pair<std::int64_t, int>> notesByBar(const Arranging& a, std::size_t track)
{
    std::vector<std::pair<std::int64_t, int>> notes;
    const auto& song = a.document.song();
    for (const auto& event : song.events(song.tracks()[track]->id()).all())
    {
        if (const auto* note = std::get_if<core::Note>(&event.data))
        {
            notes.emplace_back(song.meter().toBarBeatTick(event.tick).bar, note->pitch);
        }
    }
    return notes;
}

using BarNotes = std::vector<std::pair<std::int64_t, int>>;

} // namespace

TEST_CASE("dab deletes bars and closes the gap, as far as the section's end; dib leaves it")
{
    Arranging a{barNotes()};
    const auto before = notesByBar(a, 0);
    a.type(U"ldib"); // bar 2's notes, leaving the bar
    CHECK(notesByBar(a, 0) ==
          BarNotes{{1, 61}, {3, 63}, {4, 64}, {5, 65}, {6, 66}, {7, 67}, {8, 68}});
    a.type(U"u");
    a.type(U"dab"); // bar 2: bars 3 and 4 move up, bar 4 left silent
    CHECK(notesByBar(a, 0) ==
          BarNotes{{1, 61}, {2, 63}, {3, 64}, {5, 65}, {6, 66}, {7, 67}, {8, 68}});
    CHECK(notesByBar(a, 1) == BarNotes{{1, 40}});
    CHECK(a.ws.registerEvents().size() == 1);
    CHECK(a.ws.registerArea().span == a.document.song().meter().barStart(2));
    a.type(U"u");
    CHECK(notesByBar(a, 0) == before);

    a.type(U"l5dab"); // from bar 3, stopping at the section's end
    CHECK(notesByBar(a, 0) == BarNotes{{1, 61}, {2, 62}, {5, 65}, {6, 66}, {7, 67}, {8, 68}});
    a.type(U"u");
    a.type(U"ggdab");
    CHECK(notesByBar(a, 0) ==
          BarNotes{{1, 62}, {2, 63}, {3, 64}, {5, 65}, {6, 66}, {7, 67}, {8, 68}});
    CHECK(a.document.song().sectionStart(1) == a.document.song().meter().barStart(5));
}

TEST_CASE("P inserts what was yanked, moving the rest of the section later, asking to grow it")
{
    Arranging a{barNotes()};
    const auto& song = a.document.song();
    const auto before = notesByBar(a, 0);
    a.type(U"yylP"); // bar 1's note into bar 2: bar 4's would go past the section's end
    CHECK(a.ws.message() == "Lengthen Section 1 by a bar to make room? (y/n)");
    a.type(U"n");
    CHECK(a.ws.message() == "Nothing done");
    CHECK(notesByBar(a, 0) == before);

    a.type(U"Py");
    CHECK(
        notesByBar(a, 0) ==
        BarNotes{{1, 61}, {2, 61}, {3, 62}, {4, 63}, {5, 64}, {6, 65}, {7, 66}, {8, 67}, {9, 68}});
    CHECK(song.sectionStart(1) == song.meter().barStart(6));
    CHECK(notesByBar(a, 1) == BarNotes{{1, 40}});
    a.type(U"u"); // the growing and the insert as one step
    CHECK(notesByBar(a, 0) == before);
    CHECK(song.sectionStart(1) == song.meter().barStart(5));

    // With room at the section's end, no question; an empty bar inserts silence.
    a.type(U"gg3lyy"); // bar 4
    a.type(U"dd");     // now empty
    a.type(U"ggyyP");
    CHECK(a.ws.message().empty());
    CHECK(notesByBar(a, 0) ==
          BarNotes{{1, 61}, {2, 61}, {3, 62}, {4, 63}, {5, 65}, {6, 66}, {7, 67}, {8, 68}});
}

TEST_CASE(":bar-delete takes bars out of every track, keeping them in the register as a section")
{
    Arranging a{barNotes()};
    const auto& song = a.document.song();
    const auto before = notesByBar(a, 0);
    a.type(U"l"); // bar 2
    CHECK(a.registry.runLine("bar-delete") ==
          CommandResult{true,
                        "Deleted bar 2 on every track; p on the section strip puts them back as a "
                        "section",
                        false});
    CHECK(notesByBar(a, 0) ==
          BarNotes{{1, 61}, {2, 63}, {3, 64}, {4, 65}, {5, 66}, {6, 67}, {7, 68}});
    CHECK(song.sectionStart(1) == song.meter().barStart(4));
    CHECK(song.sections().size() == 2);
    REQUIRE(a.ws.registerArea().sections.size() == 1);
    CHECK(a.ws.registerArea().sections[0]->name() == "From Section 1");

    // Put back as a section of its own, from the strip.
    a.type(U"gs$p");
    REQUIRE(song.sections().size() == 3);
    CHECK(song.sections()[2]->name() == "From Section 1");
    CHECK(notesByBar(a, 0).back() == std::pair<std::int64_t, int>{8, 62});
    a.type(U"p"); // another copy, numbered
    CHECK(song.sections()[3]->name() == "From Section 1 (2)");
    a.type(U"uuu");
    CHECK(notesByBar(a, 0) == before);
    CHECK(a.ws.message() == "Undid: Delete bar 2 on every track");

    a.type(U"jgg");
    CHECK(a.registry.runLine("bar-delete 2").message.starts_with("Deleted bars 1–2"));
    CHECK(notesByBar(a, 1).empty());
}

TEST_CASE(":bar-insert puts empty bars in on every track, in a meter given or the one there")
{
    Arranging a{barNotes()};
    const auto& song = a.document.song();
    a.type(U"l"); // bar 2
    CHECK(a.registry.runLine("bar-insert") == CommandResult{true, {}, false});
    CHECK(notesByBar(a, 0) ==
          BarNotes{{1, 61}, {3, 62}, {4, 63}, {5, 64}, {6, 65}, {7, 66}, {8, 67}, {9, 68}});
    CHECK(song.sectionStart(1) == song.meter().barStart(6));
    a.type(U"u");
    CHECK(a.ws.message() == "Undid: Insert a bar before bar 2 on every track");

    CHECK_FALSE(a.registry.runLine("bar-insert 3/4 2").warning);
    CHECK(song.meter().signatureAt(song.meter().barStart(2)) == core::TimeSignature{3, 4});
    CHECK(song.meter().signatureAt(song.meter().barStart(4)) == core::TimeSignature{4, 4});
    CHECK(notesByBar(a, 0)[1] == std::pair<std::int64_t, int>{4, 62});
    CHECK(a.registry.runLine("bar-insert 3/5").message ==
          "bar-insert takes a number of bars and a meter, such as 2 3/4");
}

TEST_CASE("bars on every track are refused where a track's own bars wouldn't line up")
{
    auto threeFour = barNotes();
    threeFour.updateMeter(threeFour.tracks()[1]->id(),
                          [](core::MeterMap& meter) { meter.set(0, core::TimeSignature{3, 4}); });
    Arranging a{std::move(threeFour)};
    const auto before = a.document.song();
    a.type(U"l"); // bar 2, a beat into Two's second bar
    CHECK(a.registry.runLine("bar-insert").message ==
          "Two's bars don't line up with a bar; put them in on each track instead");
    CHECK(a.registry.runLine("bar-delete").message ==
          "Two's bars don't line up with bar 2; delete them on each track instead");
    CHECK(a.document.song() == before);
    CHECK_FALSE(a.document.modified());
}

TEST_CASE("e edits the section's name and length in the inspector on the strip")
{
    Strip s;
    const auto& song = s.document.song();
    const auto& editor = s.ws.arrangement().inspectorEditor();
    s.type(U"e"); // on a track
    CHECK(s.ws.message() == "Nothing to edit in a bar here; e edits a note in the piano roll, "
                            "or a section on the section strip");
    CHECK_FALSE(editor.active());

    s.type(U"gsle"); // B, three bars
    REQUIRE(editor.active());
    CHECK(editor.heading() == "Section");
    CHECK(editor.line().text() == U"B");
    s.type(U"Bridge");
    s.press(Key::of(NamedKey::Tab));
    CHECK(s.names() == std::vector<std::string>{"A", "Bridge", "C"});
    CHECK(editor.heading() == "Length");
    CHECK(editor.line().text() == U"3");
    s.press(Key::of(NamedKey::Up)); // a bar longer, straight away
    CHECK(song.sectionStart(2) == 6 * kBar);
    s.type(U"x");
    s.press(Key::of(NamedKey::Enter));
    CHECK(s.ws.message() == "A length is a number of bars, such as 8, or a change such as +2");
    s.press(Key::of(NamedKey::Backspace));
    s.type(U"-3");
    s.press(Key::of(NamedKey::Enter));
    CHECK_FALSE(editor.active());
    CHECK(song.sectionStart(2) == 3 * kBar);
    s.type(U"u");
    CHECK(s.ws.message() == "Undid: Shorten Bridge to 1 bar");
}

namespace
{

// One section of eight bars, with markers at the start, at bar 3, and two in bar 5, off its
// bar line.
core::Song markerSong()
{
    core::Song song{core::kPpqn};
    song.addTrack("Bass");
    song.setMarker(0, "Top");
    song.setMarker(2 * kBar, "Chorus");
    song.setMarker(4 * kBar + kQuarter, "Fill");
    song.setMarker(4 * kBar + 2 * kQuarter, "Late");
    return song;
}

std::vector<std::string> sectionNames(const core::Song& song)
{
    std::vector<std::string> names;
    for (const auto& section : song.sections())
    {
        names.push_back(section->name());
    }
    return names;
}

} // namespace

TEST_CASE(":marker-to-section starts a section at a marker, asking unless it's on it")
{
    Arranging a{markerSong()};
    const auto& song = a.document.song();
    a.type(U"2l"); // bar 3, on Chorus
    CHECK(a.registry.runLine("marker-to-section") == CommandResult{true, {}, false});
    CHECK(a.ws.message() == "Chorus starts at 3:1:000, from its marker");
    CHECK(sectionNames(song) == std::vector<std::string>{"Section 1", "Chorus"});
    CHECK(song.sectionStart(1) == 2 * kBar);
    CHECK(song.markers().size() == 3);
    a.type(U"u");
    CHECK(song.sections().size() == 1);

    a.type(U"l"); // bar 4: the marker before, Chorus, asked about
    a.registry.runLine("marker-to-section");
    CHECK(a.ws.message() == "Start a section from the marker Chorus at 3:1:000? (y/n)");
    a.type(U"n");
    CHECK(a.ws.message() == "Nothing done");
    CHECK(song.sections().size() == 1);

    a.type(U"2l"); // bar 6: Late, off its bar line, the section at the bar line before
    a.registry.runLine("marker-to-section");
    CHECK(a.ws.message() == "Start a section at 5:1:000 from the marker Late at 5:3:000? (y/n)");
    a.type(U"y");
    CHECK(sectionNames(song) == std::vector<std::string>{"Section 1", "Late"});
    CHECK(song.sectionStart(1) == 4 * kBar);

    a.type(U"gg"); // Top, at the section's start
    CHECK(a.registry.runLine("marker-to-section").message ==
          "Top is at the start of Section 1, so there's no section to start");
}

TEST_CASE(":marker-to-section-all goes through the markers, asking about those off a bar line")
{
    Arranging a{markerSong()};
    const auto& song = a.document.song();
    a.type(U"l"); // bar 2, where the cursor comes back to
    a.registry.runLine("marker-to-section-all");
    CHECK(a.ws.message() ==
          "Start a section at 5:1:000 from the marker Fill at 5:2:000? (y/n, Escape stops)");
    CHECK(a.page().bar() == 5);         // showing it
    CHECK(song.sections().size() == 1); // nothing yet
    a.type(U"y");                       // Late is then in a bar with Fill
    CHECK(a.ws.message() == "Turned 2 markers into sections; 1 was at a section's start; 1 was "
                            "in a bar with another");
    CHECK(sectionNames(song) == std::vector<std::string>{"Section 1", "Chorus", "Fill"});
    CHECK(song.markers().size() == 2);
    CHECK(a.page().bar() == 2);
    a.type(U"u"); // all of it, as one step
    CHECK(song.sections().size() == 1);
    CHECK(song.markers().size() == 4);

    // Escape stops, with nothing done; no goes on to the next.
    a.registry.runLine("marker-to-section-all");
    a.press(Key::of(NamedKey::Escape));
    CHECK(a.ws.message() == "Nothing done");
    CHECK(song.sections().size() == 1);
    CHECK(a.page().bar() == 2);
    a.registry.runLine("marker-to-section-all");
    a.type(U"n");
    CHECK(a.ws.message() ==
          "Start a section at 5:1:000 from the marker Late at 5:3:000? (y/n, Escape stops)");
    a.type(U"n");
    CHECK(a.ws.message() == "Turned 1 marker into a section; 1 was at a section's start; 2 were "
                            "left as they were");
}
