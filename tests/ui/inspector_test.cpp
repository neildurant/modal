#include <modal/ui/inspector.hpp>

#include <catch2/catch_test_macros.hpp>

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
    s.setMarker(2 * q, "fill");
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
                                                     {"Time", "0:00.500"},
                                                     {"Pitch", "E1 (28)"},
                                                     {"Velocity", "96"},
                                                     {"Length", "0:0:480 (1/8)"},
                                                     {"Ends", "1:2:480"},
                                                     {"Release", "40"}});

    const auto cc = of(list, RowKind::Event, 0);
    CHECK(cc.kind == "CC");
    CHECK(cc.fields[2] == InspectorField{"Controller", "74 Brightness"});
    CHECK(of(list, RowKind::Event, 2).fields[2] == InspectorField{"Controller", "3"});

    const auto sysex = of(list, RowKind::Event, 3);
    CHECK(sysex.kind == "SysEx");
    CHECK(sysex.fields[2] == InspectorField{"Bytes", "3"});
    CHECK(sysex.fields[3] == InspectorField{"SysEx", "F0 7E F7"});
    const auto lyric = of(list, RowKind::Event, 4);
    CHECK(lyric.kind == "Lyric");
    CHECK(lyric.fields[2] == InspectorField{"Text", "a long line of lyrics"});
}

TEST_CASE("on a row that isn't an event, the inspector says what the row is")
{
    const auto s = song();
    const EventList list{s, s.tracks()[0]->id()};

    const auto section = of(list, RowKind::Section, 1);
    CHECK(section.kind == "Section");
    CHECK(section.fields == std::vector<InspectorField>{{"Position", "2:1:000"},
                                                        {"Time", "0:02.000"},
                                                        {"Section", "Verse"},
                                                        {"Bars", "2–2"},
                                                        {"Length", "1 bar, 0:02.000"}});
    const auto tempo = of(list, RowKind::Tempo);
    CHECK(tempo.fields[2] == InspectorField{"Tempo", "120 BPM"});
    CHECK(tempo.fields[3] == InspectorField{"Per quarter", "500 ms"});
    auto odd = song();
    odd.updateTempo([](core::TempoMap& t) { t.set(0, core::Tempo{594'059}); });
    const EventList oddList{odd, odd.tracks()[0]->id()};
    CHECK(of(oddList, RowKind::Tempo).fields[3] == InspectorField{"Per quarter", "594.059 ms"});
    odd.updateTempo([](core::TempoMap& t) { t.set(0, core::Tempo{594'500}); });
    const EventList halfList{odd, odd.tracks()[0]->id()};
    CHECK(of(halfList, RowKind::Tempo).fields[3] == InspectorField{"Per quarter", "594.5 ms"});
    CHECK(of(list, RowKind::Meter).fields[2] == InspectorField{"Meter", "4/4"});
    CHECK(of(list, RowKind::Marker).fields[2] == InspectorField{"Marker", "fill"});
    CHECK(of(list, RowKind::End).fields[2] == InspectorField{"Song", "2 bars, 0:04.000"});
}
