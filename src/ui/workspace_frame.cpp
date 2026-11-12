// SPDX-License-Identifier: GPL-3.0-or-later

// Drawing the workspace: its frame of regions, the status line, messages and listings
// (split from workspace.cpp, November 2026, after a code review).

#include "workspace_commands.hpp"

#include <modal/core/pitch.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/completion.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/event_edit.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/key_notation.hpp>
#include <modal/ui/palette.hpp>
#include <modal/ui/settings.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <map>
#include <span>
#include <tuple>
#include <utility>

namespace modal::ui
{

namespace
{

constexpr Style kBarText{Role::BarText, Role::Bar, kPlain};

} // namespace

Region Workspace::listingRegion() const
{
    // Above the status line, as the hint menu is: each name right-aligned in the keys' colour,
    // a colon in one column, and what it is.
    const int margin = BarSpacing::forCell(cell_).margin;
    const int pad = cell_.height / 4;
    const int height = static_cast<int>(listing_.size()) * cell_.height + 2 * pad;
    const auto status = panelBase();
    Region region{.bounds = {status.x, status.y - height, status.width, height},
                  .background = Role::Panel,
                  .text = {},
                  .under = {},
                  .over = {}};
    std::size_t width = 0;
    for (const auto& [name, text] : listing_)
    {
        width = std::max(width, decodeUtf8(name).size());
    }
    const auto room = static_cast<std::size_t>(std::max(
        (region.bounds.width - 2 * margin) / std::max(cell_.width, 1) - static_cast<int>(width) - 2,
        1));
    for (std::size_t i = 0; i < listing_.size(); ++i)
    {
        const auto& [name, said] = listing_[i];
        const auto shown = decodeUtf8(name);
        auto text = decodeUtf8(said);
        if (text.size() > room)
        {
            text = text.substr(0, room - 1) + U"…";
        }
        const int y = pad + static_cast<int>(i) * cell_.height;
        const auto x = [&](std::size_t column)
        { return margin + static_cast<int>(column) * cell_.width; };
        region.text.push_back(
            {x(width - shown.size()), y, shown, {Role::HintKey, Role::Panel, kPlain}});
        region.text.push_back({x(width), y, U":", {Role::Dim, Role::Panel, kPlain}});
        region.text.push_back({x(width + 2), y, text, {Role::Text, Role::Panel, kPlain}});
    }
    return region;
}

Frame Workspace::frame() const
{
    Frame frame;
    view().addRegions(frame);
    if (keysPage_)
    {
        frame.push_back(keysRegion());
    }
    if (const auto shown = completions())
    {
        frame.push_back(completionRegion(*shown));
    }
    if (prompt_.palette)
    {
        frame.push_back(paletteRegion());
    }
    if (const auto shown = hints())
    {
        frame.push_back(hintRegion(*shown));
    }
    if (const auto lines = messageLines(); !lines.empty())
    {
        frame.push_back(messageRegion(lines));
    }
    if (!listing_.empty())
    {
        frame.push_back(listingRegion());
    }
    if (freezeChoice_)
    {
        frame.push_back(freezeChoiceRegion());
    }
    frame.push_back(messageRowRegion());
    if (mode_ == Mode::CommandLine && !view().typing())
    {
        frame.push_back(promptRegion());
    }
    frame.push_back(statusRegion()); // last, never covered
    return frame;
}

std::string Workspace::operatorAbout() const
{
    // "Transpose up 3 semitones · then a range or object; >> this bar's notes" (decided
    // November 2026, after a review of the grammar): a count before an operator with a how far
    // of its own is how far; before the others it's how many bars, as before d.
    const auto& name = operator_->name;
    const int n = std::max(operator_->count, 1);
    const auto count = [&](std::string_view one, std::string_view many)
    { return n == 1 ? std::string{one} : std::to_string(n) + " " + std::string{many}; };
    std::string what;
    bool bars = true;
    if (name == "operator-transpose-up" || name == "operator-transpose-down")
    {
        what = std::string{"Transpose "} + (name.ends_with("up") ? "up " : "down ") +
               count("a semitone", "semitones");
        bars = false;
    }
    else if (name == "operator-shift-later" || name == "operator-shift-earlier")
    {
        what = std::string{"Shift "} + (name.ends_with("later") ? "later" : "earlier") + " by " +
               count("a grid step", "grid steps");
        bars = false;
    }
    else if (const auto* spec = findTransform(name))
    {
        what = std::string{spec->label.substr(0, spec->label.find(" {}"))};
    }
    else
    {
        const std::array<std::pair<std::string_view, std::string_view>, 6> kVerbs{{
            {"operator-delete", "Delete"},
            {"operator-yank", "Yank"},
            {"operator-quantize", "Quantise"},
            {"operator-velocity", "Change the velocity of"},
            {"operator-humanize", "Humanise"},
            {"operator-transform", "Transform"},
        }};
        const auto found =
            std::ranges::find(kVerbs, name, &std::pair<std::string_view, std::string_view>::first);
        what = found != kVerbs.end() ? std::string{found->second} : std::string{name};
    }
    if (what.empty())
    {
        what = std::string{name};
    }
    auto text = what + " · then a range or object";
    if (const auto keys = keysOf(name); decodeUtf8(keys).size() == 1)
    {
        if (const auto line = view().describeRange(name, "operator-line"))
        {
            text += "; " + keys + keys + " " + *line;
            if (bars && operator_->count > 1)
            {
                text += ", and " + std::to_string(operator_->count - 1) + " more";
            }
        }
    }
    return text;
}

Region Workspace::statusRegion() const
{
    auto region = layoutBar(statusBar(), width_, cell_, BarSpacing::forCell(cell_));
    region.bounds = statusRow();
    return region;
}

Region Workspace::messageRowRegion() const
{
    // A panel always there, so what's said has a place; a warning marked at its left edge too.
    // A message too long for it is shown above it alone, not cut short here as well.
    const bool above = !messageLines().empty();
    auto region = layoutBar(messageBar(!above), width_, cell_, BarSpacing::forCell(cell_));
    region.bounds = messageRow();
    if (!above && !message_.empty() && messageKind_ == MessageKind::Warning && !promptRowShown())
    {
        region.under.push_back(
            {{0, 0, std::max(BarSpacing::forCell(cell_).margin / 2, 2), cell_.height},
             Role::Warning,
             255});
    }
    return region;
}

std::vector<std::string> Workspace::messageLines() const
{
    // Whether the message row holds it, with what's beside it; if not, its words in lines as
    // wide as the window.
    if (message_.empty() || promptRowShown())
    {
        return {};
    }
    const auto bar = messageBar();
    const auto spacing = BarSpacing::forCell(cell_);
    const int columns = std::max((width_ - 2 * spacing.margin) / cell_.width, 1);
    int used = (spacing.gap + cell_.width - 1) / cell_.width;
    for (const auto* side : {&bar.left, &bar.right})
    {
        for (const auto& segment : *side)
        {
            used += static_cast<int>(decodeUtf8(segment.text).size());
        }
    }
    if (used <= columns)
    {
        return {};
    }
    std::vector<std::string> lines(1);
    std::size_t length = 0;
    for (std::size_t at = 0; at < message_.size();)
    {
        const auto space = message_.find(' ', at);
        const auto word =
            message_.substr(at, space == std::string::npos ? std::string::npos : space - at);
        const auto wide = decodeUtf8(word).size();
        if (length > 0 && length + 1 + wide > static_cast<std::size_t>(columns))
        {
            lines.emplace_back();
            length = 0;
        }
        lines.back() += (length > 0 ? " " : "") + word;
        length += (length > 0 ? 1 : 0) + wide;
        at = space == std::string::npos ? message_.size() : space + 1;
    }
    return lines;
}

Region Workspace::messageRegion(const std::vector<std::string>& lines) const
{
    const int margin = BarSpacing::forCell(cell_).margin;
    const int pad = cell_.height / 4;
    const int height = static_cast<int>(lines.size()) * cell_.height + 2 * pad;
    const auto status = panelBase();
    Region region{.bounds = {status.x, status.y - height, status.width, height},
                  .background = Role::Panel,
                  .text = {},
                  .under = {},
                  .over = {}};
    const Style style{messageKind_ == MessageKind::Warning ? Role::Warning : Role::Message,
                      Role::Panel, kPlain};
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        region.text.push_back(
            {margin, pad + static_cast<int>(i) * cell_.height, decodeUtf8(lines[i]), style});
    }
    return region;
}

