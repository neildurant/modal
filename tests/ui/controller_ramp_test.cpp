// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/document.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
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
    // Ctrl-Enter, keeping what the inspector's changed.
    void keep() { ws.handle(Key::of(NamedKey::Enter, kCtrl), 0); }
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
    "Ramping with , c r takes the bar from the values held there, shaped in the inspector and "
    "kept as one step")
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
    // It opens on From, the first of its values.
    CHECK(r.view().inspectorEditor().heading() == "From");

    r.type(U"\t127\n");
    CHECK(r.ramps()[0].to == 127); // at once, to hear and see
    CHECK(r.view().inspectorEditor().heading() == "To");
    r.keep();
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
    r.type(U",cr50\n");
    REQUIRE(r.ramps().size() == 1);
    CHECK(r.ramps()[0].from == 50);
    r.type(U"\x1b");
    CHECK(r.ramps().empty());
    CHECK(r.values() == before);
    CHECK_FALSE(r.document.canRedo());
    CHECK(r.ws.message() == "No ramp made");
    CHECK_FALSE(r.view().typing());

    // e on a ramp opens it again; its changes are cancelled together.
    r.type(U",cr");
    r.keep();
    REQUIRE(r.ramps().size() == 1);
    const auto kept = r.ramps()[0];
    r.type(U"e");
    REQUIRE(r.view().draftingRamp());
    r.type(U"5\n\t+20\n");
    CHECK(r.ramps()[0].from == 5);
    CHECK(r.ramps()[0].to == 100);
    r.type(U"\x1b");
    REQUIRE(r.ramps().size() == 1);
    CHECK(r.ramps()[0] == kept);
    CHECK(r.ws.message() == "The ramp is as it was");
    CHECK_FALSE(r.document.canRedo());

    // The Cancel button does the same.
    r.type(U"e9\n");
    r.goTo("Cancel");
    r.type(U"\n");
    CHECK(r.ramps()[0] == kept);
    CHECK_FALSE(r.view().draftingRamp());
}

TEST_CASE("a ramp's fields: its ends each on their own, its values, shape, amount and density")
{
    Ramps r;
    r.type(U",cr");
    r.keep();
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
    r.type(U"c\t"); // a letter opens the list at its first choice starting so
    REQUIRE(r.ramps()[0].shape == core::ControllerRamp::Shape::Curve);
    REQUIRE(r.view().inspectorEditor().heading() == "Amount");
    r.type(U"-40\t8");
    r.keep(); // what's typed set first
    CHECK(r.ramps()[0].amount == -40);
    CHECK(r.ramps()[0].density == 8);
    CHECK_FALSE(r.view().draftingRamp());
    // The end can't come before the start.
    r.type(U"e");
    r.goTo("End");
    r.type(U"1:1:0\t");
    CHECK(r.ws.message() == "A ramp has to end after it starts");
    r.type(U"\x1b\x1b"); // what was typed, then the draft
    CHECK(r.ramps()[0].end() == 3 * q);
    CHECK_FALSE(r.view().draftingRamp());
}

TEST_CASE("a ramp's shape is stepped with its list closed, and chosen from it when open")
{
    using Shape = core::ControllerRamp::Shape;
    Ramps r;
    r.type(U",cr");
    r.goTo("Shape");
    CHECK(r.view().inspectorEditor().list() == nullptr);
    r.ws.handle(Key::of(NamedKey::Down), 0);
    CHECK(r.ramps()[0].shape == Shape::Curve);
    CHECK(r.view().inspectorEditor().list() == nullptr);
    r.ws.handle(Key::of(NamedKey::Down), 0);
    CHECK(r.ramps()[0].shape == Shape::SCurve);
    r.ws.handle(Key::of(NamedKey::Up), 0);
    CHECK(r.ramps()[0].shape == Shape::Curve);
    // Enter opens the list; Enter there chooses and closes it, staying on the field.
    r.type(U"\n");
    REQUIRE(r.view().inspectorEditor().list() != nullptr);
    r.ws.handle(Key::of(NamedKey::Down), 0);
    r.ws.handle(Key::of(NamedKey::Down), 0);
    r.type(U"\n");
    CHECK(r.ramps()[0].shape == Shape::EqualPower);
    CHECK(r.view().inspectorEditor().list() == nullptr);
    CHECK(r.view().inspectorEditor().heading() == "Shape");
    // Alt-Down opens it too, and Escape closes it, changing nothing.
    r.ws.handle(Key::of(NamedKey::Down, kAlt), 0);
    REQUIRE(r.view().inspectorEditor().list() != nullptr);
    r.ws.handle(Key::of(NamedKey::Up), 0);
    r.type(U"\x1b");
    CHECK(r.view().inspectorEditor().list() == nullptr);
    CHECK(r.ramps()[0].shape == Shape::EqualPower);
    CHECK(r.view().draftingRamp());
    // And round again by letters.
    r.type(U"l\n");
    CHECK(r.ramps()[0].shape == Shape::Linear);
}

TEST_CASE("Enter sets a ramp's field and stays on it, and the Keep button keeps the ramp")
{
    Ramps r;
    r.type(U",cr\t");
    REQUIRE(r.view().inspectorEditor().heading() == "To");
    r.type(U"100\n");
    CHECK(r.ramps()[0].to == 100);
    CHECK(r.view().draftingRamp());
    CHECK(r.view().inspectorEditor().heading() == "To");
    CHECK(r.view().inspectorEditor().replacing());
    r.type(U"90\n");
    CHECK(r.ramps()[0].to == 90);
    // Enter with nothing new does nothing more.
    r.type(U"\n");
    CHECK(r.view().draftingRamp());
    // Escape puts back what's typed, staying in the draft.
    r.type(U"5\x1b");
    CHECK(r.view().draftingRamp());
    CHECK(r.ramps()[0].to == 90);
    r.goTo("Keep");
    r.type(U"\n");
    CHECK_FALSE(r.view().draftingRamp());
    CHECK(r.ws.message() == "Made the ramp");
    CHECK(r.ramps()[0].to == 90);
}

TEST_CASE("a hint says how to change a ramp, and another how to finish")
{
    Ramps r;
    r.type(U",cr");
    r.keep();
    CHECK(r.view().inspection().kind == "Ramp");
    CHECK(r.view().typingHint() == "A ramp: e or Enter changes it, , c R makes it events");
    r.type(U"e");
    CHECK(r.view().typingHint() == std::string{kPanelHint} + "; Space plays");
}

