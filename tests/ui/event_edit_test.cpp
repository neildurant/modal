// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

#include <modal/ui/event_edit.hpp>
#include <modal/ui/event_text.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

using namespace modal;
using namespace modal::ui;

namespace
{

// The event a result holds, which must be one.
core::Event changed(const EditResult& result)
{
    if (const auto* error = std::get_if<std::string>(&result))
    {
        FAIL(*error);
    }
    return std::get<core::Event>(result);
}

std::string refused(const EditResult& result)
{
    REQUIRE(std::holds_alternative<std::string>(result));
    return std::get<std::string>(result);
}

core::Note noteOf(const core::Event& event)
{
    return std::get<core::Note>(event.data);
}

} // namespace

TEST_CASE("a note's fields are set by value or by a change")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const core::MeterMap meter{ppqn};
    const core::Event note{q, core::Note{60, 100, q / 2, 64}};

    CHECK(noteOf(changed(setField(note, Column::Data, "E2", meter))).pitch == 40);
    CHECK(noteOf(changed(setField(note, Column::Data, "61", meter))).pitch == 61);
    CHECK(noteOf(changed(setField(note, Column::Data, "+12", meter))).pitch == 72);
    CHECK(noteOf(changed(setField(note, Column::Value, " 80 ", meter))).velocity == 80);
    CHECK(noteOf(changed(setField(note, Column::Value, "-40", meter))).velocity == 60);
    CHECK(noteOf(changed(setField(note, Column::Release, "0", meter))).releaseVelocity == 0);

    CHECK(noteOf(changed(setField(note, Column::Length, "1/8.", meter))).duration ==
          core::NoteFraction{3, 16}.toTicks(ppqn));
    CHECK(noteOf(changed(setField(note, Column::Length, "0:1:010", meter))).duration == q + 10);
    CHECK(noteOf(changed(setField(note, Column::Length, "+1/16", meter))).duration ==
          q / 2 + q / 4);
    CHECK(noteOf(changed(setField(note, Column::Length, "-0:0:010", meter))).duration ==
          q / 2 - 10);

    CHECK(changed(setField(note, Column::Position, "2:3:010", meter)).tick == 6 * q + 10);
    CHECK(changed(setField(note, Column::Position, "3", meter)).tick == 8 * q);
    CHECK(changed(setField(note, Column::Position, "+1/4", meter)).tick == 2 * q);
    CHECK(changed(setField(note, Column::Position, "-0:1:000", meter)).tick == 0);

    // Out of range, or not what the field takes.
    CHECK(refused(setField(note, Column::Value, "0", meter)).starts_with("A velocity must be"));
    CHECK(refused(setField(note, Column::Value, "+40", meter)).starts_with("A velocity must"));
    CHECK(refused(setField(note, Column::Data, "H2", meter)).starts_with("\"H2\" isn't a pitch"));
    CHECK(refused(setField(note, Column::Length, "-1/4", meter)).starts_with("A length is"));
    CHECK(refused(setField(note, Column::Position, "-1/2", meter)).starts_with("A position is"));
    CHECK(refused(setField(note, Column::Position, "1:5:000", meter)).starts_with("A position"));
}

TEST_CASE("other events' fields")
{
    const core::MeterMap meter{core::kPpqn};
    const core::Event cc{0, core::ControlChange{7, 100}};
    CHECK(std::get<core::ControlChange>(changed(setField(cc, Column::Data, "10", meter)).data)
              .controller == 10);
    CHECK(refused(setField(cc, Column::Value, "128", meter)).starts_with("A value must be"));

    const core::Event bend{0, core::PitchBend{0}};
    CHECK(std::get<core::PitchBend>(changed(setField(bend, Column::Value, "-8192", meter)).data)
              .value == -8192);
    CHECK(refused(setField(bend, Column::Value, "8192", meter)).starts_with("A bend must be"));

    const core::Event lyric{0, core::makeMetaText(core::MetaText::Kind::Lyric, "la")};
    CHECK(*std::get<core::MetaText>(changed(setField(lyric, Column::Data, "la la", meter)).data)
               .text == "la la");

    const core::Event sysex{0, core::makeSysEx({0xF0, 0x7E, 0xF7})};
    CHECK(changed(setField(sysex, Column::Data, "F0 7F F7", meter)).data ==
          core::EventData{core::makeSysEx({0xF0, 0x7F, 0xF7})});
    CHECK(refused(setField(sysex, Column::Data, "F0 7F", meter)).starts_with("SysEx must"));
    CHECK(refused(setField(sysex, Column::Data, "F0 7G F7", meter)).starts_with("SysEx is"));
}

TEST_CASE("stepping a field: by 1, a semitone, a beat or a 1/16, kept within its range")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    core::MeterMap meter{ppqn};
    meter.set(4 * q, {6, 8});
    const core::Event note{q, core::Note{60, 120, q, 64}};

    CHECK(noteOf(changed(stepField(note, Column::Value, 5, meter))).velocity == 125);
    CHECK(noteOf(changed(stepField(note, Column::Value, 20, meter))).velocity == 127);
    CHECK(noteOf(changed(stepField(note, Column::Data, -2, meter))).pitch == 58);
    CHECK(noteOf(changed(stepField(note, Column::Length, 1, meter))).duration == q + q / 4);
    CHECK(changed(stepField(note, Column::Position, 2, meter)).tick == 3 * q);
    CHECK(changed(stepField(note, Column::Position, -5, meter)).tick == 0);

    // In 6/8 a beat is an eighth.
    const core::Event later{4 * q, core::Note{60, 1, q, 64}};
    CHECK(changed(stepField(later, Column::Position, 1, meter)).tick == 4 * q + q / 2);
    CHECK(noteOf(changed(stepField(later, Column::Value, -3, meter))).velocity == 1);

    const core::Event text{0, core::makeMetaText(core::MetaText::Kind::Text, "x")};
    CHECK(refused(stepField(text, Column::Data, 1, meter)) == "Text and SysEx can't be stepped");
}

TEST_CASE("a note may not overlap another of its pitch")
{
    const auto ppqn = MODAL_ANY_PPQN();
    const auto q = ppqn.quarter();
    const core::MeterMap meter{ppqn};
    core::EventList events;
    events.insert({0, core::Note{60, 100, q}});     // 0
    events.insert({4 * q, core::Note{60, 100, q}}); // 2
    events.insert({0, core::Note{64, 100, 4 * q}}); // 1, after the C4 at its tick
    const auto at = [&](core::Tick tick) { return positionText(meter, tick); };

    CHECK_FALSE(overlap(events, 0, {0, core::Note{60, 100, 2 * q}}, meter));
    CHECK(overlap(events, 0, {0, core::Note{60, 100, 4 * q + 1}}, meter) ==
          "Would run into the C4 at " + at(4 * q));
    CHECK(overlap(events, std::nullopt, {0, core::Note{64, 100, 1}}, meter) ==
          "Would overlap the E4 at " + at(0));
    CHECK(overlap(events, 2, {2 * q, core::Note{64, 100, q}}, meter) ==
          "Would overlap the E4 at " + at(0));
    CHECK_FALSE(overlap(events, std::nullopt, {q, core::Note{60, 100, q}}, meter));
    CHECK_FALSE(overlap(events, std::nullopt, {0, core::ControlChange{1, 2}}, meter));
}
