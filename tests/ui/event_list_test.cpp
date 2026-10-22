// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/event_list.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

// A song of two sections, a bass track with a few events and a lead in a meter of its own.
struct Two
{
    Two()
    {
        bass = song.addTrack("Bass");
        lead = song.addTrack("Lead");
        song.updateTempo([&](core::TempoMap& tempo) { tempo.set(4 * q, core::Tempo{400'000}); });
        song.setMarker(2 * q, "fill");
        song.updateEvents(bass,
                          [&](core::EventList& events)
                          {
                              events.insert({0, core::Note{28, 96, q / 2}});
                              events.insert({0, core::ProgramChange{5}});
                              events.insert({q + 240, core::ControlChange{74, 64}});
                              events.insert({4 * q, core::Note{29, 90, q / 2 + 12, 40}});
                          });
        song.updateMeter(lead, [](core::MeterMap& meter)
                         { meter = core::MeterMap{meter.ppqn(), {7, 8}}; });
        song.splitSection(4 * q, "Verse");
        song.fitToContent();
    }

    static constexpr core::Tick q = 960;
    core::Song song{core::kPpqn};
    core::TrackId bass{0};
    core::TrackId lead{0};
};

std::vector<std::string> column(const EventList& list, Column c)
{
    std::vector<std::string> cells;
    for (std::size_t i = 0; i < list.rows().size(); ++i)
    {
        cells.push_back(list.text(i)[c]);
    }
    return cells;
}

std::vector<bool> dim(const EventList& list)
{
    std::vector<bool> result;
    for (const auto& row : list.rows())
    {
        result.push_back(row.dim);
    }
    return result;
}

} // namespace

TEST_CASE("a track's list has its events, the conductor's rows dimmed, and the end")
{
    const Two two;
    const EventList list{two.song, two.bass};
    CHECK(column(list, Column::Event) ==
          std::vector<std::string>{"section", "meter", "tempo", "program", "note", "cc", "marker",
                                   "section", "tempo", "note", "end"});
    CHECK(column(list, Column::Position) ==
          std::vector<std::string>{"1:1:000", "1:1:000", "1:1:000", "1:1:000", "1:1:000", "1:2:240",
                                   "1:3:000", "2:1:000", "2:1:000", "2:1:000", "3:1:000"});
    CHECK(dim(list) ==
          std::vector<bool>{true, true, true, false, false, false, true, true, true, false, false});

    // The fields: a note's pitch, velocity, length and release.
    const auto note = list.text(9);
    CHECK(note[Column::Data] == "F1 (29)");
    CHECK(note[Column::Value] == "90");
    CHECK(note[Column::Length] == "0:0:492 (≈1/8)");
    CHECK(note[Column::Release] == "40");
    CHECK(note.fields == std::vector<Column>{Column::Position, Column::Data, Column::Value,
                                             Column::Length, Column::Release});
    CHECK(list.text(5).fields ==
          std::vector<Column>{Column::Position, Column::Data, Column::Value});
    CHECK(list.text(10).fields == std::vector<Column>{Column::Position});
    CHECK(list.text(0)[Column::Data] == "Section 1");
    CHECK(list.text(2)[Column::Data] == "120");
    CHECK(list.text(8)[Column::Data] == "150");
}

TEST_CASE("the order at a tick: section, meter, tempo, markers, then events as sent")
{
    // At 1:1:000 the program comes before the note, as sent.
    const Two two;
    const EventList list{two.song, two.bass};
    CHECK(list.rows()[3].kind == RowKind::Event);
    CHECK(list.text(3)[Column::Event] == "program");
    CHECK(list.rowAt(4 * Two::q) == 7); // the verse's section row
    CHECK(list.rowAt(100 * Two::q) == list.rows().size() - 1);
}

TEST_CASE("a track with its own meter shows it undimmed, in its own bars")
{
    const Two two;
    const EventList list{two.song, two.lead};
    std::vector<std::string> meters;
    for (std::size_t i = 0; i < list.rows().size(); ++i)
    {
        if (list.rows()[i].kind == RowKind::Meter)
        {
            CHECK_FALSE(list.rows()[i].dim);
            meters.push_back(list.text(i)[Column::Position] + " " + list.text(i)[Column::Data]);
        }
    }
    // A 7/8 bar is three and a half quarters, so the intro's four quarters are a bar and a
    // 1/8 remainder, and the verse starts a new 7/8 bar.
    CHECK(meters == std::vector<std::string>{"1:1:000 7/8", "2:1:000 1/8", "3:1:000 7/8"});
}

TEST_CASE("the conductor's list has its own rows, undimmed, and no events")
{
    const Two two;
    const EventList list{two.song, std::nullopt};
    CHECK(column(list, Column::Event) == std::vector<std::string>{"section", "meter", "tempo",
                                                                  "marker", "section", "tempo",
                                                                  "end"});
    for (const auto& row : list.rows())
    {
        CHECK_FALSE(row.dim);
    }
}
