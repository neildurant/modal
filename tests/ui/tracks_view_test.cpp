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
    t.type(U"2l");
    t.enter(U"My MIDI output");
    CHECK(t.track().port == "My MIDI output");
    t.type(U"l");
    t.enter(U"10");
    CHECK(t.track().channel == 10);
    t.type(U"l");
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
    t.type(U"6lVj"); // transpose, first two tracks
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
    t.type(U"h");
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
    t.type(U"$");
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
    t.type(U"$");
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
    t.type(U"$");
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
    t.type(U"$");
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
    CHECK(t.ws.tracks().inspectorEditor().heading() == "Output port");
    t.press(NamedKey::Tab, kShift);
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
    t.type(U"$");
    t.enter(U"-40ms");
    const auto frame = t.ws.frame();
    const auto& runs = frame.front().text;
    for (const auto& column : trackColumns())
    {
        const auto heading = std::ranges::find_if(
            runs,
            [&](const TextRun& run)
            {
                return run.y == 0 &&
                       run.text.find(decodeUtf8(column.heading)) != std::u32string::npos;
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
    t.ws.resize(1200, 600, {10, 20});
    int others = 0;
    for (const auto& column : trackColumns())
        others += column.field == TrackField::Name ? 0 : column.width;
    CHECK(headingX(U"Mute") - start == (120 - 2 - others) * 10);

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
            if (region.background == Role::Panel)
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
    t.type(U"6l"); // channel
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
