// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/demo_song.hpp>
#include <modal/ui/settings.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <optional>

using namespace modal;
using namespace modal::ui;

TEST_CASE("gt opens the tracks view at a numbered track")
{
    core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace ws{registry, document};
    registry.add(windowCommands());
    ws.resize(1200, 600, {10, 20});
    ws.show(ViewKind::Arrangement);
    for (auto c : std::u32string_view{U"3gt"})
    {
        ws.handle(Key::typed(c), 0);
    }
    CHECK(ws.shown() == ViewKind::Tracks);
    CHECK(ws.tracks().cursor().track == document.song().tracks()[2]->id());
}

namespace
{
struct Tracking
{
    Tracking()
    {
        registry.add(windowCommands());
        ws.resize(1200, 600, {10, 20});
        type(U"gt");
    }
    void type(std::u32string_view keys)
    {
        for (auto c : keys)
        {
            ws.handle(Key::typed(c), 0);
        }
    }
    void press(NamedKey key, Modifiers modifiers = kNoModifiers)
    {
        ws.handle(Key::of(key, modifiers), 0);
    }
    void enter(std::u32string_view value)
    {
        press(NamedKey::Enter);
        type(value);
        press(NamedKey::Enter);
    }
    const core::Track& track(std::size_t i = 0) const { return *document.song().tracks()[i]; }
    core::Document document{demoSong()};
    CommandRegistry registry;
    Workspace ws{registry, document};
};
} // namespace

TEST_CASE("track fields edit settings and musical offsets without moving events")
{
    Tracking t;
    const auto events = t.document.song().events(t.track().id());
    t.enter(U"Renamed bass");
    CHECK(t.track().name == "Renamed bass");
    t.type(U"l+");
    CHECK(t.track().mute);
    t.type(U"l+");
    CHECK(t.track().solo);
    t.type(U"3l");
    t.enter(U"10");
    CHECK(t.track().channel == 10);
    t.type(U"4l");
    t.enter(U"-12");
    CHECK(t.track().transpose == -12);
    t.type(U"l");
    t.enter(U"+15");
    CHECK(t.track().velocityOffset == 15);
    t.type(U"l");
    t.enter(U"+1/16-22t");
    CHECK(t.track().timeOffset.amount() ==
          core::NoteFraction{1, 16}.toTicks(t.document.song().ppqn()) - 22);
    CHECK(t.document.song().events(t.track().id()) == events);
    t.enter(U"-40ms");
    CHECK(t.track().timeOffset == core::TimeOffset{std::chrono::milliseconds{-40}});
    t.type(U"u");
    CHECK(t.track().timeOffset.musical());
}

TEST_CASE("bulk track edits validate every row and undo together")
{
    Tracking t;
    t.type(U"9lVj"); // transpose, first two tracks
    const auto first = t.track().transpose;
    const auto second = t.track(1).transpose;
    t.type(U"+");
    CHECK(t.track().transpose == first + 1);
    CHECK(t.track(1).transpose == second + 1);
    t.press(NamedKey::Escape);
    t.type(U"u");
    CHECK(t.track().transpose == first);
    CHECK(t.track(1).transpose == second);
    t.type(U"Vj");
    t.enter(U"12");
    CHECK(t.track(1).transpose == 12);
    CHECK(t.track(2).transpose == 12);
    CHECK(t.ws.mode() == Mode::Visual);
    t.press(NamedKey::Escape);
    t.type(U"4h");
    t.enter(U"16"); // channel
    t.type(U"Vk+");
    CHECK(t.track(2).channel == 16);
    CHECK(t.track(1).channel != 16);
    CHECK(t.ws.message().find("1 to 16") != std::string::npos);
}

TEST_CASE("whole-track registers insert fresh IDs and replace Visual rows in one undo")
{
    Tracking t;
    const auto original = t.document.song();
    const auto first = t.track().id();
    const auto second = t.track(1).id();
    const auto count = original.tracks().size();
    t.type(U"Vjy");
    t.type(U"p");
    REQUIRE(t.document.song().tracks().size() == count + 2);
    CHECK(t.track(2).id() != first);
    CHECK(t.track(3).id() != second);
    CHECK(t.document.song().events(t.track(2).id()) == original.events(first));
    CHECK(t.track(2).name == original.track(first)->name + " (2)");
    t.type(U"u");
    CHECK(t.document.song() == original);
    t.type(U"Vp");
    CHECK(t.document.song().tracks().size() == count + 1);
    t.type(U"u");
    CHECK(t.document.song() == original);
    t.type(U"V\"ay");
    t.type(U"j\"aP");
    CHECK(t.document.song().tracks().size() == count + 1);
}

TEST_CASE("whole-track material cannot replace event selections or sections")
{
    Tracking t;
    t.type(U"Vy");
    const auto before = t.document.song();
    for (auto keys : {U"gap", U"grvp", U"gevp", U"gagsvp"})
    {
        t.type(keys);
        CHECK(t.document.song() == before);
        CHECK(t.ws.message().find("tracks view") != std::string::npos);
        t.press(NamedKey::Escape);
    }
}

TEST_CASE("the tracks view keeps time and pitch while changing rows and views")
{
    Tracking t;
    const auto id = t.track().id();
    const auto tick = t.document.song().ppqn().quarter() + 1;
    t.ws.tracks().setCursor({id, tick, 73});
    t.press(NamedKey::PageDown, kCtrl);
    CHECK(t.ws.tracks().cursor().track == t.track(1).id());
    CHECK(t.ws.tracks().cursor().tick == tick);
    CHECK(t.ws.tracks().cursor().pitch == 73);
    t.type(U"gr");
    CHECK(t.ws.pianoRoll().cursor().tick == tick);
    CHECK(t.ws.pianoRoll().cursor().pitch == 73);
    t.press(NamedKey::Backspace);
    CHECK(t.ws.shown() == ViewKind::Tracks);
    CHECK(t.ws.tracks().cursor().tick == tick);
}

TEST_CASE("named track registers can repeat the unnamed yank and append whole tracks")
{
    Tracking t;
    const auto count = t.document.song().tracks().size();
    t.type(U"VyV\"ayjV\"Ay\"ap");
    CHECK(t.document.song().tracks().size() == count + 2);
}

