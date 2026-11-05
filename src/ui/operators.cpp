// SPDX-License-Identifier: GPL-3.0-or-later

// The operators other than d and y (decided for milestone 6): each view says what a range or
// object takes, and the workspace changes those events the same way in every view.

#include <modal/core/controllers.hpp>
#include <modal/core/event_query.hpp>
#include <modal/core/note_fraction.hpp>
#include <modal/core/pedalling.hpp>
#include <modal/core/pitch.hpp>
#include <modal/core/transforms.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <numeric>
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

// What a transform's told: its amount, the first word not NAME=VALUE (or its default), and
// its options. to= takes the rest of the line, as a track's name may have spaces, whether typed
// on the command line or in answer to a question.
struct Told
{
    std::string amount;
    std::vector<std::pair<std::string, std::string>> options;
};

Told told(std::string_view text, std::string_view name)
{
    Told result;
    std::size_t at = 0;
    while (at < text.size())
    {
        const auto start = text.find_first_not_of(" \t,", at);
        if (start == std::string_view::npos)
        {
            break;
        }
        if (text.substr(start).starts_with("to="))
        {
            auto track = text.substr(start + 3);
            track.remove_suffix(track.size() -
                                std::min(track.find_last_not_of(" \t") + 1, track.size()));
            result.options.emplace_back("to", track);
            break;
        }
        const auto end = std::min(text.find_first_of(" \t,", start), text.size());
        const auto word = text.substr(start, end - start);
        if (const auto equals = word.find('='); equals != std::string_view::npos && equals > 0)
        {
            result.options.emplace_back(word.substr(0, equals), word.substr(equals + 1));
        }
        else if (result.amount.empty())
        {
            result.amount = word;
        }
        else
        {
            result.amount += " " + std::string{word};
        }
        at = end;
    }
    if (result.amount.empty() && !name.empty())
    {
        // Its default: the first setting's, or its question's.
        if (const auto* spec = Workspace::findTransform(name))
        {
            result.amount = spec->settings.empty() ? spec->answer : spec->settings.front().answer;
        }
    }
    return result;
}

// A whole number, with or without a % after it, from 0 to most.
std::optional<int> percentUpTo(std::string_view text, int most)
{
    if (text.ends_with('%'))
    {
        text.remove_suffix(1);
    }
    int value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size() || value < 0 ||
        value > most)
    {
        return std::nullopt;
    }
    return value;
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

void Workspace::askSettings(const std::string& op, const OperatorTarget& target, Answers answers)
{
    // The first setting not yet given asked for, its default at Enter; an answer may give
    // others as NAME=VALUE, which aren't then asked for. Once all are given, done.
    const auto settings = transformSettings(op);
    for (std::size_t i = 0; i < settings.size(); ++i)
    {
        const std::string name{settings[i].name};
        if (std::ranges::find(answers, name, &Answers::value_type::first) != answers.end())
        {
            continue;
        }
        auto label = std::string{findTransform(op)->label};
        label.replace(label.find("{}"), 2, target.what);
        const auto answer = defaultAnswer(settings[i].answer, target, answers);
        label += ", " + std::string{settings[i].question} + " (" +
                 (answer.empty() ? std::string{settings[i].form} : answer) + "): ";
        askFor(
            std::move(label),
            [this, op, target, answers, name](const std::string& text) mutable
            {
                for (const auto& [option, given] : told(text, {}).options)
                {
                    answers.emplace_back(option, given);
                }
                answers.emplace_back(name, told(text, {}).amount);
                askSettings(op, target, std::move(answers));
            },
            answer);
        prompt_.answerCommand = op;
        prompt_.answerSetting = i;
        return;
    }
    // In the settings' order, so the last (a track's name, which may have spaces) comes last;
    // then any others given.
    std::string text;
    for (const auto& setting : settings)
    {
        const auto given =
            std::ranges::find(answers, std::string{setting.name}, &Answers::value_type::first);
        text += (setting.name.empty() ? given->second
                                      : std::string{setting.name} + "=" + given->second) +
                " ";
    }
    for (const auto& [name, value] : answers)
    {
        if (std::ranges::find(settings, std::string_view{name}, &TransformSetting::name) ==
            settings.end())
        {
            text += name + "=" + value + " ";
        }
    }
    (this->*findTransform(op)->apply)(op, target, text);
}

std::string Workspace::defaultAnswer(std::string_view answer, const OperatorTarget& target,
                                     const Answers& answers) const
{
    // A default worked out as it's asked: the tracks' grid, the pedalling settings, a new track
    // named after the first's and the side moved, the range's first note or the search's query.
    if (answer == "{grid}")
    {
        return gridOf(target);
    }
    if (answer == "{lift}" || answer == "{press}")
    {
        return std::to_string((answer == "{lift}" ? pedalLift_ : pedalPress_).count()) + "ms";
    }
    if (answer == "{track}")
    {
        const auto side =
            std::ranges::find(answers, std::string{"side"}, &Answers::value_type::first);
        const auto* first =
            target.parts.empty() ? nullptr : document_.song().track(target.parts[0].track);
        return (first != nullptr ? first->name : std::string{"Track"}) +
               (side != answers.end() && side->second == "below" ? " low" : " high");
    }
    if (answer == "{first}")
    {
        return firstPitch(target);
    }
    if (answer == "{search}")
    {
        return searching_.query ? searching_.text : std::string{};
    }
    return std::string{answer};
}

bool Workspace::askTransform(const TransformSpec& spec, const OperatorTarget& target)
{
    // As it asks: nothing, a key and then the rest, its settings in turn, or one question.
    switch (spec.asks)
    {
    case TransformSpec::Asks::Nothing:
        return (this->*spec.apply)(spec.name, target, {});
    case TransformSpec::Asks::Key:
        askKey(target, [this, op = std::string{spec.name}, target](const core::Key& key)
               { askKeyed(op, target, key); });
        return true;
    case TransformSpec::Asks::InTurn:
        askSettings(std::string{spec.name}, target, {});
        return true;
    case TransformSpec::Asks::Once:
        break;
    }
    const auto answer = defaultAnswer(spec.answer, target, {});
    auto label = std::string{spec.label};
    label.replace(label.find("{}"), 2, target.what);
    label += " (" + (answer.empty() ? std::string{spec.examples} : answer) + "): ";
    askFor(
        std::move(label), [this, name = std::string{spec.name}, target](const std::string& text)
        { applyTransform(name, target, text); }, answer);
    prompt_.answerCommand = spec.name;
    return true;
}

