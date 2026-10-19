#include <modal/ui/demo_song.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>

using namespace modal;
using namespace modal::ui;

TEST_CASE("the demo song has sections in several meters, marked, on most tracks")
{
    const auto ppqn = GENERATE(core::kPpqn, core::Ppqn{96});
    CAPTURE(ppqn.value);
    const auto song = demoSong(ppqn);
    REQUIRE(song.tracks().size() == 14);

    const auto& drums = song.meter(song.tracks()[0]->id());
    CHECK(drums.signatureAt(drums.barStart(8)) == core::TimeSignature{4, 4});
    CHECK(drums.signatureAt(drums.barStart(9)) == core::TimeSignature{7, 8});
    CHECK(drums.signatureAt(drums.barStart(27)) == core::TimeSignature{2, 4});
    CHECK(drums.signatureAt(drums.barStart(28)) == core::TimeSignature{3, 4});
    CHECK(drums.signatureAt(drums.barStart(48)) == core::TimeSignature{5, 4});
    CHECK(song.meter(song.tracks()[1]->id()) == drums); // the same changes on every track but two

    // Two tracks keep meters of their own throughout.
    const auto& ostinato = song.meter(song.tracks()[6]->id());
    CHECK(ostinato.changes().size() == 1);
    CHECK(ostinato.signatureAt(0) == core::TimeSignature{3, 4});
    CHECK(song.meter(song.tracks()[7]->id()).signatureAt(0) == core::TimeSignature{5, 8});

    // Markers at each section but the extra bar.
    const auto markers = song.markers();
    REQUIRE(markers.size() == 6);
    CHECK(markers[0].tick == 0);
    CHECK(markers[0].name == "Intro");
    CHECK(markers[1].tick == drums.barStart(9));
    CHECK(markers[1].name == "Verse");
    CHECK(markers[3].tick == drums.barStart(28));
    CHECK(markers[3].name == "Bridge");
    CHECK(markers[5].tick == drums.barStart(48));

    // Every note ends by the end of the last bar, and the empty tracks are empty.
    const auto end = drums.barStart(54);
    for (const auto& track : song.tracks())
    {
        for (const auto& event : song.events(track->id()).all())
        {
            REQUIRE(event.isNote());
            CHECK(event.tick + std::get<core::Note>(event.data).duration <= end);
        }
    }
    CHECK(song.events(song.tracks()[0]->id()).all().back().tick >= drums.barStart(53));
    for (std::size_t i = 10; i < 14; ++i)
    {
        CHECK(song.events(song.tracks()[i]->id()).empty());
        CHECK(song.meter(song.tracks()[i]->id()) == drums);
    }
}

TEST_CASE("some tracks of the demo song leave bars empty")
{
    const auto song = demoSong();
    const auto piano = song.tracks()[2]->id();
    const auto& meter = song.meter(piano);
    int empty = 0;
    for (std::int64_t bar = 1; bar <= 26; ++bar) // the piano plays all through these
    {
        const auto events =
            song.events(piano).startingIn(meter.barStart(bar), meter.barStart(bar + 1));
        empty += events.empty() ? 1 : 0;
    }
    CHECK(empty > 0);
    CHECK(empty < 13);
}