TEST_CASE("a ramp's points aren't edited one by one, and , c R makes them events")
{
    Ramps r;
    r.type(U",cr");
    r.keep();
    REQUIRE(r.ramps().size() == 1);
    const auto playing = r.values();
    for (const auto keys : {U"o", U"\x01"})
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
    r.keep();

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

TEST_CASE("a ramp's fields keep their places as its shape changes and as each is edited")
{
    Ramps r;
    // Where each heading is across the inspector.
    const auto places = [&]
    {
        std::vector<int> xs;
        for (const auto& region : r.ws.frame())
        {
            for (const auto& run : region.text)
            {
                for (const auto heading : {U"Start", U"End", U"From", U"To", U"Shape", U"Amount",
                                           U"Density", U"Controller"})
                {
                    if (run.text == heading)
                    {
                        xs.push_back(run.x);
                    }
                }
            }
        }
        return xs;
    };
    r.type(U",cr");
    const auto first = places();
    REQUIRE(first.size() == 8);
    r.ws.handle(Key::of(NamedKey::Down), 0); // From, a value down
    r.goTo("Shape");
    CHECK(places() == first);
    r.ws.handle(Key::of(NamedKey::Down), 0); // Curve, its amount editable
    CHECK(r.ramps()[0].shape == core::ControllerRamp::Shape::Curve);
    CHECK(places() == first);
    r.type(U"\t");
    CHECK(r.view().inspectorEditor().heading() == "Amount");
    r.type(U"\t");
    CHECK(r.view().inspectorEditor().heading() == "Density");
    CHECK(places() == first);
    r.ws.handle(Key::of(NamedKey::Tab, kShift), 0);
    r.ws.handle(Key::of(NamedKey::Tab, kShift), 0);
    r.ws.handle(Key::of(NamedKey::Down), 0); // S-curve: Amount is passed over
    r.type(U"\t");
    CHECK(r.view().inspectorEditor().heading() == "Density");
    CHECK(places() == first);
    r.keep();
    CHECK(places() == first);
}

TEST_CASE("x deletes a ramp, what it hid playing again, and p puts it back")
{
    Ramps r;
    const auto before = r.values();
    r.type(U",cr\t127");
    r.keep();
    const auto made = r.ramps()[0];
    r.type(U"x");
    CHECK(r.ramps().empty());
    CHECK(r.values() == before);
    CHECK(r.ws.message() == "Deleted the ramp: what it hid plays again");
    // Put at the cursor, in bar 2, as it was.
    r.type(U"]b");
    REQUIRE(r.view().time() == 4 * r.q());
    r.type(U"p");
    REQUIRE(r.ramps().size() == 1);
    auto moved = made;
    moved.start = 4 * r.q();
    CHECK(r.ramps()[0] == moved);
    r.type(U"u");
    CHECK(r.ramps().empty());
}

TEST_CASE("Alt-h and Alt-l move a ramp, and = and - move its end, steps undoing together")
{
    Ramps r;
    const auto q = r.q();
    const auto step = q / 4; // the grid's sixteenth
    r.type(U"vllll");        // a quarter
    r.type(U",cr");
    r.keep();
    REQUIRE(r.ramps().size() == 1);
    REQUIRE(r.ramps()[0].start == 0);
    REQUIRE(r.ramps()[0].end() == q);
    r.type(U"0"); // the cursor on its start
    r.ws.handle(Key::typed(U'l', kAlt), 0);
    r.ws.handle(Key::typed(U'l', kAlt), 0);
    CHECK(r.ramps()[0].start == 2 * step);
    CHECK(r.ramps()[0].length == q);    // its length kept
    CHECK(r.view().time() == 2 * step); // the cursor with it
    r.type(U"=");
    CHECK(r.ramps()[0].end() == q + 3 * step);
    r.type(U"--");
    CHECK(r.ramps()[0].end() == q + step);
    r.type(U"u");
    CHECK(r.ramps()[0].start == 0);
    CHECK(r.ramps()[0].end() == q);

    // Not before the song's start, nor its end before its start.
    r.ws.handle(Key::typed(U'h', kAlt), 0);
    CHECK(r.ws.message() == "That's before the song's start");
    r.type(U"5-");
    CHECK(r.ws.message() == "A ramp has to end after it starts");
    CHECK(r.ramps()[0].end() == q);
}

TEST_CASE("d and y in the lane take whole ramps, refusing to cut one, and p retargets them")
{
    Ramps r;
    const auto q = r.q();
    r.type(U",cr\t127");
    r.keep();
    REQUIRE(r.ramps().size() == 1);

    // Part of it is refused, naming it.
    r.type(U"vl");
    r.type(U"d");
    CHECK(r.ws.message().starts_with("That would cut the cc1 ramp at 1:1:0"));
    CHECK(r.ramps().size() == 1);
    if (r.ws.mode() == Mode::Visual)
    {
        r.ws.handle(Key::of(NamedKey::Escape), 0);
    }
    REQUIRE(r.view().inLane());

    // The bar takes it whole, its points not as events.
    r.type(U"yib");
    CHECK(r.ws.message().ends_with(" and a ramp yanked"));

    // Into another controller's lane, it becomes that controller's.
    r.ws.handle(Key::of(NamedKey::Escape), 0);
    r.line(U"lane cc11");
    r.type(U"gl]bp");
    REQUIRE(r.ramps().size() == 2);
    CHECK(r.ramps()[1].lane == core::Lane{.kind = core::Lane::Kind::Controller, .controller = 11});
    CHECK(r.ramps()[1].start == 4 * q);
    // Not into pitch bend's.
    r.ws.handle(Key::of(NamedKey::Escape), 0);
    r.line(U"lane bend");
    r.type(U"glp");
    CHECK(r.ws.message().starts_with("A cc1 ramp can't go into the bend lane"));
    CHECK(r.ramps().size() == 2);

    // dib deletes it with the events it hid.
    r.ws.handle(Key::of(NamedKey::Escape), 0);
    r.line(U"lane cc1");
    r.type(U"gl");
    for (int i = 0; i < 16; ++i)
    {
        r.type(U"h");
    }
    REQUIRE(r.view().time() == 0);
    r.type(U"dib");
    REQUIRE(r.ramps().size() == 1);
    CHECK(r.ramps()[0].lane.controller == 11);
    CHECK(std::ranges::none_of(r.document.song().sourceEvents(r.id()).all(),
                               [](const core::Event& event)
                               {
                                   const auto* change =
                                       std::get_if<core::ControlChange>(&event.data);
                                   return change != nullptr && change->controller == 1;
                               }));
}

TEST_CASE("a transform leaves a ramp's points alone, saying so")
{
    Ramps r;
    r.type(U",cr\t127");
    r.keep();
    const auto playing = r.values();
    r.ws.handle(Key::of(NamedKey::Escape), 0); // back to the notes
    r.line(U"thin cc1 per=1 step=64");
    CHECK(r.values() == playing);
    CHECK(r.ws.message().find("a ramp left alone (, c R makes one events)") != std::string::npos);
}

TEST_CASE("bars can't be deleted or inserted through a ramp")
{
    Ramps r;
    const auto q = r.q();
    // From the last beat of bar 1 into bar 2, a grid step of a sixteenth at a time.
    for (int i = 0; i < 12; ++i)
    {
        r.type(U"l");
    }
    REQUIRE(r.view().time() == 3 * q);
    r.type(U"vllllllll");
    r.type(U",cr");
    r.keep();
    REQUIRE(r.ramps().size() == 1);
    REQUIRE(r.ramps()[0].start == 3 * q);
    REQUIRE(r.ramps()[0].end() == 5 * q);
    const auto before = r.document.song();
    CHECK(r.registry.runLine("bar-delete").message.starts_with("That would cut a ramp in Synth"));
    CHECK(r.registry.runLine("bar-insert").message.starts_with("That would cut a ramp in Synth"));
    CHECK(r.document.song() == before);
}

TEST_CASE("taking time out and putting it back carries ramps with the events they hide")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    const auto q = ppqn.quarter();
    auto song = synth(ppqn);
    const auto id = song.tracks()[0]->id();
    const core::Lane modulation{.kind = core::Lane::Kind::Controller, .controller = 1};
    REQUIRE(song.addRamp(id, {.lane = modulation, .start = q, .length = q, .from = 0, .to = 100})
                .done());
    REQUIRE(song.addRamp(id, {.lane = modulation, .start = 5 * q, .length = q, .from = 9, .to = 1})
                .done());
    const auto hidden = song.sourceEvents(id);

    // A span through a ramp is one to refuse.
    CHECK(rampCut(song, 0, 0, 0, q + 1));
    CHECK_FALSE(rampCut(song, 0, 0, 0, 4 * q));

    // Bar 1 out: its ramp and events into the register, the later ramp a bar earlier.
    auto removed = rippleDelete(song, 0, 0, 0, 4 * q);
    REQUIRE(removed.ramps.size() == 1);
    REQUIRE(removed.ramps[0].size() == 1);
    CHECK(removed.ramps[0][0].start == q);
    REQUIRE(song.ramps(id).size() == 1);
    CHECK(song.ramps(id)[0].start == q);

    // And back in: as it was.
    rippleInsert(song, removed, 0, 0);
    REQUIRE(song.ramps(id).size() == 2);
    CHECK(song.ramps(id)[0].start == q);
    CHECK(song.ramps(id)[1].start == 5 * q);
    CHECK(song.sourceEvents(id) == hidden);

    // Put over what's there, a ramp landing on another is refused, the song as it was.
    const auto before = song;
    CHECK(rampPutRefusal(song, takeEvents(song, 0, 0, 0, 4 * q), 0, 4 * q + q / 2, false, 0)
              .value_or("") == "A ramp would touch or overlap another of its controller there");
    CHECK(song == before);
}