std::string Workspace::optionsCommand() const
{
    // The transform asking, or the one on the : line, for its settings.
    if (prompt_.answer)
    {
        return prompt_.answerCommand;
    }
    const auto text = encodeUtf8(prompt_.line.text());
    const auto start = text.find_first_not_of(' ');
    if (start == std::string::npos)
    {
        return {};
    }
    const auto* info = commands_.find(text.substr(start, text.find(' ', start) - start));
    return info != nullptr ? std::string{info->name} : std::string{};
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
        refuse("No keys start the transforms, for a menu of them", MessageKind::Warning);
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
    const auto [motion, count] =
        selection_ ? selectionRange(name) : std::pair{std::string{"operator-line"}, 0};
    auto target = view().target(motion, count);
    if (!target)
    {
        return {true};
    }
    if (const auto range = view().cutRange(motion, count))
    {
        target->span = std::pair{range->from, range->to};
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

bool Workspace::velocityOverTime(std::string_view name, const OperatorTarget& target,
                                 const std::string& text)
{
    using Kind = core::VelocityDynamics::Kind;
    using Centre = core::VelocityDynamics::Centre;
    const auto kind = name.ends_with("compress") ? Kind::Compress
                      : name.ends_with("expand") ? Kind::Expand
                                                 : Kind::Shape;
    const auto word = kind == Kind::Compress ? std::string{"Compress"}
                      : kind == Kind::Expand ? std::string{"Expand"}
                                             : std::string{"Phrase"};
    const auto said = told(text, name);
    const auto percent = percentUpTo(said.amount, kind == Kind::Compress ? 100 : 1000);
    if (!percent)
    {
        refuse("Not a percentage: " + said.amount +
                   (kind == Kind::Compress ? "; type 0% to 100%, as 50%"
                                           : "; type one up to 1000%, as 50%"),
               MessageKind::Warning);
        return true;
    }
    core::VelocityDynamics dynamics{.kind = kind, .percent = *percent};
    // The window: bars (1bar, 2bars) at the note's meter, or a note length (1/2).
    std::string window = "1bar";
    for (const auto& [option, value] : said.options)
    {
        if (option == "window")
        {
            window = value;
        }
        else if (option == "centre" || option == "center")
        {
            const auto fixed = percentUpTo(value, 127);
            if (value == "local" || value == "average" || (fixed && *fixed > 0))
            {
                dynamics.centre = value == "local"     ? Centre::Local
                                  : value == "average" ? Centre::Average
                                                       : Centre::Fixed;
                dynamics.fixed = fixed.value_or(64);
                continue;
            }
            refuse("Not a centre: " + value + "; type local, average or a velocity",
                   MessageKind::Warning);
            return true;
        }
        else if (option == "threshold" && value == "all")
        {
            continue;
        }
        else if (option == "threshold")
        {
            const auto level = value.size() > 1 ? percentUpTo(value.substr(1), 127) : std::nullopt;
            if (!level || (value[0] != '>' && value[0] != '<'))
            {
                refuse("Not a threshold: " + value + "; type all, >90 or <30",
                       MessageKind::Warning);
                return true;
            }
            (value[0] == '>' ? dynamics.above : dynamics.below) = *level;
        }
        else
        {
            refuse(word + " takes window=, centre= and threshold=, not " + option + "=",
                   MessageKind::Warning);
            return true;
        }
    }
    std::optional<std::int64_t> bars;
    std::optional<core::NoteFraction> length;
    if (window.ends_with("bars") || window.ends_with("bar"))
    {
        const auto count = window.substr(0, window.find("bar"));
        bars = count.empty() ? 1 : percentUpTo(count, 64).value_or(0);
    }
    else
    {
        length = core::parseNoteFraction(window);
    }
    if ((bars && *bars < 1) || (!bars && !length))
    {
        refuse("Not a window: " + window + "; type 1bar, 2bars or a note length, as 1/2",
               MessageKind::Warning);
        return true;
    }
    const auto& song = document_.song();
    const auto limits = velocityLimits_;
    return changeEvents((kind == Kind::Shape ? "Scale the phrase dynamics of " + target.what
                                             : word + " the velocities of " + target.what) +
                            " by " + std::to_string(*percent) + "%",
                        target,
                        [&](const core::EventList& events, const auto& part)
                        {
                            auto those = dynamics;
                            const auto& meter = song.meter(part.track);
                            those.window = [&](core::Tick tick)
                            {
                                return bars ? *bars * meter.signatureAt(tick).barLength().toTicks(
                                                          song.ppqn())
                                            : length->toTicks(song.ppqn());
                            };
                            return core::velocityDynamics(events, part.events, those, limits);
                        });
}

bool Workspace::velocityInChords(std::string_view name, const OperatorTarget& target,
                                 const std::string& text)
{
    const auto said = told(text, name);
    if (!said.options.empty())
    {
        refuse("The chord tools take no options, as " + said.options.front().first + "=",
               MessageKind::Warning);
        return true;
    }
    const auto how = name.ends_with("accent")     ? core::ChordVelocity::Accent
                     : name.ends_with("balance")  ? core::ChordVelocity::Balance
                     : name.ends_with("compress") ? core::ChordVelocity::Compress
                                                  : core::ChordVelocity::Expand;
    core::VelocityRamp amount;
    std::string title;
    if (how == core::ChordVelocity::Balance)
    {
        const auto range = core::parseVelocityRange(said.amount, false);
        if (!range)
        {
            refuse("Not a range of velocities: " + said.amount +
                       "; type the bottom's and the top's, as 70-95",
                   MessageKind::Warning);
            return true;
        }
        amount = *range;
        title = "Balance the chords in " + target.what + " from " + std::to_string(range->from) +
                " to " + std::to_string(range->to);
    }
    else
    {
        const bool accent = how == core::ChordVelocity::Accent;
        const auto value = accent ? semitones(said.amount) : percentUpTo(said.amount, 100);
        if (!value || *value < 0)
        {
            refuse(accent ? "Not an amount: " + said.amount + "; type one up to 127, as 10"
                          : "Not a percentage: " + said.amount + "; type 0% to 100%, as 50%",
                   MessageKind::Warning);
            return true;
        }
        amount.from = *value;
        title =
            accent ? "Accent the top notes of the chords in " + target.what + " by " +
                         std::to_string(*value)
            : how == core::ChordVelocity::Compress
                ? "Compress the chords in " + target.what + " by " + std::to_string(*value) + "%"
                : "Expand the chords in " + target.what + " by " + std::to_string(*value) + "%";
    }
    const auto& song = document_.song();
    const auto window = chordWindow_;
    const auto together = [&](core::Tick first, core::Tick tick)
    {
        const auto& tempo = song.tempo();
        return tempo.toTime(tick) - tempo.toTime(first) <= window;
    };
    const auto limits = velocityLimits_;
    return changeEvents(
        title, target, [&](const core::EventList& events, const auto& part)
        { return core::chordVelocity(events, part.events, together, how, amount, limits); });
}

std::vector<core::Scale> Workspace::scales() const
{
    std::vector<core::Scale> all{core::builtInScales().begin(), core::builtInScales().end()};
    all.insert(all.end(), userScales_.begin(), userScales_.end());
    return all;
}

std::string Workspace::firstPitch(const OperatorTarget& target) const
{
    // The earliest note's, the lowest of a chord, for inverting about by default.
    std::optional<std::pair<core::Tick, int>> first;
    for (const auto& part : target.parts)
    {
        const auto& events = document_.song().events(part.track);
        for (const auto i : part.events)
        {
            if (const auto* note = std::get_if<core::Note>(&events[i].data))
            {
                first = std::min(first.value_or(std::pair{events[i].tick, int{note->pitch}}),
                                 std::pair{events[i].tick, int{note->pitch}});
            }
        }
    }
    return core::pitchName(static_cast<std::uint8_t>(first ? first->second : 60));
}

void Workspace::askKey(const OperatorTarget& target, std::function<void(const core::Key&)> then)
{
    // In a line of its own, so Up and Down go through the keys used before. The key the notes
    // fit best offered at Enter, unless they're too few to tell, then the last used; Tab at
    // the start lists the best few (decided November 2026).
    constexpr std::size_t kEnoughToTell = 6;
    std::vector<core::Sounding> notes;
    for (const auto& part : target.parts)
    {
        const auto& events = document_.song().events(part.track);
        for (const auto i : part.events)
        {
            if (const auto* note = std::get_if<core::Note>(&events[i].data))
            {
                notes.push_back({note->pitch, note->duration});
            }
        }
    }
    keySuggestions_.clear();
    if (notes.size() >= kEnoughToTell)
    {
        for (const auto& key : core::suggestKeys(notes, scales()))
        {
            keySuggestions_.push_back(key.text());
        }
    }
    const auto offered = keySuggestions_.empty() ? lastKey_ : keySuggestions_.front();
    std::swap(prompt_.line, prompt_.keyLine);
    askFor(
        "Key for " + target.what + " (" + offered + "): ",
        [this, then = std::move(then)](const std::string& text)
        {
            const auto key = core::parseKey(text, scales());
            if (!key)
            {
                refuse("Not a key: " + text +
                           "; type a tonic and a scale, as D dorian, or a tonic alone for "
                           "major, as Eb",
                       MessageKind::Warning);
                return;
            }
            lastKey_ = key->text();
            then(*key);
        },
        offered);
    prompt_.keyPrompt = true;
}

void Workspace::askKeyed(const std::string& op, const OperatorTarget& target, const core::Key& key)
{
    const bool invert = op == "transform-invert-key";
    const auto first = firstPitch(target);
    askFor((invert ? "Invert " : "Transpose ") + target.what + " in " + key.text() +
               (invert ? " about (" + first + "): " : " by how many degrees (+2, -3): "),
           [this, op, target, key](const std::string& text) { applyKeyed(op, target, key, text); },
           invert ? first : std::string{});
}

bool Workspace::applyKeyed(std::string_view op, const OperatorTarget& target, const core::Key& key,
                           const std::string& text)
{
    if (op == "transform-invert-key")
    {
        const auto pivot = core::parsePitch(trimmed(text));
        if (!pivot)
        {
            refuse("Not a note: " + text + "; type one to invert about, as E4",
                   MessageKind::Warning);
            return true;
        }
        return changeEvents("Invert " + target.what + " about " + core::pitchName(*pivot) + " in " +
                                key.text(),
                            target, [&](const core::EventList& events, const auto& part)
                            { return core::invertInKey(events, part.events, key, *pivot); });
    }
    const auto by = semitones(text);
    if (!by)
    {
        refuse("Not a number of degrees: " + text + "; type +2, -3 or 7", MessageKind::Warning);
        return true;
    }
    return changeEvents("Transpose " + target.what + (*by >= 0 ? " up " : " down ") +
                            times(std::abs(*by), "a degree", "degrees") + " in " + key.text(),
                        target, [&](const core::EventList& events, const auto& part)
                        { return core::transposeInKey(events, part.events, key, *by); });
}

bool Workspace::applyTransform(std::string_view name, const OperatorTarget& target,
                               const std::string& text)
{
    // A transform with what it's been told, the answer to its question or what follows its
    // name at the : prompt: those asking in turn asked for the rest; the others done.
    const auto* spec = findTransform(name);
    if (spec == nullptr)
    {
        return false;
    }
    if (spec->asks == TransformSpec::Asks::InTurn)
    {
        // A track's name, which may have spaces, is the rest of the line after its NAME= (found
        // in a code review, November 2026).
        Answers answers;
        auto rest = text;
        for (const auto& setting : spec->settings)
        {
            const auto at =
                setting.tracks ? rest.find(std::string{setting.name} + "=") : std::string::npos;
            if (at != std::string::npos)
            {
                answers.emplace_back(setting.name, trimmed(std::string_view{rest}.substr(
                                                       at + setting.name.size() + 1)));
                rest.erase(at);
            }
        }
        const auto said = told(rest, {});
        for (const auto& option : said.options)
        {
            answers.push_back(option);
        }
        if (!said.amount.empty())
        {
            answers.emplace_back("", said.amount);
        }
        askSettings(std::string{name}, target, std::move(answers));
        return true;
    }
    return (this->*spec->apply)(name, target, text);
}

bool Workspace::applyKeyLine(std::string_view name, const OperatorTarget& target,
                             const std::string& text)
{
    // key= with a tonic, and a scale if the next word names one; asking for it if not
    // given, and then for the amount if that isn't either.
    std::string rest = text;
    std::optional<core::Key> key;
    if (const auto at = rest.find("key="); at != std::string::npos)
    {
        const auto tonicEnd = std::min(rest.find(' ', at), rest.size());
        auto keyText = rest.substr(at + 4, tonicEnd - at - 4);
        auto end = tonicEnd;
        const auto scaleStart = rest.find_first_not_of(' ', tonicEnd);
        if (scaleStart != std::string::npos)
        {
            const auto scaleEnd = std::min(rest.find(' ', scaleStart), rest.size());
            const auto scale = rest.substr(scaleStart, scaleEnd - scaleStart);
            const auto all = scales();
            if (core::findScale(scale, all) != nullptr)
            {
                keyText += " " + scale;
                end = scaleEnd;
            }
        }
        key = core::parseKey(keyText, scales());
        if (!key)
        {
            refuse("Not a key: " + keyText +
                       "; type key= and a tonic and a scale, as "
                       "key=D dorian",
                   MessageKind::Warning);
            return true;
        }
        lastKey_ = key->text();
        rest.erase(at, end - at);
    }
    const auto amount = std::string{trimmed(rest)};
    const auto then = [this, op = std::string{name}, target, amount](const core::Key& k)
    {
        if (amount.empty())
        {
            askKeyed(op, target, k);
            return;
        }
        applyKeyed(op, target, k, amount);
    };
    if (key)
    {
        then(*key);
        return true;
    }
    askKey(target, then);
    return true;
}

bool Workspace::applyInvert(std::string_view /*name*/, const OperatorTarget& target,
                            const std::string& text)
{
    const auto pivot = core::parsePitch(trimmed(text));
    if (!pivot)
    {
        refuse("Not a note: " + text + "; type one to invert about, as E4", MessageKind::Warning);
        return true;
    }
    return changeEvents("Invert " + target.what + " about " + core::pitchName(*pivot), target,
                        [&](const core::EventList& events, const auto& part)
                        { return core::invert(events, part.events, *pivot); });
}

bool Workspace::applyStretch(std::string_view /*name*/, const OperatorTarget& target,
                             const std::string& text)
{
    // A ratio: 2, 1/2, 3/2 or 150%.
    auto ratio = std::string{trimmed(text)};
    std::int64_t numerator = 0;
    std::int64_t denominator = 1;
    const auto whole = [](std::string_view digits, std::int64_t& value)
    {
        const auto [end, error] =
            std::from_chars(digits.data(), digits.data() + digits.size(), value);
        return !digits.empty() && error == std::errc{} && end == digits.data() + digits.size();
    };
    bool read = false;
    if (ratio.ends_with('%'))
    {
        denominator = 100;
        read = whole(std::string_view{ratio}.substr(0, ratio.size() - 1), numerator);
    }
    else if (const auto slash = ratio.find('/'); slash != std::string::npos)
    {
        read = whole(std::string_view{ratio}.substr(0, slash), numerator) &&
               whole(std::string_view{ratio}.substr(slash + 1), denominator);
    }
    else
    {
        read = whole(ratio, numerator);
    }
    // In its lowest terms, checked before anything's multiplied (found overflowing in a code
    // review, November 2026).
    if (read && numerator >= 1 && denominator >= 1)
    {
        const auto divisor = std::gcd(numerator, denominator);
        numerator /= divisor;
        denominator /= divisor;
    }
    if (!read || numerator < 1 || denominator < 1 || numerator > core::kMaxStretchTerm ||
        denominator > core::kMaxStretchTerm || numerator > core::kMaxStretch * denominator ||
        denominator > core::kMaxStretch * numerator)
    {
        refuse("Not a ratio: " + text +
                   "; type 2 for half time, 1/2 for double time, or a percentage, as "
                   "150%",
               MessageKind::Warning);
        return true;
    }
    // About the range's first event, on every track alike, so the tracks keep in time with
    // each other (found in a code review, November 2026).
    const auto& song = document_.song();
    std::optional<core::Tick> about;
    for (const auto& part : target.parts)
    {
        const auto& events = song.events(part.track);
        for (const auto i : part.events)
        {
            if (!target.notesOnly || events[i].isNote())
            {
                about = std::min(about.value_or(events[i].tick), events[i].tick);
            }
        }
    }
    if (!about)
    {
        refuse("Nothing to stretch in " + target.what, MessageKind::Information);
        return true;
    }
    const auto end = song.end();
    return changeEvents("Stretch " + target.what + " by " + ratio, target,
                        [&](const core::EventList& events, const auto& part)
                        {
                            return core::stretch(events, part.events, numerator, denominator,
                                                 *about, end, target.notesOnly);
                        });
}

bool Workspace::applyLength(std::string_view name, const OperatorTarget& target,
                            const std::string& text)
{
    // legato, a length (1/8) or a percentage (80%).
    const auto said =
        name == "transform-legato" ? std::string{"legato"} : std::string{trimmed(text)};
    const auto& song = document_.song();
    auto how = core::LengthChange::Legato;
    core::Tick amount = 0;
    std::string title = "Make " + target.what + " legato";
    if (said.ends_with('%'))
    {
        const auto percent = percentUpTo(said, 1000);
        if (!percent || *percent < 1)
        {
            refuse("Not a percentage: " + said + "; type one from 1% up, as 80%",
                   MessageKind::Warning);
            return true;
        }
        how = core::LengthChange::Percent;
        amount = *percent;
        title = "Scale the lengths of " + target.what + " to " + said;
    }
    else if (said != "legato")
    {
        const auto length = core::parseNoteFraction(said);
        if (!length)
        {
            refuse("Not a length: " + said +
                       "; type legato, a length, as 1/8, or a "
                       "percentage, as 80%",
                   MessageKind::Warning);
            return true;
        }
        how = core::LengthChange::Fixed;
        amount = length->toTicks(song.ppqn());
        title = "Set the lengths of " + target.what + " to " + core::toString(*length);
    }
    const auto window = chordWindow_;
    const auto together = [&](core::Tick first, core::Tick tick)
    {
        const auto& tempo = song.tempo();
        return tempo.toTime(tick) - tempo.toTime(first) <= window;
    };
    return changeEvents(
        title, target, [&](const core::EventList& events, const auto& part)
        { return core::changeLengths(events, part.events, how, amount, together); });
}

bool Workspace::applyFilter(std::string_view /*name*/, const OperatorTarget& target,
                            const std::string& text)
{
    const auto parsed = core::parseQuery(text, scales());
    if (!parsed.query)
    {
        refuse(parsed.error, MessageKind::Warning);
        return true;
    }
    const auto ppqn = document_.song().ppqn();
    return changeEvents(
        "Delete the events of " + target.what + " matching " + std::string{trimmed(text)}, target,
        [&](const core::EventList& events, const auto& part)
        {
            return core::removed(events, part.events, [&](const core::Event& event)
                                 { return core::matches(*parsed.query, event.data, ppqn); });
        });
}

bool Workspace::applyRetrograde(std::string_view /*name*/, const OperatorTarget& target,
                                const std::string& /*text*/)
{
    return changeEvents("Retrograde " + target.what, target,
                        [](const core::EventList& events, const auto& part)
                        { return core::retrograde(events, part.events); });
}

bool Workspace::applyTranspose(std::string_view /*name*/, const OperatorTarget& target,
                               const std::string& text)
{
    const auto by = semitones(text);
    if (!by)
    {
        refuse("Not a number of semitones: " + text + "; type +12, -3 or 7", MessageKind::Warning);
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

bool Workspace::applyLift(std::string_view name, const OperatorTarget& target,
                          const std::string& text)
{
    const auto limits = velocityLimits_;
    const bool lift = name == "transform-velocity-lift";
    const auto percent = core::parsePercent(text);
    if (!percent)
    {
        refuse("Not a percentage: " + text + "; type 0% to 100%, as 25%", MessageKind::Warning);
        return true;
    }
    return changeEvents((lift ? "Lift the velocities of " : "Lower the velocities of ") +
                            target.what + " by " + std::to_string(*percent) + "%",
                        target,
                        [&](const core::EventList& events, const auto& part)
                        {
                            return lift
                                       ? core::liftVelocity(events, part.events, *percent, limits)
                                       : core::lowerVelocity(events, part.events, *percent, limits);
                        });
}

bool Workspace::applyLimit(std::string_view /*name*/, const OperatorTarget& target,
                           const std::string& text)
{
    const auto limits = velocityLimits_;
    const auto range = core::parseVelocityRange(text, false);
    if (!range || range->from > range->to)
    {
        refuse("Not a range of velocities: " + text +
                   "; type the lowest and the "
                   "highest, as 20-110",
               MessageKind::Warning);
        return true;
    }
    return changeEvents(
        "Keep the velocities of " + target.what + " between " + std::to_string(range->from) +
            " and " + std::to_string(range->to),
        target, [&](const core::EventList& events, const auto& part)
        { return core::limitVelocity(events, part.events, range->from, range->to, limits); });
}

bool Workspace::applyRamp(std::string_view /*name*/, const OperatorTarget& target,
                          const std::string& text)
{
    const auto limits = velocityLimits_;
    const auto ramp = core::parseVelocityRange(text, true);
    if (!ramp)
    {
        refuse("Not a ramp: " + text +
                   "; type the velocities at its start and end, as "
                   "60-100, or percentages, as 80%-120%",
               MessageKind::Warning);
        return true;
    }
    const auto unit = ramp->percent ? std::string{"%"} : std::string{};
    return changeEvents("Ramp the velocities of " + target.what + " from " +
                            std::to_string(ramp->from) + unit + " to " + std::to_string(ramp->to) +
                            unit,
                        target, [&](const core::EventList& events, const auto& part)
                        { return core::rampVelocity(events, part.events, *ramp, limits); });
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
    if (!target->span && motion != "preset")
    {
        if (const auto range = view().cutRange(motion, count))
        {
            target->span = std::pair{range->from, range->to};
        }
    }
    if (op == "operator-transform")
    {
        return openTransformMenu(std::move(*target));
    }
    if (const auto* spec = findTransform(op))
    {
        return askTransform(*spec, *target);
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
            "Shift " + target->what + (later ? " later" : " earlier") + " by " +
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
                       refuse("Not a velocity: " + text +
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
                                [c = *change, limits = velocityLimits_](
                                    const core::EventList& events, const auto& part)
                                { return core::changeVelocity(events, part.events, c, limits); });
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
        // With pedals there, as quantizepedal says: going with their notes, staying where they
        // are, or asking (decided October 2026; the setting November 2026).
        const bool pedalled =
            std::ranges::any_of(target->parts, [&](const OperatorTarget::Part& part)
                                { return core::hasPedals(song.events(part.track), part.events); });
        if (!pedalled || quantizePedal_ != QuantizePedal::Ask)
        {
            return quantize(*target, pedalled && quantizePedal_ == QuantizePedal::Follow);
        }
        ask(
            "Move the pedalling with its notes?", [this, target = *target]
            { quantize(target, true); }, [this, target = *target] { quantize(target, false); });
        return true;
    }
    return false;
}

bool Workspace::quantize(const OperatorTarget& target, bool pedals, const Quantizing& how)
{
    using Move = core::QuantizeHow::Move;
    const auto& song = document_.song();
    std::string title = "Quantise " + target.what;
    if (how.grid)
    {
        title += " to " + core::toString(*how.grid);
    }
    if (how.move != Move::Starts)
    {
        title += how.move == Move::Ends   ? ", their ends"
                 : how.move == Move::Both ? ", their starts and ends"
                                          : ", their lengths";
    }
    if (how.strength != 100)
    {
        title += ", " + std::to_string(how.strength) + "% of the way";
    }
    if (how.swing != 50)
    {
        title += ", swung " + std::to_string(how.swing) + "%";
    }
    title += pedals ? ", with its pedalling" : "";
    return changeEvents(
        title, target,
        [&](const core::EventList& events, const auto& part)
        {
            const auto& meter = song.meter(part.track);
            const auto step = std::max<core::Tick>(
                how.grid.value_or(song.track(part.track)->grid).toTicks(song.ppqn()), 1);
            const auto nearest = [&](core::Tick tick)
            {
                // Not onto the song's end, where nothing can start.
                const auto to = core::nearestGridLine(meter, tick, step, how.swing);
                return to < song.end() ? to : tick;
            };
            // An end may go onto it, as a note can end there; a start may not, whatever moves
            // (found in a code review, November 2026).
            const auto nearestEnd = [&](core::Tick tick)
            { return core::nearestGridLine(meter, tick, step, how.swing); };
            return core::quantize(events, part.events, nearest, pedals,
                                  {.strength = how.strength,
                                   .move = how.move,
                                   .step = [step](core::Tick) { return step; },
                                   .nearestEnd = nearestEnd});
        });
}

bool Workspace::thinAsked(std::string_view /*name*/, const OperatorTarget& target,
                          const std::string& text)
{
    const auto said = told(text, {});
    core::ThinWhat what;
    const auto& which = said.amount;
    if (which == "bend")
    {
        what.kind = core::ThinWhat::Kind::Bend;
    }
    else if (which == "aftertouch")
    {
        what.kind = core::ThinWhat::Kind::Aftertouch;
    }
    else if (which.starts_with("cc"))
    {
        const auto controller = percentUpTo(std::string_view{which}.substr(2), 127);
        if (!controller || which.ends_with('%'))
        {
            refuse("Not a controller: " + which + "; type cc and its number, as cc1",
                   MessageKind::Warning);
            return true;
        }
        if (core::isSwitchController(*controller))
        {
            refuse(which + " is a switch, as the pedals are, which thinning leaves alone; "
                           "the pedalling tools clean them up",
                   MessageKind::Warning);
            return true;
        }
        what = {core::ThinWhat::Kind::Controller, *controller};
    }
    else if (which != "all")
    {
        refuse("Not what to thin: " + which + "; type all, bend, aftertouch or cc1",
               MessageKind::Warning);
        return true;
    }
    int per = 16;
    int step = 2;
    for (const auto& [option, value] : said.options)
    {
        const auto number = percentUpTo(value, option == "per" ? 96 : 64);
        if ((option != "per" && option != "step") || !number || value.ends_with('%') ||
            (option == "per" && *number < 1))
        {
            refuse(option == "per"    ? "Not how many a quarter note: " + value + "; type 1 to 96"
                   : option == "step" ? "Not a change: " + value + "; type 0 to 64"
                                      : "Thin takes per= and step=, not " + option + "=",
                   MessageKind::Warning);
            return true;
        }
        (option == "per" ? per : step) = *number;
    }
    const auto q = document_.song().ppqn().quarter();
    return changeEvents("Thin the " + (which == "all" ? std::string{"controllers"} : which) +
                            " of " + target.what + " to " + std::to_string(per) + " a quarter note",
                        target,
                        [&](const core::EventList& events, const auto& part)
                        {
                            return core::thin(events, part.events, what,
                                              std::max<core::Tick>(q / per, 1), step, q);
                        });
}

bool Workspace::splitAsked(std::string_view /*name*/, const OperatorTarget& target,
                           const std::string& given)
{
    const auto said = told(given, {});
    std::string to;
    const auto at = core::parsePitch(trimmed(said.amount));
    if (!at)
    {
        refuse("Not a note: " + said.amount + "; type one to split at, as C4",
               MessageKind::Warning);
        return true;
    }
    bool up = true;
    for (const auto& [option, value] : said.options)
    {
        if (option == "side" && (value == "above" || value == "below"))
        {
            up = value == "above";
        }
        else if (option == "to")
        {
            to = value;
        }
        else
        {
            refuse(option == "side" ? "Not a side: " + value + "; type above or below"
                                    : "Split takes side= and to=, not " + option + "=",
                   MessageKind::Warning);
            return true;
        }
    }
    if (to.empty())
    {
        refuse("Split needs a track to move the notes to", MessageKind::Warning);
        return true;
    }
    // The track of that name, ignoring case, or a new one after the first's, playing as it does.
    const auto& song = document_.song();
    const auto lower = [](std::string text)
    {
        std::ranges::transform(text, text.begin(),
                               [](char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    std::optional<core::TrackId> existing;
    std::size_t position = 0;
    for (std::size_t i = 0; i < song.tracks().size(); ++i)
    {
        const auto& track = *song.tracks()[i];
        if (lower(track.name) == lower(to))
        {
            existing = track.id();
        }
        if (!target.parts.empty() && track.id() == target.parts[0].track)
        {
            position = i + 1;
        }
    }
    if (existing && std::ranges::any_of(target.parts, [&](const OperatorTarget::Part& part)
                                        { return part.track == *existing; }))
    {
        refuse("The notes are on " + to + " already: split them to another track",
               MessageKind::Warning);
        return true;
    }
    const auto moves = [&](const core::Event& event)
    {
        const auto* note = std::get_if<core::Note>(&event.data);
        return note != nullptr && (up ? note->pitch >= *at : note->pitch < *at);
    };
    std::size_t count = 0;
    for (const auto& part : target.parts)
    {
        const auto& events = song.events(part.track);
        count += static_cast<std::size_t>(
            std::ranges::count_if(part.events, [&](std::size_t i) { return moves(events[i]); }));
    }
    if (count == 0)
    {
        refuse(std::string{"No notes "} + (up ? "at or above " : "below ") + core::pitchName(*at) +
                   " to move",
               MessageKind::Information);
        return true;
    }
    const auto what = count == 1 ? std::string{"1 note"} : std::to_string(count) + " notes";
    const auto title = "Move " + what + (up ? " at or above " : " below ") + core::pitchName(*at) +
                       " to " + (existing ? song.track(*existing)->name : to);
    document_.edit(title, view().place(),
                   [&](core::Song& edited)
                   {
                       auto into = existing;
                       if (!into)
                       {
                           into = edited.addTrack(position, to);
                           const auto* first = song.track(target.parts[0].track);
                           edited.updateTrack(*into,
                                              [&](core::Track& track)
                                              {
                                                  track.port = first->port;
                                                  track.channel = first->channel;
                                                  track.grid = first->grid;
                                              });
                       }
                       std::vector<core::Event> moving;
                       for (const auto& part : target.parts)
                       {
                           edited.updateEvents(part.track,
                                               [&](core::EventList& events)
                                               {
                                                   for (auto it = part.events.rbegin();
                                                        it != part.events.rend(); ++it)
                                                   {
                                                       if (moves(events[*it]))
                                                       {
                                                           moving.push_back(events[*it]);
                                                           events.erase(*it);
                                                       }
                                                   }
                                               });
                       }
                       edited.updateEvents(*into, [&](core::EventList& events)
                                           { events = core::merged(events, std::move(moving)); });
                   });
    refresh();
    setMessage(title, MessageKind::Information);
    return true;
}

bool Workspace::remapAsked(std::string_view /*name*/, const OperatorTarget& target,
                           const std::string& text)
{
    const auto said = told(text, {});
    std::string to;
    for (const auto& [option, value] : said.options)
    {
        if (option != "to")
        {
            refuse("Remap takes to=, not " + option + "=", MessageKind::Warning);
            return true;
        }
        to = value;
    }
    const auto controller = [](std::string_view word) -> std::optional<int>
    {
        if (!word.starts_with("cc") || word.ends_with('%'))
        {
            return std::nullopt;
        }
        return percentUpTo(word.substr(2), 127);
    };
    const auto from = said.amount;
    if (const auto a = controller(from))
    {
        const auto b = controller(to);
        if (!b)
        {
            refuse("Not a controller: " + to + "; remap " + from + " to another, as cc11",
                   MessageKind::Warning);
            return true;
        }
        return changeEvents("Remap " + from + " of " + target.what + " to " + to, target,
                            [&](const core::EventList& events, const auto& part)
                            { return core::remapController(events, part.events, *a, *b); });
    }
    const auto a = core::parsePitch(trimmed(from));
    const auto b = core::parsePitch(trimmed(to));
    if (!a || !b)
    {
        refuse(!a ? "Not a controller or a note: " + from + "; type cc1 or C1"
                  : "Not a note: " + to + "; remap " + from + " to another, as D1",
               MessageKind::Warning);
        return true;
    }
    return changeEvents("Remap the " + core::pitchName(*a) + "s of " + target.what + " to " +
                            core::pitchName(*b),
                        target, [&](const core::EventList& events, const auto& part)
                        { return core::remapPitch(events, part.events, *a, *b); });
}

bool Workspace::pedalAsked(std::string_view op, const OperatorTarget& target,
                           const std::string& text)
{
    // Milliseconds, as typed: 10ms, 10, -20ms.
    const auto ms = [](std::string_view word) -> std::optional<std::chrono::milliseconds>
    {
        if (word.ends_with("ms"))
        {
            word.remove_suffix(2);
        }
        int value = 0;
        const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
        if (word.empty() || error != std::errc{} || end != word.data() + word.size() ||
            std::abs(value) > 10000)
        {
            return std::nullopt;
        }
        return std::chrono::milliseconds{value};
    };
    const auto said = told(text, {});
    auto liftBy = pedalLift_;
    auto pressBy = pedalPress_;
    std::chrono::milliseconds near{200};
    std::chrono::milliseconds flicker{30};
    for (const auto& [option, value] : said.options)
    {
        const auto time = ms(value);
        if (!time || (*time).count() < 0)
        {
            refuse("Not a time: " + value + "; type milliseconds, as 30ms", MessageKind::Warning);
            return true;
        }
        if (option == "lift")
        {
            liftBy = *time;
        }
        else if (option == "down")
        {
            pressBy = *time;
        }
        else if (option == "near")
        {
            near = *time;
        }
        else if (option == "flicker")
        {
            flicker = *time;
        }
        else
        {
            refuse("The pedalling tools don't take " + option + "=", MessageKind::Warning);
            return true;
        }
    }
    const auto& song = document_.song();
    const auto& tempo = song.tempo();
    const auto shifted = [&](core::Tick tick, std::chrono::milliseconds by)
    {
        const auto time = tempo.toTime(tick) + by;
        return time.count() < 0 ? core::Tick{0} : tempo.toTickExtended(time);
    };
    const core::PedalTiming timing{[&](core::Tick tick) { return shifted(tick, -liftBy); },
                                   [&](core::Tick tick) { return shifted(tick, pressBy); }};
    const auto window = chordWindow_;
    const auto together = [&](core::Tick first, core::Tick tick)
    { return tempo.toTime(tick) - tempo.toTime(first) <= window; };
    // The stretch: the range's time, or else from its first event to its last note's end.
    const auto stretchOf = [&](const core::EventList& events, const OperatorTarget::Part& part)
    {
        if (target.span)
        {
            return *target.span;
        }
        core::Tick from = song.end();
        core::Tick to = 0;
        for (const auto i : part.events)
        {
            const auto* note = std::get_if<core::Note>(&events[i].data);
            from = std::min(from, events[i].tick);
            to = std::max(to, events[i].tick + (note != nullptr ? note->duration : 0) + 1);
        }
        return std::pair{from, std::max(from, to)};
    };
    // What it does, for the undo step, and to each track's events.
    std::string title;
    std::function<core::Transformed(const core::EventList&, const OperatorTarget::Part&)> change;
    if (op == "transform-pedal-repedal" || op == "transform-pedal-each")
    {
        const bool each = op == "transform-pedal-each";
        const bool beats = each && said.amount == "beat";
        if (each && said.amount != "bar" && !beats)
        {
            refuse("Not bar or beat: " + said.amount, MessageKind::Warning);
            return true;
        }
        title = each ? "Pedal " + target.what + " afresh on each " + (beats ? "beat" : "bar")
                     : "Re-pedal " + target.what + " at its chord changes";
        change = [&, each, beats](const core::EventList& events, const OperatorTarget::Part& part)
        {
            const auto [from, to] = stretchOf(events, part);
            std::vector<core::Tick> changes;
            core::Tick end = to;
            if (each)
            {
                const auto& meter = song.meter(part.track);
                auto line = beats ? meter.beatAtOrAfter(from)
                                  : meter.barStart(meter.toBarBeatTick(from).bar);
                if (line < from)
                {
                    line = meter.barStart(meter.toBarBeatTick(from).bar + 1);
                }
                while (line < to)
                {
                    changes.push_back(line);
                    line = beats ? meter.beatAtOrAfter(line + 1)
                                 : meter.barStart(meter.toBarBeatTick(line).bar + 1);
                }
                end = timing.lift(to);
            }
            else
            {
                std::vector<std::size_t> notes;
                end = from;
                for (const auto i : part.events)
                {
                    if (const auto* note = std::get_if<core::Note>(&events[i].data))
                    {
                        notes.push_back(i);
                        end = std::max(end, events[i].tick + note->duration);
                    }
                }
                for (const auto& chord : core::chordsOf(events, notes, together))
                {
                    changes.push_back(core::chordStart(events, chord));
                }
            }
            return core::repedal(events, from, to, changes, end, timing);
        };
    }
    else if (op == "transform-pedal-retime")
    {
        title = "Re-time the pedalling of " + target.what;
        change = [&](const core::EventList& events, const OperatorTarget::Part& part)
        {
            const auto [from, to] = stretchOf(events, part);
            std::vector<std::size_t> notes;
            for (const auto i : part.events)
            {
                if (events[i].isNote())
                {
                    notes.push_back(i);
                }
            }
            std::vector<core::Tick> changes;
            for (const auto& chord : core::chordsOf(events, notes, together))
            {
                changes.push_back(core::chordStart(events, chord));
            }
            return core::retimePedal(events, from, to, changes, timing,
                                     [&](core::Tick tick) { return shifted(tick, near) - tick; });
        };
    }
    else if (op == "transform-pedal-clean")
    {
        title = "Clean up the pedalling of " + target.what;
        change = [&](const core::EventList& events, const OperatorTarget::Part& part)
        {
            const auto [from, to] = stretchOf(events, part);
            return core::cleanPedals(events, from, to, [&](core::Tick tick)
                                     { return shifted(tick, flicker) - tick; });
        };
    }
    else if (op == "transform-pedal-clear")
    {
        const std::array<std::pair<std::string_view, int>, 3> kNames{
            {{"sustain", 64}, {"sostenuto", 66}, {"soft", 67}}};
        std::vector<int> controllers;
        for (const auto& [name, controller] : kNames)
        {
            if (said.amount == name || said.amount == "all")
            {
                controllers.push_back(controller);
            }
        }
        if (controllers.empty())
        {
            refuse("Not a pedal: " + said.amount + "; type sustain, sostenuto, soft or all",
                   MessageKind::Warning);
            return true;
        }
        title = "Clear the " + (said.amount == "all" ? std::string{"pedalling"} : said.amount) +
                " of " + target.what;
        change = [&, controllers](const core::EventList& events, const OperatorTarget::Part& part)
        {
            const auto [from, to] = stretchOf(events, part);
            return core::clearPedals(events, from, to, controllers);
        };
    }
    else if (op == "transform-pedal-offset")
    {
        const auto by = ms(said.amount);
        if (!by)
        {
            refuse("Not a time: " + said.amount + "; type milliseconds, as 20ms or -20ms",
                   MessageKind::Warning);
            return true;
        }
        title =
            "Offset the pedalling of " + target.what + " by " + std::to_string(by->count()) + "ms";
        change = [&, by](const core::EventList& events, const OperatorTarget::Part& part)
        {
            const auto [from, to] = stretchOf(events, part);
            return core::offsetPedals(
                events, from, to,
                [&](core::Tick tick)
                {
                    // Before the song's start, as far before it as to be refused, not put at it
                    // (found in a code review, November 2026).
                    const auto time = tempo.toTime(tick) + *by;
                    return time.count() < 0 ? -tick - 1 : tempo.toTickExtended(time) - tick;
                },
                song.end());
        };
    }
    else
    {
        title = "Bake the sustain of " + target.what + " into its notes";
        change = [&](const core::EventList& events, const OperatorTarget::Part& part)
        {
            const auto [from, to] = stretchOf(events, part);
            return core::bakePedal(events, from, to);
        };
    }
    return changeEvents(title, target, change);
}

std::string Workspace::gridOf(const OperatorTarget& target) const
{
    // The tracks' grid, if they share one, or else each its own.
    const auto& song = document_.song();
    std::optional<core::NoteFraction> shared;
    for (const auto& part : target.parts)
    {
        const auto grid = song.track(part.track)->grid;
        if (shared && *shared != grid)
        {
            return "grid";
        }
        shared = grid;
    }
    return shared ? core::toString(*shared) : std::string{"grid"};
}

bool Workspace::quantizeAsked(std::string_view /*name*/, const OperatorTarget& target,
                              const std::string& text)
{
    using Move = core::QuantizeHow::Move;
    const auto said = told(text, {});
    Quantizing how;
    if (said.amount != "grid")
    {
        how.grid = core::parseNoteFraction(said.amount);
        if (!how.grid)
        {
            refuse("Not a grid: " + said.amount + "; type a note length, as 1/16, or grid",
                   MessageKind::Warning);
            return true;
        }
    }
    for (const auto& [option, value] : said.options)
    {
        if (option == "strength" || option == "swing")
        {
            const bool strength = option == "strength";
            const auto percent = percentUpTo(value, 100);
            if (!percent || (!strength && (*percent < 25 || *percent > 75)))
            {
                refuse(strength ? "Not a strength: " + value + "; type 0% to 100%"
                                : "Not a swing: " + value + "; type 25% to 75%, 50% straight",
                       MessageKind::Warning);
                return true;
            }
            (strength ? how.strength : how.swing) = *percent;
        }
        else if (option == "move")
        {
            const std::array<std::pair<std::string_view, Move>, 4> kMoves{
                {{"starts", Move::Starts},
                 {"ends", Move::Ends},
                 {"both", Move::Both},
                 {"lengths", Move::Lengths}}};
            const auto found =
                std::ranges::find(kMoves, value, &std::pair<std::string_view, Move>::first);
            if (found == kMoves.end())
            {
                refuse("Not what to move: " + value + "; type starts, ends, both or lengths",
                       MessageKind::Warning);
                return true;
            }
            how.move = found->second;
        }
        else
        {
            refuse("Quantise takes strength=, swing= and move=, not " + option + "=",
                   MessageKind::Warning);
            return true;
        }
    }
    // With pedals there and starts moving, asking whether they go with their notes, as q does.
    const auto& song = document_.song();
    const bool starts = how.move == Move::Starts || how.move == Move::Both;
    const bool pedalled =
        starts &&
        std::ranges::any_of(target.parts, [&](const OperatorTarget::Part& part)
                            { return core::hasPedals(song.events(part.track), part.events); });
    if (!pedalled)
    {
        return quantize(target, false, how);
    }
    ask(
        "Move the pedalling with its notes?", [this, target, how] { quantize(target, true, how); },
        [this, target, how] { quantize(target, false, how); });
    return true;
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
                refuse(refusalText(*refusal, song.meter(part.track)), MessageKind::Warning);
                return true;
            }
            changed.push_back(std::move(std::get<core::EventList>(result)));
            continue;
        }
        auto result = change(song.events(part.track), part);
        if (const auto* refusal = std::get_if<core::Refusal>(&result))
        {
            refuse(refusalText(*refusal, song.meter(part.track)), MessageKind::Warning);
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
