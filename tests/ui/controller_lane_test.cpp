// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

constexpr CellSize kCell{10, 20};

// A synth part in 4/4: a note in bar 1 and one in bar 2; the mod wheel (CC 1) at 10, 40 and 80
// on bar 1's first three beats; pitch bend at +1000 in bar 2 and back to 0 on its third beat;
// and the volume (CC 7) at 100 at the start.
core::Song synth(core::Ppqn ppqn)
{
    const auto q = ppqn.quarter();
    core::Song song{ppqn};
    const auto id = song.addTrack("Synth");
    song.updateEvents(id,
                      [&](core::EventList& events)
                      {
                          events.insert({0, core::Note{60, 90, q}});
                          events.insert({4 * q, core::Note{62, 90, q}});
                          events.insert({0, core::ControlChange{1, 10}});
                          events.insert({q, core::ControlChange{1, 40}});
                          events.insert({2 * q, core::ControlChange{1, 80}});
                          events.insert({0, core::ControlChange{7, 100}});
                          events.insert({4 * q, core::PitchBend{1000}});
                          events.insert({6 * q, core::PitchBend{0}});
                      });
    song.fitToContent();
    return song;
}

struct Lanes
{
    explicit Lanes(core::Ppqn ppqn = core::kPpqn)
        : ppqn{ppqn}
        , document{synth(ppqn)}
    {
        registry.add(windowCommands());
        ws.setLineHandler([&](const std::string& line) { registry.runLine(line); });
        ws.resize(120 * kCell.width, 30 * kCell.height, kCell);
        ws.show(ViewKind::Arrangement);
        type(U"gr");
    }

    // Keys typed, \t for Tab and \v for Shift-Tab.
    void type(std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            ws.handle(key == U'\t'   ? Key::of(NamedKey::Tab)
                      : key == U'\v' ? Key::of(NamedKey::Tab, kShift)
                                     : Key::typed(key),
                      0);
        }
    }
    void press(const Key& key) { ws.handle(key, 0); }
    void enter() { press(Key::of(NamedKey::Enter)); }
    void line(std::u32string_view text)
    {
        type(U":");
        type(text);
        enter();
    }

    core::Tick q() const { return ppqn.quarter(); }
    const core::Track& track() const { return *document.song().tracks()[0]; }
    const PianoRollView& view() const { return ws.pianoRoll(); }

    // The values of the events the lane shows, in time.
    std::vector<int> values(const core::Lane& lane) const
    {
        std::vector<int> result;
        for (const auto& event : document.song().events(track().id()).all())
        {
            if (lane.shows(event.data) && !event.isNote())
            {
                result.push_back(lane.value(event.data));
            }
        }
        return result;
    }

    core::Ppqn ppqn;
    core::Document document;
    CommandRegistry registry;
    Workspace ws{registry, document};
};

const core::Lane kModWheel{.kind = core::Lane::Kind::Controller, .controller = 1};
const core::Lane kBend{.kind = core::Lane::Kind::PitchBend};

} // namespace

TEST_CASE(":lane chooses what the lane shows, kept with the track and undone as an edit")
{
    Lanes l;
    CHECK(l.view().laneLabel() == "vel");
    l.line(U"lane cc1");
    CHECK(l.track().lane == kModWheel);
    CHECK(l.view().laneLabel() == "cc1");
    l.line(U"lane bend");
    CHECK(l.track().lane == kBend);
    // Not a lane: nothing changes.
    CHECK(l.registry.runLine("lane cc200").message.starts_with("A lane is velocity"));
    CHECK(l.track().lane == kBend);
    // Choosing one after another undoes as one.
    l.type(U"u");
    CHECK(l.track().lane == core::Lane{});
}

TEST_CASE("]c and [c step through the controllers the track has")
{
    Lanes l;
    l.type(U"gl");
    l.type(U"]c");
    CHECK(l.track().lane == kModWheel);
    l.type(U"]c");
    CHECK(l.track().lane.controller == 7);
    l.type(U"]c");
    CHECK(l.track().lane == kBend);
    l.type(U"]c");
    CHECK(l.track().lane == core::Lane{});
    l.type(U"[c");
    CHECK(l.track().lane == kBend);
}