Bar Workspace::messageBar(bool withMessage) const
{
    // Everything whose length varies, so the status line under it never moves (decided
    // 12 November 2026): what was just said, or else what's going on; and at the right, keys
    // waiting, the search and macro recording.
    Bar row;
    row.background = Role::MessageLine;
    if (promptRowShown())
    {
        return row; // the prompt is drawn here instead
    }
    const Style text{Role::BarText, Role::MessageLine, kPlain};
    const Style dim{Role::Dim, Role::MessageLine, kPlain};
    const Style accent{Role::Accent, Role::MessageLine, kPlain};
    if (!message_.empty())
    {
        const auto role = messageKind_ == MessageKind::Warning ? Role::Warning : Role::Message;
        if (withMessage)
            row.left.push_back({message_, {role, Role::MessageLine, kPlain}});
    }
    else if (freezeChoice_)
    {
        row.left.push_back({"Up and Down choose, Enter freezes, Escape cancels", text});
    }
    else if (trackMove_)
    {
        row.left.push_back({"j and k move, Enter accepts, Escape puts back", text});
    }
    else if (const auto hint = view().typingHint())
    {
        row.left.push_back({*hint, text});
    }
    else if (operator_ && mode_ == Mode::OperatorPending)
    {
        // What the operator waiting will do, and what its doubled key takes.
        row.left.push_back({operatorAbout(), text});
    }
    else if (const auto selected = mode_ == Mode::Visual ? view().selectionStatus() : std::nullopt)
    {
        row.left.push_back({*selected, text});
        if (const auto warning = view().selectionWarning())
        {
            row.left.push_back({"; " + *warning, {Role::Warning, Role::MessageLine, kPlain}});
        }
        if (selection_ && selection_->cut)
        {
            row.left.push_back({"; cutting at the edges", accent});
        }
        if (selection_ && selection_->filter)
        {
            row.left.push_back({"; only the matches", accent});
        }
    }
    if (searching_.query)
    {
        row.right.push_back({"/" + searching_.text + " ", dim});
        row.right.push_back({view().matchStatus(*searching_.query), text});
    }
    if (const auto keys = pending(); !keys.empty())
    {
        if (!row.right.empty())
            row.right.push_back({"   ", text});
        row.right.push_back({keys, {Role::Accent, Role::MessageLine, kBold}});
    }
    if (macros_.recording != 0)
    {
        const auto ends = keysFor("macro-record");
        if (!row.right.empty())
            row.right.push_back({"   ", text});
        row.right.push_back({"recording " + macroName(macros_.recording) + ", " +
                                 (ends.empty() ? std::string{":macro-record"} : ends) + " ends it",
                             {Role::Accent, Role::MessageLine, kBold}});
    }
    return row;
}