TEST_CASE("dd and yy on a ramp take it, without a selection")
{
    Ramps r;
    const auto before = r.values();
    r.type(U",cr\t127");
    r.keep();
    const auto made = r.ramps()[0];
    r.type(U"yy");
    CHECK(r.ws.message() == "Yanked the cc1 ramp");
    CHECK(r.ramps().size() == 1);
    r.type(U"]bp");
    REQUIRE(r.ramps().size() == 2);
    CHECK(r.ramps()[1].start == 4 * r.q());
    CHECK(r.ramps()[1].to == made.to);
    r.type(U"dd"); // the one at the cursor, as x
    REQUIRE(r.ramps().size() == 1);
    CHECK(r.ramps()[0] == made);
    CHECK(r.ws.message() == "Deleted the ramp: what it hid plays again");
    // Off a ramp, dd takes the bar's events, as before: there are none in bar 2.
    r.type(U"dd");
    CHECK(r.ws.message() == "No events there to delete");
    CHECK(r.ramps().size() == 1);
    CHECK(r.values().size() > before.size()); // bar 1's ramp still playing
}

TEST_CASE("controller shaping keys take the lane's stream, change a selection and undo whole")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto before = r.document.song();
    r.type(U"vib,cs\n50%\n");
    CHECK(r.values() == std::vector<int>{5, 20, 40});
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.type(U",clib\n30-60\n");
    CHECK(r.values() == std::vector<int>{30, 40, 60});
    r.type(U"u,ciib\n");
    CHECK(r.values() == std::vector<int>{117, 87, 47});
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.type(U",xKsib\n+10\n");
    CHECK(r.values() == std::vector<int>{20, 50, 90});
    r.type(U"u,xKlib\n30-60\n");
    CHECK(r.values() == std::vector<int>{30, 40, 60});
    r.type(U"u,xKiib\n");
    CHECK(r.values() == std::vector<int>{117, 87, 47});
    r.type(U"u");
    CHECK(r.document.song() == before);
}

