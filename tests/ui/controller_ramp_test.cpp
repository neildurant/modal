// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

constexpr CellSize kCell{10, 20};

// A synth part in 4/4 over two bars: the mod wheel (CC 1) at 10, 40 and 80 on bar 1's first
// three beats, and a note in each bar.
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
                      });
    song.fitToContent();
    return song;
}

// The piano roll on the synth part, its lane showing the mod wheel, the cursor in the lane at
// the start.
struct Ramps
{
    explicit Ramps(core::Ppqn ppqn = core::kPpqn)
        : ppqn{ppqn}
        , document{synth(ppqn)}
    {
        registry.add(windowCommands());
        ws.setLineHandler([&](const std::string& line) { registry.runLine(line); });
        ws.resize(120 * kCell.width, 30 * kCell.height, kCell);
        ws.show(ViewKind::Arrangement);
        type(U"gr");
        line(U"lane cc1");
        type(U"gl");
        document.breakMerge();
    }

    // Keys typed, \t for Tab and \x1b for Escape.
    void type(std::u32string_view keys)
    {
        for (const char32_t key : keys)
        {
            ws.handle(key == U'\t'     ? Key::of(NamedKey::Tab)
                      : key == U'\x1b' ? Key::of(NamedKey::Escape)
                      : key == U'\n'   ? Key::of(NamedKey::Enter)
                                       : Key::typed(key),
                      0);
        }
    }
    void line(std::u32string_view text)
    {
        type(U":");
        type(text);
        type(U"\n");
    }

    // Tabs round the open draft's fields to one.
    void goTo(std::string_view heading)
    {
        for (int i = 0; i < 10 && view().inspectorEditor().heading() != heading; ++i)
        {
            type(U"\t");
        }
        REQUIRE(view().inspectorEditor().heading() == heading);
    }

    core::Tick q() const { return ppqn.quarter(); }
    core::TrackId id() const { return document.song().tracks()[0]->id(); }
    std::span<const core::ControllerRamp> ramps() const { return document.song().ramps(id()); }
    const PianoRollView& view() const { return ws.pianoRoll(); }

    // The mod wheel's values as they play.
    std::vector<int> values() const
    {
        std::vector<int> result;
        for (const auto& event : document.song().events(id()).all())
        {
            if (const auto* change = std::get_if<core::ControlChange>(&event.data))
            {
                result.push_back(change->value);
            }
        }
        return result;
    }

    core::Ppqn ppqn;
    core::Document document;
    CommandRegistry registry;
    Workspace ws{registry, document};
};

} // namespace

TEST_CASE(
    "Ramping with , c r takes the bar from the values held there, shaped in the inspector, Enter "
    "keeping it as one step")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto before = r.values();
    r.type(U",cr");
    REQUIRE(r.ramps().size() == 1);
    const auto made = r.ramps()[0];
    CHECK(made.start == 0);
    CHECK(made.end() == 4 * r.q() - 1);
    CHECK(made.from == 10);
    CHECK(made.to == 80);
    CHECK(r.view().draftingRamp());
    CHECK(r.view().typing());
    // It opens on To, the value to set first.
    CHECK(r.view().inspectorEditor().heading() == "To");

    r.type(U"127\t");
    CHECK(r.ramps()[0].to == 127); // at once, to hear and see
    CHECK(r.view().inspectorEditor().heading() == "Shape");
    r.type(U"\n");
    CHECK_FALSE(r.view().draftingRamp());
    CHECK_FALSE(r.view().typing());
    CHECK(r.ramps()[0].to == 127);
    CHECK(r.ws.message() == "Made the ramp");
    // The ramp's points play in place of the values it hides.
    CHECK(r.values().front() == 10);
    CHECK(r.values().back() == 127);

    // One undo takes it all back.
    r.type(U"u");
    CHECK(r.ramps().empty());
    CHECK(r.values() == before);
}

TEST_CASE("Escape cancels a draft: a new ramp isn't made, and a changed one is as it was")
{
    Ramps r;
    const auto before = r.values();
    r.type(U",cr50\t");
    REQUIRE(r.ramps().size() == 1);
    r.type(U"\x1b");
    CHECK(r.ramps().empty());
    CHECK(r.values() == before);
    CHECK_FALSE(r.document.canRedo());
    CHECK(r.ws.message() == "No ramp made");
    CHECK_FALSE(r.view().typing());

    // e on a ramp opens it again; its changes are cancelled together.
    r.type(U",cr\n");
    REQUIRE(r.ramps().size() == 1);
    const auto kept = r.ramps()[0];
    r.type(U"e");
    REQUIRE(r.view().draftingRamp());
    r.type(U"5\t");
    CHECK(r.ramps()[0].to == 5);
    r.type(U"\t+20\t"); // past Shape to Density, set, and round to Start
    r.type(U"\x1b");
    REQUIRE(r.ramps().size() == 1);
    CHECK(r.ramps()[0] == kept);
    CHECK(r.ws.message() == "The ramp is as it was");
    CHECK_FALSE(r.document.canRedo());
}

