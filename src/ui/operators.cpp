// SPDX-License-Identifier: GPL-3.0-or-later

// The operators other than d and y (decided for milestone 6): each view says what a range or
// object takes, and the workspace changes those events the same way in every view.

#include <modal/core/controllers.hpp>
#include <modal/core/pitch.hpp>
#include <modal/core/transforms.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <random>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace modal::ui
{

namespace
{

// An event as words, for messages: "the C4 at 3:1:000", "the Sustain at 3:1:120".
std::string eventText(const core::Event& event, const core::MeterMap& meter)
{
    std::string what = "event";
    if (const auto* note = std::get_if<core::Note>(&event.data))
    {
        what = core::pitchName(note->pitch);
    }
    else if (const auto* change = std::get_if<core::ControlChange>(&event.data))
    {
        const auto name = core::controllerName(change->controller);
        what = name.empty() ? "controller " + std::to_string(change->controller) : name;
    }
    return "the " + what + " at " + positionText(meter, event.tick);
}

std::string refusalText(const core::Refusal& refusal, const core::MeterMap& meter)
{
    using enum core::Refusal::Reason;
    auto text = eventText(refusal.event, meter);
    text[0] = 'T';
    const auto other = refusal.other ? eventText(*refusal.other, meter) : std::string{};
    switch (refusal.reason)
    {
    case AboveHighest:
        return text + " would go above G9, the highest note";
    case BelowLowest:
        return text + " would go below C-1, the lowest note";
    case BeforeStart:
        return text + " would go before the song's start";
    case PastEnd:
        return text + " would go past the song's end";
    case Overlap:
        return text + " would overlap " + other;
    case Order:
        return text + " would move past " + other;
    }
    return text;
}

std::string times(int count, std::string_view one, std::string_view many)
{
    return count == 1 ? std::string{one} : std::to_string(count) + " " + std::string{many};
}

std::string_view trimmed(std::string_view text)
{
    const auto start = text.find_first_not_of(" \t");
    if (start == std::string_view::npos)
    {
        return {};
    }
    return text.substr(start, text.find_last_not_of(" \t") - start + 1);
}

// A number of semitones, up or down: 7, +12, -3.
std::optional<int> semitones(std::string_view text)
{
    text = trimmed(text);
    if (text.starts_with('+'))
    {
        text.remove_prefix(1);
    }
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size() ||
        std::abs(value) > 127)
    {
        return std::nullopt;
    }
    return value;
}

} // namespace

bool isOperator(std::string_view command)
{
    // The transforms are operators too, each in the transform menu (decided November 2026).
    constexpr std::array<std::string_view, 10> kOperators{
        "operator-delete",         "operator-yank",        "operator-transpose-up",
        "operator-transpose-down", "operator-shift-later", "operator-shift-earlier",
        "operator-quantize",       "operator-velocity",    "operator-humanize",
        "operator-transform"};
    return command.starts_with("transform-") ||
           std::ranges::find(kOperators, command) != kOperators.end();
}

std::vector<Key> Workspace::transformKeys() const
{
    // The keys the transforms' bindings start with, the menu's (<leader>x at first), so a
    // user's own bindings make the menu theirs.
    std::optional<std::vector<Key>> common;
    for (const auto& [keys, binding] : keymap_.bindings(Mode::Normal))
    {
        if (!binding.command.starts_with("transform-") || keys.size() < 2)
        {
            continue;
        }
        const std::vector<Key> before(keys.begin(), keys.end() - 1);
        if (!common)
        {
            common = before;
            continue;
        }
        const auto [mine, theirs] = std::ranges::mismatch(*common, before);
        common->erase(mine, common->end());
    }
    return common.value_or(std::vector<Key>{});
}

bool Workspace::openTransformMenu(OperatorTarget target)
{
    // ! and a range: the range kept and the menu's keys typed, so the hint menu shows the
    // transforms at once and the next key picks one, as the same keys before a range would
    // (decided November 2026).
    const auto keys = transformKeys();
    if (keys.empty())
    {
        setMessage("No keys start the transforms, for a menu of them", MessageKind::Warning);
        return true;
    }
    transformTarget_ = std::move(target);
    for (const auto& key : keys)
    {
        keys_.key(keymapHere(), Mode::Normal, keys_.typed(key), lastTime_);
    }
    return true;
}

