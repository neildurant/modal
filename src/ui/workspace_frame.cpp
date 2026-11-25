// SPDX-License-Identifier: GPL-3.0-or-later

// Drawing the workspace: its frame of regions, the status line, messages and listings
// (split from workspace.cpp, November 2026, after a code review).

#include "workspace_commands.hpp"

#include <modal/core/pitch.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/completion.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/event_edit.hpp>
#include <modal/ui/key_notation.hpp>
#include <modal/ui/palette.hpp>
#include <modal/ui/settings.hpp>
#include <modal/ui/status_bar.hpp>
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
    if (drumMapChoice_)
        frame.push_back(drumMapChoiceRegion());
    if (songChoice_)
        frame.push_back(songChoiceRegion());
    if (loopLabelChoice_)
    {
        const auto row = messageRow();
        const int margin = BarSpacing::forCell(cell_).margin;
        const Rect field{margin, row.y, 26 * cell_.width, row.height};
        frame.push_back(loopLabelChoice_->list.region(field, {0, 0, width_, row.y}, cell_));
    }
    if (trackImportChoice_)
        frame.push_back(trackImportRegion());
    if (recentChoice_)
        frame.push_back(recentRegion());
    if (backupChoice_)
        frame.push_back(backupRegion());
    if (eventInsertChoice_)
    {
        const auto row = messageRow();
        const int margin = BarSpacing::forCell(cell_).margin;
        const Rect field{margin, row.y, 26 * cell_.width, row.height};
        frame.push_back(eventInsertChoice_->region(field, {0, 0, width_, row.y}, cell_));
    }
    if (filePicker_ && !question_)
        frame.push_back(filePickerRegion());
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
    // A list narrowed by typing: its label, what's typed and the cursor after it, as the :
    // prompt has, so it's plain it can be typed into (decided 20 November 2026, after use).
    const auto typing = [&](std::string label, const std::u32string& typed)
    {
        row.left.push_back({std::move(label) + encodeUtf8(typed), text});
        row.left.push_back({" ", {Role::CursorText, Role::Cursor, kPlain}});
    };
    if (!message_.empty())
    {
        const auto role = messageKind_ == MessageKind::Warning ? Role::Warning : Role::Message;
        if (withMessage)
            row.left.push_back({message_, {role, Role::MessageLine, kPlain}});
    }
    else if (drumMapChoice_)
    {
        if (drumMapChoice_->stage == DrumMapChoice::Stage::Review)
            row.left.push_back(
                {"Drum map: " + encodeUtf8(drumMapChoice_->list.typed()) +
                     "  PageUp/Down scroll; Up/Down choose, Enter takes, Escape leaves",
                 text});
        else
        {
            typing("Drum map: ", drumMapChoice_->list.typed());
            row.left.push_back(
                {"  Type to narrow; Up/Down choose, Enter takes, Escape leaves", text});
        }
    }
    else if (freezeChoice_)
    {
        row.left.push_back({"Up and Down choose, Enter freezes, Escape cancels", text});
    }
    else if (eventInsertChoice_)
    {
        typing("Insert event: ", eventInsertChoice_->typed());
        row.right.push_back({"Type to narrow · ↑↓ choose · Enter accepts · Esc cancels", dim});
    }
    else if (loopLabelChoice_)
    {
        const bool punch = loopLabelChoice_->purpose == LabelPurpose::Punch;
        typing(loopLabelChoice_->from ? (punch ? "Punch out: " : "Loop end: ")
                                      : (punch ? "Punch in: " : "Loop start: "),
               loopLabelChoice_->list.typed());
        row.right.push_back({"Type to narrow · ↑↓ choose · Enter accepts · Esc cancels", dim});
    }
    else if (recentChoice_)
    {
        typing("Recent songs: ", recentChoice_->list->typed());
        row.right.push_back(
            {"Type to narrow · ↑↓ choose · Enter opens · Del removes from the list", dim});
    }
    else if (backupChoice_)
    {
        // Nothing to type: a list this short needs no narrowing.
        row.left.push_back({"Backups of " + utf8Text(backupChoice_->song.filename()), text});
        row.right.push_back({"↑↓ choose · Enter opens it as a new song · Esc leaves", dim});
    }
    else if (trackImportChoice_)
    {
        const auto& choice = *trackImportChoice_;
        const auto ticked = std::ranges::count(choice.ticked, true);
        row.left.push_back({"Import from " + utf8Text(choice.file.filename()) + ": " +
                                std::to_string(ticked) + " of " +
                                std::to_string(choice.ticked.size()) + " ticked",
                            text});
        row.right.push_back(
            {"↑↓ move · Space ticks · a all or none · Enter imports · Esc cancels", dim});
    }
    else if (songChoice_)
    {
        typing("Open songs: ", songChoice_->list.typed());
        row.right.push_back({"› Active · + Unsaved edits · ↑↓ Choose · Enter switches", dim});
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
    StatusBarState state;
    using StatusMode = StatusBarState::Mode;
    state.mode = drumMapChoice_ || filePicker_ || eventInsertChoice_ || songChoice_ ||
                         trackImportChoice_ || recentChoice_ || backupChoice_ || loopLabelChoice_
                     ? StatusMode::Edit
                 : freezeChoice_   ? StatusMode::Freeze
                 : trackMove_      ? StatusMode::Move
                 : view().typing() ? StatusMode::Edit
                 : mode_ == Mode::Visual || (mode_ == Mode::CommandLine && selection_)
                     ? StatusMode::Visual
                     : StatusMode::Normal;
    const auto& song = document_.song();
    state.overdub = song.recordMode == core::RecordMode::Overdub;
    state.punch = punchSpan().has_value();
    state.loop = loopSpan().has_value();
    if (player_)
    {
        const auto playing = player_->position();
        const auto at = playing.value_or(player_->marker());
        const auto place = placeParts(song, view().cursorMeter(), at);
        using TransportState = StatusBarState::TransportState;
        auto transportState = player_->looping() ? TransportState::Looping
                              : playing          ? TransportState::Playing
                                                 : TransportState::Stopped;
        if (const auto phase = player_->recordPhase())
        {
            using enum core::Player::RecordPhase;
            transportState = *phase == CountIn   ? TransportState::CountIn
                             : *phase == PreRoll ? TransportState::PreRoll
                             : state.overdub     ? TransportState::Overdubbing
                                                 : TransportState::Recording;
        }
        else if (const auto leadIn = player_->leadInPhase())
        {
            transportState = *leadIn == core::Player::LeadInPhase::CountIn
                                 ? TransportState::PlaybackCountIn
                                 : TransportState::PlaybackPreRoll;
        }
        state.transport = StatusBarState::Transport{
            transportState, playing.has_value(), place.position,        place.time,
            place.meter,    tempoText(song, at), sectionText(song, at), sectionText(song, at, 6)};
        state.quantizeBypassed = player_->bypassQuantize();
    }
    state.armed =
        std::ranges::count_if(song.tracks(), [](const auto& track) { return track->arm; });
    state.countIn = song.countIn;
    state.playbackLeadIn = song.playbackLeadIn;
    state.clickWhen = song.metronome.when;
    state.clickAvailable = clickPort();
    state.thru = thru_;
    state.audition = audition_;
    state.rewind = rewind_;
    state.inputActive = inputLitUntil_.has_value();
    state.inputLost = lostWhileRecording();
    state.modified = document_.modified();
    return modal::ui::statusBar(state, width_, cell_);
}

} // namespace modal::ui