TEST_CASE("a ramp's fields: its ends each on their own, its values, shape, amount and density")
{
    Ramps r;
    r.type(U",cr\n");
    REQUIRE(r.ramps().size() == 1);
    const auto q = r.q();
    r.type(U"e");
    // Start, End, From, To, Shape, (Amount), Density.
    r.goTo("Start");
    r.type(U"1:2:0\t");
    CHECK(r.ramps()[0].start == q);
    CHECK(r.ramps()[0].end() == 4 * q - 1); // the end stays
    r.type(U"1:4:0\t");
    CHECK(r.ramps()[0].end() == 3 * q);
    r.type(U"0\t200\t");
    CHECK(r.ws.message().starts_with("A value goes from 0 to 127"));
    CHECK(r.view().inspectorEditor().heading() == "To");
    r.ws.handle(Key::typed(U'u', kCtrl), 0); // cleared, to put right
    r.type(U"100\t");
    CHECK(r.ramps()[0].from == 0);
    CHECK(r.ramps()[0].to == 100);
    r.type(U"c\t"); // the list's first choice starting with c
    REQUIRE(r.ramps()[0].shape == core::ControllerRamp::Shape::Curve);
    REQUIRE(r.view().inspectorEditor().heading() == "Amount");
    r.type(U"-40\t8\n");
    CHECK(r.ramps()[0].amount == -40);
    CHECK(r.ramps()[0].density == 8);
    // The end can't come before the start.
    r.type(U"e");
    r.goTo("End");
    r.type(U"1:1:0\t");
    CHECK(r.ws.message() == "A ramp has to end after it starts");
    r.type(U"\x1b");
    CHECK(r.ramps()[0].end() == 3 * q);
}

TEST_CASE("a ramp's points aren't edited one by one, and , c R makes them events")
{
    Ramps r;
    r.type(U",cr\n");
    REQUIRE(r.ramps().size() == 1);
    const auto playing = r.values();
    for (const auto keys : {U"x", U"o", U"\x01"})
    {
        if (std::u32string_view{keys} == U"\x01")
        {
            r.ws.handle(Key::typed(U'a', kCtrl), 0);
        }
        else
        {
            r.type(keys);
        }
        CHECK(r.ws.message().starts_with("That's a ramp's"));
        CHECK(r.values() == playing);
    }
    // Past the ramp, events are edited as before.
    r.type(U",cR");
    CHECK(r.ramps().empty());
    CHECK(r.values() == playing);
    CHECK(r.ws.message() == "The ramp is events now, to edit one by one");
    r.type(U"u");
    CHECK(r.ramps().size() == 1);
}

TEST_CASE("Ramping with , c r takes a lane selection's time, and needs a controller lane")
{
    Ramps r;
    r.type(U"vll");
    REQUIRE(r.ws.mode() == Mode::Visual);
    r.type(U",cr");
    // From one end of the selection to the other, two grid steps of a sixteenth.
    REQUIRE(r.ramps().size() == 1);
    CHECK(r.ramps()[0].start == 0);
    CHECK(r.ramps()[0].end() == r.q() / 2);
    CHECK(r.ws.mode() != Mode::Visual);
    r.type(U"\n");

    // A second over the same time touches it, and is refused.
    r.type(U",cr");
    CHECK(r.ws.message().starts_with("That would touch or overlap the ramp at 1:1:0"));
    CHECK(r.ramps().size() == 1);
    CHECK_FALSE(r.view().typing());

    r.line(U"lane velocity");
    r.type(U",cr");
    CHECK(r.ws.message().starts_with(", c r ramps a continuous controller"));
}

TEST_CASE("Space plays and stops while a ramp's draft is open, and the draft stays")
{
    Ramps r;
    r.type(U",cr ");
    CHECK(r.ws.message() == "Nothing to play to"); // no player in the test
    CHECK(r.view().draftingRamp());
    r.ws.cancelDrafts();
    CHECK_FALSE(r.view().draftingRamp());
    CHECK(r.ramps().empty());
}