CommandResult Workspace::transformLine(std::string_view name, std::string_view arguments)
{
    // At the : prompt, the selection's events, or else the cursor's bar's, as a doubled
    // operator takes it; asking, as the keys do, when it's given nothing.
    auto target = view().target(selection_ ? "visual" : "operator-line", 0);
    if (!target)
    {
        return {true};
    }
    if (selection_ && selection_->filter && !onlyMatches(*target))
    {
        return {true};
    }
    if (trimmed(arguments).empty())
    {
        presetTarget_ = std::move(target);
        transform(name, 1, "preset", 0);
        presetTarget_.reset();
        return {true};
    }
    applyTransform(name, *target, std::string{arguments});
    return {true};
}

bool Workspace::applyTransform(std::string_view name, const OperatorTarget& target,
                               const std::string& text)
{
    // A transform with what it's been told: the answer to its question, or what follows its
    // name at the : prompt.
    if (name == "transform-transpose")
    {
        const auto by = semitones(text);
        if (!by)
        {
            setMessage("Not a number of semitones: " + text + "; type +12, -3 or 7",
                       MessageKind::Warning);
            return true;
        }
        if (*by == 0)
        {
            setMessage("Transposing by 0 semitones changes nothing", MessageKind::Information);
            return true;
        }
        return changeEvents("Transpose " + target.what + (*by > 0 ? " up " : " down ") +
                                times(std::abs(*by), "a semitone", "semitones"),
                            target, [by = *by](const core::EventList& events, const auto& part)
                            { return core::transpose(events, part.events, by); });
    }
    return false;
}

bool Workspace::transform(std::string_view op, int amount, std::string_view motion, int count)
{
    auto target =
        presetTarget_ ? std::exchange(presetTarget_, std::nullopt) : view().target(motion, count);
    if (!target)
    {
        return true;
    }
    if (motion == "visual" && selection_ && selection_->filter && !onlyMatches(*target))
    {
        return true;
    }
    target->cut = cutting_;
    if (op == "operator-transform")
    {
        return openTransformMenu(std::move(*target));
    }
    if (op == "transform-transpose")
    {
        askFor("Transpose " + target->what + " by how many semitones (+12, -3): ",
               [this, target = *target](const std::string& text)
               { applyTransform("transform-transpose", target, text); });
        return true;
    }
    const auto& song = document_.song();
    if (op == "operator-transpose-up" || op == "operator-transpose-down")
    {
        const bool up = op == "operator-transpose-up";
        const int by = up ? amount : -amount;
        return changeEvents("Transpose " + target->what + (up ? " up " : " down ") +
                                times(amount, "a semitone", "semitones"),
                            *target, [by](const core::EventList& events, const auto& part)
                            { return core::transpose(events, part.events, by); });
    }
    if (op == "operator-shift-later" || op == "operator-shift-earlier")
    {
        // By each track's grid step.
        const bool later = op == "operator-shift-later";
        return changeEvents(
            "Move " + target->what + (later ? " later" : " earlier") + " by " +
                times(amount, "a grid step", "grid steps"),
            *target,
            [&](const core::EventList& events, const auto& part)
            {
                const auto step =
                    std::max<core::Tick>(song.track(part.track)->grid.toTicks(song.ppqn()), 1);
                return core::shift(events, part.events, (later ? amount : -amount) * step,
                                   song.end(), target->notesOnly);
            });
    }
    if (op == "operator-velocity")
    {
        askFor("Velocity of " + target->what + " (80, +10, 80%): ",
               [this, target = *target](const std::string& text)
               {
                   const auto change = core::parseVelocityChange(text);
                   if (!change)
                   {
                       setMessage("Not a velocity: " + text +
                                      "; type a value (80), a change (+10, -10) or a "
                                      "percentage (80%)",
                                  MessageKind::Warning);
                       return;
                   }
                   using enum core::VelocityChange::Kind;
                   const auto amount = std::to_string(change->amount);
                   const auto title =
                       change->kind == Set ? "Set the velocity of " + target.what + " to " + amount
                       : change->kind == Add
                           ? "Change the velocity of " + target.what + " by " +
                                 (change->amount > 0 ? "+" : "") + amount
                           : "Scale the velocity of " + target.what + " to " + amount + "%";
                   changeEvents(title, target,
                                [c = *change](const core::EventList& events, const auto& part)
                                { return core::changeVelocity(events, part.events, c); });
               });
        return true;
    }
    if (op == "operator-humanize")
    {
        // Up to humanizetime either way at each note's own tempo.
        const auto& tempo = song.tempo();
        const auto most = [&](core::Tick tick)
        { return tempo.toTickExtended(tempo.toTime(tick) + humanizeTime_) - tick; };
        const auto random = [this](std::int64_t low, std::int64_t high)
        { return std::uniform_int_distribution<std::int64_t>{low, high}(random_); };
        return changeEvents("Humanise " + target->what, *target,
                            [&](const core::EventList& events, const auto& part)
                            {
                                return core::humanize(events, part.events, most, humanizeVelocity_,
                                                      song.end(), random);
                            });
    }
    if (op == "operator-quantize")
    {
        // With pedals there, asking whether they go with their notes (decided October 2026).
        const bool pedalled =
            std::ranges::any_of(target->parts, [&](const OperatorTarget::Part& part)
                                { return core::hasPedals(song.events(part.track), part.events); });
        if (!pedalled)
        {
            return quantize(*target, false);
        }
        ask(
            "Move the pedalling with its notes?", [this, target = *target]
            { quantize(target, true); }, [this, target = *target] { quantize(target, false); });
        return true;
    }
    return false;
}

