// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/freeze.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <utility>
#include <variant>

namespace modal::ui
{
namespace
{
constexpr std::array kFreezeParts{core::FreezePart::All, core::FreezePart::Transpose,
                                  core::FreezePart::Velocity, core::FreezePart::Quantize,
                                  core::FreezePart::TimeOffset};
} // namespace

std::optional<std::string> Workspace::freezeReason(std::span<const core::TrackId> tracks,
                                                   core::FreezePart part) const
{
    if (player_ != nullptr && (player_->playing() || player_->stopping()))
        return "Stop playback before freezing tracks";
    const bool bypass = player_ != nullptr && player_->bypassQuantize();
    for (const auto id : tracks)
    {
        const auto* track = document_.song().track(id);
        if (track == nullptr)
            return "No track to freeze";
        if (auto why = core::freezeUnavailable(*track, part, bypass))
            return why;
    }
    return std::nullopt;
}

CommandResult Workspace::trackFreeze(std::string_view arguments)
{
    const auto id = view().cursor().track;
    if (!id || where() == Where::Strip)
        return CommandResult::error("Freeze needs a track, not the conductor or section strip");
    if (selection_ && selection_->where != Where::TrackList)
        return CommandResult::error(
            "Freeze acts on whole tracks: leave Visual mode, or select rows in Tracks");
    std::vector<core::TrackId> ids = selection_ ? tracks_->selected() : std::vector{*id};
    if (player_ != nullptr && (player_->playing() || player_->stopping()))
        return CommandResult::error("Stop playback before freezing tracks");
    const auto first = arguments.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        freezeChoice_ = FreezeChoice{std::move(ids), 0};
        return {true};
    }
    arguments = arguments.substr(first, arguments.find_last_not_of(" \t") - first + 1);
    const auto part = core::freezePart(arguments);
    if (!part)
        return CommandResult::error("Choose all, transpose, velocity, quantise or time");
    return applyFreeze(ids, *part);
}

CommandResult Workspace::applyFreeze(std::span<const core::TrackId> ids, core::FreezePart part)
{
    if (auto why = freezeReason(ids, part))
        return CommandResult::error(std::move(*why));
    const auto& song = document_.song();
    std::vector<core::FrozenTrack> prepared;
    std::size_t left = 0;
    std::size_t fallbacks = 0;
    for (const auto id : ids)
    {
        auto result =
            core::freezeTrack(song, id, part, player_ != nullptr && player_->bypassQuantize());
        if (const auto* refusal = std::get_if<core::FreezeRefusal>(&result))
            return CommandResult::error(song.track(id)->name + " at " +
                                        positionText(song.meter(id), refusal->at) + ": " +
                                        refusal->reason);
        auto frozen = std::get<core::FrozenTrack>(std::move(result));
        left += frozen.leftInPlace;
        fallbacks += frozen.quantizeFallback ? 1 : 0;
        prepared.push_back(std::move(frozen));
    }
    const auto title = "Freeze " + std::string{core::freezeName(part)} + " on " +
                       std::to_string(ids.size()) + (ids.size() == 1 ? " track" : " tracks");
    const bool changed = document_.edit(
        title, view().place(),
        [&](core::Song& into)
        {
            for (const auto& frozen : prepared)
            {
                const auto id = frozen.settings.id();
                into.updateEvents(id, [&](core::EventList& events) { events = frozen.events; });
                into.updateTrack(id, [&](core::Track& track) { track = frozen.settings; });
            }
        });
    if (!changed)
        return CommandResult::error("Nothing to freeze");
    endVisual();
    refresh();
    std::string message = title;
    if (left > 0)
        message += "; " + std::to_string(left) + " notes left where stored after quantise clashes";
    if (fallbacks > 0)
        message += "; quantise kept stored timing on " + std::to_string(fallbacks) +
                   " tracks to preserve pedal order";
    return {true, std::move(message), left > 0 || fallbacks > 0};
}

bool Workspace::freezeChoiceKey(const Key& key)
{
    message_.clear();
    if (key.named == NamedKey::Escape)
    {
        freezeChoice_.reset();
        return true;
    }
    if (key.named == NamedKey::Up || key.named == NamedKey::Down)
    {
        const bool down = key.named == NamedKey::Down;
        auto& choice = *freezeChoice_;
        auto next = choice.chosen;
        while (down ? next + 1 < kFreezeParts.size() : next > 0)
        {
            next = down ? next + 1 : next - 1;
            if (!freezeReason(choice.tracks, kFreezeParts[next]))
            {
                choice.chosen = next;
                break;
            }
        }
    }
    else if (key.named == NamedKey::Enter)
    {
        const auto choice = *freezeChoice_;
        auto result = applyFreeze(choice.tracks, kFreezeParts[choice.chosen]);
        report(result);
        if (!result.refused)
            freezeChoice_.reset();
    }
    return true;
}

Region Workspace::freezeChoiceRegion() const
{
    const auto& choice = *freezeChoice_;
    const auto& bottom = layout_.bottom[0];
    const int margin = BarSpacing::forCell(cell_).margin;
    const int pad = cell_.height / 4;
    const int height = static_cast<int>(kFreezeParts.size() + 1) * cell_.height + 2 * pad;
    Region region{.bounds = {bottom.x, std::max(0, bottom.y - height), bottom.width, height},
                  .background = Role::Panel,
                  .text = {},
                  .under = {},
                  .over = {}};
    region.text.push_back(
        {margin, pad, U"Freeze playback transforms", {Role::Accent, Role::Panel, kBold}});
    for (std::size_t i = 0; i < kFreezeParts.size(); ++i)
    {
        const auto why = freezeReason(choice.tracks, kFreezeParts[i]);
        auto text = std::string{core::freezeName(kFreezeParts[i])};
        if (why)
            text += " — " + *why;
        auto shown = decodeUtf8(text);
        const auto room =
            static_cast<std::size_t>(std::max(1, (bottom.width - 2 * margin) / cell_.width));
        if (shown.size() > room)
            shown = shown.substr(0, room - 1) + U"…";
        const bool chosen = i == choice.chosen;
        const Style style{why      ? Role::Dim
                          : chosen ? Role::CursorText
                                   : Role::Text,
                          chosen ? Role::Cursor : Role::Panel, kPlain};
        region.text.push_back(
            {margin, pad + static_cast<int>(i + 1) * cell_.height, std::move(shown), style});
    }
    return region;
}

} // namespace modal::ui
