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
    // A message too long for it is shown above it alone, not cut short here as well.
    auto region =
        layoutBar(statusBar(messageLines().empty()), width_, cell_, BarSpacing::forCell(cell_));
    region.bounds = layout_.bottom[0];
    return region;
}

std::vector<std::string> Workspace::messageLines() const
{
    // Whether the status line holds it, with what's beside it; if not, its words in lines as
    // wide as the window.
    if (message_.empty())
    {
        return {};
    }
    const auto bar = statusBar();
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

Bar Workspace::statusBar(bool withMessage) const
{
    const Style label{Role::Dim, Role::Bar, kPlain};
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
    if (player_ != nullptr)
    {
        // The transport, always shown: stopped, where Space plays from; playing, the playhead.
        // Its parts are as wide as the song needs, so what follows doesn't move as it counts.
        const auto& song = document_.song();
        const auto playing = player_->position();
        const auto at = playing.value_or(player_->marker());
        const auto place = placeParts(song, view().cursorMeter(), at);
        const Style style = playing ? Style{Role::Accent, Role::Bar, kBold}
                                    : Style{Role::BarText, Role::Bar, kPlain};
        bottom.left.push_back(
            {playing ? "▶ " : "■ ", playing ? style : Style{Role::Dim, Role::Bar, kPlain}});
        bottom.left.push_back({place.position + "  " + place.time + "  " + place.meter + "  " +
                                   tempoText(song, at) + "  " + sectionText(song, at),
                               style});
    }
    if (macros_.recording != 0)
    {
        const auto ends = keysFor("macro-record");
        bottom.left.push_back({"  recording " + macroName(macros_.recording) + ", " +
                                   (ends.empty() ? std::string{":macro-record"} : ends) +
                                   " ends it",
                               {Role::Accent, Role::Bar, kBold}});
    }
    if (freezeChoice_ && (!withMessage || message_.empty()))
    {
        bottom.left.push_back({"  Up and Down choose, Enter freezes, Escape cancels", kBarText});
    }
    else if (trackMove_)
    {
        bottom.left.push_back({"  j and k move, Enter accepts, Escape puts back", kBarText});
    }
    else if (withMessage && !message_.empty())
    {
        const auto role = messageKind_ == MessageKind::Warning ? Role::Warning : Role::Message;
        bottom.left.push_back({"  " + message_, {role, Role::Bar, kPlain}});
    }
    else if (const auto hint = view().typingHint())
    {
        bottom.left.push_back({"  " + *hint, kBarText});
    }
    else if (operator_ && mode_ == Mode::OperatorPending)
    {
        // What the operator waiting will do, and what its doubled key takes.
        bottom.left.push_back({"  " + operatorAbout(), kBarText});
    }
    else if (const auto selected = mode_ == Mode::Visual ? view().selectionStatus() : std::nullopt)
    {
        bottom.left.push_back({"  " + *selected, kBarText});
        if (const auto warning = view().selectionWarning())
        {
            bottom.left.push_back({"; " + *warning, {Role::Warning, Role::Bar, kPlain}});
        }
        if (selection_ && selection_->cut)
        {
            bottom.left.push_back({"; cutting at the edges", {Role::Accent, Role::Bar, kPlain}});
        }
        if (selection_ && selection_->filter)
        {
            bottom.left.push_back({"; only the matches", {Role::Accent, Role::Bar, kPlain}});
        }
    }
    if (const auto keys = pending(); !keys.empty())
    {
        bottom.right.push_back({keys + "   ", {Role::Accent, Role::Bar, kBold}});
    }
    if (searching_.query)
    {
        bottom.right.push_back({"/" + searching_.text + " ", label});
        bottom.right.push_back({view().matchStatus(*searching_.query) + "   ", kBarText});
    }
    // Aligned to the right, so what comes and goes is at the left, and moves nothing else.
    if (player_ != nullptr && player_->bypassQuantize())
    {
        bottom.right.push_back({"Quantise bypassed   ", {Role::Warning, Role::Bar, kPlain}});
    }
    // The view's grid, the one setting that's the view's, padded so Rewind stays put.
    if (const auto grid = view().gridStatus())
    {
        bottom.right.push_back({"Grid ", label});
        bottom.right.push_back({padText(*grid, 5, false) + "   ", kBarText});
    }
    if (!thru_)
        bottom.right.push_back({"Thru off   ", {Role::Warning, Role::Bar, kPlain}});
    bottom.right.push_back({"In", {inputLitUntil_ ? Role::Accent : Role::Dim, Role::Bar, kPlain}});
    if (player_ && player_->lastInputNote())
    {
        const auto note = *player_->lastInputNote();
        bottom.right.push_back(
            {" " + core::pitchName(note.pitch) + " " + std::to_string(note.velocity) + "   ",
             kBarText});
    }
    else
        bottom.right.push_back({"          ", kBarText});
    bottom.right.push_back({rewind_ ? "Rewind on   " : "Rewind off   ", kBarText});
    // The song's file, and [+] if it has changes not saved, as in Vim.
    const auto& file = document_.file();
    const auto& imported = document_.importedFrom();
    const auto name = file       ? utf8Text(file->filename())
                      : imported ? utf8Text(imported->filename()) + " (imported)"
                                 : std::string{"No file"};
    // Room kept for [+], so nothing moves as it comes and goes.
    bottom.right.push_back({name + (document_.modified() ? " [+]" : "    "), kBarText});
    return bottom;
}

} // namespace modal::ui
