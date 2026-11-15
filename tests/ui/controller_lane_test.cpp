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

    void type(std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            ws.handle(Key::typed(key), 0);
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

TEST_CASE("in a controller's lane, Tab and h and l go event to event, and H and L between")
{
    Lanes l(GENERATE(core::kPpqn, core::Ppqn{96}));
    l.line(U"lane cc1");
    l.type(U"gggl");
    REQUIRE(l.view().laneNote());
    CHECK(l.view().time() == 0);
    l.type(U"l");
    CHECK(l.view().time() == l.q());
    l.press(Key::of(NamedKey::Tab));
    CHECK(l.view().time() == 2 * l.q());
    l.type(U"h");
    CHECK(l.view().time() == l.q());
    // A nudge on, there's no event; Tab goes on to the next.
    l.type(U"L");
    CHECK(l.view().time() > l.q());
    CHECK_FALSE(l.view().laneNote());
    // Where nothing's lit, a line in the brightest colour shows where the cursor is.
    const auto lit = [&]
    {
        const auto frame = l.ws.frame();
        return std::ranges::count_if(frame,
                                     [](const Region& region)
                                     {
                                         return std::ranges::any_of(
                                             region.over, [](const Shape& shape)
                                             { return shape.role == Role::Text; });
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
    l.type(U"l");
    l.enter();
    l.type(U"+5");
    l.enter();
    CHECK(l.values(kModWheel) == std::vector<int>{50, 45, 80});
    // A v selection over all three, raised by 10, the highest stopping at 127.
    l.type(U"0");
    l.type(U"gg");
    l.type(U"v2l");
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
    l.type(U"2l");
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
    l.type(U"l");
    REQUIRE(l.view().time() == l.q());
    l.type(U"h");
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
    l.type(U"vld");
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