TEST_CASE("controller shaping commands accept positional and named amounts and refuse actions")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto before = r.document.song();
    r.line(U"ctl-scale cc1 20-100");
    CHECK(r.values() == std::vector<int>{20, 54, 100});
    r.type(U"u");
    r.line(U"ctl-scale cc1 by=+10");
    CHECK(r.values() == std::vector<int>{20, 50, 90});
    r.type(U"u");
    r.line(U"ctl-limit cc1 30-60");
    CHECK(r.values() == std::vector<int>{30, 40, 60});
    r.type(U"u");
    r.line(U"ctl-limit cc1 range=30-60");
    CHECK(r.values() == std::vector<int>{30, 40, 60});
    r.type(U"u");
    r.line(U"ctl-invert cc1");
    CHECK(r.values() == std::vector<int>{117, 87, 47});
    r.type(U"u");
    for (const auto line : {U"ctl-scale cc64 by=50%", U"ctl-scale cc0 by=50%",
                            U"ctl-limit cc32 range=20-100", U"ctl-invert all"})
    {
        r.line(line);
        CHECK(r.ws.message().starts_with("Not a continuous controller:"));
        CHECK(r.document.song() == before);
    }
    r.line(U"ctl-limit cc1 100-20");
    CHECK(r.ws.message().starts_with("Not a controller range:"));
    r.line(U"ctl-scale cc1 128-200");
    CHECK(r.ws.message().starts_with("Not a controller scale:"));
    r.line(U"ctl-invert cc1 extra=1");
    CHECK(r.ws.message().starts_with("Unexpected controller setting:"));
    CHECK(r.document.song() == before);
}

TEST_CASE("shaping from the arrangement asks which stream, with Tab offering those in its range")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto before = r.document.song();
    r.type(U"ga");
    r.line(U"ctl-scale");
    r.type(U"\n"); // No lane default: an empty answer cancels, as in other prompts.
    CHECK(r.ws.mode() == Mode::Normal);
    CHECK(r.document.song() == before);
    r.line(U"ctl-scale");
    r.type(U"\t\n50%\n"); // Only cc1 is present, so Tab completes it.
    CHECK(r.values() == std::vector<int>{5, 20, 40});
    r.type(U"u");
    CHECK(r.document.song() == before);
    // An inactive lane is not a default for editing the piano roll's notes either.
    r.type(U"gr\x1b");
    r.line(U"ctl-invert");
    r.type(U"\n");
    CHECK(r.document.song() == before);
    r.line(U"ctl-invert");
    r.type(U"cc1\n");
    CHECK(r.values() == std::vector<int>{117, 87, 47});
}

TEST_CASE("controller shaping skips ramps and their hidden source events, counting them")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    r.type(U",cr");
    r.keep();
    const auto before = r.document.song();
    for (const auto line : {U"ctl-scale cc1 50%", U"ctl-limit cc1 20-60", U"ctl-invert cc1"})
    {
        r.line(line);
        CHECK(r.document.song() == before);
        CHECK(r.ws.message().find("ramp") != std::string::npos);
    }
    r.type(U"u");
    CHECK(r.document.song().ramps(r.id()).empty());
    CHECK(r.values() == std::vector<int>{10, 40, 80});
}

TEST_CASE("controller shaping lane defaults distinguish bend and pressure from switches")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    r.document.edit("Other streams", {},
                    [&](core::Song& song)
                    {
                        song.updateEvents(r.id(),
                                          [&](core::EventList& events)
                                          {
                                              events.insert({0, core::PitchBend{-8192}});
                                              events.insert({r.q(), core::PitchBend{0}});
                                              events.insert({2 * r.q(), core::PitchBend{8191}});
                                              events.insert({0, core::ChannelPressure{10}});
                                          });
                    });
    r.line(U"lane bend");
    const auto before = r.document.song();
    r.type(U",csib\n50%\n");
    std::vector<int> bends;
    for (const auto& event : r.document.song().events(r.id()).all())
    {
        if (const auto* bend = std::get_if<core::PitchBend>(&event.data))
        {
            bends.push_back(bend->value);
        }
    }
    CHECK(bends == std::vector<int>{-4096, 0, 4096});
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.line(U"lane aftertouch");
    const auto pressureBefore = r.document.song();
    r.type(U",ciib\n");
    const auto& events = r.document.song().events(r.id());
    const auto pressure =
        std::ranges::find_if(events.all(), [](const auto& e)
                             { return std::holds_alternative<core::ChannelPressure>(e.data); });
    REQUIRE(pressure != events.all().end());
    CHECK(std::get<core::ChannelPressure>(pressure->data).value == 117);
    r.type(U"u");
    CHECK(r.document.song() == pressureBefore);
    r.line(U"lane cc64");
    const auto switchBefore = r.document.song();
    r.line(U"ctl-invert");
    r.type(U"\n");
    CHECK(r.document.song() == switchBefore);
    CHECK(r.ws.mode() == Mode::Normal);
}

TEST_CASE("controller shaping fits ordinary points without using a ramp's points as bounds")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    r.document.edit("Ramp and ordinary points", {},
                    [&](core::Song& song)
                    {
                        song.updateEvents(
                            r.id(), [&](core::EventList& events)
                            { events.insert({3 * r.q(), core::ControlChange{1, 20}}); });
                        REQUIRE(song.addRamp(r.id(), {.lane = {core::Lane::Kind::Controller, 1},
                                                      .start = 0,
                                                      .length = r.q(),
                                                      .from = 0,
                                                      .to = 127})
                                    .done());
                    });
    const auto before = r.document.song();
    r.line(U"ctl-scale cc1 30-90");
    CHECK(r.ws.message().find("ramp") != std::string::npos);
    CHECK(r.document.song().ramps(r.id())[0] == before.ramps(r.id())[0]);
    const auto& source = r.document.song().sourceEvents(r.id());
    std::vector<int> values;
    for (const auto& event : source.all())
    {
        if (const auto* cc = std::get_if<core::ControlChange>(&event.data))
        {
            values.push_back(cc->value);
        }
    }
    CHECK(values == std::vector<int>{10, 40, 90, 30});
    r.type(U"u");
    CHECK(r.document.song() == before);
}

namespace
{

void setCleanupEvents(Ramps& r, const std::vector<core::Event>& data)
{
    r.document.edit("Controller passage", {},
                    [&](core::Song& song)
                    {
                        song.updateEvents(r.id(),
                                          [&](core::EventList& events)
                                          {
                                              events = {};
                                              for (const auto& event : data)
                                              {
                                                  events.insert(event);
                                              }
                                          });
                    });
    r.document.breakMerge();
}

std::vector<int> cleanupValues(const Ramps& r, int controller)
{
    std::vector<int> values;
    for (const auto& event : r.document.song().events(r.id()).all())
    {
        if (const auto* cc = std::get_if<core::ControlChange>(&event.data);
            cc && cc->controller == controller)
        {
            values.push_back(cc->value);
        }
    }
    return values;
}

} // namespace