TEST_CASE("gv returns to the view in which whole tracks were selected")
{
    Tracking t;
    const auto before = t.document.song();
    t.type(U"Vj");
    t.press(NamedKey::Escape);
    t.type(U"grgv");
    CHECK(t.ws.shown() == ViewKind::Tracks);
    CHECK(t.ws.mode() == Mode::Visual);
    t.type(U"yP");
    CHECK(t.document.song().tracks().size() == before.tracks().size() + 2);
}

TEST_CASE("note timing display follows the grid or a fixed setting without editing the song")
{
    Tracking t;
    Settings settings;
    t.ws.setSettings(&settings);
    const auto id = t.track().id();
    const auto ppqn = t.document.song().ppqn();
    const auto start = ppqn.whole() * 6 + ppqn.quarter() / 2 + 10;
    t.document.edit("Test note", {},
                    [&](core::Song& song)
                    {
                        song.updateEvents(
                            id,
                            [&](core::EventList& events)
                            {
                                events = {};
                                events.insert(
                                    {start, core::Note{60, 100, ppqn.quarter() / 2 + 40}});
                            });
                    });
    t.ws.tracks().setCursor({id, start, 60});
    t.type(U"gr");
    const auto song = t.document.song();
    const auto text = [&]
    {
        std::u32string all;
        for (const auto& region : t.ws.frame())
        {
            for (const auto& run : region.text)
            {
                all += run.text;
            }
        }
        return all;
    };
    CHECK(text().find(U"Fine(t)") != std::u32string::npos);
    CHECK(text().find(U"+10") != std::u32string::npos);
    CHECK(text().find(U"+40") != std::u32string::npos);
    REQUIRE(settings.set("timingunit", "1/8").empty());
    CHECK(text().find(U"1/8") != std::u32string::npos);
    CHECK(t.document.song() == song);
    CHECK_FALSE(settings.set("timingunit", "junk").empty());
    CHECK(settings.text("timingunit") == "1/8");
    t.type(U"e");
    for (int i = 0; i < 20 && t.ws.pianoRoll().inspectorEditor().heading() != "Start step"; ++i)
    {
        t.press(NamedKey::Tab);
    }
    REQUIRE(t.ws.pianoRoll().inspectorEditor().heading() == "Start step");
    t.type(U"3");
    t.press(NamedKey::Enter);
    CHECK(t.document.song().events(id)[0].tick == start + ppqn.quarter() / 2);
    t.type(U"u");
    CHECK(t.document.song() == song);
    t.type(U"ge");
    t.type(U"e");
    for (int i = 0; i < 3; ++i)
    {
        t.press(NamedKey::Tab); // Start step
    }
    t.type(U"3");
    t.press(NamedKey::Enter);
    CHECK(t.document.song().events(id)[0].tick == start + ppqn.quarter() / 2);
}

TEST_CASE("deleting whole tracks needs an explicit row selection")
{
    Tracking t;
    const auto before = t.document.song();
    t.type(U"dd");
    CHECK(t.document.song() == before);
    CHECK(t.ws.message().find("Use V") != std::string::npos);
    t.type(U"Vjd");
    CHECK(t.document.song().tracks().size() == before.tracks().size() - 2);
    t.type(U"u");
    CHECK(t.document.song() == before);
}

TEST_CASE("refusing a whole-track replacement preserves song and register")
{
    Tracking t;
    t.type(U"Vy");
    const auto reg = t.ws.registerArea();
    t.document.edit("Rename section", {},
                    [](core::Song& song) { song.renameSection(0, "Changed"); });
    const auto before = t.document.song();
    const auto version = t.document.version();
    t.type(U"Vjp");
    CHECK(t.document.song() == before);
    CHECK(t.document.version() == version);
    CHECK(t.ws.registerArea() == reg);
    CHECK(t.ws.message().find("section names") != std::string::npos);
}

TEST_CASE("track columns start visible and keep the active field visible after resizing")
{
    Tracking t;
    auto frame = t.ws.frame();
    REQUIRE_FALSE(frame.empty());
    CHECK(frame.front().text.front().text.starts_with(U"No."));
    CHECK(frame.front().text.front().x >= 0);
    t.ws.resize(600, 300, {10, 20});
    t.type(U"$3h");
    frame = t.ws.frame();
    for (const auto& run : frame.front().text)
    {
        if (run.style.background == Role::Cursor)
        {
            CHECK(run.x >= 0);
            CHECK(run.x + static_cast<int>(run.text.size()) * 10 <= 600);
        }
    }
}

TEST_CASE("refusing mixed whole-track register append leaves deletions unapplied")
{
    for (const bool takingTracks : {false, true})
    {
        Tracking t;
        t.type(takingTracks ? U"ge\"ayygtV" : U"V\"aygev");
        const auto before = t.document.song();
        const auto version = t.document.version();
        const auto reg = t.ws.registerArea();
        t.type(U"\"Ad");
        CHECK(t.ws.message().find("Cannot append") != std::string::npos);
        CHECK(t.document.song() == before);
        CHECK(t.document.version() == version);
        CHECK(t.ws.registerArea() == reg);
        t.press(NamedKey::Escape);
        t.type(takingTracks ? U"ge\"ap" : U"gt\"ap");
        if (!takingTracks)
            CHECK(t.document.song().tracks().size() == before.tracks().size() + 1);
    }
}

TEST_CASE("track toggles are compact badges and numeric cells align on the right")
{
    Tracking t;
    const auto find = [&](const Frame& frame, std::u32string_view text) -> const TextRun&
    {
        const auto run =
            std::ranges::find_if(frame.front().text, [&](const TextRun& candidate)
                                 { return candidate.text.find(text) != std::u32string::npos; });
        REQUIRE(run != frame.front().text.end());
        return *run;
    };
    t.type(U"l+"); // mute
    t.type(U"l+"); // solo
    const auto frame = t.ws.frame();
    for (const auto& [text, role] :
         {std::pair{U" M ", Role::TrackMute}, std::pair{U" S ", Role::TrackSolo},
          std::pair{U" C ", Role::TrackChase}})
    {
        const auto& badge = find(frame, text);
        CHECK(badge.style.foreground == Role::ToggleText);
        CHECK(std::ranges::any_of(frame.front().under,
                                  [&](const Shape& shape)
                                  {
                                      return shape.role == role && shape.rect.x == badge.x &&
                                             shape.rect.y > badge.y && shape.rect.height < 20;
                                  }));
        for (const auto& run : frame.front().text)
            if (&run != &badge && run.x == badge.x && run.y == badge.y)
                CHECK(run.text.find_first_not_of(U' ') == std::u32string::npos);
    }
    const auto& first = frame.front().text[trackColumns().size()];
    CHECK(first.text == U"  1");
    t.type(U"l-"); // chase, on the same track only
    CHECK_FALSE(t.track().chaseNotes);
    CHECK(t.track(1).chaseNotes);
    t.type(U"u");
    CHECK(t.track().chaseNotes);
}