TEST_CASE("the lane's list, , c c, has Velocity first, then the latest used, then the track's own")
{
    Lanes l;
    l.type(U",cc");
    REQUIRE(l.view().laneChooser() != nullptr);
    // Narrowed by typing: 1 is the mod wheel, the track's own, before any others with a 1.
    l.type(U"1");
    l.enter();
    CHECK(l.track().lane == kModWheel);
    CHECK(l.view().laneChooser() == nullptr);
    l.line(U"lane bend");
    // The list opens on the lane shown: bend, now the latest used, second after Velocity; and
    // the mod wheel, used before it, third.
    l.type(U",cc");
    REQUIRE(l.view().laneChooser() != nullptr);
    CHECK(l.view().laneChooser()->chosen() == 1);
    l.press(Key::of(NamedKey::Down));
    l.enter();
    CHECK(l.track().lane == kModWheel);
    // Velocity is always first.
    l.type(U",cc");
    l.press(Key::of(NamedKey::Up));
    l.press(Key::of(NamedKey::Up));
    l.enter();
    CHECK(l.track().lane == core::Lane{});
    l.line(U"lane cc1");
    // Escape leaves it as it was.
    l.type(U",cc");
    l.press(Key::of(NamedKey::Escape));
    CHECK(l.view().laneChooser() == nullptr);
    CHECK(l.track().lane == kModWheel);
}

TEST_CASE("in a controller's lane, Tab goes event to event, h and l a grid step, H and L a nudge")
{
    Lanes l(GENERATE(core::kPpqn, core::Ppqn{96}));
    l.line(U"lane cc1");
    l.type(U"gggl");
    REQUIRE(l.view().laneNote());
    CHECK(l.view().time() == 0);
    l.type(U"\t");
    CHECK(l.view().time() == l.q());
    l.press(Key::of(NamedKey::Tab));
    CHECK(l.view().time() == 2 * l.q());
    l.type(U"\v");
    CHECK(l.view().time() == l.q());
    // A nudge on, there's no event; Tab goes on to the next.
    l.type(U"L");
    CHECK(l.view().time() > l.q());
    CHECK_FALSE(l.view().laneNote());
    // Where nothing's lit, a line in the brightest colour shows where the cursor is.
    const auto lit = [&]
    {
        const auto frame = l.ws.frame();
        return std::ranges::count_if(
            frame,
            [](const Region& region)
            {
                return std::ranges::any_of(
                    region.over, [](const Shape& shape)
                    { return shape.role == Role::Text && shape.rect.height >= kCell.height; });
            });
    };
    CHECK(lit() == 1);
    l.press(Key::typed(U'a', kCtrl));
    CHECK(l.ws.message() == "No event here: Tab goes to the next");
    l.press(Key::of(NamedKey::Tab));
    CHECK(l.view().time() == 2 * l.q());
    CHECK(lit() == 0);
}

TEST_CASE("a controller's values change as velocities do, and the inspector shows the event")
{
    Lanes l;
    l.line(U"lane cc1");
    l.type(U"gggl");
    l.press(Key::typed(U'a', kCtrl));
    CHECK(l.values(kModWheel) == std::vector<int>{11, 40, 80});
    l.type(U"50");
    l.enter();
    CHECK(l.values(kModWheel) == std::vector<int>{50, 40, 80});
    // Enter alone asks: +5 on the next.
    l.type(U"\t");
    l.enter();
    l.type(U"+5");
    l.enter();
    CHECK(l.values(kModWheel) == std::vector<int>{50, 45, 80});
    // A v selection over all three, raised by 10, the highest stopping at 127.
    l.type(U"0");
    l.type(U"gg");
    l.type(U"v2\t");
    l.type(U"10");
    l.press(Key::typed(U'a', kCtrl));
    CHECK(l.values(kModWheel) == std::vector<int>{60, 55, 90});
    // The volume and notes are untouched.
    CHECK(l.values({.kind = core::Lane::Kind::Controller, .controller = 7}) ==
          std::vector<int>{100});
}

