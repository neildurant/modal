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
    a.type(U"+");
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
    SECTION("dG and d$: to the end, the last bar too")
    {
        for (const auto* keys : {U"dG", U"d$"})
        {
            Arranging a{operatorSong()};
            a.type(U"2l");
            a.type(keys);
            CHECK(ticksOf(a.document, 0) == std::vector<core::Tick>{0, kBar + 2 * kQuarter});
        }
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

TEST_CASE("x deletes the cursor's bar, saying when there's nothing in it")
{
    Arranging a{operatorSong()};
    a.type(U"6l"); // bar 7, empty
    a.type(U"x");
    CHECK(a.ws.message() == "Nothing to delete in bar 7 of Bass");
    a.type(U"gg3x");
    CHECK(ticksOf(a.document, 0) == std::vector<core::Tick>{3 * kBar + kQuarter});
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

    // Yank B, put it after C, and a copy before A.
    s.type(U"yy");
    CHECK(s.ws.message() == "B yanked");
    s.type(U"$p");
    CHECK(s.names() == std::vector<std::string>{"A", "B", "C", "B 2"});
    CHECK(s.strip() == 3u);
    s.type(U"0P");
    CHECK(s.names() == std::vector<std::string>{"B 3", "A", "B", "C", "B 2"});

    // d with a motion: this section and the next.
    s.type(U"d2l");
    CHECK(s.names() == std::vector<std::string>{"B", "C", "B 2"});
    // A song keeps at least one section.
    s.type(U"d$");
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