TEST_CASE("musical track offsets are shown as they were typed")
{
    Tracking t;
    t.type(U"$3h");
    t.enter(U"+1/16-14t");
    const auto shown = [&]
    {
        std::u32string result;
        for (const auto& region : t.ws.frame())
            for (const auto& run : region.text)
                result += run.text;
        return result;
    };
    CHECK(shown().find(U"+1/16-14t") != std::u32string::npos);
    t.enter(U"+1/16-33t"); // nearer another fraction, whose name it doesn't take
    CHECK(shown().find(U"+1/16-33t") != std::u32string::npos);
    t.enter(U"-1/16+14t");
    CHECK(shown().find(U"-1/16+14t") != std::u32string::npos);
    t.enter(U"+200t");
    CHECK(shown().find(U"+200t") != std::u32string::npos);
    t.enter(U"-40ms");
    CHECK(shown().find(U"-40ms") != std::u32string::npos);
}

TEST_CASE("the note inspector aligns start and length coarse-fine pairs")
{
    Tracking t;
    const auto id = t.track().id();
    const auto step = core::NoteFraction{1, 16}.toTicks(t.document.song().ppqn());
    t.document.edit("Example note", {},
                    [&](core::Song& song)
                    {
                        song.updateEvents(
                            id,
                            [&](core::EventList& events)
                            {
                                events = {};
                                events.insert({step + 2, core::Note{33, 95, 3 * step + 1, 117}});
                            });
                    });
    t.ws.tracks().setCursor({id, step + 2, 33});
    t.type(U"gr");
    const auto frame = t.ws.frame();
    const auto find = [&](std::u32string_view text) -> TextRun
    {
        std::optional<TextRun> found;
        for (const auto& region : frame)
        {
            if (region.background == Role::Panel)
            {
                const auto run = std::ranges::find(region.text, text, &TextRun::text);
                if (run != region.text.end())
                {
                    found = *run;
                    break;
                }
            }
        }
        REQUIRE(found.has_value());
        return *found;
    };
    const auto pitch = find(U"Pitch:");
    const auto velocity = find(U"Velocity:");
    const auto off = find(U"Off Vel:");
    CHECK(pitch.x == velocity.x);
    CHECK(pitch.x == off.x);
    CHECK(velocity.y == pitch.y + 20);
    CHECK(off.y == velocity.y + 20);
    const auto start = find(U"Start:");
    const auto length = find(U"Length:");
    CHECK(start.x == length.x);
    CHECK(length.y == start.y + 20);
    CHECK(find(U"Fine(t)").x > start.x);
    const auto startCoarse = find(U"2");
    const auto startFine = find(U"+2");
    const auto lengthCoarse = find(U"3");
    const auto lengthFine = find(U"+1");
    CHECK(startFine.x == lengthFine.x);
    CHECK(startCoarse.x == lengthCoarse.x);
    // On the right of its column, as the values to its left are, under its heading.
    CHECK(startFine.x + 2 * 10 == find(U"Fine(t)").x + 7 * 10);
    CHECK(find(U"Note").y < pitch.y);
    CHECK(find(U"Note").style.foreground == Role::Accent);
    CHECK(find(U"Note").style.attributes == kBold);
    CHECK(find(U"Ends:").x == start.x);
    CHECK(find(U"Ends:").y == length.y + 20);
    CHECK(find(U"Units:").y == find(U"Note").y);
    CHECK(pitch.y == start.y);
    CHECK(startFine.y == startCoarse.y);
    CHECK(lengthFine.y == lengthCoarse.y);
    t.ws.resize(600, 400, {10, 20});
    t.type(U"e");
    for (int i = 0; i < 5; ++i)
        t.press(NamedKey::Tab);
    REQUIRE(t.ws.pianoRoll().inspectorEditor().heading() == "Position");
    for (const auto& region : t.ws.frame())
        if (region.background == Role::Panel)
            for (const auto& run : region.text)
                if (run.style.background == Role::Selection || run.style.background == Role::Cursor)
                {
                    CHECK(run.x >= 0);
                    CHECK(run.x + static_cast<int>(run.text.size()) * 10 <= 600);
                }
}

TEST_CASE("chasing toggles from the palette command for the current track and undoes")
{
    Tracking t;
    const auto before = t.document.song();
    t.ws.run("track-chase-toggle", 0);
    CHECK_FALSE(t.track().chaseNotes);
    CHECK(t.track(1).chaseNotes);
    CHECK(t.ws.message().find("Chase off") != std::string::npos);
    t.type(U"u");
    CHECK(t.document.song() == before);
    t.type(U"gc");
    t.ws.run("track-chase-toggle", 0);
    CHECK(t.document.song() == before);
    CHECK(t.ws.message().find("Select a track") != std::string::npos);
}

TEST_CASE("stepping a musical offset keeps the parts it was typed in")
{
    Tracking t;
    t.type(U"$3h");
    t.enter(U"+1/16-33t");
    const auto text = [&] { return core::offsetText(t.track().timeOffset); };
    t.type(U"_"); // a tick, in the fine part
    CHECK(text() == "+1/16-34t");
    t.type(U"+");
    CHECK(text() == "+1/16-33t");
    REQUIRE(t.track().grid == core::NoteFraction{1, 16});
    t.type(U"="); // a grid step, in the fraction
    CHECK(text() == "+1/8-33t");
    t.type(U"-");
    CHECK(text() == "+1/16-33t");
    t.type(U"-"); // down to nothing but the fine part
    CHECK(text() == "-33t");
    t.type(U"-"); // which is plain ticks from here on
    CHECK(t.track().timeOffset.amount() ==
          -33 - core::NoteFraction{1, 16}.toTicks(t.document.song().ppqn()));
    t.enter(U"-1/16+22t");
    t.type(U"-");
    CHECK(text() == "-1/8+22t");
    t.type(U"==");
    CHECK(text() == "+22t");
    t.enter(U"-40ms");
    t.type(U"=");
    CHECK(text() == "-39ms");
}

