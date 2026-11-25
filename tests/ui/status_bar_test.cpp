// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/status_bar.hpp>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

using namespace modal::ui;

namespace
{
constexpr CellSize kCell{10, 20};

StatusBarState stateWithTransport()
{
    StatusBarState state;
    state.transport = StatusBarState::Transport{StatusBarState::TransportState::Stopped,
                                                false,
                                                "1:1:000",
                                                "0:00.000",
                                                "4/4",
                                                "120 BPM",
                                                "Introduction",
                                                "Intro…"};
    return state;
}

Region render(const StatusBarState& state, int width = 2200, CellSize cell = kCell)
{
    return layoutBar(statusBar(state, width, cell), width, cell, BarSpacing::forCell(cell));
}

bool contains(const Region& region, std::u32string_view text)
{
    return std::ranges::any_of(region.text, [&](const auto& run)
                               { return run.text.find(text) != std::u32string::npos; });
}

TextRun field(const Region& region, std::u32string_view text)
{
    const auto found = std::ranges::find_if(
        region.text, [&](const auto& run) { return run.text.find(text) != std::u32string::npos; });
    REQUIRE(found != region.text.end());
    return *found;
}
} // namespace

TEST_CASE("status presentation keeps mode panels equally wide without a transport")
{
    auto state = StatusBarState{};
    using Mode = StatusBarState::Mode;
    const auto [mode, text] =
        GENERATE(std::pair{Mode::Normal, U" NORMAL "}, std::pair{Mode::Visual, U" VISUAL "},
                 std::pair{Mode::Edit, U" EDIT "}, std::pair{Mode::Move, U" MOVE "},
                 std::pair{Mode::Freeze, U" FREEZE "});
    state.mode = mode;
    const auto region = render(state);
    CHECK(field(region, text).style == Style{Role::ModeText, Role::Mode, kBold | kInset});
    CHECK_FALSE(contains(region, U"STOP"));
    CHECK_FALSE(contains(region, U"BPM"));
    CHECK(contains(region, U"ARM"));
    CHECK(contains(region, U" + "));
    const auto bar = statusBar(state, 2200, kCell);
    REQUIRE(bar.left.size() == 2);
    CHECK(decodeUtf8(bar.left[0].text + bar.left[1].text).size() == 9);
}

TEST_CASE("status presentation gives every transport phase its established text and colour")
{
    using State = StatusBarState::TransportState;
    struct Case
    {
        State state;
        std::u32string_view text;
        Role colour;
    };
    const auto item = GENERATE(Case{State::Stopped, U"■ STOP", Role::Indicator},
                               Case{State::Playing, U"▶ PLAY", Role::Accent},
                               Case{State::CountIn, U"● COUNT", Role::IndicatorArm},
                               Case{State::PreRoll, U"● PRE", Role::IndicatorArm},
                               Case{State::Recording, U"● REC", Role::IndicatorArm},
                               Case{State::Overdubbing, U"● ODUB", Role::IndicatorArm});
    auto state = stateWithTransport();
    const auto stopped = render(state);
    state.transport->state = item.state;
    state.transport->positionActive = GENERATE(false, true);
    const auto region = render(state);
    CHECK(field(region, item.text).style.background == item.colour);
    CHECK(field(region, U"1:1:000").style.foreground ==
          (state.transport->positionActive ? Role::Accent : Role::BarText));
    CHECK(field(region, U" 4/4 ").x == field(stopped, U" 4/4 ").x);
    for (const auto& run : region.text)
        if (run.style.background != Role::Bar)
            CHECK((run.style.attributes & kInset) != 0);
}