bool Workspace::quantize(const OperatorTarget& target, bool pedals)
{
    const auto& song = document_.song();
    return changeEvents("Quantise " + target.what + (pedals ? ", with its pedalling" : ""), target,
                        [&](const core::EventList& events, const auto& part)
                        {
                            const auto& meter = song.meter(part.track);
                            const auto step = std::max<core::Tick>(
                                song.track(part.track)->grid.toTicks(song.ppqn()), 1);
                            const auto nearest = [&](core::Tick tick)
                            {
                                // Not onto the song's end, where nothing can start.
                                const auto to = core::nearestGridLine(meter, tick, step);
                                return to < song.end() ? to : tick;
                            };
                            return core::quantize(events, part.events, nearest, pedals);
                        });
}

bool Workspace::changeEvents(std::string title, const OperatorTarget& target,
                             const EventsChange& change)
{
    // Each track's events as changed, or the first refusal; then all of them as one change.
    const auto& song = document_.song();
    std::vector<core::EventList> changed;
    for (const auto& part : target.parts)
    {
        // Cutting at the edges, the notes crossing them cut first, and what's inside taken.
        const auto& range = target.cut;
        if (range && std::ranges::find(range->tracks, part.track) != range->tracks.end())
        {
            const auto which = [&](const core::Note& note) { return range->takes(note); };
            const auto cut = core::cutAt(core::cutAt(song.events(part.track), range->from, which),
                                         range->to, which);
            OperatorTarget::Part inside{part.track, {}};
            for (std::size_t i = 0; i < cut.size(); ++i)
            {
                const auto* note = std::get_if<core::Note>(&cut[i].data);
                if (cut[i].tick >= range->from && cut[i].tick < range->to &&
                    (note != nullptr ? range->takes(*note) : !range->pitches))
                {
                    inside.events.push_back(i);
                }
            }
            auto result = change(cut, inside);
            if (const auto* refusal = std::get_if<core::Refusal>(&result))
            {
                setMessage(refusalText(*refusal, song.meter(part.track)), MessageKind::Warning);
                return true;
            }
            changed.push_back(std::move(std::get<core::EventList>(result)));
            continue;
        }
        auto result = change(song.events(part.track), part);
        if (const auto* refusal = std::get_if<core::Refusal>(&result))
        {
            setMessage(refusalText(*refusal, song.meter(part.track)), MessageKind::Warning);
            return true;
        }
        changed.push_back(std::move(std::get<core::EventList>(result)));
    }
    bool any = false;
    for (std::size_t i = 0; i < changed.size(); ++i)
    {
        any = any || changed[i] != song.events(target.parts[i].track);
    }
    if (!any)
    {
        setMessage("Nothing changed in " + target.what, MessageKind::Information);
        return true;
    }
    document_.edit(std::move(title), view().place(),
                   [&](core::Song& s)
                   {
                       for (std::size_t i = 0; i < changed.size(); ++i)
                       {
                           s.updateEvents(target.parts[i].track,
                                          [&](core::EventList& events) { events = changed[i]; });
                       }
                   });
    refresh();
    return true;
}

} // namespace modal::ui