TEST_CASE("a stepped musical offset stays within the limit")
{
    Tracking t;
    t.type(U"$3h");
    t.enter(U"+64/1");
    t.type(U"=");
    CHECK(t.ws.message().find("limit") != std::string::npos);
    CHECK(core::offsetText(t.track().timeOffset) == "+64/1");
}

TEST_CASE("Tab in track field editing commits and passes over the buttons")
{
    Tracking t;
    t.press(NamedKey::Enter); // the name
    t.type(U"Renamed");
    t.press(NamedKey::Tab);
    CHECK(t.track().name == "Renamed");
    CHECK(t.ws.tracks().choosingPort());
    t.press(NamedKey::Tab, kShift);
    CHECK_FALSE(t.ws.tracks().choosingPort());
    CHECK(t.ws.tracks().inspectorEditor().heading() == "Name");
}

TEST_CASE("Enter toggles the track buttons without opening an editor")
{
    Tracking t;
    t.type(U"l"); // mute
    t.press(NamedKey::Enter);
    CHECK(t.track().mute);
    CHECK_FALSE(t.ws.tracks().inspectorEditor().active());
    t.press(NamedKey::Enter);
    CHECK_FALSE(t.track().mute);
    t.type(U"l"); // solo
    t.press(NamedKey::Enter);
    CHECK(t.track().solo);
    t.type(U"u");
    CHECK_FALSE(t.track().solo);
    t.type(U"l"); // chase, which e toggles too, the inspector having no field for it
    const bool chase = t.track().chaseNotes;
    t.press(NamedKey::Enter);
    CHECK(t.track().chaseNotes != chase);
    t.type(U"e");
    CHECK(t.track().chaseNotes == chase);
    CHECK_FALSE(t.ws.tracks().inspectorEditor().active());
}

TEST_CASE("Enter on a button sets selected rows to the opposite of the cursor's row")
{
    Tracking t;
    t.document.edit("Mute the first", {},
                    [&](core::Song& song)
                    {
                        song.updateTrack(song.tracks()[0]->id(),
                                         [](core::Track& track) { track.mute = true; });
                    });
    const auto before = t.document.song();
    t.type(U"lVj"); // the cursor on track 2, not muted; track 1 is
    t.press(NamedKey::Enter);
    CHECK(t.track(0).mute);
    CHECK(t.track(1).mute);
    t.press(NamedKey::Enter);
    CHECK_FALSE(t.track(0).mute);
    CHECK_FALSE(t.track(1).mute);
    t.press(NamedKey::Escape);
    t.type(U"uu");
    CHECK(t.document.song() == before);
}

TEST_CASE("a selected track row is an unbroken band across the columns")
{
    Tracking t;
    const auto bandEdges = [&]
    {
        // Where the row's text backgrounds cover, in cells from the first column.
        const auto frame = t.ws.frame();
        std::vector<bool> covered;
        for (const auto& run : frame.front().text)
        {
            if (run.y != 20 || run.style.background == Role::Background)
                continue;
            const auto first = static_cast<std::size_t>(run.x / 10 - 1);
            covered.resize(std::max(covered.size(), first + run.text.size()));
            std::fill_n(covered.begin() + static_cast<std::ptrdiff_t>(first), run.text.size(),
                        true);
        }
        return covered;
    };
    t.type(U"V");
    const auto covered = bandEdges();
    int width = 0;
    for (const auto& column : trackColumns())
        width += column.width;
    CHECK(covered.size() == static_cast<std::size_t>(width));
    CHECK(std::ranges::find(covered, false) == covered.end());
}

TEST_CASE("M, S and the track commands toggle the current track from every view")
{
    const auto view = GENERATE(as<std::u32string>{}, U"gt", U"ga", U"gr", U"ge");
    Tracking t;
    t.type(view);
    t.type(U"M");
    CHECK(t.track().mute);
    CHECK(t.ws.message() == "Mute on: " + t.track().name);
    t.type(U"S");
    CHECK(t.track().solo);
    CHECK(t.ws.message() == "Solo on: " + t.track().name);
    t.type(U"uu");
    CHECK_FALSE(t.track().mute);
    CHECK_FALSE(t.track().solo);
    t.type(U",tm");
    CHECK(t.track().mute);
    t.type(U",ts");
    CHECK(t.track().solo);
    const bool chase = t.track().chaseNotes;
    t.type(U",tc");
    CHECK(t.track().chaseNotes != chase);
}

TEST_CASE("the status line shows the cursor's time in every view, and the inspector doesn't")
{
    const auto view = GENERATE(as<std::u32string>{}, U"gt", U"ga", U"gr", U"ge");
    Tracking t;
    t.type(view);
    const auto frame = t.ws.frame();
    std::u32string status;
    for (const auto& run : frame.back().text)
        status += run.text;
    CHECK(status.find(U"1:1:000  0:00.000") != std::u32string::npos);
    for (const auto& region : frame)
        if (region.background == Role::Panel)
            for (const auto& run : region.text)
                CHECK(run.text != U"Time");
}

TEST_CASE("numeric columns' headings are right-aligned over their values")
{
    Tracking t;
    t.type(U"$3h");
    t.enter(U"-40ms");
    const auto frame = t.ws.frame();
    const auto& runs = frame.front().text;
    for (const auto& column : trackColumns())
    {
        const auto heading = std::ranges::find_if(
            runs,
            [&](const TextRun& run)
            {
                const auto start = run.text.find_first_not_of(U' ');
                const auto end = run.text.find_last_not_of(U' ');
                return run.y == 0 && start != std::u32string::npos &&
                       run.text.substr(start, end - start + 1) == decodeUtf8(column.heading);
            });
        REQUIRE(heading != runs.end());
        if (column.numeric)
        {
            CHECK(heading->text.ends_with(decodeUtf8(column.heading)));
        }
        else
        {
            CHECK(heading->text.starts_with(decodeUtf8(column.heading)));
        }
    }
    const auto value =
        std::ranges::find_if(runs, [](const TextRun& run) { return run.text.ends_with(U"-40ms"); });
    const auto last = std::ranges::find_if(runs, [](const TextRun& run)
                                           { return run.text.ends_with(U"Time offset"); });
    REQUIRE(value != runs.end());
    REQUIRE(last != runs.end());
    CHECK(value->x + static_cast<int>(value->text.size()) * 10 ==
          last->x + static_cast<int>(last->text.size()) * 10);
}