TEST_CASE("status presentation distinguishes armed recording settings from transport activity")
{
    auto state = stateWithTransport();
    state.overdub = true;
    state.countIn.mode = modal::core::CountInSettings::Mode::PreRoll;
    state.countIn.preRollBeats = 3;
    const auto before = render(state);
    CHECK(field(before, U"OVERDUB").style.background == Role::Indicator);
    // Nothing armed, the lead-in is playback's, none here (decided 25 November 2026).
    CHECK(field(before, U"NO COUNT").style.background == Role::Indicator);
    state.playbackLeadIn.mode = modal::core::CountInSettings::Mode::CountIn;
    state.playbackLeadIn.bars = 2;
    CHECK(field(render(state), U"COUNT 2 BARS").style.background == Role::IndicatorCount);
    state.armed = 2;
    const auto after = render(state);
    CHECK(field(after, U"ARM 2").style.background == Role::IndicatorArm);
    CHECK(field(after, U"OVERDUB").style.background == Role::IndicatorRecordMode);
    CHECK(field(after, U"PRE 3 BEATS").style.background == Role::IndicatorCount);
    CHECK(contains(after, U"■ STOP"));
    for (const auto word : {U"CLICK", U"THRU", U"AUDITION", U"REWIND", U" IN "})
        CHECK(field(before, word).x == field(after, word).x);
    // The groups apart: what recording will do, then how the playhead will move, a dim line
    // between them.
    CHECK(field(after, U"PUNCH").x < field(after, U"OVERDUB").x);
    CHECK(field(after, U"OVERDUB").x < field(after, U"│").x);
    CHECK(field(after, U"│").x < field(after, U"LOOP").x);
    CHECK(field(after, U"│").style.foreground == Role::Dim);
}

TEST_CASE("status presentation formats count-in and pre-roll units and the disabled count")
{
    using Mode = modal::core::CountInSettings::Mode;
    struct Case
    {
        Mode mode;
        int bars;
        int beats;
        std::u32string_view text;
    };
    const auto item = GENERATE(
        Case{Mode::CountIn, 1, 0, U"COUNT 1 BAR"}, Case{Mode::CountIn, 2, 0, U"COUNT 2 BARS"},
        Case{Mode::CountIn, 1, 1, U"COUNT 1 BEAT"}, Case{Mode::CountIn, 1, 3, U"COUNT 3 BEATS"},
        Case{Mode::PreRoll, 2, 0, U"PRE 2 BARS"}, Case{Mode::PreRoll, 1, 1, U"PRE 1 BEAT"},
        Case{Mode::None, 1, 0, U"NO COUNT"});
    StatusBarState state;
    state.armed = 1;
    state.countIn = {item.mode, item.bars, item.beats, item.bars, item.beats};
    CHECK(field(render(state), item.text).style.background ==
          (item.mode == Mode::None ? Role::Indicator : Role::IndicatorCount));
}

TEST_CASE("status presentation keeps compact recording labels when settings give way")
{
    using Mode = modal::core::CountInSettings::Mode;
    const auto [mode, text] =
        GENERATE(std::pair{Mode::CountIn, U"CNT 3B"}, std::pair{Mode::PreRoll, U"PRE 3B"},
                 std::pair{Mode::None, U"NO CNT"});
    auto state = stateWithTransport();
    state.armed = 2;
    state.overdub = true;
    state.countIn.mode = mode;
    state.countIn.beats = state.countIn.preRollBeats = 3;
    const auto region = render(state, 800);
    CHECK(contains(region, U"ARM 2"));
    CHECK(contains(region, U"ODUB"));
    CHECK(contains(region, text));
    CHECK(contains(region, U"CLK"));
    CHECK_FALSE(contains(region, U"OVERDUB"));
}

