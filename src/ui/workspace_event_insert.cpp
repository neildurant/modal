// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/core/song_file.hpp>
#include <modal/ui/workspace.hpp>

#include <array>
#include <charconv>
#include <utility>

namespace modal::ui
{
namespace
{

struct EventKind
{
    std::string_view label;
    std::string_view command;
    std::string_view field;
    std::string_view second = {};
    std::optional<core::MetaText::Kind> text = std::nullopt;
};

constexpr std::array kEventKinds{
    EventKind{"Tempo", "tempo", "Tempo (1–1000 BPM): "},
    EventKind{"Program change", "program", "Program number (0–127): "},
    EventKind{"Control change", "cc", "Controller number (0–127): ", "Controller value (0–127): "},
    EventKind{"Pitch bend", "bend", "Pitch bend (−8192 to 8191): "},
    EventKind{"Channel pressure (aftertouch)", "pressure", "Channel pressure (0–127): "},
    EventKind{"Polyphonic pressure (aftertouch)", "polypressure",
              "Pitch (e.g. C4): ", "Polyphonic pressure (0–127): "},
    EventKind{"SysEx", "sysex", "SysEx hex bytes (F0 … F7): "},
    EventKind{"Text", "text", "Text: ", {}, core::MetaText::Kind::Text},
    EventKind{"Lyric", "lyric", "Lyric: ", {}, core::MetaText::Kind::Lyric},
    EventKind{"Cue", "cue", "Cue: ", {}, core::MetaText::Kind::CuePoint},
    EventKind{"Copyright", "copyright", "Copyright: ", {}, core::MetaText::Kind::Copyright},
    EventKind{"Instrument name",
              "instrument",
              "Instrument name: ",
              {},
              core::MetaText::Kind::InstrumentName},
};

} // namespace

bool Workspace::eventInsertChoiceKey(const Key& key)
{
    message_.clear();
    const auto match = keymap_.match(Mode::CommandLine, std::vector<Key>{key});
    const auto command =
        match.binding ? std::string_view{match.binding->command} : std::string_view{};
    if (command == "panic")
        return run("panic", 0).changed;
    if (command == "command-line-cancel")
        eventInsertChoice_.reset();
    else if (command == "command-line-run")
    {
        if (const auto kind = eventInsertChoice_->chosen())
        {
            eventInsertChoice_.reset();
            askEventField(*kind, view().cursor(), document_.version());
        }
    }
    else if (!eventInsertChoice_->run(command) && key.named == NamedKey::None &&
             (key.modifiers & (kCtrl | kAlt)) == 0)
        eventInsertChoice_->type(key.character);
    return true;
}

void Workspace::askEventField(std::size_t kind, const SongCursor& cursor, std::uint64_t version,
                              std::optional<std::string> first, bool retry)
{
    const auto& event = kEventKinds[kind];
    const auto label = first ? event.second : event.field;
    askFor((retry ? "Invalid input. " : "") + std::string{label},
           [this, kind, cursor, version, first = std::move(first)](const std::string& answer)
           {
               if (version != document_.version() || cursor != view().cursor())
               {
                   refuse("The insertion point changed; open the event chooser again");
                   return;
               }
               const auto& event = kEventKinds[kind];
               if (event.text)
               {
                   report(putEvent(cursor, core::makeMetaText(*event.text, answer)));
                   return;
               }
               const auto arguments = std::string{event.command} + " " +
                                      (first ? *first + " " : std::string{}) + answer;
               if (!first && !event.second.empty())
               {
                   // Validate the first field with an in-range value before asking for the second.
                   if (core::parseEvent(arguments + " 0").data)
                       askEventField(kind, cursor, version, answer);
                   else
                       askEventField(kind, cursor, version, std::nullopt, true);
                   return;
               }
               if (const auto result = insertEvent(arguments); result.refused)
                   askEventField(kind, cursor, version, first, true);
               else
                   report(result);
           },
           {}, true);
}

CommandResult Workspace::insertEvent(std::string_view text)
{
    const auto cursor = view().cursor();
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        std::vector<std::string> labels;
        const auto kinds =
            cursor.track ? std::span{kEventKinds}.subspan(0) : std::span{kEventKinds}.subspan(0, 1);
        for (const auto& kind : kinds)
            labels.emplace_back(kind.label);
        eventInsertChoice_.emplace(std::move(labels), 0, true);
        eventInsertChoice_->name("event type");
        return {true, {}, false};
    }
    text = text.substr(first, text.find_last_not_of(" \t") - first + 1);
    if (text == "tempo" || text.starts_with("tempo "))
    {
        // The conductor's, whichever track the cursor is on.
        auto number = text.substr(std::min(text.size(), std::string_view{"tempo"}.size()));
        number.remove_prefix(std::min(number.find_first_not_of(' '), number.size()));
        double bpm = 0;
        const auto [end, error] =
            std::from_chars(number.data(), number.data() + number.size(), bpm);
        if (error != std::errc{} || end != number.data() + number.size() || !(bpm >= 1) ||
            bpm > 1000)
        {
            return CommandResult::error("A tempo must be from 1 to 1000 BPM");
        }
        const auto tempo = core::Tempo::fromBpm(bpm);
        document_.edit(
            "Set tempo to " + core::bpmText(tempo), view().place(), [&](core::Song& song)
            { song.updateTempo([&](core::TempoMap& map) { map.set(cursor.tick, tempo); }); });
        view().refresh();
        return {true, "Tempo " + core::bpmText(tempo) + " from " + view().position(cursor.tick),
                false};
    }
    if (!cursor.track)
    {
        return CommandResult::error("The conductor holds only tempo and meter changes");
    }
    auto parsed = core::parseEvent(text);
    if (!parsed.data)
    {
        return CommandResult::error(parsed.error);
    }
    return putEvent(cursor, std::move(*parsed.data));
}

CommandResult Workspace::putEvent(const SongCursor& cursor, core::EventData data)
{
    if (!cursor.track)
        return CommandResult::error("The conductor holds only tempo and meter changes");
    const core::Event event{cursor.tick, std::move(data)};
    document_.edit("Insert an event", view().place(),
                   [&](core::Song& song)
                   {
                       song.updateEvents(*cursor.track,
                                         [&](core::EventList& events) { events.insert(event); });
                   });
    view().refresh();
    return {true, "Inserted at " + view().position(cursor.tick), false};
}

} // namespace modal::ui