TEST_CASE("the name column grows with the longest name, up to a limit")
{
    Tracking t;
    const auto headingX = [&](std::u32string_view heading)
    {
        const auto frame = t.ws.frame();
        const auto run = std::ranges::find_if(
            frame.front().text, [&](const TextRun& candidate)
            { return candidate.y == 0 && candidate.text.starts_with(heading); });
        REQUIRE(run != frame.front().text.end());
        return run->x;
    };
    const auto shown = [&](std::u32string_view text)
    {
        const auto frame = t.ws.frame();
        return std::ranges::any_of(frame.front().text, [&](const TextRun& run)
                                   { return run.text.find(text) != run.text.npos; });
    };
    const int start = headingX(U"Name");
    const int usual = headingX(U"Mute");
    CHECK(usual - start == trackColumns()[1].width * 10);

    const std::u32string thirty = U"A name of thirty characters ok";
    REQUIRE(thirty.size() == 30);
    t.ws.resize(1000, 600, {10, 20}); // too narrow for the whole table
    t.enter(thirty);
    CHECK(shown(thirty));
    CHECK(headingX(U"Mute") - start == 31 * 10);

    // Cut short at 32 characters, the whole name being in the bar above and the inspector.
    const auto longer = thirty + U" and a good deal more than that, and then some";
    t.enter(longer);
    CHECK(headingX(U"Mute") - start == 33 * 10);
    CHECK(shown(thirty + U" …"));
    t.ws.resize(2000, 600, {10, 20});
    CHECK(shown(longer));

    // A wider window has room for more, without the table going past its edge.
    t.ws.resize(1800, 600, {10, 20});
    int others = 0;
    for (const auto& column : trackColumns())
        others += column.field == TrackField::Name ? 0 : column.width;
    CHECK(headingX(U"Mute") - start == (180 - 2 - others) * 10);

    t.type(U"uu");
    CHECK(headingX(U"Mute") - start == trackColumns()[1].width * 10);
}

TEST_CASE("a field that typing would replace is highlighted whole, until the cursor moves")
{
    Tracking t;
    const auto highlighted = [&]
    {
        std::u32string text;
        for (const auto& region : t.ws.frame())
            for (const auto& run : region.text)
                if (run.style.background == Role::Cursor)
                    text += run.text;
        return text;
    };
    const auto name = decodeUtf8(t.track().name);
    REQUIRE(name.size() > 1);
    t.press(NamedKey::Enter);
    CHECK(highlighted() == name);
    t.press(NamedKey::Left); // keeps the value, with a cursor in it
    CHECK(highlighted() == name.substr(name.size() - 1));
    t.type(U"x");
    CHECK(highlighted().size() == 1);
    t.press(NamedKey::Escape);
    t.press(NamedKey::Enter);
    CHECK(highlighted() == name);
    t.type(U"N"); // replaces it, and the cursor follows
    CHECK(highlighted() == U" ");
    t.press(NamedKey::Enter);
    CHECK(t.track().name == "N");
}

TEST_CASE("Backspace and Delete clear a value highlighted whole, and only then")
{
    const auto key = GENERATE(NamedKey::Backspace, NamedKey::Delete);
    Tracking t;
    t.enter(U"Bassline");
    t.press(NamedKey::Enter);
    t.press(key);
    CHECK(t.ws.tracks().inspectorEditor().line().text().empty());
    t.type(U"Keys");
    t.press(NamedKey::Enter);
    CHECK(t.track().name == "Keys");

    // With the cursor moved, they take a character.
    t.press(NamedKey::Enter);
    t.press(NamedKey::Left);
    t.press(key);
    CHECK(t.ws.tracks().inspectorEditor().line().text().size() == 3);
    t.press(NamedKey::Escape);
    CHECK(t.track().name == "Keys");

    // Left empty, Enter sets it, or says why it can't be.
    t.press(NamedKey::Enter);
    t.press(key);
    t.press(NamedKey::Enter);
    CHECK(t.track().name.empty());
    t.type(U"5l"); // channel
    REQUIRE(t.track().channel > 0);
    const auto channel = t.track().channel;
    t.press(NamedKey::Enter);
    t.press(key);
    t.press(NamedKey::Enter);
    CHECK(t.track().channel == channel);
    CHECK(t.ws.message() == "Enter a whole number");
}

TEST_CASE("M, S and , t c toggle selected rows in the tracks view, as one undo step")
{
    Tracking t;
    const auto before = t.document.song();
    t.type(U"Vj"); // the cursor on track 2
    t.type(U"M");
    CHECK(t.track(0).mute);
    CHECK(t.track(1).mute);
    CHECK_FALSE(t.track(2).mute);
    CHECK(t.ws.mode() == Mode::Visual);
    t.type(U"S");
    CHECK(t.track(0).solo);
    CHECK(t.track(1).solo);
    const auto chase = t.track(1).chaseNotes;
    t.type(U",tc");
    CHECK(t.track(0).chaseNotes != chase);
    CHECK(t.track(1).chaseNotes != chase);
    t.type(U"M");
    CHECK_FALSE(t.track(0).mute);
    CHECK_FALSE(t.track(1).mute);
    t.press(NamedKey::Escape);
    t.type(U"uuuu");
    CHECK(t.document.song() == before);
}

TEST_CASE("M and S on a selection are for the tracks view")
{
    const auto view = GENERATE(as<std::u32string>{}, U"ga", U"gr", U"ge");
    Tracking t;
    t.type(view);
    t.type(U"V");
    REQUIRE(t.ws.mode() == Mode::Visual);
    const auto before = t.document.song();
    t.type(U"M");
    t.type(U"S");
    CHECK(t.document.song() == before);
    CHECK(t.ws.mode() == Mode::Visual);
}

TEST_CASE("the conductor has no mute or solo to toggle")
{
    Tracking t;
    const auto before = t.document.song();
    t.type(U"gcM");
    CHECK(t.ws.message() == "Select a track to mute");
    t.type(U"S");
    CHECK(t.ws.message() == "Select a track to solo");
    CHECK(t.document.song() == before);
}

namespace
{
const std::u32string_view kToProgram = U"8l";

bool inspectorHas(const Frame& frame, std::u32string_view wanted)
{
    for (const auto& region : frame)
    {
        for (const auto& run : region.text)
        {
            if (run.text.find(wanted) != std::u32string::npos)
            {
                return true;
            }
        }
    }
    return false;
}
} // namespace