TEST_CASE("controller cleanup keys smooth the lane's values and undo as one edit")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto u = r.q() / 8;
    setCleanupEvents(r, {{0, core::ControlChange{1, 10}},
                         {u, core::ControlChange{1, 100}},
                         {2 * u, core::ControlChange{1, 10}},
                         {3 * u, core::ControlChange{1, 100}},
                         {4 * u, core::ControlChange{1, 10}}});
    const auto before = r.document.song();
    const auto cursor = r.view().cursor();
    r.type(U",cmib\n\n\n"); // lane, 50%, 1/16
    CHECK(r.values() == std::vector<int>{10, 70, 40, 70, 10});
    CHECK(r.view().cursor() == cursor);
    CHECK(r.ws.messageKind() != MessageKind::Warning);
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.type(U"vib,xKm\n100%\n\n");
    CHECK(r.values() == std::vector<int>{10, 40, 70, 40, 10});
    r.type(U"u");
    CHECK(r.document.song() == before);
}

TEST_CASE("controller cleanup removes exact repeats using context before the selected bar")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto q = r.q();
    setCleanupEvents(r, {{0, core::ControlChange{1, 40}},
                         {4 * q, core::ControlChange{1, 40}},
                         {4 * q + q / 8, core::ControlChange{1, 80}},
                         {4 * q + q / 4, core::ControlChange{1, 80}}});
    r.type(U"]b");
    const auto before = r.document.song();
    const auto cursor = r.view().cursor();
    r.type(U",cuib\n");
    CHECK(r.values() == std::vector<int>{40, 80});
    CHECK(r.view().cursor() == cursor);
    CHECK(r.ws.messageKind() != MessageKind::Warning);
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.type(U",xKuib\n");
    CHECK(r.values() == std::vector<int>{40, 80});
    r.type(U"u");
    CHECK(r.document.song() == before);
}

TEST_CASE(
    "controller cleanup defaults to all outside a continuous lane and offers streams with Tab")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto u = r.q() / 8;
    setCleanupEvents(r, {{0, core::ControlChange{1, 10}},
                         {u, core::ControlChange{1, 100}},
                         {2 * u, core::ControlChange{1, 10}},
                         {3 * u, core::ControlChange{1, 10}},
                         {0, core::ControlChange{7, 20}},
                         {u, core::ControlChange{7, 120}},
                         {2 * u, core::ControlChange{7, 20}},
                         {3 * u, core::ControlChange{7, 20}}});
    r.type(U"ga");
    const auto before = r.document.song();
    r.line(U"ctl-smooth");
    r.type(U"\n\n\n");
    CHECK(cleanupValues(r, 1) == std::vector<int>{10, 70, 25, 10});
    CHECK(cleanupValues(r, 7) == std::vector<int>{20, 87, 37, 20});
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.line(U"ctl-dedupe");
    r.type(U"\n");
    CHECK(cleanupValues(r, 1) == std::vector<int>{10, 100, 10});
    CHECK(cleanupValues(r, 7) == std::vector<int>{20, 120, 20});
    r.type(U"u");
    CHECK(r.document.song() == before);
    // Tab offers the streams in the target, as well as all.
    r.line(U"ctl-dedupe");
    r.type(U"cc\t");
    REQUIRE(r.ws.completions());
    CHECK(r.ws.completions()->matches == std::vector<std::string>{"cc1", "cc7"});
    r.type(U"\x1b");
    // A switch lane has no continuous stream of its own: default to all here too.
    r.type(U"gr");
    r.line(U"lane cc64");
    r.line(U"ctl-dedupe");
    r.type(U"\n");
    CHECK(cleanupValues(r, 1) == std::vector<int>{10, 100, 10});
    CHECK(cleanupValues(r, 7) == std::vector<int>{20, 120, 20});
}

TEST_CASE("controller cleanup uses a track's meter for smoothing windows in bars")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto q = r.q();
    setCleanupEvents(r, {{0, core::ControlChange{1, 10}},
                         {q / 4, core::ControlChange{1, 100}},
                         {q, core::ControlChange{1, 50}},
                         {7 * q / 4, core::ControlChange{1, 10}},
                         {2 * q, core::ControlChange{1, 100}}});
    r.document.edit("Track meter", {},
                    [&](core::Song& song)
                    {
                        song.updateMeter(r.id(), [](core::MeterMap& meter)
                                         { meter = core::MeterMap{meter.ppqn(), {3, 4}}; });
                    });
    const auto before = r.document.song();
    r.line(U"ctl-smooth cc1 strength=100% window=1bar");
    CHECK(r.values() == std::vector<int>{10, 43, 54, 65, 100});
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.line(U"ctl-smooth cc1 strength=100% window=1/1");
    CHECK(r.values() == std::vector<int>{10, 54, 54, 54, 100});
}

TEST_CASE("controller cleanup refuses invalid settings and protects ramps")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto before = r.document.song();
    for (const auto line :
         {U"ctl-smooth cc64 strength=50% window=1/16", U"ctl-smooth cc0 strength=50% window=1/16",
          U"ctl-smooth cc1 strength=101% window=1/16", U"ctl-smooth cc1 strength=-1% window=1/16",
          U"ctl-smooth cc1 strength=50% window=0bar", U"ctl-smooth cc1 strength=50% window=1%bar",
          U"ctl-smooth cc1 strength=50% window=1/0",
          U"ctl-smooth cc1 strength=50% window=1/16 extra=1", U"ctl-dedupe cc64",
          U"ctl-dedupe cc1 step=2"})
    {
        r.line(line);
        CHECK(r.ws.messageKind() == MessageKind::Warning);
        CHECK(r.document.song() == before);
    }
    r.type(U",cr");
    r.keep();
    const auto withRamp = r.document.song();
    for (const auto line : {U"ctl-smooth all strength=100% window=1bar", U"ctl-dedupe all"})
    {
        r.line(line);
        CHECK(r.document.song() == withRamp);
        CHECK(r.ws.message().find("ramp") != std::string::npos);
    }
    r.type(U"u");
    CHECK(r.document.song() == before);
}

TEST_CASE("controller range transforms also shape a lane containing no notes")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    setCleanupEvents(r, {{0, core::ControlChange{1, 10}}, {r.q(), core::ControlChange{1, 80}}});
    const auto before = r.document.song();
    r.line(U"ctl-scale cc1 50%");
    CHECK(r.values() == std::vector<int>{5, 40});
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.line(U"ctl-limit cc1 30-60");
    CHECK(r.values() == std::vector<int>{30, 60});
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.line(U"ctl-invert cc1");
    CHECK(r.values() == std::vector<int>{117, 47});
    r.type(U"u");
    CHECK(r.document.song() == before);
}

