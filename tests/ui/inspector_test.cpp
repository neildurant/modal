// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/inspector.hpp>
#include <modal/ui/musical_time.hpp>
#include <modal/ui/view_parts.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace modal;
using namespace modal::ui;
using namespace std::chrono_literals;

namespace
{

constexpr core::Tick q = 960;

// A song of two sections with a bass track of a few events, at 120 BPM: a quarter is 500 ms.
core::Song song()
{
    core::Song s{core::kPpqn};
    const auto bass = s.addTrack("Bass");
    s.updateTrack(bass, [](core::Track& t) { t.channel = 3; });
    s.setLabel(2 * q, "fill");
    s.updateEvents(bass,
                   [](core::EventList& events)
                   {
                       events.insert({q, core::Note{28, 96, q / 2, 40}});
                       events.insert({q, core::ControlChange{74, 64}});
                       events.insert({2 * q, core::ControlChange{3, 1}});
                       events.insert({3 * q, core::makeSysEx({0xF0, 0x7E, 0xF7})});
                       events.insert({3 * q, core::makeMetaText(core::MetaText::Kind::Lyric,
                                                                "a long line of lyrics")});
                   });
    s.splitSection(4 * q, "Verse");
    s.fitToContent();
    return s;
}

// The inspection of the first row of a kind, or of the n-th event.
Inspection of(const EventList& list, RowKind kind, std::size_t skip = 0)
{
    std::size_t i = 0;
    while (i < list.rows().size() && !(list.rows()[i].kind == kind && skip-- == 0))
    {
        ++i;
    }
    REQUIRE(i < list.rows().size());
    return inspect(list, i);
}

} // namespace

