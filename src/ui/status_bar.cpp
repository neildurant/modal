// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/event_text.hpp>
#include <modal/ui/status_bar.hpp>
#include <modal/ui/view_parts.hpp>

#include <algorithm>
#include <array>

namespace modal::ui
{
namespace
{
constexpr Style kBarText{Role::BarText, Role::Bar, kPlain};
}

Bar statusBar(const StatusBarState& state, int width, CellSize cell)
{
    Bar bottom;
    const auto mode = [&]() -> std::string
    {
        using enum StatusBarState::Mode;
        switch (state.mode)
        {
        case Freeze:
            return " FREEZE ";
        case Move:
            return " MOVE ";
        case Edit:
            return " EDIT ";
        case Visual:
            return " VISUAL ";
        case Normal:
            return " NORMAL ";
        }
        return " NORMAL ";
    }();
    bottom.left = {{mode, {Role::ModeText, Role::Mode, kBold}}};
    // As wide as the widest mode, so the transport after it stays put.
    bottom.left.push_back(
        {std::string(std::string_view{" NORMAL "}.size() - mode.size() + 1, ' '), kBarText});
    // Where the window's too narrow for everything, what's least needed gives way first, in
    // steps (decided 12 November 2026): +, first as the file's name was (decided 25 November
    // 2026, the title bar showing it too, so the smallest usual window keeps what it kept); the
    // recording and playback groups joined again, their line going (decided 25 November 2026,
    // for the same reason); the section cut short, short words for the indicators, the settings,
    // the section, the input light, the meter and the tempo; and last the punch range's indicator
    // (decided 24 November 2026, so the narrowest window keeps what it kept before it). The mode,
    // the transport and the rest of the recording group stay.
    enum class Stage
    {
        Full,
        HideModified,
        JoinGroups,
        ShortSection,
        ShortIndicators,
        HideSettings,
        HideSection,
        HideInput,
        HideMeter,
        HideTempo,
        HidePunch,
    };
    constexpr std::array kStages{Stage::Full,         Stage::HideModified,    Stage::JoinGroups,
                                 Stage::ShortSection, Stage::ShortIndicators, Stage::HideSettings,
                                 Stage::HideSection,  Stage::HideInput,       Stage::HideMeter,
                                 Stage::HideTempo,    Stage::HidePunch};
    const auto transport = [&](Stage stage)
    {
        std::vector<Segment> segments;
        if (!state.transport)
            return segments;
        // The transport, always shown: stopped, where Space plays from; playing, the playhead.
        // Its parts are as wide as the song needs, so what follows doesn't move as it counts.
        // Each part on a panel of its own (decided 12 November 2026): the transport, then the
        // meter, the tempo and the section there.
        const auto& place = *state.transport;
        const Style panel{Role::BarText, Role::Indicator, kPlain};
        const Style style =
            place.positionActive ? Style{Role::Accent, Role::Indicator, kBold} : panel;
        const auto gap = [&] { segments.push_back({" ", kBarText}); };
        // What the transport's doing, first, as a block always there and as wide whatever it
        // says, so nothing after it moves (decided November 2026, the block 12 November 2026):
        // stopped, dim; playing, lit; recording, and how, lit as arming is.
        using TransportState = StatusBarState::TransportState;
        std::string word = "■ STOP";
        Style stateStyle{Role::IndicatorText, Role::Indicator, kPlain};
        if (place.state == TransportState::Playing || place.state == TransportState::Looping ||
            place.state == TransportState::PlaybackCountIn ||
            place.state == TransportState::PlaybackPreRoll)
        {
            word = place.state == TransportState::Looping           ? "↩ LOOP"
                   : place.state == TransportState::PlaybackCountIn ? "▶ COUNT"
                   : place.state == TransportState::PlaybackPreRoll ? "▶ PRE"
                                                                    : "▶ PLAY";
            stateStyle = {Role::IndicatorLitText, Role::Accent, kBold};
        }
        else if (place.state != TransportState::Stopped)
        {
            word = place.state == TransportState::CountIn       ? "● COUNT"
                   : place.state == TransportState::PreRoll     ? "● PRE"
                   : place.state == TransportState::Overdubbing ? "● ODUB"
                                                                : "● REC";
            stateStyle = {Role::IndicatorLitText, Role::IndicatorArm, kBold};
        }
        segments.push_back({" " + padText(word, 7, false) + " ", stateStyle});
        gap();
        segments.push_back({" " + place.position + "  " + place.time + " ", style});
        if (stage < Stage::HideMeter)
        {
            gap();
            segments.push_back({" " + place.meter + " ", panel});
        }
        if (stage < Stage::HideTempo)
        {
            gap();
            segments.push_back({" " + place.tempo + " ", panel});
        }
        if (stage < Stage::HideSection)
        {
            gap();
            segments.push_back(
                {" " + (stage < Stage::ShortSection ? place.section : place.shortSection) + " ",
                 panel});
        }
        return segments;
    };
    // The indicators (decided 12 November 2026): each always in its place, as wide whatever it
    // says, dim when off and lit in its own colour when on, or the warning colour when something's
    // wrong; in groups, recording, then the settings, then the input, then unsaved changes.
    const auto armed = state.armed;
    const auto indicators = [&](Stage stage)
    {
        std::vector<Segment> segments;
        const bool brief = stage >= Stage::ShortIndicators;
        enum class Lit : std::uint8_t
        {
            Off,
            On,
            Warning,
        };
        const auto indicator = [&](std::string_view full, std::string_view shortText,
                                   std::size_t width, std::size_t shortWidth, Lit lit, Role colour)
        {
            const Style style =
                lit == Lit::Off       ? Style{Role::IndicatorText, Role::Indicator, kPlain}
                : lit == Lit::Warning ? Style{Role::IndicatorLitText, Role::Warning, kBold}
                                      : Style{Role::IndicatorLitText, colour, kBold};
            segments.push_back({" " +
                                    padText(std::string{brief ? shortText : full},
                                            brief ? shortWidth : width, false) +
                                    " ",
                                style});
            segments.push_back({" ", kBarText});
        };
        // One cell between indicators in a group, and three between groups (decided 12 November
        // 2026, after use), a dim line in the middle one so the groups read as groups (decided
        // 25 November 2026, after trying a wider gap and a dot), taking no more room.
        const auto gap = [&]
        {
            segments.push_back({"│", {Role::Dim, Role::Bar, kPlain}});
            segments.push_back({" ", kBarText});
        };
        const auto lit = [](bool on) { return on ? Lit::On : Lit::Off; };

        const auto arm = armed > 0 ? "ARM " + std::to_string(armed) : std::string{"ARM"};
        indicator(arm, arm, 5, 5, lit(armed > 0), Role::IndicatorArm);
        // In groups (decided 25 November 2026, after use): what recording will do, the tracks
        // armed, the punch range and replace or overdub; then how the playhead will move, the
        // loop, the lead-in and the click. A punch range armed (decided 24 November 2026) is
        // shown stopped too, as R records it.
        if (stage < Stage::HidePunch)
            indicator("PUNCH", "PUNCH", 5, 5, lit(state.punch), Role::IndicatorArm);
        const bool overdub = state.overdub;
        indicator(overdub ? "OVERDUB" : "REPLACE", overdub ? "ODUB" : "REPL", 7, 4, lit(armed > 0),
                  Role::IndicatorRecordMode);
        if (stage < Stage::JoinGroups)
            gap();
        // A loop armed and on (decided 25 November 2026), as Space plays it.
        if (stage < Stage::HidePunch)
            indicator("LOOP", "LOOP", 4, 4, lit(state.loop), Role::Accent);
        {
            // The lead-in before the next start (decided 25 November 2026): recording's while
            // tracks are armed, as R would count in, and otherwise playback's, as Space would.
            using enum core::CountInSettings::Mode;
            const auto& countIn = armed > 0 ? state.countIn : state.playbackLeadIn;
            const auto count = [](int n, std::string_view one)
            { return std::to_string(n) + " " + std::string{one} + (n == 1 ? "" : "S"); };
            const bool beats =
                countIn.mode == CountIn ? countIn.beats > 0 : countIn.preRollBeats > 0;
            const int n = countIn.mode == CountIn
                              ? (beats ? countIn.beats : countIn.bars)
                              : (beats ? countIn.preRollBeats : countIn.preRollBars);
            const auto word = std::string{countIn.mode == CountIn ? "COUNT " : "PRE "};
            const auto full = countIn.mode == None ? std::string{"NO COUNT"}
                                                   : word + count(n, beats ? "BEAT" : "BAR");
            const auto shortText = countIn.mode == None
                                       ? std::string{"NO CNT"}
                                       : (countIn.mode == CountIn ? "CNT " : "PRE ") +
                                             std::to_string(n) + (beats ? "B" : "");
            indicator(full, shortText, 13, 6, lit(countIn.mode != None), Role::IndicatorCount);
        }
        {
            // Lit when the click will sound: always, or with tracks armed while recording or for
            // a count-in, which clicks whatever the metronome's set to; the warning colour if it
            // has nowhere to go.
            using When = core::MetronomeSettings::When;
            const auto when = state.clickWhen;
            const bool clicks =
                when == When::Always ||
                (armed > 0 && (when == When::Recording ||
                               state.countIn.mode == core::CountInSettings::Mode::CountIn));
            indicator("CLICK", "CLK", 5, 3,
                      !clicks                ? Lit::Off
                      : state.clickAvailable ? Lit::On
                                             : Lit::Warning,
                      Role::IndicatorClick);
        }
        if (stage < Stage::HideSettings)
        {
            gap();
            indicator("THRU", "THR", 4, 3, lit(state.thru), Role::IndicatorThru);
            indicator("AUDITION", "AUD", 8, 3, lit(state.audition), Role::IndicatorAudition);
            indicator("REWIND", "RWD", 6, 3, lit(state.rewind), Role::IndicatorRewind);
            // Quantise bypassed is never simply on: dim, or the warning colour as a reminder
            // that what's heard isn't what's set.
            indicator("Q BYPASS", "QBY", 8, 3, state.quantizeBypassed ? Lit::Warning : Lit::Off,
                      Role::Warning);
        }
        if (stage < Stage::HideInput)
        {
            // The input light alone (decided 25 November 2026): each input's last message is in
            // the Ports view.
            gap();
            segments.push_back(
                {" IN ",
                 // In the warning colour from input lost while recording until it stops.
                 state.inputLost     ? Style{Role::IndicatorLitText, Role::Warning, kBold}
                 : state.inputActive ? Style{Role::IndicatorLitText, Role::IndicatorInput, kBold}
                                     : Style{Role::IndicatorText, Role::Indicator, kPlain}});
            segments.push_back({" ", kBarText});
        }
        if (stage < Stage::HideModified)
        {
            // + for changes not saved, as Vim's [+], the chip doing the brackets' work; dim when
            // there are none, so it's always in its place (decided 25 November 2026, when the
            // file's name left the line for the title bar and Ctrl-g, and + after use).
            gap();
            indicator("+", "+", 1, 1, state.modified ? Lit::Warning : Lit::Off, Role::Warning);
        }
        // No space after the last.
        if (!segments.empty() && segments.back().text == " ")
            segments.pop_back();
        return segments;
    };
    const auto length = [](const std::vector<Segment>& segments)
    {
        std::size_t n = 0;
        for (const auto& segment : segments)
            n += decodeUtf8(segment.text).size();
        return n;
    };
    const auto spacing = BarSpacing::forCell(cell);
    const auto columns = static_cast<std::size_t>(
        std::max((width - 2 * spacing.margin) / std::max(cell.width, 1), 1));
    const auto gapColumns =
        static_cast<std::size_t>((spacing.gap + cell.width - 1) / std::max(cell.width, 1));
    std::size_t stage = 0;
    while (stage + 1 < kStages.size() && length(bottom.left) + length(transport(kStages[stage])) +
                                                 gapColumns + length(indicators(kStages[stage])) >
                                             columns)
        ++stage;
    const auto left = transport(kStages[stage]);
    const auto right = indicators(kStages[stage]);
    bottom.left.insert(bottom.left.end(), left.begin(), left.end());
    bottom.right.insert(bottom.right.end(), right.begin(), right.end());
    // Every field a chip on the bar, a little short of its top and bottom, so the bar reads as
    // what they stand on however closely they're packed (decided 12 November 2026, after use).
    for (auto* side : {&bottom.left, &bottom.right})
        for (auto& segment : *side)
            if (segment.style.background != bottom.background)
                segment.style.attributes |= kInset;
    return bottom;
}

} // namespace modal::ui