TEST_CASE("a pitch bend lane goes up and down from the middle, and takes negative changes")
{
    Lanes l;
    l.line(U"lane bend");
    l.type(U"gl");
    l.press(Key::of(NamedKey::Tab));
    REQUIRE(l.view().laneNote());
    CHECK(l.values(kBend) == std::vector<int>{1000, 0});
    l.enter();
    l.type(U"-1500");
    l.enter();
    CHECK(l.values(kBend) == std::vector<int>{-500, 0});
    l.type(U"100");
    l.press(Key::typed(U'x', kCtrl));
    CHECK(l.values(kBend) == std::vector<int>{-600, 0});
}

TEST_CASE("a Ctrl-v box in a controller's lane takes the events with values inside it")
{
    Lanes l;
    l.line(U"lane cc1");
    l.type(U"gggl");
    // From 10 to 80 takes all three; lowering the top edge to 70 leaves 80 out.
    l.press(Key::typed(U'v', kCtrl));
    l.type(U"2\t");
    CHECK(l.view().laneTaken().size() == 3);
    l.type(U"J");
    CHECK(l.view().laneTaken().size() == 2);
    CHECK(l.view().selectionStatus()->starts_with("2 events, values 10–70"));
}

TEST_CASE("H and L nudge the cursor off the grid in the notes, and o adds a note there")
{
    Lanes l;
    l.type(U"ggL");
    const auto at = l.view().time();
    CHECK(at > 0);
    CHECK(at < l.q() / 4); // short of the first grid step
    l.type(U"o");
    const auto& events = l.document.song().events(l.track().id()).all();
    CHECK(std::ranges::any_of(events,
                              [&](const core::Event& e) { return e.isNote() && e.tick == at; }));
    // h goes back to the grid.
    l.type(U"h");
    CHECK(l.view().time() == 0);
    // In the velocity lane, h and l go note to note, and H and L are refused.
    l.type(U"gl");
    l.type(U"H");
    CHECK(l.ws.message().starts_with("H and L move by a nudge in the notes"));
}

TEST_CASE("o adds a controller's event at the cursor, at the value before it")
{
    Lanes l;
    l.line(U"lane cc1");
    l.type(U"gggl");
    // Half a beat on, between 10 and 40: o takes 10.
    l.type(U"\t");
    REQUIRE(l.view().time() == l.q());
    l.type(U"\v");
    l.type(U"LLLL");
    const auto at = l.view().time();
    REQUIRE_FALSE(l.view().laneNote());
    l.type(U"o");
    CHECK(l.values(kModWheel) == std::vector<int>{10, 10, 40, 80});
    CHECK(l.view().laneNote());
    // Again there is refused: there's one already.
    l.type(U"o");
    CHECK(l.ws.message().starts_with("There's an event here already"));
    // A number and Enter on an empty spot adds one at that value; Enter alone asks.
    l.type(U"L");
    l.type(U"30");
    l.enter();
    CHECK(l.values(kModWheel) == std::vector<int>{10, 10, 30, 40, 80});
    l.type(U"L");
    l.enter();
    l.type(U"+5");
    l.enter();
    CHECK(l.values(kModWheel) == std::vector<int>{10, 10, 30, 35, 40, 80});
    CHECK(at > 0);
    // In the velocity lane, there's nothing to add.
    l.line(U"lane velocity");
    l.type(U"o");
    CHECK(l.ws.message().starts_with("A velocity belongs to its note"));
}

TEST_CASE("x deletes a controller's event, and d a selection's, into the register")
{
    Lanes l;
    l.line(U"lane cc1");
    l.type(U"gggl");
    l.type(U"x");
    CHECK(l.values(kModWheel) == std::vector<int>{40, 80});
    l.type(U"u");
    CHECK(l.values(kModWheel) == std::vector<int>{10, 40, 80});
    l.type(U"v\td");
    CHECK(l.values(kModWheel) == std::vector<int>{80});
    // The notes, the volume and the bend are untouched.
    CHECK(l.values({.kind = core::Lane::Kind::Controller, .controller = 7}) ==
          std::vector<int>{100});
    CHECK(l.values(kBend) == std::vector<int>{1000, 0});
}