TEST_CASE("times from the song's start are minutes, seconds and milliseconds")
{
    CHECK(songTime(0ms) == "0:00.000");
    CHECK(songTime(62'250ms) == "1:02.250");
    CHECK(songTime(3'602'250ms) == "1:00:02.250");
}

TEST_CASE("the inspector shows every property of an event, with what the row can't")
{
    const auto s = song();
    const EventList list{s, s.tracks()[0]->id()};

    const auto note = of(list, RowKind::Event, 1); // the CC is first at its tick
    CHECK(note.kind == "Note");
    CHECK(note.fields == std::vector<InspectorField>{{"Position", "1:2:000"},
                                                     {"Pitch", "E1 (28)"},
                                                     {"Velocity", "96"},
                                                     {"Length", "0:0:480 (1/8)"},
                                                     {"Ends", "1:2:480"},
                                                     {"Release", "40"}});

    const auto cc = of(list, RowKind::Event, 0);
    CHECK(cc.kind == "CC");
    CHECK(cc.fields[1] == InspectorField{"Controller", "74 Brightness"});
    CHECK(of(list, RowKind::Event, 2).fields[1] == InspectorField{"Controller", "3"});

    const auto sysex = of(list, RowKind::Event, 3);
    CHECK(sysex.kind == "SysEx");
    CHECK(sysex.fields[1] == InspectorField{"Bytes", "3"});
    CHECK(sysex.fields[2] == InspectorField{"SysEx", "F0 7E F7"});
    const auto lyric = of(list, RowKind::Event, 4);
    CHECK(lyric.kind == "Lyric");
    CHECK(lyric.fields[1] == InspectorField{"Text", "a long line of lyrics"});
}

TEST_CASE("on a row that isn't an event, the inspector says what the row is")
{
    const auto s = song();
    const EventList list{s, s.tracks()[0]->id()};

    const auto section = of(list, RowKind::Section, 1);
    CHECK(section.kind == "Section");
    CHECK(section.fields ==
          std::vector<InspectorField>{{"Position", "2:1:000"},
                                      {.heading = "Section", .value = "Verse", .underKind = true},
                                      {"Bars", "2–2"},
                                      {"Length", "1 bar, 0:02.000"}});
    const auto tempo = of(list, RowKind::Tempo);
    CHECK(tempo.fields[1] == InspectorField{"Tempo", "120 BPM"});
    CHECK(tempo.fields[2] == InspectorField{"Per quarter", "500 ms"});
    auto odd = song();
    odd.updateTempo([](core::TempoMap& t) { t.set(0, core::Tempo{594'059}); });
    const EventList oddList{odd, odd.tracks()[0]->id()};
    CHECK(of(oddList, RowKind::Tempo).fields[2] == InspectorField{"Per quarter", "594.059 ms"});
    odd.updateTempo([](core::TempoMap& t) { t.set(0, core::Tempo{594'500}); });
    const EventList halfList{odd, odd.tracks()[0]->id()};
    CHECK(of(halfList, RowKind::Tempo).fields[2] == InspectorField{"Per quarter", "594.5 ms"});
    CHECK(of(list, RowKind::Meter).fields[1] == InspectorField{"Meter", "4/4"});
    CHECK(of(list, RowKind::Label).fields[1] == InspectorField{"Label", "fill"});
    CHECK(of(list, RowKind::End).fields[1] == InspectorField{"Song", "2 bars, 0:04.000"});
}

TEST_CASE("coarse and fine timing preserves exact starts and lengths")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    const core::MeterMap meter{ppqn};
    const core::NoteFraction unit{1, 16};
    const auto step = unit.toTicks(ppqn);
    const auto tick = meter.barStart(7) + 2 * step + 1;
    const core::Event event{tick, core::Note{60, 90, 2 * step + 2}};
    Inspection inspection;
    addMusicalTiming(inspection, event, meter, unit);
    const auto value = [&](std::string_view heading) -> std::string
    {
        for (const auto& field : inspection.fields)
            if (field.heading == heading)
                return field.value;
        return {};
    };
    CHECK(value("Start step") == "3");
    CHECK(value("Start fine") == "+1t");
    CHECK(value("Length steps") == "2");
    CHECK(value("Length fine") == "+2t");
    CHECK(value("Ends step") == "5");
    CHECK(value("Ends fine") == "+3t");
    const auto changed = setMusicalTiming(event, "Start step", "4", meter, unit);
    REQUIRE(std::holds_alternative<core::Event>(changed));
    CHECK(std::get<core::Event>(changed).tick == tick + step);
    const auto length = setMusicalTiming(event, "Length steps", "3", meter, unit);
    REQUIRE(std::holds_alternative<core::Event>(length));
    CHECK(std::get<core::Note>(std::get<core::Event>(length).data).duration == 3 * step + 2);
    const auto early = setMusicalTiming(event, "Start fine", "-1t", meter, unit);
    REQUIRE(std::holds_alternative<core::Event>(early));
    CHECK(std::get<core::Event>(early).tick == tick - 2);
    CHECK(std::holds_alternative<std::string>(
        setMusicalTiming(event, "Start step", "0", meter, unit)));
    CHECK(std::holds_alternative<std::string>(
        setMusicalTiming(event, "Start step", "10000000000000000", meter, unit)));
    const auto boundary = core::Event{meter.barStart(2) - 1, core::Note{60, 90, step - 1}};
    inspection = {};
    addMusicalTiming(inspection, boundary, meter, unit);
    CHECK(value("Start step") == "1");
    CHECK(value("Start fine") == "-1t");
    CHECK(value("Length fine") == "-1t");
}

TEST_CASE("a note's end is edited by its step and fine ticks, changing only its length")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    const core::MeterMap meter{ppqn};
    const core::NoteFraction unit{1, 16};
    const auto step = unit.toTicks(ppqn);
    const auto tick = meter.barStart(3);
    const core::Event event{tick, core::Note{60, 90, 2 * step + 1}}; // ends at step 3, +1
    const auto lengthOf = [](const EditResult& result)
    {
        REQUIRE(std::holds_alternative<core::Event>(result));
        return std::get<core::Note>(std::get<core::Event>(result).data).duration;
    };
    CHECK(lengthOf(setMusicalTiming(event, "Ends step", "5", meter, unit)) == 4 * step + 1);
    CHECK(lengthOf(setMusicalTiming(event, "Ends fine", "-1t", meter, unit)) == 2 * step - 1);
    CHECK(lengthOf(stepMusicalTiming(event, "Ends step", 1, meter, unit)) == 3 * step + 1);
    CHECK(lengthOf(stepMusicalTiming(event, "Ends fine", -1, meter, unit)) == 2 * step);
    const auto moved = setMusicalTiming(event, "Ends step", "5", meter, unit);
    CHECK(std::get<core::Event>(moved).tick == tick);
    const core::Event even{tick, core::Note{60, 90, 2 * step}};
    CHECK(std::holds_alternative<std::string>(
        setMusicalTiming(even, "Ends step", "1", meter, unit))); // where it starts
    CHECK(std::holds_alternative<std::string>(
        setMusicalTiming(event, "Ends step", "0", meter, unit)));

    // A note ending on a bar line ends at the first step of the next bar.
    const core::Event whole{tick, core::Note{60, 90, meter.barStart(4) - tick}};
    Inspection inspection;
    addMusicalTiming(inspection, whole, meter, unit);
    const auto field = std::ranges::find(inspection.fields, "Ends step", &InspectorField::heading);
    REQUIRE(field != inspection.fields.end());
    CHECK(field->value == "1");
}

TEST_CASE("the beat digits for a note follow the widest meter in its track")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    core::MeterMap meter{ppqn};
    const core::Event event{0, core::Note{60, 90, ppqn.quarter()}};
    Inspection plain;
    addMusicalTiming(plain, event, meter, {1, 16});
    CHECK(plain.beatDigits == 1);
    meter.set(meter.barStart(2), core::TimeSignature{12, 8});
    Inspection wide;
    addMusicalTiming(wide, event, meter, {1, 16});
    CHECK(wide.beatDigits == 2);
}

TEST_CASE("the note inspector's exact values line up their colons under B:B:T")
{
    const auto beats = GENERATE(1, 2);
    Inspection inspection;
    inspection.kind = "Note";
    inspection.beatDigits = beats;
    inspection.fields = {{"Position", "17:1:490"},
                         {"Units", "1/8"},
                         {"Length", "0:10:520 (1/8)"},
                         {"Ends", "17:12:050"}};
    const auto region = inspectorRegion(inspection, {0, 0, 900, 90}, 900, CellSize{10, 20}, 8);
    const auto colons = [&](std::u32string_view value) -> std::pair<int, int>
    {
        const auto run = std::ranges::find_if(region.text, [&](const TextRun& r)
                                              { return r.text.find(value) != r.text.npos; });
        REQUIRE(run != region.text.end());
        const auto first = run->text.find(U':');
        const auto second = run->text.find(U':', first + 1);
        return {run->x + static_cast<int>(first) * 10, run->x + static_cast<int>(second) * 10};
    };
    const auto heading = colons(U"B:");
    if (beats == 2)
    {
        CHECK(colons(U"10:520") == heading);
        CHECK(colons(U"12:050") == heading);
        CHECK(colons(U" 1:490") == heading);
    }
    else
    {
        CHECK(colons(U"1:490").second == heading.second);
    }
}
