// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/view_state.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace modal;
using namespace modal::ui;

TEST_CASE("each view's state comes back from an undo step as it went in, and only for it")
{
    const core::TrackId track{3};
    const ArrangeState arrange{.topTrack = 2, .leftTick = 960, .zoom = 3, .strip = 1};
    const auto a = placeWith(track, 480, arrange);
    CHECK(arrangeStateOf(a) == arrange);
    CHECK_FALSE(pianoRollStateOf(a));
    CHECK_FALSE(eventListStateOf(a));
    CHECK(arrangeStateOf(placeWith(track, 0, ArrangeState{}))->strip == std::nullopt);

    const PianoRollState roll{
        .pitch = 64, .topPitch = 80, .pageSize = 2, .viewStart = 1920, .viewLength = 3840};
    const auto p = placeWith(track, 0, roll);
    CHECK(pianoRollStateOf(p) == roll);
    CHECK_FALSE(arrangeStateOf(p));
    CHECK(pianoRollStateOf(placeWith(track, 0, PianoRollState{}))->viewLength == std::nullopt);

    const EventListState list{.top = 12, .down = 2};
    const auto e = placeWith(std::nullopt, 0, list);
    CHECK(eventListStateOf(e) == list);
    CHECK_FALSE(pianoRollStateOf(e));

    // A place with no view's state, as the core makes, is none of theirs.
    CHECK_FALSE(arrangeStateOf(core::EditPlace{track, 0}));
}