TEST_CASE("Alt-h and Alt-l move a controller's event a grid step, Alt-H and Alt-L a nudge")
{
    Lanes l;
    l.line(U"lane cc1");
    l.type(U"gggl");
    l.press(Key::of(NamedKey::Tab)); // the 40, on beat 2
    l.press(Key::typed(U'l', kAlt));
    // A sixteenth later, the cursor going with it.
    CHECK(l.view().time() == l.q() + l.q() / 4);
    REQUIRE(l.view().laneNote());
    l.press(Key::typed(U'l', kAlt | kShift));
    CHECK(l.view().time() > l.q() + l.q() / 4);
    // Back a grid step at a time to the start, where the 10 is, is refused there.
    for (int i = 0; i < 6; ++i)
    {
        l.press(Key::typed(U'h', kAlt));
    }
    CHECK(l.view().time() == l.q() / 4);
    CHECK(l.ws.message() == "Another event is there already");
    CHECK(l.values(kModWheel) == std::vector<int>{10, 40, 80});
}

namespace
{

// The inspector's text, its runs in order down and across, a space between.
std::u32string inspectorText(const Lanes& l)
{
    const auto frame = l.ws.frame();
    std::vector<TextRun> runs;
    for (const auto& region : frame)
    {
        if (region.background == Role::Panel)
        {
            runs.insert(runs.end(), region.text.begin(), region.text.end());
        }
    }
    std::ranges::sort(runs, [](const TextRun& a, const TextRun& b)
                      { return a.y != b.y ? a.y < b.y : a.x < b.x; });
    std::u32string text;
    for (const auto& run : runs)
    {
        text += run.text + U" ";
    }
    return text;
}

// The width of the lane's selection as drawn, or 0.
int selectionWidth(const Lanes& l)
{
    const auto frame = l.ws.frame();
    const auto name = std::ranges::find_if(
        frame, [](const Region& region)
        { return !region.text.empty() && region.text.front().text == U"cc1"; });
    for (const auto& region : frame)
    {
        if (name != frame.end() && region.bounds.y == name->bounds.y &&
            region.bounds.x > name->bounds.x)
        {
            for (const auto& shape : region.under)
            {
                if (shape.role == Role::Selection && shape.alpha == 255)
                {
                    return shape.rect.width;
                }
            }
        }
    }
    return 0;
}

} // namespace

TEST_CASE("the value prompt starts with the value, a number replacing it and +10 building on it")
{
    Lanes l;
    l.line(U"lane cc1");
    l.type(U"gggl");
    l.enter();
    CHECK(l.ws.commandLine().text() == U"10");
    l.type(U"+5");
    l.enter();
    CHECK(l.values(kModWheel) == std::vector<int>{15, 40, 80});
    l.enter();
    CHECK(l.ws.commandLine().text() == U"15");
    l.type(U"100");
    l.enter();
    CHECK(l.values(kModWheel) == std::vector<int>{100, 40, 80});
}

TEST_CASE("between a controller's events, the inspector shows the value held there")
{
    Lanes l;
    l.line(U"lane cc1");
    l.type(U"gggl\tL"); // a nudge past the 40
    REQUIRE_FALSE(l.view().laneNote());
    const auto text = inspectorText(l);
    CHECK(text.find(U"No event") != std::u32string::npos);
    CHECK(text.find(U"1 Modulation") != std::u32string::npos);
    CHECK(text.find(U" 40 ") != std::u32string::npos);
}

TEST_CASE("a selection in a controller's lane reaches the cursor between events")
{
    Lanes l;
    l.line(U"lane cc1");
    l.type(U"gggl");
    l.type(U"v\t");
    const auto to = selectionWidth(l);
    CHECK(to > 0);
    l.type(U"20L"); // on past the 40, towards the 80
    CHECK(selectionWidth(l) > to);
    CHECK(l.view().laneTaken().size() == 2);
    // From between events, too.
    l.press(Key::of(NamedKey::Escape));
    l.type(U"Lv");
    l.type(U"l");
    CHECK(selectionWidth(l) > 0);
}