TEST_CASE("the Program column sets a track's setup, with empty leaving it unset")
{
    Tracking t;
    t.type(kToProgram);
    CHECK(t.track().programSetup.empty());
    t.enter(U"12");
    CHECK(t.track().programSetup.program == 12);
    t.enter(U"0");
    CHECK(t.track().programSetup.program == 0); // not the same as unset

    t.press(NamedKey::Enter);
    t.press(NamedKey::Backspace);
    t.press(NamedKey::Enter);
    CHECK_FALSE(t.track().programSetup.program);
    t.type(U"u");
    CHECK(t.track().programSetup.program == 0);

    t.enter(U"128");
    CHECK(t.ws.tracks().typing());
    t.press(NamedKey::Escape);
    CHECK(t.track().programSetup.program == 0);
}

TEST_CASE("Tab goes through the bank fields and Program, and on to the later columns")
{
    Tracking t;
    t.type(U"6l"); // the bank's MSB
    t.press(NamedKey::Enter);
    t.type(U"1");
    t.press(NamedKey::Tab);
    REQUIRE(t.ws.tracks().inspectorEditor().heading() == "LSB");
    t.type(U"2");
    t.press(NamedKey::Tab);
    REQUIRE(t.ws.tracks().inspectorEditor().heading() == "Program");
    t.type(U"5");
    t.press(NamedKey::Tab);
    CHECK(t.ws.tracks().inspectorEditor().heading() == "Transpose");
    CHECK(t.track().programSetup == core::ProgramSetup{1, 2, 5});
    t.press(NamedKey::Tab, kShift);
    CHECK(t.ws.tracks().inspectorEditor().heading() == "Program");
    t.press(NamedKey::Escape);
    CHECK(t.track().programSetup == core::ProgramSetup{1, 2, 5});
}

TEST_CASE("a track field is edited in its cell, not the inspector")
{
    Tracking t;
    t.type(U"5l"); // channel, numeric and so on the right of its column
    const auto cellRun = [&](Role background)
    {
        const auto frame = t.ws.frame();
        std::optional<TextRun> found;
        for (const auto& run : frame.front().text)
        {
            if (run.style.background == background && run.y == 20)
            {
                found = run;
            }
        }
        return found;
    };
    const auto before = cellRun(Role::Cursor);
    REQUIRE(before);
    const auto beforeEnd = before->x + static_cast<int>(before->text.size()) * 10;
    t.press(NamedKey::Enter);
    const auto editing = cellRun(Role::Cursor); // the value, highlighted whole
    REQUIRE(editing);
    CHECK(editing->x + static_cast<int>(editing->text.size()) * 10 == beforeEnd);
    t.type(U"1");
    REQUIRE(cellRun(Role::Selection));
    CHECK(cellRun(Role::Selection)->text.find(U'1') != std::u32string::npos);
    CHECK_FALSE(inspectorHas(t.ws.frame(), U"Channel "));
}

TEST_CASE("a track's grid and default note are edited in the table")
{
    Tracking t;
    t.type(U"$h"); // the default length, before the velocity at the end
    CHECK_FALSE(t.track().noteLength);
    t.enter(U"1/8t");
    REQUIRE(t.track().noteLength);
    CHECK(*t.track().noteLength == core::NoteFraction{1, 12});
    t.enter(U"grid");
    CHECK_FALSE(t.track().noteLength);
    t.enter(U"nonsense");
    CHECK(t.ws.tracks().typing());
    t.press(NamedKey::Escape);

    t.type(U"h");
    t.enter(U"1/8.");
    CHECK(t.track().grid == core::NoteFraction{3, 16});

    t.type(U"$");
    t.enter(U"64");
    CHECK(t.track().noteVelocity == 64);
    t.type(U"=");
    CHECK(t.track().noteVelocity == 65);
    t.enter(U"0");
    CHECK(t.ws.message() == "The velocity must be 1 to 127");
    t.press(NamedKey::Escape);
    t.type(U"u");
    CHECK(t.track().noteVelocity == 64);
}

TEST_CASE("stepping a setup starts from unset, and a selection is set together")
{
    Tracking t;
    t.type(kToProgram);
    t.type(U"=");
    CHECK(t.track().programSetup.program == 0);
    t.type(U"=");
    CHECK(t.track().programSetup.program == 1);
    t.type(U"Vj");
    t.enter(U"40");
    CHECK(t.track(0).programSetup.program == 40);
    CHECK(t.track(1).programSetup.program == 40);
    t.press(NamedKey::Escape);
    t.type(U"u");
    CHECK(t.track(0).programSetup.program == 1);
    CHECK_FALSE(t.track(1).programSetup.program);
}

TEST_CASE("a track with its own program changes is marked, and says where they are")
{
    Tracking t;
    const auto id = t.track().id();
    t.document.edit("Add programs", {},
                    [&](core::Song& song)
                    {
                        const auto q = song.ppqn().quarter();
                        song.updateEvents(id,
                                          [&](core::EventList& events)
                                          {
                                              events.insert({4 * q, core::ProgramChange{3}});
                                              events.insert({8 * q, core::ProgramChange{4}});
                                          });
                    });
    t.type(kToProgram);
    const auto frame = t.ws.frame();
    CHECK(inspectorHas(frame, U"Program events"));
    CHECK(inspectorHas(frame, U"2, first at"));
    bool marked = false;
    for (const auto& region : frame)
    {
        for (const auto& run : region.text)
        {
            marked = marked || run.text.ends_with(U"-•");
        }
    }
    CHECK(marked);
}

TEST_CASE("tracks on one channel with different setups are warned about, but allowed")
{
    Tracking t;
    t.document.edit("Share a channel", {},
                    [&](core::Song& song)
                    {
                        const auto tracks = song.tracks();
                        song.updateTrack(tracks[1]->id(), [&](core::Track& track)
                                         { track.channel = tracks[0]->channel; });
                        song.updateTrack(tracks[1]->id(),
                                         [&](core::Track& track) { track.port = tracks[0]->port; });
                        song.updateTrack(tracks[1]->id(), [](core::Track& track)
                                         { track.programSetup.program = 2; });
                    });
    const auto warned = [&]
    {
        const auto frame = t.ws.frame();
        for (const auto& region : frame)
        {
            for (const auto& run : region.text)
            {
                if (run.style.foreground == Role::Warning &&
                    run.text.find(U'2') != std::u32string::npos)
                {
                    return true;
                }
            }
        }
        return false;
    };
    t.type(kToProgram);
    t.type(U"j");
    t.type(U"k");
    CHECK_FALSE(warned());

    t.enter(U"1");
    CHECK(t.track(0).programSetup.program == 1);
    t.type(U"l");
    CHECK(warned());
    t.type(U"h");
    CHECK(inspectorHas(t.ws.frame(), U"Differs from tracks on this channel"));

    t.enter(U"2");
    CHECK_FALSE(warned());
    CHECK_FALSE(inspectorHas(t.ws.frame(), U"Differs from tracks on this channel"));
}