TEST_CASE("status presentation warns about click routing only when the click is enabled")
{
    using When = modal::core::MetronomeSettings::When;
    using Mode = modal::core::CountInSettings::Mode;
    struct Case
    {
        When when;
        Mode count;
        std::size_t armed;
        bool enabled;
    };
    const auto item = GENERATE(
        Case{When::Off, Mode::None, 1, false}, Case{When::Recording, Mode::None, 0, false},
        Case{When::Recording, Mode::None, 1, true}, Case{When::Always, Mode::None, 0, true},
        Case{When::Off, Mode::CountIn, 1, true}, Case{When::Off, Mode::PreRoll, 1, false});
    StatusBarState state;
    state.clickWhen = item.when;
    state.countIn.mode = item.count;
    state.armed = item.armed;
    state.clickAvailable = GENERATE(false, true);
    CHECK(field(render(state), U"CLICK").style.background == (!item.enabled ? Role::Indicator
                                                              : state.clickAvailable
                                                                  ? Role::IndicatorClick
                                                                  : Role::Warning));
}

TEST_CASE("status presentation shows setting toggles and input loss without owning timers")
{
    StatusBarState state;
    state.thru = state.audition = state.rewind = false;
    const auto off = render(state);
    for (const auto text : {U"THRU", U"AUDITION", U"REWIND", U"Q BYPASS", U" IN "})
        CHECK(field(off, text).style.background == Role::Indicator);
    state.thru = state.audition = state.rewind = true;
    state.quantizeBypassed = true;
    state.inputActive = true;
    const auto on = render(state);
    CHECK(field(on, U"THRU").style.background == Role::IndicatorThru);
    CHECK(field(on, U"AUDITION").style.background == Role::IndicatorAudition);
    CHECK(field(on, U"REWIND").style.background == Role::IndicatorRewind);
    CHECK(field(on, U"Q BYPASS").style.background == Role::Warning);
    CHECK(field(on, U" IN ").style.background == Role::IndicatorInput);
    CHECK_FALSE(contains(on, U"CC64")); // the light alone: the last message is the Ports view's
    state.inputLost = true;
    CHECK(field(render(state), U" IN ").style.background == Role::Warning);
    state.inputActive = false;
    CHECK(field(render(state), U" IN ").style.background == Role::Warning);
}

TEST_CASE("status presentation keeps + in its place, lit only with unsaved changes, no file")
{
    StatusBarState state;
    const auto saved = render(state);
    state.modified = true;
    const auto modified = render(state);
    CHECK(field(saved, U" + ").style.background == Role::Indicator);
    CHECK(field(modified, U" + ").style.background == Role::Warning);
    CHECK(field(saved, U" + ").x == field(modified, U" + ").x);
    CHECK(field(saved, U" IN ").x == field(modified, U" IN ").x);
    CHECK_FALSE(contains(saved, U"No file"));
}

TEST_CASE("status presentation preserves the order of width compromises at pixel boundaries")
{
    auto state = stateWithTransport();
    const std::array<std::u32string_view, 10> parts{U" + ",   U"Introduction", U"REPLACE", U"THR",
                                                    U"Intro", U" IN ",         U" 4/4 ",   U" BPM",
                                                    U"PUNCH", U"LOOP"};
    std::array<bool, 10> disappeared{};
    for (int width = 2200; width >= 650; --width)
    {
        const auto region = render(state, width);
        std::array<bool, 10> present{};
        for (std::size_t i = 0; i < parts.size(); ++i)
        {
            present[i] = contains(region, parts[i]);
            CHECK_FALSE((disappeared[i] && present[i]));
            disappeared[i] = disappeared[i] || !present[i];
        }
        const auto first = std::ranges::find(present, true);
        CHECK(std::all_of(first, present.end(), std::identity{}));
        for (const auto word : {U" NORMAL ", U"■ STOP", U"1:1:000", U"ARM"})
            CHECK(contains(region, word));
    }
    CHECK(std::ranges::all_of(disappeared, std::identity{}));
    for (const auto width : {0, 1, 5, 20, 100, 300})
    {
        const auto region = render(state, width);
        for (const auto& run : region.text)
        {
            CHECK(run.x >= 0);
            CHECK(run.x + static_cast<int>(run.text.size()) * kCell.width <= width);
        }
    }
}