TEST_CASE("a Ctrl-v box started between a controller's events starts at the value held there")
{
    Lanes l;
    l.line(U"lane cc1");
    l.type(U"gggl\tL"); // a nudge past the 40
    l.press(Key::typed(U'v', kCtrl));
    CHECK(l.view().selectionStatus()->find("values 40") != std::string::npos);
}

TEST_CASE("once a Ctrl-v box's edge is set with k or j, it no longer follows the events")
{
    Lanes l;
    l.line(U"lane cc1");
    l.type(U"gggl");
    l.press(Key::typed(U'v', kCtrl));
    // Onto the 40: the corner takes it.
    l.type(U"\t");
    CHECK(l.view().selectionStatus()->find("values 10–40") != std::string::npos);
    // Set to 50 by hand, it stays there as the cursor goes on to the 80.
    l.type(U"10k");
    l.type(U"\t");
    CHECK(l.view().selectionStatus()->find("values 10–50") != std::string::npos);
    // A box started afresh follows again.
    l.press(Key::of(NamedKey::Escape));
    l.press(Key::typed(U'v', kCtrl));
    l.type(U"\v");
    CHECK(l.view().selectionStatus()->find("values 40–80") != std::string::npos);
}

TEST_CASE("a search's matching controller events are in the match colour, and n goes to them")
{
    Lanes l;
    l.line(U"lane cc1");
    l.type(U"gggl");
    // The mod wheel above 30: the 40 and the 80.
    l.type(U"/");
    l.type(U"cc1>30");
    l.enter();
    const auto matched = [&]
    {
        const auto frame = l.ws.frame();
        int count = 0;
        for (const auto& region : frame)
        {
            for (const auto& shape : region.under)
            {
                count += shape.role == Role::Match ? 1 : 0;
            }
        }
        return count;
    };
    // The cursor's bar is lit, the other match in the match colour.
    CHECK(l.view().time() == l.q());
    CHECK(matched() == 1);
    l.type(U"n");
    CHECK(l.view().time() == 2 * l.q());
    CHECK(l.view().laneNote());
    CHECK(matched() == 1);
}

namespace
{

// The synth with sustain: down at beat 1 and lifted half a beat after the C4 ends, so it holds
// it; then down at beat 3, eased to 90 and held to beat 5.
void pedal(Lanes& l)
{
    const auto q = l.q();
    l.document.edit("Pedal", {},
                    [&](core::Song& song)
                    {
                        song.updateEvents(
                            song.tracks()[0]->id(),
                            [&](core::EventList& events)
                            {
                                events.insert({0, core::ControlChange{64, 127}});
                                events.insert({q + q / 2, core::ControlChange{64, 0}});
                                events.insert({2 * q, core::ControlChange{64, 127}});
                                events.insert({2 * q + q / 2, core::ControlChange{64, 90}});
                                events.insert({4 * q, core::ControlChange{64, 0}});
                            });
                    });
}

const core::Lane kSustain{.kind = core::Lane::Kind::Controller, .controller = 64};

} // namespace

TEST_CASE("a pedal's lane goes region to region, the inspector showing where it's down")
{
    Lanes l;
    pedal(l);
    l.line(U"lane cc64");
    l.type(U"gggl");
    REQUIRE(l.view().laneNote());
    CHECK(inspectorText(l).find(U"Pedal") != std::u32string::npos);
    l.press(Key::of(NamedKey::Tab));
    CHECK(l.view().time() == 2 * l.q());
    // Half-pedalled within it, still one region: no further.
    l.press(Key::of(NamedKey::Tab));
    CHECK(l.view().time() == 2 * l.q());
    l.type(U"\v");
    CHECK(l.view().time() == 0);
    // Between them, pedal up.
    l.type(U"\t20H"); // back from the second region, past the first's lift
    CHECK_FALSE(l.view().laneNote());
    CHECK(inspectorText(l).find(U"Pedal up") != std::u32string::npos);
    // Its values change in the values view, , c v, as a controller's.
    l.press(Key::typed(U'a', kCtrl));
    CHECK(l.ws.message().starts_with("A pedal's regions are down or up"));
    l.type(U",cv");
    CHECK_FALSE(l.view().isSwitchLane());
    l.type(U"gg");
    l.press(Key::typed(U'x', kCtrl));
    CHECK(l.values(kSustain) == std::vector<int>{126, 0, 127, 90, 0});
    l.type(U",cv");
    CHECK(l.view().isSwitchLane());
}