TEST_CASE("program remapping takes matching events in the range and keeps banks and setup")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto q = r.q();
    setCleanupEvents(r, {{0, core::ControlChange{0, 2}},
                         {0, core::ControlChange{32, 3}},
                         {0, core::ProgramChange{5}},
                         {q, core::ProgramChange{6}},
                         {2 * q, core::ProgramChange{5}},
                         {4 * q, core::ProgramChange{5}}});
    r.document.edit("Program setup", {},
                    [&](core::Song& song)
                    {
                        song.updateTrack(
                            r.id(), [](core::Track& track)
                            { track.programSetup = {.bankMsb = 2, .bankLsb = 3, .program = 5}; });
                    });
    r.type(U"ga");
    const auto before = r.document.song();
    const auto programs = [&]
    {
        std::vector<int> values;
        for (const auto& event : r.document.song().events(r.id()).all())
        {
            if (const auto* p = std::get_if<core::ProgramChange>(&event.data))
            {
                values.push_back(p->program);
            }
        }
        return values;
    };
    r.line(U"remap program 5 to=program 127");
    CHECK(programs() == std::vector<int>{127, 6, 127, 5});
    CHECK(cleanupValues(r, 0) == std::vector<int>{2});
    CHECK(cleanupValues(r, 32) == std::vector<int>{3});
    CHECK(r.document.song().track(r.id())->programSetup == before.track(r.id())->programSetup);
    CHECK(r.ws.messageKind() != MessageKind::Warning);
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.type(U",xmibprogram 5\nprogram 0\n");
    CHECK(programs() == std::vector<int>{0, 6, 0, 5});
    r.type(U"u");
    CHECK(r.document.song() == before);
    for (const auto line : {U"remap program 128 to=program 0", U"remap program 5 to=program -1",
                            U"remap program 5 to=cc1", U"remap cc1 to=program 5"})
    {
        r.line(line);
        CHECK(r.ws.messageKind() == MessageKind::Warning);
        CHECK(r.document.song() == before);
    }
}

TEST_CASE("remap asks the type, then from and to in that type's form, offering the range's")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto q = r.q();
    setCleanupEvents(r, {{0, core::Note{60, 90, q}},
                         {q, core::Note{64, 90, q}},
                         {0, core::ControlChange{1, 10}},
                         {q, core::ControlChange{7, 40}},
                         {0, core::ProgramChange{5}},
                         {2 * q, core::ProgramChange{12}}});
    const auto before = r.document.song();
    const auto version = r.document.version();
    // The prompt's row, with its question and the hint for what is asked.
    const auto asking = [&](std::u32string_view text)
    {
        std::u32string row;
        const auto frame = r.ws.frame();
        for (const auto& run : frame[frame.size() - 2].text)
        {
            row += run.text;
        }
        return row.find(text) != std::u32string::npos;
    };
    const auto pitches = [&]
    {
        std::vector<int> result;
        for (const auto& event : r.document.song().events(r.id()).all())
        {
            if (const auto* note = std::get_if<core::Note>(&event.data))
            {
                result.push_back(note->pitch);
            }
        }
        return result;
    };
    const auto programs = [&]
    {
        std::vector<int> result;
        for (const auto& event : r.document.song().events(r.id()).all())
        {
            if (const auto* program = std::get_if<core::ProgramChange>(&event.data))
            {
                result.push_back(program->program);
            }
        }
        return result;
    };

    // In a controller's lane: a controller, the lane's own, Tab offering those in the range.
    r.type(U",xmib");
    CHECK(asking(U"type (controller)"));
    r.type(U"\n");
    CHECK(asking(U"from controller (cc1)"));
    r.type(U"cc\t");
    REQUIRE(r.ws.completions());
    CHECK(r.ws.completions()->matches == std::vector<std::string>{"cc1", "cc7"});
    r.type(U"\x1b");
    CHECK(r.document.song() == before);
    CHECK(r.document.version() == version);

    // Enter twice and a number: cc1 becomes cc11, undone as one step and repeated with dot.
    r.type(U",xmib\n\n");
    CHECK(asking(U"to controller"));
    CHECK(asking(U"0 to 127, or cc11"));
    r.type(U"11\n");
    CHECK(r.ws.messageKind() != MessageKind::Warning);
    CHECK(cleanupValues(r, 11) == std::vector<int>{10});
    CHECK(cleanupValues(r, 1).empty());
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.type(U".");
    CHECK(cleanupValues(r, 11) == std::vector<int>{10});
    r.type(U"u");
    CHECK(r.document.song() == before);

    // In the notes: a note, the range's first.
    r.type(U"\x1b");
    r.type(U",xmib");
    CHECK(asking(U"type (note)"));
    r.type(U"\n");
    CHECK(asking(U"from note (C4)"));
    r.type(U"\nD4\n");
    CHECK(pitches() == std::vector<int>{62, 64});
    r.type(U"u");
    CHECK(r.document.song() == before);

    // Programs, by number, from the arrangement.
    r.type(U"ga");
    r.type(U",xmibprogram\n");
    CHECK(asking(U"from program (5)"));
    r.type(U"\n");
    CHECK(asking(U"to program (0 to 127)"));
    r.type(U"9\n");
    CHECK(programs() == std::vector<int>{9, 12});
    r.type(U"u");
    CHECK(r.document.song() == before);

    // Refusals change nothing: a value outside the type, a type there isn't, a mismatch.
    r.type(U",xmibprogram\n\n200\n");
    CHECK(r.ws.message() == "Not a program: 200; type 0 to 127");
    r.type(U",xmibbogus\n");
    CHECK(r.ws.message().starts_with("Not a type to remap: bogus"));
    r.type(U",xmibnote\n\ncc7\n");
    CHECK(r.ws.message().starts_with("Not a note: cc7"));
    CHECK(r.document.song() == before);
    CHECK(r.document.version() == version);

    // The command line takes the type, or a form that says it, asking nothing.
    r.line(U"remap type=program 12 to=13");
    CHECK(programs() == std::vector<int>{5, 13});
    r.type(U"u");
    r.line(U"remap cc7 to=cc10");
    CHECK(cleanupValues(r, 10) == std::vector<int>{40});
    r.type(U"u");
    CHECK(r.document.song() == before);
}