TEST_CASE("the bar shows a track's setup when it has one")
{
    Tracking t;
    CHECK_FALSE(inspectorHas(t.ws.frame(), U"Program:"));
    t.type(kToProgram);
    t.enter(U"12");
    CHECK(inspectorHas(t.ws.frame(), U"Program:"));
    CHECK_FALSE(inspectorHas(t.ws.frame(), U"Bank:"));
    t.type(U"2h");
    t.enter(U"3");
    CHECK(inspectorHas(t.ws.frame(), U"Bank:"));
    CHECK(inspectorHas(t.ws.frame(), U" 3/-"));

    t.type(U"ge");
    CHECK(inspectorHas(t.ws.frame(), U"Program:"));
    CHECK(inspectorHas(t.ws.frame(), U" 3/-"));
}

namespace
{
// A workspace with some output ports, on the Output port column.
struct ChoosingPorts : Tracking
{
    ChoosingPorts()
    {
        ws.setPortFinder(
            [this](std::string_view)
            {
                Workspace::PortLookup lookup;
                lookup.ports = ports;
                return lookup;
            });
        type(U"4l");
    }
    std::vector<std::string> ports{"Pro-800", "loopMIDI Port 1", "loopMIDI Port 2"};

    std::vector<std::u32string> listed()
    {
        std::vector<std::u32string> rows;
        const auto frame = ws.frame();
        for (const auto& run : frame[1].text)
        {
            auto text = run.text;
            text.erase(text.find_last_not_of(U' ') + 1);
            rows.push_back(text.substr(text.find_first_not_of(U' ')));
        }
        return rows;
    }
};
} // namespace

TEST_CASE("a track's port is chosen from a list of the ports there are")
{
    ChoosingPorts t;
    t.press(NamedKey::Enter);
    REQUIRE(t.ws.tracks().choosingPort());
    CHECK(t.listed() == std::vector<std::u32string>{U"default", U"Pro-800", U"loopMIDI Port 1",
                                                    U"loopMIDI Port 2"});

    t.press(NamedKey::Down);
    t.press(NamedKey::Enter);
    CHECK_FALSE(t.ws.tracks().choosingPort());
    CHECK(t.track().port == "Pro-800");

    SECTION("it opens on the current port, and Escape leaves it")
    {
        t.press(NamedKey::Enter);
        t.press(NamedKey::Down);
        t.press(NamedKey::Escape);
        CHECK_FALSE(t.ws.tracks().choosingPort());
        CHECK(t.track().port == "Pro-800");
        t.press(NamedKey::Enter);
        t.press(NamedKey::Down);
        t.press(NamedKey::Enter);
        CHECK(t.track().port == "loopMIDI Port 1");
    }
    SECTION("typing narrows the list, in any case, and Backspace widens it")
    {
        t.press(NamedKey::Enter);
        t.type(U"PORT 2");
        CHECK(t.listed() == std::vector<std::u32string>{U"loopMIDI Port 2"});
        t.press(NamedKey::Backspace);
        CHECK(t.listed() == std::vector<std::u32string>{U"loopMIDI Port 1", U"loopMIDI Port 2"});
        t.type(U"2");
        t.press(NamedKey::Enter);
        CHECK(t.track().port == "loopMIDI Port 2");
    }
    SECTION("nothing can be chosen that isn't there")
    {
        t.press(NamedKey::Enter);
        t.type(U"nothing like it");
        CHECK(t.listed() == std::vector<std::u32string>{U"No port matches"});
        t.press(NamedKey::Enter);
        CHECK(t.ws.tracks().choosingPort());
        CHECK(t.track().port == "Pro-800");
        CHECK(t.ws.message() == "No port matches that");
        t.press(NamedKey::Escape);
    }
    SECTION("default goes back to the default port, and undo restores the last")
    {
        t.press(NamedKey::Enter);
        t.type(U"default");
        t.press(NamedKey::Enter);
        CHECK(t.track().port.empty());
        t.type(U"u");
        CHECK(t.track().port == "Pro-800");
    }
    SECTION("a selection all takes the port, in one undo step")
    {
        t.type(U"Vj"); // the cursor on the second, on the default port
        t.press(NamedKey::Enter);
        t.press(NamedKey::Down);
        t.press(NamedKey::Down);
        t.press(NamedKey::Enter);
        CHECK(t.track(0).port == "loopMIDI Port 1");
        CHECK(t.track(1).port == "loopMIDI Port 1");
        t.press(NamedKey::Escape);
        t.type(U"u");
        CHECK(t.track(0).port == "Pro-800");
        CHECK(t.track(1).port.empty());
    }
    SECTION("a selection takes the port the cursor's track already has")
    {
        t.type(U"Vj");
        t.type(U"o"); // the cursor back on the first, on Pro-800
        t.press(NamedKey::Enter);
        t.press(NamedKey::Enter);
        CHECK(t.track(0).port == "Pro-800");
        CHECK(t.track(1).port == "Pro-800");
    }
    SECTION("Tab takes the one chosen, and goes on")
    {
        t.press(NamedKey::Enter);
        t.press(NamedKey::Down);
        t.press(NamedKey::Tab);
        CHECK(t.track().port == "loopMIDI Port 1");
        CHECK(t.ws.tracks().inspectorEditor().heading() == "Channel");
        t.press(NamedKey::Escape);
    }
}

TEST_CASE("a long list of ports shows a few at a time, following the choice")
{
    ChoosingPorts t;
    t.ports.clear();
    for (int i = 0; i < 300; ++i)
    {
        t.ports.push_back("Port " + std::to_string(1000 + i));
    }
    t.press(NamedKey::Enter);
    CHECK(t.listed().size() == 8);
    for (int i = 0; i < 20; ++i)
    {
        t.press(NamedKey::Down);
    }
    CHECK(t.listed().size() == 8);
    CHECK(t.listed().back() == U"Port 1019");
    t.press(NamedKey::Enter);
    CHECK(t.track().port == "Port 1019");
}