TEST_CASE("the toggles , T s and , T S turn the sustain tails and tint on and off")
{
    Lanes l;
    pedal(l);
    // Shapes in the notes' colour beyond the notes: the tails, and with the tint, more.
    const auto count = [&]
    {
        const auto frame = l.ws.frame();
        std::size_t shapes = 0;
        for (const auto& region : frame)
        {
            shapes += region.under.size();
        }
        return shapes;
    };
    const auto tails = count();
    l.type(U",Ts");
    CHECK(l.ws.message() == "Sustain tails hidden");
    CHECK(count() < tails);
    l.type(U",TS");
    CHECK(l.ws.message() == "Notes tinted where the sustain pedal is down");
    CHECK(count() > tails - 2);
}

TEST_CASE("0 and Enter set a controller's value to 0, though 0 alone goes to the start")
{
    Lanes l;
    l.line(U"lane cc1");
    l.type(U"gggl\t\t"); // the 80
    l.type(U"0");
    l.enter();
    CHECK(l.values(kModWheel) == std::vector<int>{10, 40, 0});
    // Other numbers as before.
    l.type(U"20");
    l.enter();
    CHECK(l.values(kModWheel) == std::vector<int>{10, 40, 20});
}

namespace
{

// The sustain's events, as their times and values.
std::vector<std::pair<core::Tick, int>> pedalling(const Lanes& l)
{
    std::vector<std::pair<core::Tick, int>> result;
    for (const auto& event : l.document.song().events(l.track().id()).all())
    {
        if (kSustain.shows(event.data))
        {
            result.emplace_back(event.tick, kSustain.value(event.data));
        }
    }
    return result;
}

} // namespace

TEST_CASE("o presses the pedal at the cursor, up to just before the next region")
{
    Lanes l;
    pedal(l);
    const auto q = l.q();
    l.line(U"lane cc64");
    l.type(U"gggl\t"); // the second region, at beat 3
    l.type(U"H");      // a nudge before it, between the regions
    const auto at = l.view().time();
    REQUIRE_FALSE(l.view().laneNote());
    l.type(U"o");
    CHECK(pedalling(l) == std::vector<std::pair<core::Tick, int>>{{0, 127},
                                                                  {q + q / 2, 0},
                                                                  {at, 127},
                                                                  {2 * q - 1, 0},
                                                                  {2 * q, 127},
                                                                  {2 * q + q / 2, 90},
                                                                  {4 * q, 0}});
    // In a region, it's down already.
    l.type(U"o");
    CHECK(l.ws.message().starts_with("The pedal's down here already"));
}

TEST_CASE("= and - lift a pedal's region later or earlier, never into the next")
{
    Lanes l;
    pedal(l);
    const auto q = l.q();
    l.line(U"lane cc64");
    l.type(U"gggl");
    l.type(U"=");
    CHECK(pedalling(l).at(1) == std::pair<core::Tick, int>{q + q / 2 + q / 4, 0});
    l.type(U"-");
    CHECK(pedalling(l).at(1) == std::pair<core::Tick, int>{q + q / 2, 0});
    // Into the next region is refused.
    l.type(U"3=");
    CHECK(l.ws.message() == "That would run into the next region");
    // Lifted before its half-pedalled value, that value goes, as it would press it again.
    l.type(U"\t6-");
    CHECK(pedalling(l) == std::vector<std::pair<core::Tick, int>>{
                              {0, 127}, {q + q / 2, 0}, {2 * q, 127}, {2 * q + q / 2, 0}});
}