Bar Workspace::statusBar() const
{
    Bar bottom;
    const std::string mode = freezeChoice_     ? " FREEZE "
                             : trackMove_      ? " MOVE "
                             : view().typing() ? " EDIT "
                             : mode_ == Mode::Visual || (mode_ == Mode::CommandLine && selection_)
                                 ? " VISUAL "
                                 : " NORMAL ";
    bottom.left = {{mode, {Role::ModeText, Role::Mode, kBold}}};
    // As wide as the widest mode, so the transport after it stays put.
    bottom.left.push_back(
        {std::string(std::string_view{" NORMAL "}.size() - mode.size() + 1, ' '), kBarText});
    // Where the window's too narrow for everything, what's least needed gives way first, in
    // steps (decided 12 November 2026): the file's name, the input's last message, the section
    // cut short, short words for the indicators, the settings, the section, the input light,
    // the meter and last the tempo. The mode, the transport and the recording group stay.
    constexpr int kSteps = 9;
    const auto transport = [&](int step)
    {
        std::vector<Segment> segments;
        if (player_ == nullptr)
            return segments;
        // The transport, always shown: stopped, where Space plays from; playing, the playhead.
        // Its parts are as wide as the song needs, so what follows doesn't move as it counts.
        // Each part on a panel of its own (decided 12 November 2026): the transport, then the
        // meter, the tempo and the section there.
        const auto& song = document_.song();
        const auto playing = player_->position();
        const auto at = playing.value_or(player_->marker());
        const auto place = placeParts(song, view().cursorMeter(), at);
        const Style panel{Role::BarText, Role::Indicator, kPlain};
        const Style style = playing ? Style{Role::Accent, Role::Indicator, kBold} : panel;
        const auto gap = [&] { segments.push_back({" ", kBarText}); };
        // What the transport's doing, first, as a block always there and as wide whatever it
        // says, so nothing after it moves (decided November 2026, the block 12 November 2026):
        // stopped, dim; playing, lit; recording, and how, lit as arming is.
        const bool overdub = song.recordMode == core::RecordMode::Overdub;
        std::string word = "■ STOP";
        Style state{Role::IndicatorText, Role::Indicator, kPlain};
        if (const auto phase = player_->recordPhase())
        {
            using enum core::Player::RecordPhase;
            word = *phase == CountIn   ? "● COUNT"
                   : *phase == PreRoll ? "● PRE"
                   : overdub           ? "● ODUB"
                                       : "● REC";
            state = {Role::IndicatorLitText, Role::IndicatorArm, kBold};
        }
        else if (playing)
        {
            word = "▶ PLAY";
            state = {Role::IndicatorLitText, Role::Accent, kBold};
        }
        segments.push_back({" " + padText(word, 7, false) + " ", state});
        gap();
        segments.push_back({" " + place.position + "  " + place.time + " ", style});
        if (step < 8)
        {
            gap();
            segments.push_back({" " + place.meter + " ", panel});
        }
        if (step < 9)
        {
            gap();
            segments.push_back({" " + tempoText(song, at) + " ", panel});
        }
        if (step < 6)
        {
            gap();
            segments.push_back({" " + sectionText(song, at, step < 3 ? 16 : 6) + " ", panel});
        }
        return segments;
    };
    // The indicators (decided 12 November 2026): each always in its place, as wide whatever it
    // says, dim when off and lit in its own colour when on, or the warning colour when something's
    // wrong; in groups, recording, then the settings, then the input, then the file.
    const auto& song = document_.song();
    const auto armed =
        std::ranges::count_if(song.tracks(), [](const auto& track) { return track->arm; });
    const auto indicators = [&](int step)
    {
        std::vector<Segment> segments;
        const bool brief = step >= 4;
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
        const auto gap = [&] { segments.push_back({" ", kBarText}); };
        const auto lit = [](bool on) { return on ? Lit::On : Lit::Off; };

        const auto arm = armed > 0 ? "ARM " + std::to_string(armed) : std::string{"ARM"};
        indicator(arm, arm, 5, 5, lit(armed > 0), Role::IndicatorArm);
        const bool overdub = song.recordMode == core::RecordMode::Overdub;
        indicator(overdub ? "OVERDUB" : "REPLACE", overdub ? "ODUB" : "REPL", 7, 4, lit(armed > 0),
                  Role::IndicatorRecordMode);
        {
            using enum core::CountInSettings::Mode;
            const auto& countIn = song.countIn;
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
            indicator(full, shortText, 13, 6, lit(armed > 0 && countIn.mode != None),
                      Role::IndicatorCount);
        }
        {
            // Lit when the click will sound: always, or with tracks armed while recording or for
            // a count-in, which clicks whatever the metronome's set to; the warning colour if it
            // has nowhere to go.
            using When = core::MetronomeSettings::When;
            const auto when = song.metronome.when;
            const bool clicks =
                when == When::Always ||
                (armed > 0 && (when == When::Recording ||
                               song.countIn.mode == core::CountInSettings::Mode::CountIn));
            indicator("CLICK", "CLK", 5, 3,
                      !clicks       ? Lit::Off
                      : clickPort() ? Lit::On
                                    : Lit::Warning,
                      Role::IndicatorClick);
        }
        if (step < 5)
        {
            gap();
            indicator("THRU", "THR", 4, 3, lit(thru_), Role::IndicatorThru);
            indicator("AUDITION", "AUD", 8, 3, lit(audition_), Role::IndicatorAudition);
            indicator("REWIND", "RWD", 6, 3, lit(rewind_), Role::IndicatorRewind);
            // Quantise bypassed is never simply on: dim, or the warning colour as a reminder
            // that what's heard isn't what's set.
            indicator("Q BYPASS", "QBY", 8, 3,
                      player_ != nullptr && player_->bypassQuantize() ? Lit::Warning : Lit::Off,
                      Role::Warning);
        }
        if (step < 7)
        {
            // The input light, and the last message in, fixed in width so nothing after it
            // moves (decided November 2026).
            gap();
            segments.push_back(
                {" IN ",
                 // In the warning colour from input lost while recording until it stops.
                 lostWhileRecording() ? Style{Role::IndicatorLitText, Role::Warning, kBold}
                 : inputLitUntil_     ? Style{Role::IndicatorLitText, Role::IndicatorInput, kBold}
                                      : Style{Role::IndicatorText, Role::Indicator, kPlain}});
            if (step < 2)
            {
                std::span<const std::uint8_t> last;
                if (player_ && player_->lastInput())
                    last = std::span{player_->lastInput()->bytes}.first(player_->lastInput()->size);
                segments.push_back(
                    {" " + inputText(last) + " ", {Role::BarText, Role::Indicator, kPlain}});
            }
            segments.push_back({" ", kBarText});
        }
        if (step < 1)
        {
            // The song's file, and [+] if it has changes not saved, as in Vim, with room kept
            // for [+] so nothing moves as it comes and goes.
            const auto& file = document_.file();
            const auto& imported = document_.importedFrom();
            const auto name = file       ? utf8Text(file->filename())
                              : imported ? utf8Text(imported->filename()) + " (imported)"
                                         : std::string{"No file"};
            gap();
            segments.push_back({" " + name + (document_.modified() ? " [+] " : "     "),
                                {Role::BarText, Role::Indicator, kPlain}});
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
    const auto spacing = BarSpacing::forCell(cell_);
    const auto columns = static_cast<std::size_t>(
        std::max((width_ - 2 * spacing.margin) / std::max(cell_.width, 1), 1));
    const auto gapColumns =
        static_cast<std::size_t>((spacing.gap + cell_.width - 1) / std::max(cell_.width, 1));
    int step = 0;
    while (step < kSteps &&
           length(bottom.left) + length(transport(step)) + gapColumns + length(indicators(step)) >
               columns)
        ++step;
    const auto left = transport(step);
    const auto right = indicators(step);
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