TEST_CASE("a port that isn't there stays until something else is chosen")
{
    ChoosingPorts t;
    t.document.edit("Set a port", {},
                    [&](core::Song& song)
                    {
                        song.updateTrack(song.tracks()[0]->id(),
                                         [](core::Track& track) { track.port = "Gone"; });
                    });
    t.press(NamedKey::Enter);
    CHECK(t.listed().back() == U"Gone (not found)");
    t.press(NamedKey::Enter); // on it already
    CHECK(t.track().port == "Gone");
}

namespace
{
std::string order(const Tracking& t, std::size_t n = 5)
{
    std::string names;
    for (std::size_t i = 0; i < n; ++i)
    {
        names += t.track(i).name.substr(0, 1);
    }
    return names;
}
constexpr const char* kStart = "DBPSL"; // Drums, Bass, Piano, Strings, Lead
} // namespace

TEST_CASE("reordering moves a track through the others until Enter or Escape")
{
    Tracking t;
    CHECK(order(t) == kStart);
    t.type(U",to");
    t.type(U"jj");
    CHECK(order(t) == "BPDSL");
    CHECK(t.ws.tracks().cursor().track == t.track(2).id()); // the cursor stays on it

    SECTION("Enter accepts it, as one undo step")
    {
        t.press(NamedKey::Down);
        t.press(NamedKey::Up);
        t.press(NamedKey::Enter);
        CHECK(order(t) == "BPDSL");
        t.type(U"j"); // an ordinary key again
        CHECK(t.ws.tracks().cursor().track == t.track(3).id());
        t.type(U"u");
        CHECK(order(t) == kStart);
        t.press(NamedKey::Tab); // nothing, in Normal mode
        CHECK_FALSE(t.document.canUndo());
    }
    SECTION("Escape puts it back, with no undo step")
    {
        t.press(NamedKey::Escape);
        CHECK(order(t) == kStart);
        CHECK_FALSE(t.document.canUndo());
        CHECK_FALSE(t.document.modified());
        CHECK(t.ws.tracks().cursor().track == t.track(0).id());
    }
    SECTION("each is its own step, however soon the next")
    {
        t.press(NamedKey::Enter);
        t.type(U",to");
        t.type(U"k");
        t.press(NamedKey::Enter);
        CHECK(order(t) == "BDPSL");
        t.type(U"u");
        CHECK(order(t) == "BPDSL");
        t.type(U"u");
        CHECK(order(t) == kStart);
    }
    SECTION("it stops at the ends, and counts and gg and G work")
    {
        t.type(U"kkkkkk");
        CHECK(order(t) == kStart);
        t.type(U"3j");
        CHECK(order(t) == "BPSDL");
        t.type(U"gg");
        CHECK(order(t) == kStart);
        t.type(U"G");
        const auto last = t.document.song().tracks().size() - 1;
        CHECK(t.track(last).name == "Drums");
        t.type(U"99j");
        CHECK(t.track(last).name == "Drums");
        t.press(NamedKey::Escape);
        CHECK(order(t) == kStart);
    }
    SECTION("every other key does nothing")
    {
        const auto before = t.document.song();
        t.type(U"dupxM ");
        t.press(NamedKey::Tab);
        t.press(NamedKey::Backspace);
        CHECK(t.document.song() == before);
        t.type(U"j");
        CHECK(order(t) == "BPSDL"); // still moving
        t.press(NamedKey::Escape);
    }
}

TEST_CASE("a V selection of tracks moves as a block, and accepting ends the selection")
{
    Tracking t;
    t.type(U"Vj"); // Drums and Bass
    t.type(U",to");
    t.type(U"j");
    CHECK(order(t) == "PDBSL");
    SECTION("Enter")
    {
        t.press(NamedKey::Enter);
        CHECK(t.ws.mode() == Mode::Normal);
        t.type(U"u");
        CHECK(order(t) == kStart);
    }
    SECTION("Escape leaves it selected")
    {
        t.press(NamedKey::Escape);
        CHECK(order(t) == kStart);
        CHECK(t.ws.mode() == Mode::Visual);
        t.press(NamedKey::Escape);
    }
    SECTION("the whole block stops at the end")
    {
        t.type(U"G");
        const auto last = t.document.song().tracks().size() - 1;
        CHECK(t.track(last).name == "Bass");
        CHECK(t.track(last - 1).name == "Drums");
        t.press(NamedKey::Escape);
        t.press(NamedKey::Escape);
    }
}

TEST_CASE("Alt-Up and Alt-Down move a track at once, undoing together")
{
    Tracking t;
    t.press(NamedKey::Down, kAlt);
    CHECK(order(t) == "BDPSL");
    CHECK(t.ws.tracks().cursor().track == t.track(1).id());
    t.press(NamedKey::Down, kAlt);
    CHECK(order(t) == "BPDSL");
    t.type(U"j"); // a move of the cursor, and then the next track
    t.press(NamedKey::Up, kAlt);
    CHECK(order(t) == "BPSDL");
    t.type(U"u");
    CHECK(order(t) == kStart);

    t.type(U"gg");
    t.press(NamedKey::Up, kAlt);
    CHECK(order(t) == kStart);
    CHECK(t.ws.message() == "Already the first track");

    // Visual: the block, staying selected.
    t.type(U"Vj");
    t.press(NamedKey::Down, kAlt);
    CHECK(order(t) == "PDBSL");
    CHECK(t.ws.mode() == Mode::Visual);
    t.press(NamedKey::Escape);
}

TEST_CASE("the tracks being moved are shown, and the status line says what to do")
{
    Tracking t;
    t.type(U",toj");
    const auto frame = t.ws.frame();
    bool band = false;
    for (const auto& run : frame.front().text)
    {
        band = band || (run.y == 2 * 20 && run.style.background == Role::Selection);
    }
    CHECK(band);
    CHECK(inspectorHas(frame, U" MOVE "));
    CHECK(inspectorHas(frame, U"Enter accepts"));
    t.press(NamedKey::Enter);
    CHECK_FALSE(inspectorHas(t.ws.frame(), U" MOVE "));
}