TEST_CASE("Alt-h and Alt-l move a pedal's region with its values, and x deletes it")
{
    Lanes l;
    pedal(l);
    const auto q = l.q();
    l.line(U"lane cc64");
    l.type(U"gggl\t\t");
    REQUIRE(l.view().time() == 2 * q);
    l.press(Key::typed(U'l', kAlt));
    CHECK(pedalling(l) == std::vector<std::pair<core::Tick, int>>{{0, 127},
                                                                  {q + q / 2, 0},
                                                                  {2 * q + q / 4, 127},
                                                                  {2 * q + q / 2 + q / 4, 90},
                                                                  {4 * q + q / 4, 0}});
    CHECK(l.view().time() == 2 * q + q / 4);
    // Back into the first is refused.
    l.type(U"3");
    l.press(Key::typed(U'h', kAlt));
    CHECK(l.ws.message() == "That would run into another region");
    l.type(U"x");
    CHECK(pedalling(l) == std::vector<std::pair<core::Tick, int>>{{0, 127}, {q + q / 2, 0}});
    // d on a selection takes whole regions.
    l.type(U"u");
    l.type(U"ggv\td");
    CHECK(pedalling(l).empty());
}

TEST_CASE("selected pedal regions cannot be lengthened into one another")
{
    Lanes l(GENERATE(core::kPpqn, core::Ppqn{96}));
    pedal(l);
    const auto original = pedalling(l);
    l.line(U"lane cc64");
    l.type(U"ggglv\t");
    const auto before = l.document.song();
    const auto time = l.view().time();
    const auto selection = l.view().selectionSpan();
    const auto taken = l.view().laneTaken();
    l.document.markSaved();
    // The first lift would touch or pass the second press, by grid steps or 5 ms nudges.
    l.type(GENERATE(U"2=", U"3=", U"50+", U"51+"));
    CHECK(l.ws.message() == "That would run into the next region");
    CHECK(l.document.song() == before);
    CHECK_FALSE(l.document.modified());
    CHECK(l.view().time() == time);
    CHECK(l.ws.mode() == Mode::Visual);
    CHECK(l.view().selectionSpan() == selection);
    CHECK(l.view().laneTaken() == taken);
    // A smaller lengthening still works for both, and undoes as one step.
    l.type(U"=");
    CHECK(pedalling(l).at(1).first == l.q() + l.q() / 2 + l.q() / 4);
    CHECK(pedalling(l).back().first == 4 * l.q() + l.q() / 4);
    l.press(Key::of(NamedKey::Escape));
    l.type(U"u");
    CHECK(pedalling(l) == original);
    CHECK_FALSE(l.document.modified());
}

TEST_CASE("pedal-region editing keeps the view's messages for musical refusals")
{
    Lanes l(GENERATE(core::kPpqn, core::Ppqn{96}));
    pedal(l);
    l.line(U"lane cc64");
    l.type(U"gggl");
    const auto before = l.document.song();
    l.document.markSaved();
    SECTION("a region cannot be shortened to nothing")
    {
        l.type(U"6-");
        CHECK(l.ws.message() == "That would leave nothing of the region: x deletes it");
    }
    SECTION("a lift cannot go past the song's end")
    {
        l.type(U"\t1000=");
        CHECK(l.ws.message() == "That would go past the song's end");
    }
    SECTION("a held region has no lift to move")
    {
        l.type(U",cv\t\t\t\tx,cv"); // delete the final lift in the values view
        l.type(U"gg\t=");
        CHECK(l.ws.message() == "That region holds to the song's end, with no lift to move");
        l.type(U"u"); // only the earlier deletion should have made an undo step
    }
    CHECK(l.document.song() == before);
    CHECK_FALSE(l.document.modified());
}

TEST_CASE("h and l move a grid step in a controller's or pedal's lane, note to note in velocity's")
{
    Lanes l;
    l.line(U"lane cc1");
    l.type(U"gggl");
    l.type(U"l");
    CHECK(l.view().time() == l.q() / 4);
    l.type(U"3l");
    CHECK(l.view().time() == l.q());
    REQUIRE(l.view().laneNote()); // the 40
    l.type(U"h");
    CHECK(l.view().time() == 3 * l.q() / 4);
    l.line(U"lane cc64");
    l.type(U"l");
    CHECK(l.view().time() == l.q());
}