TEST_CASE("lane timing transforms affect the active contour and preserve notes and other lanes")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto q = r.q();
    const core::Event note{q / 4, core::Note{60, 90, q / 2}};
    const core::Event other{q / 2, core::ControlChange{7, 50}};
    setCleanupEvents(r, {{0, core::ControlChange{1, 10}},
                         {q, core::ControlChange{1, 100}},
                         {3 * q, core::ControlChange{1, 40}},
                         {4 * q, core::ControlChange{1, 77}},
                         note,
                         other});
    const auto before = r.document.song();
    const auto cursor = r.view().cursor();
    const auto times = [&]
    {
        std::vector<core::Tick> ticks;
        for (const auto& event : r.document.song().events(r.id()).all())
        {
            if (const auto* cc = std::get_if<core::ControlChange>(&event.data);
                cc && cc->controller == 1)
            {
                ticks.push_back(event.tick);
            }
        }
        return ticks;
    };
    const auto othersStay = [&]
    {
        const auto events = r.document.song().events(r.id()).all();
        CHECK(std::ranges::find(events, note) != events.end());
        CHECK(std::ranges::find(events, other) != events.end());
        CHECK(r.view().cursor() == cursor);
    };
    r.line(U"retrograde");
    CHECK(cleanupValues(r, 1) == std::vector<int>{40, 100, 10, 77});
    CHECK(times() == std::vector<core::Tick>{0, q, 3 * q, 4 * q});
    othersStay();
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.type(U"vib,cx");
    CHECK(cleanupValues(r, 1) == std::vector<int>{40, 100, 10, 77});
    r.type(U"u");
    CHECK(r.document.song() == before);
    const auto step = before.track(r.id())->grid.toTicks(r.ppqn);
    r.type(U"tib");
    CHECK(times() == std::vector<core::Tick>{step, q + step, 3 * q + step, 4 * q});
    othersStay();
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.line(U"stretch 1/2");
    CHECK(times() == std::vector<core::Tick>{0, q / 2, 3 * q / 2, 4 * q});
    othersStay();
    r.type(U"u");
    CHECK(r.document.song() == before);
    // Retrograde moved to , c x and , x x; , x R no longer reverses anything.
    r.type(U"vib,xR");
    CHECK(r.document.song() == before);
}

TEST_CASE("controller retrograde refuses unknown values and partial box or search selections")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto q = r.q();
    setCleanupEvents(r, {{q, core::ControlChange{1, 100}}, {2 * q, core::ControlChange{1, 20}}});
    auto before = r.document.song();
    auto version = r.document.version();
    r.line(U"retrograde");
    CHECK(r.ws.message().starts_with("No held controller value is known"));
    CHECK(r.document.song() == before);
    CHECK(r.document.version() == version);
    setCleanupEvents(r, {{0, core::ControlChange{1, 10}},
                         {q, core::ControlChange{1, 100}},
                         {2 * q, core::ControlChange{1, 20}},
                         {3 * q, core::ControlChange{1, 40}},
                         {4 * q, core::ControlChange{1, 77}}});
    before = r.document.song();
    version = r.document.version();
    r.ws.handle(Key::typed(U'v', kCtrl), 0);
    r.type(U"]b"); // Values 10 through 77 omit the 100 inside the span.
    const auto cursor = r.view().cursor();
    r.line(U"retrograde");
    CHECK(r.ws.message().find("complete time selection") != std::string::npos);
    CHECK(r.document.song() == before);
    CHECK(r.document.version() == version);
    CHECK(r.view().cursor() == cursor);
    r.type(U"gg/cc1=100\nvib,Sm");
    r.line(U"retrograde");
    CHECK(r.ws.message().find("complete time selection") != std::string::npos);
    CHECK(r.document.song() == before);
    CHECK(r.document.version() == version);
}

TEST_CASE(
    "controller retrograde refuses its own ramps but preserves other lanes and the right edge")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto q = r.q();
    r.type(U",cr");
    r.keep();
    auto before = r.document.song();
    auto version = r.document.version();
    r.line(U"retrograde");
    CHECK(r.ws.message().find("protected") != std::string::npos);
    CHECK(r.document.song() == before);
    CHECK(r.document.version() == version);
    r.type(U"u");
    r.document.edit("Other lane ramp", {},
                    [&](core::Song& song)
                    {
                        REQUIRE(song.addRamp(r.id(), {.lane = {core::Lane::Kind::Controller, 11},
                                                      .start = 0,
                                                      .length = 3 * q,
                                                      .from = 20,
                                                      .to = 100})
                                    .done());
                    });
    before = r.document.song();
    const auto otherValues = cleanupValues(r, 11);
    r.line(U"retrograde");
    CHECK(cleanupValues(r, 1) == std::vector<int>{80, 40, 10});
    CHECK(cleanupValues(r, 11) == otherValues);
    CHECK(std::ranges::equal(r.document.song().ramps(r.id()), before.ramps(r.id())));
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.document.edit("Right boundary ramp", {},
                    [&](core::Song& song)
                    {
                        REQUIRE(song.addRamp(r.id(), {.lane = {core::Lane::Kind::Controller, 1},
                                                      .start = 4 * q,
                                                      .length = q,
                                                      .from = 50,
                                                      .to = 80})
                                    .done());
                    });
    before = r.document.song();
    r.line(U"retrograde");
    CHECK(r.ws.messageKind() != MessageKind::Warning);
    CHECK(std::ranges::equal(r.document.song().events(r.id()).startingIn(4 * q, 8 * q),
                             before.events(r.id()).startingIn(4 * q, 8 * q)));
}

