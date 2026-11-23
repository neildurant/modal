// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/song.hpp>
#include <modal/ui/clipboard.hpp>
#include <modal/ui/view_parts.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <string>

using namespace modal;
using namespace modal::ui;

namespace
{

// Two tracks over four bars of 4/4: a bass with notes, the mod wheel and a ramp in bar 1, and
// a lead in 3/4 of its own, its notes in each of its bars.
core::Song song(core::Ppqn ppqn)
{
    const auto q = ppqn.quarter();
    core::Song song{ppqn};
    song.resizeSection(0, -4);
    const auto bass = song.addTrack("Bass");
    const auto lead = song.addTrack("Lead");
    song.updateEvents(bass,
                      [&](core::EventList& events)
                      {
                          events.insert({0, core::Note{40, 90, q}});
                          events.insert({q, core::ControlChange{1, 20}});
                          events.insert({4 * q, core::Note{43, 80, 2 * q}});
                      });
    const core::Lane modulation{.kind = core::Lane::Kind::Controller, .controller = 1};
    REQUIRE(
        song.addRamp(bass, {.lane = modulation, .start = 2 * q, .length = q, .from = 0, .to = 100})
            .done());
    REQUIRE(song.setMeter(lead, 0, song.end(), core::TimeSignature{3, 4}));
    song.updateEvents(lead,
                      [&](core::EventList& events)
                      {
                          for (int bar = 0; bar < 5; ++bar)
                          {
                              events.insert({3 * q * bar, core::Note{72, 100, q / 2}});
                          }
                      });
    return song;
}

} // namespace

TEST_CASE("A register of events goes onto the clipboard as song text and comes back the same")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    const auto s = song(ppqn);
    auto reg = takeEvents(s, 0, 1, 0, s.meter().barStart(3));
    reg.bars = true;
    reg.pitch = 40;
    REQUIRE(reg.meters.size() == 2);

    const auto text = clipboardText(reg, ppqn);
    REQUIRE(text);
    CHECK(text->starts_with("modal-clip 1 events ppqn=" + std::to_string(ppqn.value)));
    CHECK(text->find("\nmodal-song 1\n") != std::string::npos);
    CHECK(isClipboardCopy(*text));

    const auto read = readClipboard(*text, ppqn);
    INFO(read.error);
    REQUIRE(read.reg);
    CHECK(read.reg->tracks == reg.tracks);
    CHECK(read.reg->ramps == reg.ramps);
    CHECK(read.reg->meters == reg.meters);
    CHECK(read.reg->span == reg.span);
    CHECK(read.reg->bars);
    CHECK(read.reg->pitch == reg.pitch);
    CHECK(clipboardText(*read.reg, ppqn) == text);
}

TEST_CASE("A copy made at another resolution is rescaled as songs are")
{
    const auto s = song(core::kPpqn);
    const auto reg = takeEvents(s, 0, 0, 0, s.meter().barStart(2));
    const auto read = readClipboard(*clipboardText(reg, core::kPpqn), core::Ppqn{96});
    REQUIRE(read.reg);
    CHECK(read.reg->span == 4 * 96);
    REQUIRE(read.reg->tracks.size() == 1);
    REQUIRE(read.reg->tracks[0].size() == reg.tracks[0].size());
    CHECK(read.reg->tracks[0][1].tick == 96);
    REQUIRE(read.reg->ramps[0].size() == 1);
    CHECK(read.reg->ramps[0][0].start == 2 * 96);
}

TEST_CASE("Sections go onto the clipboard with the tracks they were taken from")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    auto s = song(ppqn);
    REQUIRE(s.splitSection(s.meter().barStart(3), "Chorus"));
    s.setLabel(s.meter().barStart(4), "fill");
    EventRegister reg;
    reg.sections.assign(s.sections().begin(), s.sections().end());
    takeSectionsFrom(reg, s);

    const auto text = clipboardText(reg, ppqn);
    REQUIRE(text);
    CHECK(text->starts_with("modal-clip 1 sections\n"));
    const auto read = readClipboard(*text, ppqn);
    INFO(read.error);
    REQUIRE(read.reg);
    REQUIRE(read.reg->sections.size() == 2);
    CHECK(read.reg->sections[1]->name() == "Chorus");
    CHECK(read.reg->sections[1]->labels().size() == 1);
    REQUIRE(read.reg->fromTracks.size() == 2);
    CHECK(read.reg->fromTracks[1]->name == "Lead");
    CHECK(clipboardText(*read.reg, ppqn) == text);
}

TEST_CASE("Whole tracks go onto the clipboard with their settings and every part")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    auto s = song(ppqn);
    REQUIRE(s.splitSection(s.meter().barStart(3), "Chorus"));
    const auto lead = s.tracks()[1]->id();
    s.updateTrack(lead, [](core::Track& track) { track.channel = 5; });
    EventRegister reg;
    for (const auto& track : s.tracks())
    {
        reg.wholeTracks.push_back(*s.copyTrack(track->id()));
    }

    const auto text = clipboardText(reg, ppqn);
    REQUIRE(text);
    CHECK(text->starts_with("modal-clip 1 tracks\n"));
    const auto read = readClipboard(*text, ppqn);
    INFO(read.error);
    REQUIRE(read.reg);
    REQUIRE(read.reg->wholeTracks.size() == 2);
    for (std::size_t i = 0; i < 2; ++i)
    {
        auto expected = reg.wholeTracks[i];
        expected.settings = expected.settings.withId(read.reg->wholeTracks[i].settings.id());
        CHECK(read.reg->wholeTracks[i] == expected);
    }
    CHECK(read.reg->wholeTracks[1].settings.channel == 5);
}

TEST_CASE("Text that isn't a copy from Modal is told apart, and a broken one says why")
{
    CHECK_FALSE(isClipboardCopy("hello"));
    CHECK_FALSE(isClipboardCopy("modal-clip 10 events"));
    CHECK_FALSE(isClipboardCopy("modal-song 1\n"));
    CHECK(readClipboard("hello", core::kPpqn).error == "it isn't a copy from Modal");
    CHECK(readClipboard("modal-clip 1 nonsense\nmodal-song 1\n", core::kPpqn).error ==
          "its first line doesn't say what kind of copy it is");
    const auto broken =
        readClipboard("modal-clip 1 events ppqn=960 span=0\nnot a song\n", core::kPpqn);
    CHECK_FALSE(broken.reg);
    CHECK_FALSE(broken.error.empty());
    CHECK_FALSE(clipboardText(EventRegister{}, core::kPpqn));
}