TEST_CASE("a Ctrl-v box is refused among a pedal's regions, but not in its values")
{
    Lanes l;
    pedal(l);
    l.line(U"lane cc64");
    l.type(U"gggl");
    l.press(Key::typed(U'v', kCtrl));
    CHECK(l.ws.message().starts_with("A pedal's regions are down or up"));
    CHECK(l.ws.mode() == Mode::Normal);
    // Nor from v.
    l.type(U"v");
    l.press(Key::typed(U'v', kCtrl));
    CHECK(l.ws.message().starts_with("A pedal's regions are down or up"));
    l.press(Key::of(NamedKey::Escape));
    l.press(Key::of(NamedKey::Escape));
    l.type(U",cv");
    l.press(Key::typed(U'v', kCtrl));
    CHECK(l.ws.mode() == Mode::Visual);
}

TEST_CASE("o won't press the pedal on the tick another region lifts")
{
    Lanes l;
    pedal(l);
    const auto q = l.q();
    l.line(U"lane cc64");
    l.type(U"gggl");
    // The first region lifts at a beat and a half; the cursor there.
    l.type(U"6l");
    REQUIRE(l.view().time() == q + q / 2);
    l.type(U"o");
    CHECK(l.ws.message() == "The pedal lifts here: press it after the lift (L nudges on)");
    CHECK(pedalling(l).size() == 5);
    // A nudge on, it's a pedal change.
    l.type(U"Lo");
    CHECK(pedalling(l).size() == 7);
}

TEST_CASE("pedal regions a selection takes are in the selection's colour")
{
    Lanes l;
    pedal(l);
    l.line(U"lane cc64");
    l.type(U"gggl");
    const auto selected = [&]
    {
        std::size_t count = 0;
        for (const auto& region : l.ws.frame())
        {
            for (const auto& shape : region.under)
            {
                count += shape.role == Role::NoteSelected ? 1 : 0;
            }
        }
        return count;
    };
    const auto before = selected();
    l.type(U"v\t");
    CHECK(selected() > before);
}

TEST_CASE("the leader keys t y and t p yank and put whole tracks from the piano roll, and , t D "
          "duplicates")
{
    Lanes l;
    l.type(U",ty");
    CHECK(l.ws.message() == "Yanked Synth, the whole track");
    l.type(U",tp");
    REQUIRE(l.document.song().tracks().size() == 2);
    // A copy with all its events, the cursor on it.
    const auto& song = l.document.song();
    CHECK(song.events(song.tracks()[1]->id()).size() == song.events(song.tracks()[0]->id()).size());
    CHECK(l.view().track() == 1);
    l.type(U",tD");
    CHECK(l.document.song().tracks().size() == 3);
    CHECK(l.view().track() == 2);
    // The register still holds the first yank: one more.
    l.type(U",tP");
    CHECK(l.document.song().tracks().size() == 4);
    l.type(U"u");
    CHECK(l.document.song().tracks().size() == 3);
}

TEST_CASE("the leader keys r A disarm every armed track, from wherever the cursor is")
{
    Lanes l;
    l.type(U",tp"); // nothing yanked yet
    CHECK(l.ws.message() == "No whole tracks yanked: , t y yanks this one");
    l.type(U"A");
    REQUIRE(l.track().arm);
    l.type(U",rA");
    CHECK_FALSE(l.track().arm);
    CHECK(l.ws.message() == "Disarmed Synth");
    l.type(U",rA");
    CHECK(l.ws.message() == "No track is armed");
}

TEST_CASE(
    "the leader keys l l and l d put and delete a label, and , l x asks which marks to delete")
{
    Lanes l;
    l.type(U",ll");
    l.type(U"Verse");
    l.enter();
    REQUIRE(l.document.song().labels().size() == 1);
    CHECK(l.document.song().labels().front().name == "Verse");
    l.type(U",ld");
    CHECK(l.document.song().labels().empty());
    l.type(U"ma");
    REQUIRE(l.document.song().mark('a') != nullptr);
    l.type(U",lm");
    REQUIRE(l.ws.listing().size() == 1);
    CHECK(l.ws.listing().front().first == "a");
    l.press(Key::of(NamedKey::Escape)); // the list closes
    l.type(U",lx");
    l.type(U"a");
    l.enter();
    CHECK(l.document.song().mark('a') == nullptr);
}