TEST_CASE("controller retrograde of selected events reverses them over their own spacing")
{
    // An event selection has no time of its own: its last value counts as lasting as long as
    // the gap before it, but not past the lane's next event or the end of its bar. Two lone
    // events then change places, and a converted ramp's points reverse where they are.
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto q = r.q();
    const auto times = [&]
    {
        std::vector<core::Tick> ticks;
        for (const auto& event : r.document.song().events(r.id()).all())
        {
            ticks.push_back(event.tick);
        }
        return ticks;
    };
    setCleanupEvents(r, {{0, core::ControlChange{1, 20}}, {2 * q, core::ControlChange{1, 100}}});
    auto before = r.document.song();
    r.type(U"v\t,xx");
    CHECK(r.ws.messageKind() != MessageKind::Warning);
    CHECK(cleanupValues(r, 1) == std::vector<int>{100, 20});
    CHECK(times() == std::vector<core::Tick>{0, 2 * q});
    r.type(U"u");
    CHECK(r.document.song() == before);

    // A ramp over the first half of a bar, made events: its points reverse within that half,
    // the rest of the bar holding its first value where it held its last.
    setCleanupEvents(r, {{0, core::ControlChange{1, 40}},
                         {q / 2, core::ControlChange{1, 60}},
                         {q, core::ControlChange{1, 80}},
                         {3 * q / 2, core::ControlChange{1, 100}},
                         {2 * q, core::ControlChange{1, 120}}});
    r.type(U"[b");
    REQUIRE(r.view().cursor().tick == 0);
    r.type(U"v\t\t\t\t,xx");
    CHECK(cleanupValues(r, 1) == std::vector<int>{120, 100, 80, 60, 40});
    CHECK(times() == std::vector<core::Tick>{0, q / 2, q, 3 * q / 2, 2 * q});

    // The next event ends what is reversed if it comes sooner, and stays where it is.
    setCleanupEvents(r, {{0, core::ControlChange{1, 20}},
                         {2 * q, core::ControlChange{1, 100}},
                         {3 * q, core::ControlChange{1, 60}}});
    r.type(U"[b");
    REQUIRE(r.view().cursor().tick == 0);
    r.type(U"v\t,xx");
    CHECK(cleanupValues(r, 1) == std::vector<int>{100, 20, 60});
    CHECK(times() == std::vector<core::Tick>{0, q, 3 * q});

    // So does the end of the bar.
    setCleanupEvents(r, {{0, core::ControlChange{1, 20}},
                         {3 * q, core::ControlChange{1, 100}},
                         {5 * q, core::ControlChange{1, 60}}});
    r.type(U"[b");
    REQUIRE(r.view().cursor().tick == 0);
    r.type(U"v\t,xx");
    CHECK(cleanupValues(r, 1) == std::vector<int>{100, 20, 60});
    CHECK(times() == std::vector<core::Tick>{0, q, 5 * q});
}

TEST_CASE("timing edits refuse ramp destinations without moving events or the cursor")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const auto q = r.q();
    setCleanupEvents(r, {{0, core::ControlChange{1, 40}},
                         {2 * q, core::ControlChange{1, 80}},
                         {q / 2, core::ControlChange{7, 20}}});
    const auto step = r.document.song().track(r.id())->grid.toTicks(r.ppqn);
    r.document.edit("Sparse ramp", {},
                    [&](core::Song& song)
                    {
                        REQUIRE(song.addRamp(r.id(), {.lane = {core::Lane::Kind::Controller, 1},
                                                      .start = step / 2,
                                                      .length = q,
                                                      .from = 10,
                                                      .to = 80,
                                                      .density = 1})
                                    .done());
                    });
    const auto before = r.document.song();
    const auto cursor = r.view().cursor();
    const auto version = r.document.version();
    r.type(U"tib");
    CHECK(r.ws.message().find("protected") != std::string::npos);
    CHECK(r.document.song() == before);
    CHECK(r.view().cursor() == cursor);
    CHECK(r.document.version() == version);
    r.line(U"stretch 1/2");
    CHECK(r.ws.message().find("protected") != std::string::npos);
    CHECK(r.document.song() == before);
    CHECK(r.document.version() == version);
    r.ws.handle(Key::typed(U'l', kAlt), 0);
    CHECK(r.ws.message().find("protected") != std::string::npos);
    CHECK(r.document.song() == before);
    CHECK(r.view().cursor() == cursor);
    CHECK(r.document.version() == version);
    r.line(U"remap cc7 to=cc1");
    CHECK(r.ws.message().find("protected") != std::string::npos);
    CHECK(r.document.song() == before);
    CHECK(r.document.version() == version);
    r.type(U"gegg");
    auto& view = r.ws.eventList();
    for (std::size_t n = 0;
         n < view.list().rows().size() && (view.list().rows()[view.row()].kind != RowKind::Event ||
                                           view.list().rows()[view.row()].tick != 0);
         ++n)
    {
        r.type(U"j");
    }
    REQUIRE(view.list().rows()[view.row()].kind == RowKind::Event);
    REQUIRE(view.list().rows()[view.row()].tick == 0);
    r.type(U"0\n");
    r.type(decodeUtf8("1:1:" + std::to_string(step) + "\n"));
    CHECK(view.editingField());
    CHECK(r.ws.message().find("protected") != std::string::npos);
    CHECK(r.document.song() == before);
    CHECK(r.document.version() == version);
    CHECK(view.cursor().tick == 0);
}

TEST_CASE("controller retrograde follows bend and aftertouch lanes and refuses switch lanes")
{
    Ramps r{GENERATE(core::kPpqn, core::Ppqn{96})};
    const bool bend = GENERATE(true, false);
    const auto q = r.q();
    const auto data = [bend](int value) -> core::EventData
    {
        return bend ? core::EventData{core::PitchBend{static_cast<std::int16_t>(value)}}
                    : core::EventData{core::ChannelPressure{static_cast<std::uint8_t>(value)}};
    };
    const int first = bend ? -100 : 10;
    setCleanupEvents(
        r,
        {{0, data(first)}, {q, data(100)}, {3 * q, data(40)}, {q / 2, core::ControlChange{1, 50}}});
    r.line(bend ? U"lane bend" : U"lane aftertouch");
    const auto before = r.document.song();
    r.type(U",xxib");
    std::vector<int> values;
    for (const auto& event : r.document.song().events(r.id()).all())
    {
        if (const auto* value = std::get_if<core::PitchBend>(&event.data))
        {
            values.push_back(value->value);
        }
        if (const auto* value = std::get_if<core::ChannelPressure>(&event.data))
        {
            values.push_back(value->value);
        }
    }
    CHECK(values == std::vector<int>{40, 100, first});
    CHECK(cleanupValues(r, 1) == std::vector<int>{50});
    r.type(U"u");
    CHECK(r.document.song() == before);
    r.line(U"lane cc64");
    const auto switched = r.document.song();
    r.line(U"retrograde");
    CHECK(r.ws.message().find("continuous controller lane") != std::string::npos);
    CHECK(r.document.song() == switched);
}
