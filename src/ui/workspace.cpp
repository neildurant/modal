// SPDX-License-Identifier: GPL-3.0-or-later

#include "workspace_commands.hpp"

#include <modal/core/pitch.hpp>
#include <modal/core/song_file.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/completion.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/event_edit.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/key_notation.hpp>
#include <modal/ui/palette.hpp>
#include <modal/ui/settings.hpp>
#include <modal/ui/track_fields.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/view_state.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <map>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace modal::ui
{

namespace
{

// A register's letter, as its lower case.
char32_t lowerLetter(char32_t letter)
{
    return letter >= U'A' && letter <= U'Z' ? letter - U'A' + U'a' : letter;
}

bool isShared(std::string_view name)
{
    // The transforms are too, from their table.
    const auto shared = sharedCommands();
    return std::ranges::find(shared, name, &CommandInfo::name) != shared.end() ||
           Workspace::findTransform(name) != nullptr;
}

// The kind of selection v, V and Ctrl-v make.
SelectionKind selectionKind(std::string_view command)
{
    return command == "visual"        ? SelectionKind::Range
           : command == "visual-rows" ? SelectionKind::Rows
                                      : SelectionKind::Box;
}

} // namespace

core::NoteFraction Workspace::timingUnit(core::NoteFraction grid) const
{
    return settings_ ? core::parseNoteFraction(settings_->text("timingunit")).value_or(grid) : grid;
}

Keymap Workspace::defaultKeymap()
{
    Config config;
    CommandRegistry all;
    all.add(commands());
    all.add(windowCommands());
    ConfigReader reader{config, all};
    reader.runText(builtInModalrc(), "built-in modalrc", {});
    assert(reader.errors().empty()); // the tests run the built-in modalrc too
    return config.keymap;
}

Workspace::Workspace(CommandRegistry& registry, core::Document& document, Keymap keymap,
                     std::int64_t timeout)
    : commands_(registry)
    , document_(document)
    , eventList_(std::make_unique<EventListView>(document, static_cast<ViewHost&>(*this)))
    , arrange_(std::make_unique<ArrangeView>(document, static_cast<ViewHost&>(*this)))
    , pianoRoll_(std::make_unique<PianoRollView>(document, static_cast<ViewHost&>(*this)))
    , tracks_(std::make_unique<TracksView>(document, static_cast<ViewHost&>(*this)))
    , ports_(std::make_unique<PortsView>(document, static_cast<ViewHost&>(*this)))
    , current_(eventList_.get())
    , keymap_(std::move(keymap))
    , keys_(timeout)
{
    keys_.setTakesCharacter([this](std::string_view command) { return takesCharacter(command); });
    for (const auto& info : commands())
    {
        registry.add(info,
                     [this, name = info.name](const CommandCall& call) -> CommandResult
                     {
                         if (name == "port")
                         {
                             return port(call.arguments);
                         }
                         if (name == "event-insert")
                         {
                             return insertEvent(call.arguments);
                         }
                         if (name == "track-freeze")
                         {
                             return trackFreeze(call.arguments);
                         }
                         if (name == "section-insert-before" || name == "section-insert-after" ||
                             name == "section-join")
                         {
                             return sectionHere(name, call.count);
                         }
                         if (name == "section-split")
                         {
                             return section(call.arguments);
                         }
                         if (name == "mark-delete")
                         {
                             return deleteMarks(call.arguments);
                         }
                         if (name == "label")
                         {
                             return label(call.arguments);
                         }
                         if (name == "label-delete")
                         {
                             return deleteLabels();
                         }
                         if (name == "label-to-section")
                         {
                             return labelToSection();
                         }
                         if (name == "bar-delete")
                         {
                             return barDelete(call.count);
                         }
                         if (name == "bar-insert")
                         {
                             // From keys or the palette, asking how many and in what
                             // meter (decided November 2026), a count answering the first.
                             if (call.line.empty())
                             {
                                 return askBarInsert(call.count);
                             }
                             return barInsert(call.arguments);
                         }
                         if (name == "section-rename")
                         {
                             return renameSection(call.arguments);
                         }
                         if (name.starts_with("transform-") && !call.line.empty())
                         {
                             return transformLine(name, call.arguments);
                         }
                         if (auto result = view().runLine(name, call.arguments))
                         {
                             return *result;
                         }
                         if (mode_ == Mode::Visual)
                         {
                             if (const auto done = runVisual(name, call.count))
                             {
                                 return {*done};
                             }
                         }
                         if (name == "mark-go")
                         {
                             awaiting_ = Awaiting{std::string{name}, call.count};
                             return {true};
                         }
                         if (name == "find-next" || name == "find-previous")
                         {
                             return {startFind(name == "find-next", call.count)};
                         }
                         if (name == "find-repeat" || name == "find-repeat-back")
                         {
                             return {repeatFind(name == "find-repeat", call.count)};
                         }
                         if (name == "search-next" || name == "search-previous")
                         {
                             const bool next = name == "search-next";
                             if (!searching_.query)
                             {
                                 operator_.reset();
                                 mode_ = mode_ == Mode::OperatorPending ? Mode::Normal : mode_;
                                 refuse("No search yet: " + keysOf("search") + " searches",
                                        MessageKind::Information);
                                 return {true};
                             }
                             if (mode_ == Mode::OperatorPending)
                             {
                                 return {runOperator(next == searching_.forward
                                                         ? "cursor-match-next"
                                                         : "cursor-match-previous",
                                                     call.count)};
                             }
                             return {goToMatch(next, call.count)};
                         }
                         // After an operator, every command is its motion, or gives it up.
                         if (mode_ == Mode::OperatorPending)
                         {
                             return {runOperator(name, call.count)};
                         }
                         if (isShared(name))
                         {
                             return {runShared(name, call.count).value_or(false)};
                         }
                         return {runViewCommand(name, call.count)};
                     });
    }
    buildOperatorKeymap();
}

Workspace::~Workspace() = default;

void Workspace::setKeymap(Keymap keymap)
{
    keymap_ = std::move(keymap);
    buildOperatorKeymap();
}

const Keymap& Workspace::keymapHere() const
{
    resolveKeymaps();
    return keymapHere_;
}

const Keymap& Workspace::operatorKeymapHere() const
{
    resolveKeymaps();
    return operatorKeymapHere_;
}

void Workspace::resolveKeymaps() const
{
    // Keys bound to several commands mean the one that does something here (J joins sections
    // on the strip and goes down an octave in the piano roll).
    const auto here = where();
    if (resolvedFor_ == here)
    {
        return;
    }
    const auto offeredHere = [this](const std::string& command) { return offered(command); };
    keymapHere_ = keymap_.resolved(offeredHere);
    operatorKeymapHere_ = operatorKeymap_.resolved(offeredHere);
    resolvedFor_ = here;
}

void Workspace::buildOperatorKeymap()
{
    resolvedFor_.reset();
    operatorKeymap_ = keymap_;
    for (const auto& [keys, binding] : keymap_.bindings(Mode::Normal))
    {
        const auto* info = commands_.find(binding.command);
        // An operator's own key again takes the cursor's bar (dd, >>); another's is refused.
        const bool wanted =
            isOperator(binding.command) || binding.command == "cancel" || (info && info->motion);
        if (wanted && keymap_.match(Mode::OperatorPending, keys).binding == nullptr)
        {
            operatorKeymap_.bind(Mode::OperatorPending, keys, binding);
        }
    }
    // Visual mode's own keys, then the objects, which select what they take (vis), then
    // Normal mode's, but for keys that start an object's (i, a).
    const auto free = [&](const std::vector<Key>& keys)
    {
        const auto match = operatorKeymap_.match(Mode::Visual, keys);
        return match.binding == nullptr && !match.longer;
    };
    for (const auto& [keys, binding] : keymap_.bindings(Mode::OperatorPending))
    {
        const auto* info = commands_.find(binding.command);
        if (info != nullptr && info->object && free(keys))
        {
            operatorKeymap_.bind(Mode::Visual, keys, binding);
        }
    }
    // Every command a key is bound to, as in Normal mode, so the one that does something where
    // the cursor is runs (Enter opens bars on the tracks): free as Visual mode was before them.
    const auto before = operatorKeymap_;
    for (const auto& [keys, binding] : keymap_.bindings(Mode::Normal))
    {
        const auto match = before.match(Mode::Visual, keys);
        if (match.binding == nullptr && !match.longer)
        {
            operatorKeymap_.bind(Mode::Visual, keys, binding);
        }
    }
}

const std::vector<core::Event>& Workspace::registerEvents() const
{
    static const std::vector<core::Event> none;
    return register_.tracks.empty() ? none : register_.tracks.front();
}

std::string Workspace::pending() const
{
    std::string text = cutNext_ ? keysOf("cut-at-edges") : std::string{};
    if (registerName_)
    {
        text += keysOf("register-use") + encodeUtf8(std::u32string(1, *registerName_));
    }
    if (operator_)
    {
        text = (operator_->count > 0 ? std::to_string(operator_->count) : std::string{}) +
               keysOf(operator_->name);
    }
    if (awaiting_)
    {
        text += keysOf(awaiting_->command);
    }
    text += keys_.pending();
    if (text.empty() && finding_.naming && finding_.naming->redo)
    {
        // An operator's find that a sharp or flat may yet change (dfC).
        text =
            (finding_.naming->redo->op.count > 0 ? std::to_string(finding_.naming->redo->op.count)
                                                 : std::string{}) +
            keysOf(finding_.naming->redo->op.name) +
            keysOf(finding_.naming->forward ? "find-next" : "find-previous") + finding_.name;
    }
    return text;
}

void Workspace::resize(int width, int height, CellSize cell)
{
    // Called before every frame, so only a new size scrolls to the cursor, not playback.
    cell = {std::max(cell.width, 1), std::max(cell.height, 1)};
    if (width == width_ && height == height_ && cell.width == cell_.width &&
        cell.height == cell_.height)
    {
        return;
    }
    width_ = width;
    height_ = height;
    cell_ = cell;
    layOut();
}

void Workspace::layOut()
{
    if (width_ == 0)
    {
        return;
    }
    // The view's own bar and the inspector, each with half a line after it, then the view, and
    // the status line at the bottom.
    layout_ = layoutWindow(width_, height_, cell_, view().topHeights(cell_), 1, cell_.height / 2);
    view().resize(layout_, width_, cell_);
}

void Workspace::refresh()
{
    eventList_->refresh();
    arrange_->refresh();
    pianoRoll_->refresh();
    tracks_->refresh();
    ports_->refresh();
}

void Workspace::showSong()
{
    eventList_->showSong();
    arrange_->showSong();
    pianoRoll_->showSong();
    tracks_->showSong();
    ports_->showSong();
    history_.clear();
    layOut();
}

ViewKind Workspace::shown() const
{
    return current_ == arrange_.get()     ? ViewKind::Arrangement
           : current_ == tracks_.get()    ? ViewKind::Tracks
           : current_ == ports_.get()     ? ViewKind::Ports
           : current_ == pianoRoll_.get() ? ViewKind::PianoRoll
                                          : ViewKind::EventList;
}

void Workspace::show(ViewKind kind)
{
    switch (kind)
    {
    case ViewKind::Tracks:
        switchTo(*tracks_, true);
        break;
    case ViewKind::Arrangement:
        switchTo(*arrange_, true);
        break;
    case ViewKind::EventList:
        switchTo(*eventList_, true);
        break;
    case ViewKind::PianoRoll:
        switchTo(*pianoRoll_, true);
        break;
    case ViewKind::Ports:
        lookForPorts(); // as they may have come or gone since
        switchTo(*ports_, true);
        break;
    }
}

void Workspace::open(ViewKind kind)
{
    switchTo(kind == ViewKind::Tracks        ? static_cast<View&>(*tracks_)
             : kind == ViewKind::Ports       ? static_cast<View&>(*ports_)
             : kind == ViewKind::Arrangement ? static_cast<View&>(*arrange_)
             : kind == ViewKind::PianoRoll   ? static_cast<View&>(*pianoRoll_)
                                             : static_cast<View&>(*eventList_),
             true, true);
}

void Workspace::switchTo(View& view, bool remember, bool fresh)
{
    if (&view == current_)
    {
        if (fresh)
        {
            view.open(view.cursor());
        }
        return;
    }
    const auto cursor = current_->cursor();
    if (selection_)
        endVisual();
    current_->stopPlayhead(std::nullopt);
    if (remember)
    {
        history_.push_back(current_);
    }
    current_ = &view;
    view.refresh();
    layOut();
    if (fresh)
    {
        view.open(cursor);
    }
    else
    {
        view.setCursor(cursor);
    }
    if (wasPlaying_)
    {
        showPlayhead();
    }
}

bool Workspace::handle(const Key& key, std::int64_t now)
{
    // Keys typed while a macro records go into it as typed (decided October 2026, as Vim's
    // q), but for those of the binding that ends it, which it takes off: those since nothing
    // was waiting.
    if (macros_.recording != 0)
    {
        if (keys_.pending().empty())
        {
            macros_.recordedBefore = macros_.recorded.size();
        }
        macros_.recorded.push_back(key);
    }
    return recordChange(key, now);
}

bool Workspace::takesCharacter(std::string_view command) const
{
    return command == "register-use" || command == "mark-set" || command == "mark-go" ||
           command == "find-next" || command == "find-previous" || command == "macro-play" ||
           (command == "macro-record" && macros_.recording == 0);
}

bool Workspace::characterAfter(std::string_view command, const Key& key, int count)
{
    if (key.named == NamedKey::Escape)
    {
        if (operator_)
        {
            operator_.reset(); // d' Escape gives the operator up too
            mode_ = Mode::Normal;
        }
        return true;
    }
    if (key.named != NamedKey::None || (key.modifiers & (kCtrl | kAlt)) != 0)
    {
        refuse(keysFor(command) + (command.starts_with("find-") ? " takes a note name after it"
                                                                : " takes a letter after it"),
               MessageKind::Information);
        macros_.failed = macros_.depth > 0;
        if (operator_)
        {
            operator_.reset();
            mode_ = Mode::Normal;
        }
        return true;
    }
    return withCharacter(command, key.character, count);
}

bool Workspace::withCharacter(std::string_view command, char32_t character, int count)
{
    if (command == "mark-set")
    {
        return setMark(character);
    }
    if (command == "mark-go")
    {
        return goToMark(character, count);
    }
    if (command == "find-next" || command == "find-previous")
    {
        return nameFind(command, character, count);
    }
    if (command == "macro-record" || command == "macro-play")
    {
        const bool play = command == "macro-play";
        if (play && character == U'@')
        {
            if (macros_.last == 0)
            {
                refuse("No macro played yet to play again", MessageKind::Information);
                macros_.failed = macros_.depth > 0;
                return true;
            }
            return playMacro(macros_.last, count);
        }
        if (!((character >= U'a' && character <= U'z') || (character >= U'A' && character <= U'Z')))
        {
            refuse(play ? "Macros are named by letters, @a to @z, or @@ plays the last again"
                        : "Macros are named by letters, Qa to Qz, a capital adding to one",
                   MessageKind::Information);
            macros_.failed = macros_.depth > 0;
            return true;
        }
        const char32_t name = lowerLetter(character);
        if (play)
        {
            return playMacro(name, count);
        }
        macros_.recording = name;
        const auto* had = remembered_.macro(name);
        macros_.recorded = character != name && had != nullptr ? *had : std::vector<Key>{};
        macros_.recordedBefore = macros_.recorded.size();
        return true;
    }
    if (command == "register-use")
    {
        if (!((character >= U'a' && character <= U'z') || (character >= U'A' && character <= U'Z')))
        {
            refuse("Registers are named by letters, \"a to \"z, a capital adding to one",
                   MessageKind::Information);
            return true;
        }
        registerName_ = character;
        registerBefore_ = register_;
        return true;
    }
    return false;
}

EventRegister Workspace::appended(EventRegister to, const EventRegister& more)
{
    // Laid end to end (decided October 2026): after the span what's there took, its rests kept, or
    // without one, as from the event list's rows, at its last event's time; sections after
    // sections.
    if (to.empty())
    {
        return more;
    }
    if (!more.wholeTracks.empty() && !to.wholeTracks.empty())
    {
        to.wholeTracks.insert(to.wholeTracks.end(), more.wholeTracks.begin(),
                              more.wholeTracks.end());
        return to;
    }
    if (!more.sections.empty() || !to.sections.empty())
    {
        to.sections.insert(to.sections.end(), more.sections.begin(), more.sections.end());
        return to;
    }
    core::Tick offset = to.span;
    if (offset == 0)
    {
        for (const auto& events : to.tracks)
        {
            for (const auto& event : events)
            {
                offset = std::max(offset, event.tick);
            }
        }
    }
    to.tracks.resize(std::max(to.tracks.size(), more.tracks.size()));
    for (std::size_t k = 0; k < more.tracks.size(); ++k)
    {
        for (auto event : more.tracks[k])
        {
            event.tick += offset;
            to.tracks[k].push_back(std::move(event));
        }
    }
    to.span = more.span > 0 ? offset + more.span : 0;
    to.bars = to.bars && more.bars;
    return to;
}

std::string Workspace::registerAbout(const EventRegister& reg)
{
    if (!reg.wholeTracks.empty())
        return std::to_string(reg.wholeTracks.size()) + " whole tracks";
    // What a register holds: "12 events on 2 tracks", "2 sections, Verse, Chorus".
    if (!reg.sections.empty())
    {
        std::string names;
        for (const auto& section : reg.sections)
        {
            names += (names.empty() ? "" : ", ") + section->name();
        }
        return std::to_string(reg.sections.size()) +
               (reg.sections.size() == 1 ? " section, " : " sections, ") + names;
    }
    std::size_t events = 0;
    std::size_t tracks = 0;
    for (const auto& list : reg.tracks)
    {
        events += list.size();
        tracks += list.empty() ? 0 : 1;
    }
    return eventCount(events) + " on " + std::to_string(tracks) +
           (tracks == 1 ? " track" : " tracks");
}

bool Workspace::sayRegisters()
{
    // Each holding something, the unnamed first, one a line: "a : 12 events on 2 tracks.
    std::vector<std::pair<std::string, std::string>> lines;
    if (!register_.empty())
    {
        lines.emplace_back("\"\"", registerAbout(register_));
    }
    for (const auto& [name, reg] : registers_)
    {
        if (!reg.empty())
        {
            lines.emplace_back("\"" + encodeUtf8(std::u32string(1, name)), registerAbout(reg));
        }
    }
    if (lines.empty())
    {
        setMessage("The registers are empty", MessageKind::Information);
        return true;
    }
    listing_ = std::move(lines);
    return true;
}

bool Workspace::setMark(char32_t character)
{
    // m Space is the play-from marker, which the keys bind apart, unless typed slowly.
    if (character == U' ')
    {
        return run("play-from-set", 0).changed;
    }
    if (character < U'a' || character > U'z')
    {
        refuse("Marks are named by letters, a to z", MessageKind::Information);
        macros_.failed = macros_.depth > 0;
        return true;
    }
    const auto cursor = view().cursor();
    core::Mark mark{.letter = static_cast<char>(character),
                    .tick = cursor.tick,
                    .track = cursor.track,
                    .pitch = core::Mark::kNoPitch};
    if (where() == Where::PianoRoll && cursor.pitch)
    {
        mark.pitch = *cursor.pitch;
    }
    document_.edit("Mark " + std::string(1, mark.letter), view().place(),
                   [&](core::Song& song) { song.setMark(mark); });
    refresh();
    return true;
}

bool Workspace::goToMark(char32_t character, int count)
{
    // A mark's place, or with ' (or `) where the cursor was before the last jump, swapping
    // with it so '' goes back and forth. Without a pitch, the cursor keeps its own; without
    // a track (the conductor's), its track.
    const bool back = character == U'\'' || character == U'`';
    auto to = view().cursor();
    if (back)
    {
        if (!jumpedFrom_)
        {
            refuse("No jump yet to go back from", MessageKind::Information);
            macros_.failed = macros_.depth > 0;
            return true;
        }
        to = *jumpedFrom_;
    }
    else
    {
        const auto* mark = character >= U'a' && character <= U'z'
                               ? document_.song().mark(static_cast<char>(character))
                               : nullptr;
        if (mark == nullptr)
        {
            refuse(character >= U'a' && character <= U'z'
                       ? "Mark " + encodeUtf8(std::u32string(1, character)) + " isn't set"
                       : std::string{"Marks are named by letters, a to z"},
                   MessageKind::Information);
            macros_.failed = macros_.depth > 0;
            if (operator_)
            {
                operator_.reset();
                mode_ = Mode::Normal;
            }
            return true;
        }
        to.tick = mark->tick;
        if (mark->track)
        {
            to.track = mark->track;
        }
        if (mark->pitch != core::Mark::kNoPitch)
        {
            to.pitch = static_cast<std::uint8_t>(mark->pitch);
        }
    }
    if (operator_)
    {
        // The range to its time, as ]l's to a label's.
        jumpTime_ = to.tick;
        const bool done = runOperator("cursor-mark", count);
        jumpTime_ = -1;
        return done;
    }
    const auto from = view().cursor();
    view().setCursor(to);
    if (mode_ != Mode::Visual)
    {
        jumpedFrom_ = from;
    }
    return true;
}

bool Workspace::takeTarget(std::string_view op, const OperatorTarget& target, core::Tick from,
                           core::Tick span)
{
    const auto& song = document_.song();
    EventRegister taken;
    for (const auto& part : target.parts)
    {
        auto& events = taken.tracks.emplace_back();
        for (const auto i : part.events)
        {
            auto event = song.events(part.track)[i];
            event.tick -= from;
            events.push_back(std::move(event));
        }
    }
    taken.span = span;
    register_ = std::move(taken);
    if (op == "operator-yank")
    {
        setMessage(target.what + " yanked", MessageKind::Information);
        return true;
    }
    document_.edit("Delete " + target.what, view().place(),
                   [&](core::Song& edited)
                   {
                       for (auto part : target.parts)
                       {
                           std::ranges::sort(part.events);
                           edited.updateEvents(part.track,
                                               [&](core::EventList& events)
                                               {
                                                   for (auto it = part.events.rbegin();
                                                        it != part.events.rend(); ++it)
                                                   {
                                                       events.erase(*it);
                                                   }
                                               });
                       }
                   });
    refresh();
    setMessage("Deleted " + target.what, MessageKind::Information);
    return true;
}

std::string Workspace::markAbout(const core::Mark& mark) const
{
    // Where it is: "5:1:000, Bass, C3", or "5:1:000, the conductor".
    const auto& song = document_.song();
    std::string text = positionText(song.meter(), mark.tick) + ", ";
    const auto* track = mark.track ? song.track(*mark.track) : nullptr;
    if (track == nullptr)
    {
        text += "the conductor";
    }
    else
    {
        const auto index = std::ranges::find_if(song.tracks(), [&](const auto& t)
                                                { return t->id() == *mark.track; }) -
                           song.tracks().begin();
        text += track->name.empty() ? "Track " + std::to_string(index + 1) : track->name;
    }
    if (mark.pitch != core::Mark::kNoPitch)
    {
        text += ", " + core::pitchName(static_cast<std::uint8_t>(mark.pitch));
    }
    return text;
}

bool Workspace::sayMarks()
{
    std::vector<std::pair<std::string, std::string>> lines;
    for (const auto& mark : document_.song().marks())
    {
        lines.emplace_back(std::string(1, mark.letter), markAbout(mark));
    }
    if (lines.empty())
    {
        setMessage("No marks set", MessageKind::Information);
        return true;
    }
    listing_ = std::move(lines);
    return true;
}

CommandResult Workspace::deleteMarks(std::string_view letters)
{
    // :mark-delete a, abc, a-d, as Vim's :delmarks; one undo step.
    std::string wanted;
    for (std::size_t i = 0; i < letters.size(); ++i)
    {
        const char c = letters[i];
        if (c == ' ')
        {
            continue;
        }
        if (c < 'a' || c > 'z')
        {
            return CommandResult::error("Marks are named by letters: a, abc, or a-d");
        }
        if (i + 2 < letters.size() && letters[i + 1] == '-')
        {
            const char last = letters[i + 2];
            if (last < c || last > 'z')
            {
                return CommandResult::error("Marks are named by letters: a, abc, or a-d");
            }
            for (char l = c; l <= last; ++l)
            {
                wanted += l;
            }
            i += 2;
            continue;
        }
        wanted += c;
    }
    if (wanted.empty())
    {
        return CommandResult::error("Which marks? :mark-delete a, abc, or a-d");
    }
    std::string gone;
    for (const char c : wanted)
    {
        if (document_.song().mark(c) != nullptr && gone.find(c) == std::string::npos)
        {
            gone += c;
        }
    }
    if (gone.empty())
    {
        return {false, "No marks there to delete", false};
    }
    std::string names;
    for (const char c : gone)
    {
        names += (names.empty() ? "" : ", ") + std::string(1, c);
    }
    auto title = (gone.size() == 1 ? "Delete mark " : "Delete marks ") + names;
    document_.edit(title, view().place(),
                   [&](core::Song& song)
                   {
                       for (const char c : gone)
                       {
                           song.removeMark(c);
                       }
                   });
    refresh();
    return {true, title.replace(0, 6, "Deleted"), false};
}

bool Workspace::handleKey(const Key& key, std::int64_t now)
{
    // f and F repeat a find while it's the last thing done, any other key ending that, but for
    // a count or keys waiting for more.
    searching_.gnKept = false;
    refused_ = false;
    if (freezeChoice_)
    {
        return freezeChoiceKey(key);
    }
    if (trackMove_)
    {
        return trackMoveKey(key);
    }
    widenSelection(); // a key run at a pause may have moved the cursor
    const bool changed = takeKey(key, now);
    widenSelection();
    // gn's run through matches, the same; . and n keep it, and the keys of an answer to ^.
    if (!searching_.gnKept && keys_.pending().empty() && mode_ != Mode::CommandLine && !operator_)
    {
        searching_.gnRun = false;
    }
    // The transform menu given up (Escape, or a key it hasn't), its range with it.
    if (keys_.keys().empty())
    {
        transformTarget_.reset();
    }
    return changed;
}

bool Workspace::takeKey(const Key& key, std::int64_t now)
{
    lastTime_ = now;
    // A listing showing (:registers) takes the next key to close it, and nothing else.
    if (!std::exchange(listing_, {}).empty())
    {
        message_.clear();
        return true;
    }
    const bool hadMessage = !std::exchange(message_, std::string{}).empty();
    // A question asked takes only its answers: y for yes, n for no, and Escape to stop a run of
    // them or give it up; any other key shows it again, rather than being taken for no (decided
    // November 2026).
    if (question_)
    {
        const bool escape = key.named == NamedKey::Escape;
        const bool plain = key.named == NamedKey::None && (key.modifiers & (kCtrl | kAlt)) == 0;
        const bool yes = plain && key.character == U'y';
        const bool no = plain && key.character == U'n';
        if (!yes && !no && !escape)
        {
            setMessage(question_->text, MessageKind::Warning);
            return true;
        }
        auto question = *std::exchange(question_, std::nullopt);
        if (yes)
        {
            question.yes();
        }
        else if (escape && question.stop)
        {
            question.stop();
        }
        else if (no && question.no)
        {
            question.no();
        }
        else
        {
            refuse("Nothing done", MessageKind::Information);
        }
        if (!question_)
        {
            endVisual(); // a selection the question was about
        }
        return true;
    }
    // The list of keys takes the keys while it shows: q, Escape or ? closes it, j and k (or the
    // arrows and Page Down and Up) turn its pages, and other keys do nothing.
    if (keysPage_)
    {
        const auto pages = static_cast<int>(keyPages(keyList()).size());
        const bool next = key == Key::typed(U'j') || key == Key::of(NamedKey::Down) ||
                          key == Key::of(NamedKey::PageDown) || key == Key::of(NamedKey::Right);
        const bool back = key == Key::typed(U'k') || key == Key::of(NamedKey::Up) ||
                          key == Key::of(NamedKey::PageUp) || key == Key::of(NamedKey::Left);
        if (key == Key::typed(U'q') || key == Key::of(NamedKey::Escape) || key == Key::typed(U'?'))
        {
            keysPage_.reset();
        }
        else if (next || back)
        {
            keysPage_ = std::clamp(*keysPage_ + (next ? 1 : -1), 0, std::max(pages - 1, 0));
        }
        return true;
    }
    // A command waiting for a character after it ("a): the next key, Escape giving it up.
    if (const auto waiting = std::exchange(awaiting_, std::nullopt))
    {
        const bool done = characterAfter(waiting->command, key, waiting->count);
        noteWaiting(now);
        return done;
    }
    // A find's name, which a sharp or flat straight after its letter goes on (fB, then b).
    if (finding_.naming)
    {
        const bool plain = key.named == NamedKey::None && (key.modifiers & (kCtrl | kAlt)) == 0;
        if (plain && (key.character == U'#' || key.character == U'b'))
        {
            const bool done = finishNaming(key.character);
            noteWaiting(now);
            return done;
        }
        if (key.named == NamedKey::Escape && finding_.naming->redo)
        {
            cancelNaming();
            return true;
        }
        settleNaming();
        if (macros_.failed)
        {
            return true; // a find in a macro that found nothing, which stops it here
        }
    }
    // At the prompt, a character that starts no command-line binding is typed.
    if (mode_ == Mode::CommandLine && keys_.pending().empty() && key.named == NamedKey::None &&
        (key.modifiers & (kCtrl | kAlt)) == 0)
    {
        const std::vector<Key> keys{key};
        const auto match = keymap_.match(Mode::CommandLine, keys);
        if (match.binding == nullptr && !match.longer)
        {
            if (view().typing())
            {
                view().type(key.character);
            }
            else
            {
                const bool listed = completions().has_value();
                prompt_.line.insert(key.character);
                if (prompt_.palette)
                {
                    prompt_.palette->chosen = 0; // the best of what now matches
                }
                if (listed)
                {
                    refilter(); // the matches follow what's typed
                }
            }
            return true;
        }
    }
    const auto before = pending();
    const auto typedIn = mode_;
    const auto& keymap = mode_ == Mode::OperatorPending || mode_ == Mode::Visual
                             ? operatorKeymapHere()
                             : keymapHere();
    typedKeys_.assign(keys_.keys().begin(), keys_.keys().end());
    typedKeys_.push_back(key);
    const auto actions = keys_.key(keymap, mode_, key, now);
    const auto unbound =
        shownKeys(std::vector<Key>(keys_.unbound().begin(), keys_.unbound().end()));
    const bool changed = runAll(actions);
    // Keys after an operator that are no motion give the operator up, as in Vim, saying so;
    // and keys bound to nothing say so (decided October 2026, rather than nothing happening).
    if (mode_ == Mode::OperatorPending && actions.empty() && keys_.pending().empty())
    {
        if (!unbound.empty())
        {
            refuse(unbound + " isn't a range or object after " + keysOf(operator_->name),
                   MessageKind::Information);
            macros_.failed = macros_.depth > 0;
        }
        operator_.reset();
        mode_ = Mode::Normal;
    }
    else if ((typedIn == Mode::Normal || typedIn == Mode::Visual) && !unbound.empty())
    {
        refuse(unbound + " isn't a key here", MessageKind::Information);
        macros_.failed = macros_.depth > 0;
    }
    const bool hinted = hints().has_value();
    noteWaiting(now);
    // A message said, such as for a key bound to nothing, is drawn at once.
    return changed || hadMessage || !message_.empty() || pending() != before ||
           hinted != hints().has_value();
}

Where Workspace::where() const
{
    switch (shown())
    {
    case ViewKind::Arrangement:
        return arrange_->onStrip() ? Where::Strip : Where::Tracks;
    case ViewKind::PianoRoll:
        return Where::PianoRoll;
    case ViewKind::Tracks:
        return Where::TrackList;
    case ViewKind::Ports:
        return Where::PortList;
    case ViewKind::EventList:
        break;
    }
    return Where::EventList;
}

void Workspace::sayWholeRows()
{
    // In the piano roll, V takes its pitches across the whole track, which is more than is
    // shown, so the status line says so.
    if (selection_ && selection_->kind == SelectionKind::Rows && where() == Where::PianoRoll)
    {
        setMessage("These pitches across the whole track: is or ib narrows the time",
                   MessageKind::Information);
    }
}

bool Workspace::refuseObject(std::string_view command)
{
    // Where the object is, rather than doing something wider or narrower than it says
    // (decided November 2026).
    const auto keys = keysOf(command);
    refuse(command == "object-track"
               ? keys + " takes a track: use it on the track, not the section strip"
               : keys + " takes every track: use it on the section strip",
           MessageKind::Information);
    macros_.failed = macros_.depth > 0;
    return true;
}

bool Workspace::offered(std::string_view command) const
{
    const auto* info = commands_.find(command);
    return info == nullptr || info->where == 0 || (info->where & whereBit(where())) != 0;
}

std::optional<std::int64_t> Workspace::deadline() const
{
    auto deadline = keys_.deadline();
    if (hintsOn_ && waitingSince_ && !hintsShown_)
    {
        const auto shown = *waitingSince_ + hintDelay_;
        deadline = deadline ? std::min(*deadline, shown) : shown;
    }
    if (wasPlaying_ || (player_ != nullptr && player_->planWaiting()))
    {
        const auto frame = lastTime_ + kFrameInterval;
        deadline = deadline ? std::min(*deadline, frame) : frame;
    }
    if (inputLitUntil_)
    {
        deadline = deadline ? std::min(*deadline, *inputLitUntil_) : *inputLitUntil_;
    }
    if (inputDue_)
    {
        deadline = deadline ? std::min(*deadline, *inputDue_) : *inputDue_;
    }
    return deadline;
}

bool Workspace::inputWoken(std::int64_t now)
{
    lastTime_ = now;
    inputDue_ = now + kInputSettle;
    return takeInput();
}

bool Workspace::takeInput()
{
    if (player_ == nullptr)
    {
        return false;
    }
    const bool activity = player_->takeInput() > 0;
    if (activity)
        inputLitUntil_ = lastTime_ + kInputLightTime;
    // Said once for each loss, rather than at every message, as a flood keeps losing more.
    const auto lost = player_->inputLost();
    if (lost == inputLost_)
    {
        return activity;
    }
    const auto more = lost - inputLost_;
    inputLost_ = lost;
    setMessage("MIDI input lost: " + std::to_string(more) + (more == 1 ? " message" : " messages") +
                   ", arriving faster than it was taken",
               MessageKind::Warning);
    return true;
}

bool Workspace::wake(std::int64_t now)
{
    lastTime_ = now;
    const auto before = keys_.pending();
    const auto& keymap = mode_ == Mode::OperatorPending || mode_ == Mode::Visual
                             ? operatorKeymapHere()
                             : keymapHere();
    typedKeys_.assign(keys_.keys().begin(), keys_.keys().end());
    bool changed = runAll(keys_.timeout(keymap, now));
    if (waitingSince_ && !hintsShown_ && now >= *waitingSince_ + hintDelay_)
    {
        hintsShown_ = true;
        changed = changed || hints().has_value();
    }
    if (keys_.keys().empty() && !operator_ && !findWaiting())
    {
        waitingSince_.reset();
        hintsShown_ = false;
    }
    if (wasPlaying_)
    {
        player_->poll();
        showPlayhead();
        finishRecording();
        changed = true;
    }
    else if (player_ != nullptr && player_->planWaiting())
    {
        player_->poll(); // handing the engine the plan it couldn't take
    }
    if (inputDue_ && now >= *inputDue_)
    {
        // Check before draining: observing that the engine has finished publishes its final
        // batch too. A large burst may take several bounded engine passes while stopped.
        inputDue_ = player_ != nullptr && player_->inputPending()
                        ? std::optional{now + kInputSettle}
                        : std::nullopt;
        changed = takeInput() || changed;
    }
    else if (wasPlaying_)
    {
        changed = takeInput() || changed; // each frame, as recording will
    }
    if (inputLitUntil_ && now >= *inputLitUntil_)
    {
        inputLitUntil_.reset();
        changed = true;
    }
    return changed || keys_.pending() != before;
}

bool Workspace::runAll(const std::vector<Action>& actions)
{
    bool changed = false;
    fromKeys_ = true;
    for (const auto& action : actions)
    {
        changed = (action.withAfter ? characterAfter(action.command, action.after, action.count)
                                    : run(action.command, action.count).changed) ||
                  changed;
    }
    fromKeys_ = false;
    return changed;
}

void Workspace::report(const CommandResult& result)
{
    const auto kind = result.warning ? MessageKind::Warning : MessageKind::Information;
    if (result.refused)
    {
        refuse(result.message, kind);
    }
    else if (!result.message.empty())
    {
        setMessage(result.message, kind);
    }
}

CommandResult Workspace::run(std::string_view command, int count)
{
    if (command != "cut-at-edges" && !isOperator(command) && mode_ != Mode::OperatorPending)
    {
        cutNext_ = false; // | is for the next operator only
    }
    // A register named before ("a): a put puts from it, a yank or delete fills it, or with a
    // capital adds to it, the unnamed register getting what's taken either way, as in Vim.
    // (As a letter, 0 for none, rather than an optional, which GCC's release build takes for
    // uninitialised.)
    const char32_t named = command != "register-use" ? registerName_.value_or(0) : 0;
    const char32_t letter = lowerLetter(named);
    const auto letterText = encodeUtf8(std::u32string(1, letter));
    const bool put = command.starts_with("put-");
    std::optional<EventRegister> unnamed;
    if (named != 0 && put)
    {
        const auto& from = registers_[letter];
        if (from.empty())
        {
            registerName_.reset();
            refuse("Register " + letterText + " is empty", MessageKind::Warning);
            return {true};
        }
        unnamed = std::exchange(register_, from);
    }
    const bool taking =
        command == "event-yank" || command == "event-delete" || command == "operator-yank" ||
        command == "operator-delete" || command == "visual-delete-time" ||
        command == "bar-delete" ||
        (operator_ && (operator_->name == "operator-yank" || operator_->name == "operator-delete"));
    if (taking && named != 0 && named != letter)
    {
        const auto& into = registers_[letter];
        if (!into.empty() && (where() == Where::TrackList) != !into.wholeTracks.empty())
        {
            // Refuse before a delete changes the song, or a yank changes either register.
            refuse("Cannot append whole tracks and other material to the same register");
            registerName_.reset();
            return {true};
        }
    }
    auto result = commands_.run(command, count, mode_);
    report(result);
    if (named != 0 && put)
    {
        register_ = std::move(*unnamed);
        registerName_.reset();
    }
    else if (named != 0 && mode_ != Mode::OperatorPending && !awaiting_ && !prompt_.answer)
    {
        if (!(register_ == registerBefore_) || (taking && !refused_))
        {
            // Saying which register, and adding, what it now holds.
            auto& into = registers_[letter];
            if (letter == named)
            {
                into = register_;
                setMessage((message_.empty() ? std::string{"Taken"} : message_) +
                               ", into register " + letterText,
                           messageKind_);
            }
            else
            {
                into = appended(into, register_);
                setMessage("Added to register " + letterText + ", which now holds " +
                               registerAbout(into),
                           MessageKind::Information);
            }
        }
        registerName_.reset();
    }
    return result;
}

bool Workspace::runViewCommand(std::string_view command, int count)
{
    const auto cursor = view().cursor();
    if (const auto changed = view().run(command, count))
    {
        // Jumps, for '' to go back from (decided October 2026): to the song's ends, sections,
        // labels and marks, but not bars, which are steps.
        constexpr std::array<std::string_view, 6> kJumps{"go-first",
                                                         "go-last",
                                                         "cursor-section-next",
                                                         "cursor-section-previous",
                                                         "cursor-label-next",
                                                         "cursor-label-previous"};
        if (std::ranges::find(kJumps, command) != kJumps.end() && view().cursor() != cursor &&
            mode_ != Mode::Visual)
        {
            jumpedFrom_ = cursor;
        }
        // Moving the cursor or scrolling while playing lets go of the playhead (decided
        // October 2026, as in most sequencers): the view stays with the cursor, until
        // playback starts again or playhead-follow. A motion that leaves the cursor where it
        // is still brings the view back to it.
        const bool moves = command.starts_with("cursor-") || command.starts_with("scroll-") ||
                           command.starts_with("go-");
        if (wasPlaying_ && (moves || view().cursor() != cursor))
        {
            following_ = false;
        }
        // In a macro, a motion that can't move fails, as in Vim, ending a run at the song's
        // edge or the last note.
        if (macros_.depth > 0 && command.starts_with("cursor-") && view().cursor() == cursor)
        {
            macros_.failed = true;
        }
        return *changed;
    }
    return sayWhereFor(command);
}

bool Workspace::sayWhereFor(std::string_view command)
{
    // Where it does something instead, rather than where it doesn't, which reads as if it
    // were missing: "Note default is for the piano roll".
    const auto* info = commands_.find(command);
    auto title = info != nullptr ? titleOf(*info) : std::string{command};
    std::uint8_t where = info != nullptr ? info->where : 0;
    // Typed keys bound to several commands, none of them doing something here, say where each
    // does: "= is for the section strip, the piano roll and the event list".
    if (fromKeys_)
    {
        const auto mode = mode_ == Mode::Visual ? Mode::Visual : Mode::Normal;
        std::uint8_t theirs = 0;
        int bound = 0;
        bool ours = false;
        for (const auto& [keys, binding] : keymap_.bindings(mode))
        {
            if (keys == typedKeys_)
            {
                ++bound;
                ours = ours || binding.command == command;
                if (const auto* its = commands_.find(binding.command))
                {
                    theirs |= its->where;
                }
            }
        }
        if (ours && bound > 1)
        {
            title = shownKeys(typedKeys_);
            where = theirs;
        }
    }
    std::vector<std::string> places;
    constexpr std::array<std::pair<Where, std::string_view>, 6> kPlaces{{
        {Where::Tracks, "the arrangement's tracks"},
        {Where::Strip, "the section strip"},
        {Where::PianoRoll, "the piano roll"},
        {Where::EventList, "the event list"},
        {Where::TrackList, "the tracks view"},
        {Where::PortList, "the ports view"},
    }};
    for (const auto& [place, name] : kPlaces)
    {
        if ((where & whereBit(place)) != 0)
        {
            places.emplace_back(name);
        }
    }
    std::string text;
    for (std::size_t i = 0; i < places.size(); ++i)
    {
        text += (i == 0 ? "" : i + 1 == places.size() ? " and " : ", ") + places[i];
    }
    refuse(places.empty() ? title + " does nothing here" : title + " is for " + text,
           MessageKind::Information);
    macros_.failed = macros_.depth > 0;
    return true;
}

std::optional<bool> Workspace::runShared(std::string_view command, int count)
{
    if (!offered(command))
    {
        return sayWhereFor(command); // as the views' own commands do
    }
    const int n = std::max(count, 1);
    if (command.starts_with("command-line-"))
    {
        return runCommandLine(command);
    }
    if (isOperator(command))
    {
        if (transformTarget_ && command != "operator-delete" && command != "operator-yank" &&
            command != "operator-transform")
        {
            presetTarget_ = std::exchange(transformTarget_, std::nullopt);
            const bool done = transform(command, n, "preset", 0);
            presetTarget_.reset();
            return done;
        }
        operator_ = PendingOperator{std::string{command}, count};
        mode_ = Mode::OperatorPending;
        return true;
    }
    if (command == "cut-at-edges")
    {
        cutNext_ = true; // for the next operator
        return true;
    }
    if (command == "palette")
    {
        prompt_.palette = PaletteState{};
        mode_ = Mode::CommandLine;
        prompt_.line.open();
        prompt_.answer = {};
        prompt_.completing.reset();
        // The command ranked first chosen at once, once there's one, so Ctrl-p Enter runs it
        // again (decided October 2026).
        if (const auto shown = paletteShown(); !shown.empty() && shown.front().rank > 0)
        {
            prompt_.palette->chosen = 0;
        }
        return true;
    }
    if (command == "palette-forget")
    {
        remembered_.forgetCommands();
        rememberedChanged();
        setMessage("The palette's ranking of commands is forgotten", MessageKind::Information);
        return true;
    }
    if (command == "visual" || command == "visual-rows" || command == "visual-box")
    {
        const auto kind = selectionKind(command);
        document_.breakMerge();
        selection_ =
            Selection{.anchor = view().cursor(),
                      .kind = kind,
                      .where = where(),
                      .index = view().selectionIndex(),
                      .span = kind == SelectionKind::Rows ? view().rowsSpan() : std::nullopt,
                      .cut = false};
        mode_ = Mode::Visual;
        sayWholeRows();
        return true;
    }
    if (command == "visual-again")
    {
        if (!lastSelection_)
        {
            refuse("No selection yet to select again", MessageKind::Information);
            return true;
        }
        const auto saved = *lastSelection_;
        document_.breakMerge();
        const auto here = saved.selection.where;
        show(here == Where::TrackList   ? ViewKind::Tracks
             : here == Where::EventList ? ViewKind::EventList
             : here == Where::PianoRoll ? ViewKind::PianoRoll
                                        : ViewKind::Arrangement);
        selection_ = saved.selection;
        beforeObject_ = saved.beforeObject;
        view().setCursor(saved.cursor);
        mode_ = Mode::Visual;
        return true;
    }
    if (command == "repeat")
    {
        return repeatChange(count);
    }
    if (command == "register-use")
    {
        awaiting_ = Awaiting{std::string{command}, count};
        return true;
    }
    if (command == "registers")
    {
        return sayRegisters();
    }
    if (command == "macro-record" && macros_.recording != 0)
    {
        return endRecording();
    }
    if (command == "macro-record" || command == "macro-play")
    {
        awaiting_ = Awaiting{std::string{command}, count};
        return true;
    }
    if (command == "macros")
    {
        return sayMacros();
    }
    if (command == "mark-set")
    {
        awaiting_ = Awaiting{std::string{command}, count};
        return true;
    }
    if (command == "marks")
    {
        return sayMarks();
    }
    if (command == "undo" || command == "redo")
    {
        repeat_.notAChange = true;
        bool changed = false;
        for (int i = 0; i < n; ++i)
        {
            changed = undoOrRedo(command == "undo") || changed;
        }
        return changed;
    }
    if (command == "cancel")
    {
        return false;
    }
    if (command == "play-toggle" || command == "play-stop-back" || command == "play-cursor" ||
        command == "play-from-set" || command == "panic")
    {
        return transport(command);
    }
    if (command == "record")
    {
        return record();
    }
    if (command == "record-mode-toggle")
    {
        if (player_ != nullptr && player_->recording())
        {
            refuse("Recording: stop first to change the record mode");
            return true;
        }
        const bool overdub = document_.song().recordMode == core::RecordMode::Replace;
        document_.edit(
            overdub ? "Record mode: Overdub" : "Record mode: Replace", view().place(),
            [&](core::Song& song)
            { song.recordMode = overdub ? core::RecordMode::Overdub : core::RecordMode::Replace; });
        setMessage(overdub ? "Overdub: what's recorded is added to what's there"
                           : "Replace: what's recorded replaces what's there",
                   MessageKind::Information);
        return true;
    }
    if (command == "playhead-follow")
    {
        following_ = true;
        showPlayhead();
        return true;
    }
    if (command == "tracks-mute-toggle" || command == "tracks-solo-toggle" ||
        command == "tracks-chase-toggle" || command == "tracks-quantize-toggle" ||
        command == "tracks-arm-toggle" || command == "tracks-thru-toggle")
    {
        return view().run(command, count).value_or(false);
    }
    if (command == "track-arm-toggle" || command == "track-thru-toggle")
    {
        const bool arm = command == "track-arm-toggle";
        const auto id = view().cursor().track;
        if (!id)
        {
            refuse(arm ? "Select a track to arm" : "Select a track to change its thru");
            return true;
        }
        const auto* track = document_.song().track(*id);
        const bool on = !(arm ? track->arm : track->thru);
        document_.edit(arm ? (on ? "Arm" : "Disarm") : "Toggle thru", view().place(),
                       [&](core::Song& song)
                       {
                           song.updateTrack(*id, [&](core::Track& changed)
                                            { (arm ? changed.arm : changed.thru) = on; });
                       });
        if (arm)
        {
            const std::array ids{*id};
            const auto said = armedMessage(document_.song(), ids, on);
            setMessage(said.text, said.warning ? MessageKind::Warning : MessageKind::Information);
        }
        else
        {
            const auto* changed = document_.song().track(*id);
            setMessage(!on            ? "Thru off: MIDI input isn't sent to this track"
                       : changed->arm ? "Thru on: MIDI input is sent to this track"
                                      : "Thru on: MIDI input is sent to this track once it's armed",
                       MessageKind::Information);
        }
        return true;
    }
    if (command == "track-mute-toggle" || command == "track-solo-toggle")
    {
        const bool mute = command == "track-mute-toggle";
        const auto id = view().cursor().track;
        if (!id)
        {
            refuse(mute ? "Select a track to mute" : "Select a track to solo");
            return true;
        }
        const auto* track = document_.song().track(*id);
        const bool on = !(mute ? track->mute : track->solo);
        const auto name = track->name;
        document_.edit(mute ? "Toggle mute" : "Toggle solo", view().place(),
                       [&](core::Song& song)
                       {
                           song.updateTrack(*id, [&](core::Track& changed)
                                            { (mute ? changed.mute : changed.solo) = on; });
                       });
        setMessage(std::string{mute ? "Mute " : "Solo "} + (on ? "on: " : "off: ") + name,
                   MessageKind::Information);
        return true;
    }
    if (command == "track-reorder")
    {
        const auto block = trackBlock();
        if (!block)
        {
            refuse("Select a track to move");
            return true;
        }
        trackMove_ = TrackMove{block->first, block->second, block->first,
                               "track-reorder-" + std::to_string(++trackMoves_)};
        return true;
    }
    if (command == "track-move-up" || command == "track-move-down")
    {
        const auto block = trackBlock();
        if (!block)
        {
            refuse("Select a track to move");
            return true;
        }
        const auto [first, tracks] = *block;
        const auto last = document_.song().tracks().size() - tracks;
        const auto by = static_cast<std::size_t>(std::max(count, 1));
        const bool up = command == "track-move-up";
        const auto to = up ? first - std::min(first, by) : std::min(first + by, last);
        if (to == first)
        {
            refuse(up ? "Already the first track" : "Already the last track",
                   MessageKind::Information);
            return true;
        }
        // Moves one after another undo together, as with stepping a grid.
        moveTrackBlock(first, tracks, to, "track-move");
        return true;
    }
    if (command == "track-quantize-toggle")
    {
        const auto id = view().cursor().track;
        if (!id)
        {
            refuse("Select a track to quantise as it plays");
            return true;
        }
        const bool on = !document_.song().track(*id)->quantize.on;
        document_.edit(
            on ? "Quantise as it plays" : "Play as stored", view().place(), [&](core::Song& song)
            { song.updateTrack(*id, [&](core::Track& track) { track.quantize.on = on; }); });
        setMessage(on ? "Quantise on: " + quantizeSummary(*document_.song().track(*id))
                      : "Quantise off: playing as stored, its settings kept",
                   MessageKind::Information);
        return true;
    }
    if (command == "track-chase-toggle")
    {
        const auto id = view().cursor().track;
        if (!id)
        {
            refuse("Select a track to change note chasing");
            return true;
        }
        const bool on = !document_.song().track(*id)->chaseNotes;
        document_.edit(
            "Toggle note chasing", view().place(), [&](core::Song& song)
            { song.updateTrack(*id, [&](core::Track& track) { track.chaseNotes = on; }); });
        setMessage(on ? "Chase on: start notes already held at the playback position"
                      : "Chase off: wait for subsequent notes",
                   MessageKind::Information);
        return true;
    }
    if (command == "quantize-bypass-toggle")
    {
        if (player_ == nullptr)
        {
            refuse("Nothing plays here");
            return true;
        }
        player_->setBypassQuantize(!player_->bypassQuantize());
        player_->update(document_.song());
        setMessage(player_->bypassQuantize() ? "Quantise bypassed: every track plays as stored"
                                             : "Quantise back on, for the tracks that have it",
                   MessageKind::Information);
        return true;
    }
    if (command == "quantize-marks-toggle")
    {
        // Through the setting, as rewind's is.
        if (lineHandler_)
        {
            lineHandler_("set quantizemarks!");
        }
        else
        {
            quantizeMarks_ = !quantizeMarks_;
        }
        setMessage(quantizeMarks_ ? "Marks show where quantised notes will be heard"
                                  : "Marks of quantised notes hidden",
                   MessageKind::Information);
        return true;
    }
    if (command == "thru-toggle")
    {
        if (lineHandler_)
            lineHandler_("set thru!");
        else
            setThru(!thru_);
        setMessage(thru_ ? "Thru on" : "Thru off", MessageKind::Information);
        return true;
    }
    if (command == "rewind-toggle")
    {
        // Through the setting, where there is one, so it stays set as other settings change.
        if (lineHandler_)
        {
            lineHandler_("set rewind!");
        }
        else
        {
            setRewind(!rewind_);
        }
        setMessage(rewind_ ? "Rewind on: the play-from marker stays put"
                           : "Rewind off: the play-from marker moves to where playback stops",
                   MessageKind::Information);
        return true;
    }
    if (command == "grid-advance-toggle")
    {
        // Through the setting too, as rewind's is.
        if (lineHandler_)
        {
            lineHandler_("set gridadvance!");
        }
        else
        {
            pianoRoll().setGridAdvance(!pianoRoll().gridAdvance());
        }
        setMessage(pianoRoll().gridAdvance() ? "O moves on by the grid step"
                                             : "O moves on by the note's length",
                   MessageKind::Information);
        return true;
    }
    if (command == "command-line")
    {
        mode_ = Mode::CommandLine;
        prompt_.line.open();
        prompt_.answer = {};
        return true;
    }
    if (command == "search")
    {
        mode_ = Mode::CommandLine;
        std::swap(prompt_.line, prompt_.searchLine);
        prompt_.line.open();
        prompt_.answer = {};
        prompt_.searchPrompt = true;
        return true;
    }
    if (command == "search-here" || command == "search-here-back")
    {
        return searchHere(command == "search-here", count);
    }
    if (command == "search-clear")
    {
        searching_.query.reset();
        return true;
    }
    if (command == "select-match")
    {
        return selectMatch();
    }
    if (command.starts_with("select-"))
    {
        return select(command);
    }
    if (command == "keys-show")
    {
        keysPage_ = 0;
        return true;
    }
    if (command == "event-yank")
    {
        // The section strip yanks its section; everywhere else it's yin.
        if (const auto done = view().run(command, count))
        {
            return *done;
        }
        return view().operate("operator-yank", "object-note", count);
    }
    if (command == "view-ports")
    {
        show(ViewKind::Ports);
        return true;
    }
    if (command == "view-tracks")
    {
        show(ViewKind::Tracks);
        if (count > 0)
        {
            auto cursor = view().cursor();
            const auto tracks = document_.song().tracks();
            if (!tracks.empty())
                cursor.track =
                    tracks[std::min(static_cast<std::size_t>(count - 1), tracks.size() - 1)]->id();
            view().setCursor(cursor);
        }
        return true;
    }
    if (command == "view-arrangement" || command == "view-events" || command == "view-piano-roll")
    {
        show(command == "view-arrangement" ? ViewKind::Arrangement
             : command == "view-events"    ? ViewKind::EventList
                                           : ViewKind::PianoRoll);
        return true;
    }
    if (command == "view-conductor")
    {
        // The event list's track 0, at the cursor's time.
        show(ViewKind::EventList);
        auto cursor = view().cursor();
        cursor.track.reset();
        cursor.pitch.reset();
        view().setCursor(cursor);
        return true;
    }
    if (command == "view-back")
    {
        if (history_.empty())
        {
            refuse("No view to go back to", MessageKind::Information);
            return true;
        }
        auto* back = history_.back();
        history_.pop_back();
        switchTo(*back, false);
        return true;
    }
    return std::nullopt;
}

bool Workspace::runOperator(std::string_view command, int count)
{
    const bool cut = std::exchange(cutNext_, false);
    const auto op = *operator_;
    operator_.reset();
    mode_ = Mode::Normal;
    if (command == "cancel")
    {
        return true;
    }
    if (isOperator(command))
    {
        // Its own key again is the cursor's bar, as dd is.
        if (command != op.name)
        {
            refuse(keysOf(command) + " isn't a range or object after " + keysOf(op.name),
                   MessageKind::Information);
            return true;
        }
        command = "operator-line";
    }
    if (command != "operator-line")
    {
        const auto* info = commands_.find(command);
        if (info == nullptr || !(info->motion || info->object))
        {
            return false;
        }
        if (std::ranges::find(kMovesOnly, command) != kMovesOnly.end())
        {
            refuse(keysOf(command) + " only moves the cursor; for a range, use " +
                       keysOf("cursor-bar-line-next") + " or " + keysOf("cursor-section-next") +
                       ", or an object such as ib",
                   MessageKind::Information);
            return true;
        }
        if (where() == Where::EventList && (command == "cursor-first" || command == "cursor-last"))
        {
            refuse(keysOf(command) + " goes to a field, not a time, in the event list",
                   MessageKind::Information);
            return true;
        }
    }
    if (!offered(command))
    {
        return refuseObject(command);
    }
    if (command == "object-match")
    {
        // One match; a count after the operator would be lost, so it's refused (decided
        // November 2026, after a review of the grammar).
        if (count > 0)
        {
            refuse(keysOf(command) + " takes the one match; . does the next",
                   MessageKind::Information);
            macros_.failed = macros_.depth > 0;
            return true;
        }
        return operateOnMatch(op.name, std::max(op.count, 1));
    }
    // Those with no "how far" multiply the counts, as d and y do: 2qq quantises two bars
    // (decided November 2026).
    const bool howFar = op.name == "operator-transpose-up" ||
                        op.name == "operator-transpose-down" || op.name == "operator-shift-later" ||
                        op.name == "operator-shift-earlier";
    if (!howFar && op.name != "operator-delete" && op.name != "operator-yank")
    {
        const int bars =
            op.count == 0 && count == 0 ? 0 : std::max(op.count, 1) * std::max(count, 1);
        const auto run = [&] { return transform(op.name, 1, command, bars); };
        return cut ? cutThen(op.name, command, bars, run) : run();
    }
    if (op.name != "operator-delete" && op.name != "operator-yank")
    {
        // The count before the operator is how far (3>ib, up three semitones), the one after
        // it the range's (>3ib, three bars up one).
        const auto run = [&] { return transform(op.name, std::max(op.count, 1), command, count); };
        return cut ? cutThen(op.name, command, count, run) : run();
    }
    // Counts before the operator and after it multiply, as in Vim: 2d3j is d6j.
    const int total = op.count == 0 && count == 0 ? 0 : std::max(op.count, 1) * std::max(count, 1);
    const auto run = [&] { return view().operate(op.name, command, total); };
    return cut ? cutThen(op.name, command, total, run) : run();
}

bool Workspace::cutThen(std::string_view op, std::string_view motion, int count,
                        const std::function<bool()>& run)
{
    // Cutting the notes crossing the range's edges, then the operator on what's inside: d, D
    // and p after the cut, as one undo step under the operator's name; y on the song as it is,
    // what it yanks then cut to the range; the others cutting in their own change, so one that
    // asks first (^) cuts nothing if it's cancelled.
    const auto range = view().cutRange(motion, count);
    if (!range)
    {
        refuse("Cutting at the edges is for the arrangement and the piano roll",
               MessageKind::Information);
        return true;
    }
    if (op == "operator-yank")
    {
        const bool done = run();
        cutRegister(*range);
        return done;
    }
    if (op != "operator-delete" && op != "visual-delete-time")
    {
        cutting_ = range;
        const bool done = run();
        cutting_.reset();
        return done;
    }
    document_.beginGroup({}, view().place());
    cutSong(*range);
    const bool done = run();
    document_.endGroup();
    refresh();
    return done;
}

void Workspace::cutSong(const CutRange& range)
{
    document_.edit("Cut at the edges", view().place(),
                   [&](core::Song& song)
                   {
                       for (const auto id : range.tracks)
                       {
                           song.updateEvents(id,
                                             [&](core::EventList& events)
                                             {
                                                 const auto which = [&](const core::Note& note)
                                                 { return range.takes(note); };
                                                 events = core::cutAt(
                                                     core::cutAt(events, range.from, which),
                                                     range.to, which);
                                             });
                       }
                   });
}

void Workspace::cutRegister(const CutRange& range)
{
    // What y took, its ticks from the range's start, cut to it: the parts inside of the notes
    // sounding into it from before, and of those sounding on past its end.
    const auto& song = document_.song();
    const auto length = range.to - range.from;
    for (std::size_t k = 0; k < register_.tracks.size() && k < range.tracks.size(); ++k)
    {
        auto& taken = register_.tracks[k];
        for (auto& event : taken)
        {
            auto* note = std::get_if<core::Note>(&event.data);
            if (note != nullptr && range.takes(*note) && event.tick + note->duration > length)
            {
                note->duration = length - event.tick;
                note->releaseVelocity = core::Note{}.releaseVelocity;
            }
        }
        for (const auto& event : song.events(range.tracks[k]).all())
        {
            const auto* note = std::get_if<core::Note>(&event.data);
            if (note != nullptr && range.takes(*note) && event.tick < range.from &&
                event.tick + note->duration > range.from)
            {
                auto inside = *note;
                inside.duration = std::min(event.tick + note->duration, range.to) - range.from;
                if (event.tick + note->duration > range.to)
                {
                    inside.releaseVelocity = core::Note{}.releaseVelocity;
                }
                taken.insert(taken.begin(), core::Event{0, inside});
            }
        }
    }
}

std::optional<bool> Workspace::runVisual(std::string_view command, int count)
{
    // Escape or v again ends it; o goes to the other end; an object selects what it takes.
    // Motions, and the rest, are as in Normal mode, the selection following the cursor.
    if (command == "cancel")
    {
        endVisual();
        return true;
    }
    if (command == "visual" || command == "visual-rows" || command == "visual-box")
    {
        // Its own key again ends it, as in Vim; another's changes to it.
        const auto kind = selectionKind(command);
        if (selection_->kind == kind)
        {
            endVisual();
        }
        else
        {
            document_.breakMerge();
            selection_->kind = kind;
            selection_->span = kind == SelectionKind::Rows ? view().rowsSpan() : std::nullopt;
            selection_->starts.reset();
            selection_->object.reset();
            selection_->movedNotes.reset();
            sayWholeRows();
        }
        return true;
    }
    if (command == "cut-at-edges")
    {
        selection_->cut = !selection_->cut;
        return true;
    }
    if (command == "bar-open" && selection_->where == Where::Tracks)
    {
        // Enter on bars in the arrangement opens exactly those in the piano roll, on the
        // cursor's track; whole tracks open as Enter does.
        const auto span = view().selectionSpan();
        const bool whole = selection_->kind == SelectionKind::Rows && !selection_->span;
        auto cursor = view().cursor();
        endVisual();
        if (!span || whole)
        {
            return std::nullopt;
        }
        cursor.tick = span->first;
        switchTo(*pianoRoll_, true, true);
        pianoRoll_->openBars(cursor, span->first, span->second);
        return true;
    }
    if (command == "visual-other-end")
    {
        const auto other = selection_->anchor;
        selection_->anchor = view().cursor();
        selection_->index = view().selectionIndex();
        view().setCursor(other);
        if (selection_->movedNotes)
        {
            auto notes = std::make_shared<Selection::MovedNotes>(*selection_->movedNotes);
            notes->after = view().place();
            selection_->movedNotes = std::move(notes);
        }
        if (selection_->object)
        {
            selection_->object->after = view().place(); // still the object's
        }
        return true;
    }
    if (isOperator(command) || command == "event-delete" || command == "event-yank" ||
        command == "visual-delete-time")
    {
        return operateOnSelection(command, count);
    }
    if (command.starts_with("put-"))
    {
        return putOverSelection();
    }
    if (const auto* info = commands_.find(command); info != nullptr && info->object)
    {
        if (!offered(command))
        {
            return refuseObject(command);
        }
        selectObject(command, count);
        return true;
    }
    return std::nullopt;
}

void Workspace::selectObject(std::string_view command, int count)
{
    // What the object takes, from where the cursor was when the selection began or the last
    // object was chosen, so vinib is vib: another object replaces one, rather than adding to it.
    // One that finds nothing to take, saying so, leaves the selection as it was.
    const auto previous = *selection_;
    const auto previousPlace = view().place();
    const auto previousBefore = beforeObject_;
    if (selection_->object && beforeObject_)
    {
        const auto at = selection_->object->at;
        *selection_ = *beforeObject_;
        view().restore(at);
    }
    else
    {
        beforeObject_ = *selection_;
    }
    const auto at = view().place();
    const auto base = *selection_;
    message_.clear();
    selection_->movedNotes.reset(); // choosing another object deliberately replaces its members
    view().selectObject(command, count, *selection_);
    const auto& now = *selection_;
    if (!message_.empty() && view().place() == at && now.anchor == base.anchor &&
        now.kind == base.kind && now.span == base.span && now.starts == base.starts &&
        now.index == base.index)
    {
        *selection_ = previous;
        view().restore(previousPlace);
        beforeObject_ = previousBefore;
        return;
    }
    document_.breakMerge(); // deliberately replacing an object's members starts another gesture
    // Taking a v selection's place, it's the object's; with V or Ctrl-v, which chose the rows, it
    // sets only their time, and operators take the selection.
    if (base.kind == SelectionKind::Range)
    {
        selection_->object = Selection::Object{std::string{command}, count, at, view().place()};
    }
}

void Workspace::moveSelection(core::Tick by, int pitches,
                              std::vector<Selection::NotePosition> notes)
{
    if (!selection_)
    {
        return;
    }
    const auto shiftPitch = [&](std::uint8_t pitch)
    { return static_cast<std::uint8_t>(std::clamp(pitch + pitches, 0, 127)); };
    const auto shift = [&](Selection& selection)
    {
        selection.anchor.tick = std::max<core::Tick>(0, selection.anchor.tick + by);
        if (auto& pitch = selection.anchor.pitch)
        {
            *pitch = shiftPitch(*pitch);
        }
        for (auto* range : {&selection.span, &selection.starts})
        {
            if (*range)
            {
                (*range)->first = std::max<core::Tick>(0, (*range)->first + by);
                (*range)->second += by;
            }
        }
    };
    shift(*selection_);
    if (by != 0 || pitches != 0 || selection_->movedNotes)
    {
        std::ranges::sort(notes);
        selection_->movedNotes = std::make_shared<const Selection::MovedNotes>(
            Selection::MovedNotes{*view().cursor().track, std::move(notes), view().place()});
    }
    if (selection_->object)
    {
        auto& object = *selection_->object;
        if (auto state = pianoRollStateOf(object.at))
        {
            state->pitch = shiftPitch(state->pitch);
            object.at =
                placeWith(object.at.track, std::max<core::Tick>(0, object.at.tick + by), *state);
        }
        object.after = view().place();
        object.moved = true;
        if (beforeObject_)
        {
            shift(*beforeObject_);
        }
    }
}

void Workspace::widenSelection()
{
    // Once the cursor moves, the selection is from its anchor to the cursor again, as v's, the
    // object's exact time and starts let go: V's whole rows, or the rest's ends.
    if (mode_ == Mode::Visual && selection_ && selection_->movedNotes &&
        view().place() != selection_->movedNotes->after)
    {
        document_.breakMerge();
        selection_->movedNotes.reset();
    }
    if (mode_ != Mode::Visual || !selection_ || !selection_->object ||
        view().place() == selection_->object->after)
    {
        return;
    }
    document_.breakMerge();
    selection_->object.reset();
    selection_->starts.reset();
    selection_->span = selection_->kind == SelectionKind::Rows ? view().rowsSpan() : std::nullopt;
    beforeObject_.reset();
}

std::pair<std::string, int> Workspace::selectionRange(std::string_view command)
{
    if (selection_->object && !selection_->object->moved && !selection_->filter &&
        command != "visual-delete-time")
    {
        const auto& object = *selection_->object;
        view().restore(object.at);
        return {object.name, object.count};
    }
    return {"visual", 0};
}

bool Workspace::operateOnSelection(std::string_view command, int count)
{
    // d and x delete what's in it, y and Y yank it, D takes its time out, and the other
    // operators change it, the count before them saying how far; then Visual mode ends.
    const auto selection = *selection_;
    // Made by an object, and not widened since: as the operator with that object, from where
    // it was chosen.
    const auto [range, rangeCount] = selectionRange(command);
    const bool byObject = range != "visual";
    if (selection.filter && command == "visual-delete-time")
    {
        endVisual();
        refuse("D takes out the selection's time, not its matches", MessageKind::Information);
        return true;
    }
    const auto run = [&]
    {
        if (selection.filter && (command == "event-delete" || command == "operator-delete" ||
                                 command == "event-yank" || command == "operator-yank"))
        {
            return operateOnMatches(command == "event-delete" || command == "operator-delete"
                                        ? "operator-delete"
                                        : "operator-yank");
        }
        if (command == "visual-delete-time")
        {
            // Closing up within each section, whatever made the selection; taking bars out of
            // the song is :bar-delete's (decided November 2026).
            return view().operate("operator-delete", "visual-time", 0);
        }
        if (command == "event-delete" || command == "operator-delete")
        {
            return view().operate("operator-delete", range, rangeCount);
        }
        if (command == "event-yank" || command == "operator-yank")
        {
            return view().operate("operator-yank", range, rangeCount);
        }
        return transform(command, std::max(count, 1), range, rangeCount);
    };
    const auto op = command == "event-delete" ? std::string_view{"operator-delete"}
                    : command == "event-yank" ? std::string_view{"operator-yank"}
                                              : command;
    const bool done = selection.cut ? cutThen(op, range, rangeCount, run) : run();
    // What Alt-p puts at the cursor's pitch is placed by the selection's start, the pitch it
    // was begun on, rather than wherever the cursor stopped (decided November 2026, after use);
    // an object's, by the cursor where the object was chosen, as its operator's.
    if (!byObject && selection.anchor.pitch && where() == Where::PianoRoll &&
        (op == "operator-delete" || op == "operator-yank"))
    {
        register_.pitch = selection.anchor.pitch;
    }
    // Ended once the view has taken it; while the operator asks something, at the prompt as ^
    // does or yes or no as q does about pedalling, it stays shown until the answer or Escape,
    // though what's changed is already taken (decided October 2026).
    if (!prompt_.answer && !question_)
    {
        endVisual();
    }
    return done;
}

bool Workspace::putOverSelection()
{
    // p and P: what's in it goes, and the register goes in its place, as one undo step; the
    // register keeps what it had.
    if (where() == Where::TrackList)
    {
        const auto done = tracks_->run("put-replace", 1).value_or(false);
        endVisual();
        return done;
    }
    if (!register_.wholeTracks.empty())
    {
        refuse("Put whole tracks in the tracks view");
        return true;
    }
    const auto what = view().selectionStatus().value_or("the selection");
    const auto span = view().selectionSpan();
    if (selection_->filter)
    {
        endVisual();
        refuse("p puts over the selection's time, not its matches", MessageKind::Information);
        return true;
    }
    if (register_.empty())
    {
        endVisual();
        refuse("Nothing yanked or deleted to put", MessageKind::Warning);
        return true;
    }
    const bool strip = selection_->where == Where::Strip;
    if (register_.sections.empty() == strip)
    {
        endVisual();
        refuse(strip ? "The register holds bars of tracks: put them on the tracks"
                     : "The register holds sections: put them on the section strip",
               MessageKind::Warning);
        return true;
    }
    const auto saved = register_;
    const auto before = document_.song();
    const auto place = view().place();
    const auto selected = SavedSelection{*selection_, view().cursor(), beforeObject_};
    const auto first = span ? document_.song().sectionAt(span->first) : 0;
    const auto cut = selection_->cut ? view().cutRange("visual", 0) : std::nullopt;
    const auto title = "Put over " + what;
    document_.beginGroup(title, place);
    if (cut)
    {
        cutSong(*cut);
    }
    view().operate("operator-delete", "visual", 0);
    register_ = saved;
    endVisual();
    if (!refused_ && strip)
    {
        // Before what followed them, or after the last.
        view().run(first < document_.song().sections().size() ? "put-before" : "put-after", 1);
    }
    else if (!refused_)
    {
        if (span)
        {
            auto at = view().cursor();
            at.tick = span->first;
            view().setCursor(at);
        }
        view().run("put-merge", 1);
    }
    if (refused_)
    {
        // Deleting or cutting may already have changed the song. Restore it within the
        // group, so a refused replacement leaves no undo step and keeps any redo history.
        document_.edit(title, place, [&](core::Song& song) { song = before; });
        document_.endGroup();
        refresh();
        view().restore(place);
        lastSelection_ = selected;
        return true;
    }
    document_.endGroup();
    refresh();
    std::size_t total = 0;
    for (const auto& events : saved.tracks)
    {
        total += events.size();
    }
    const auto put =
        !saved.sections.empty()
            ? (saved.sections.size() == 1 ? saved.sections.front()->name()
                                          : std::to_string(saved.sections.size()) + " sections")
            : eventCount(total);
    setMessage("Put " + put + " in place of " + what, MessageKind::Information);
    return true;
}

void Workspace::endVisual()
{
    if (selection_)
    {
        lastSelection_ = SavedSelection{*selection_, view().cursor(), beforeObject_};
        document_.breakMerge();
    }
    selection_.reset();
    beforeObject_.reset();
    if (mode_ == Mode::Visual) // not once an operator has asked at the prompt, as ^ does
    {
        mode_ = Mode::Normal;
    }
}

bool Workspace::operateOnSections(std::string_view op, int count)
{
    const auto& song = document_.song();
    const auto range = sectionsFrom(song, view().cursor().tick, count);
    const auto what = sectionsText(song, range);
    register_ = EventRegister{};
    for (auto i = range.first; i < range.first + range.count; ++i)
    {
        register_.sections.push_back(song.sections()[i]);
    }
    if (op == "operator-yank")
    {
        setMessage(what + " yanked; p on the section strip puts it back", MessageKind::Information);
        return true;
    }
    if (range.count >= song.sections().size())
    {
        refuse("A song keeps at least one section", MessageKind::Warning);
        return true;
    }
    document_.edit("Delete " + (range.count == 1 ? "section " : std::string{}) + what,
                   view().place(),
                   [&](core::Song& s) { s.removeSections(range.first, range.count); });
    refresh();
    return true;
}

std::string Workspace::keysOf(std::string_view command) const
{
    // Its shortest keys in Normal mode, or after an operator for an object (gn, is).
    std::string shortest;
    for (const auto mode : {Mode::Normal, Mode::OperatorPending})
    {
        for (const auto& [keys, binding] : keymapHere().bindings(mode))
        {
            if (binding.command == command)
            {
                auto text = shownKeys(keys);
                if (shortest.empty() || text.size() < shortest.size())
                {
                    shortest = std::move(text);
                }
            }
        }
        if (!shortest.empty())
        {
            break;
        }
    }
    return shortest.empty() ? std::string{command} : shortest;
}

bool Workspace::undoOrRedo(bool undo)
{
    const auto step = undo ? document_.undo() : document_.redo();
    if (!step)
    {
        refuse(undo ? "Nothing to undo" : "Nothing to redo", MessageKind::Information);
        return true;
    }
    // Back where the change was made: its track, time and scroll.
    view().restore(step->place);
    setMessage((undo ? "Undid: " : "Redid: ") + step->title, MessageKind::Information);
    return true;
}

bool Workspace::transport(std::string_view command)
{
    if (player_ == nullptr)
    {
        refuse("Nothing to play to", MessageKind::Warning);
        return true;
    }
    // The engine refuses only while its queue is full, for an instant; then nothing's changed,
    // and the key can be pressed again.
    bool taken = true;
    if (command == "play-toggle")
    {
        taken = player_->toggle();
    }
    else if (command == "play-stop-back")
    {
        const bool was = player_->playing();
        taken = player_->toggle();
        returning_ = taken ? was : returning_;
    }
    else if (command == "play-cursor" && player_->recording())
    {
        // Playing from elsewhere would end the pass without saying so.
        refuse("Recording: Space stops it first");
        return true;
    }
    else if (command == "play-cursor" || command == "play-from-set")
    {
        player_->setMarker(view().cursor().tick);
        if (command == "play-cursor")
        {
            taken = player_->play();
            following_ = following_ || taken;
        }
    }
    else
    {
        taken = player_->panic();
    }
    if (!taken)
    {
        refuse("The engine is busy and didn't take that: try again", MessageKind::Warning);
    }
    showPlayhead();
    return true;
}

namespace
{
// Tracks by name, in song order, or "track 3" for one without: "Bass, Piano".
std::string trackNames(const core::Song& song, std::span<const core::TrackId> ids)
{
    std::string text;
    const auto tracks = song.tracks();
    for (std::size_t i = 0; i < tracks.size(); ++i)
    {
        if (std::ranges::find(ids, tracks[i]->id()) != ids.end())
        {
            text += (text.empty() ? "" : ", ") +
                    (tracks[i]->name.empty() ? "track " + std::to_string(i + 1) : tracks[i]->name);
        }
    }
    return text;
}
} // namespace

bool Workspace::record()
{
    if (player_ == nullptr)
    {
        refuse("Nothing to record from", MessageKind::Warning);
        return true;
    }
    if (player_->playing() || player_->stopping())
    {
        refuse(player_->recording() ? "Already recording: Space stops" : "Stop first to record");
        return true;
    }
    std::vector<core::TrackId> armed;
    for (const auto& track : document_.song().tracks())
    {
        if (track->arm)
            armed.push_back(track->id());
    }
    if (armed.empty())
    {
        refuse("No track is armed: , r a arms this one");
        return true;
    }
    if (!player_->record(document_.song()))
    {
        refuse("The engine is busy and didn't take that: try again", MessageKind::Warning);
        return true;
    }
    const bool overdub = document_.song().recordMode == core::RecordMode::Overdub;
    auto text =
        (overdub ? "Overdubbing onto " : "Recording onto ") + trackNames(document_.song(), armed);
    if (!player_->clicking())
        text += "; no click, with no metronome port or default output";
    // Several armed is often a mistake, so said as a warning (see Arming in the design).
    setMessage(text, armed.size() > 1 || !player_->clicking() ? MessageKind::Warning
                                                              : MessageKind::Information);
    showPlayhead();
    return true;
}

void Workspace::finishRecording()
{
    if (player_ == nullptr || !player_->recordingStopped())
    {
        return;
    }
    auto take = player_->finishRecording(document_.song());
    if (!take)
    {
        return;
    }
    if (take->song == document_.song())
    {
        setMessage("Nothing recorded: stopped before the record point", MessageKind::Information);
        return;
    }
    const auto names = trackNames(document_.song(), take->tracks);
    const auto title = take->tracks.size() == 1
                           ? "Record onto " + names
                           : "Record onto " + std::to_string(take->tracks.size()) + " tracks";
    document_.edit(title, view().place(), [&](core::Song& song) { song = std::move(take->song); });
    setMessage("Recorded onto " + names, MessageKind::Information);
}

void Workspace::setPlayer(core::Player* player)
{
    if (player_ != player)
    {
        view().stopPlayhead(std::nullopt);
        wasPlaying_ = false;
        returning_ = false;
        playedFrom_.reset();
    }
    player_ = player;
    if (player_ != nullptr)
    {
        player_->setRewind(rewind_);
        player_->setThru(thru_);
    }
}

void Workspace::setThru(bool on)
{
    thru_ = on;
    if (player_)
        player_->setThru(on);
}

void Workspace::setRewind(bool rewind)
{
    rewind_ = rewind;
    if (player_ != nullptr)
    {
        player_->setRewind(rewind);
    }
}

void Workspace::showPlayhead()
{
    const bool playing = player_ != nullptr && player_->playing();
    if (playing && !wasPlaying_)
    {
        following_ = true;
        playedFrom_ = view().cursor();
    }
    if (playing)
    {
        // Until the engine starts, the playhead is at the play-from marker, where it will start.
        lastPlayhead_ = player_->position().value_or(player_->marker());
        view().showPlayhead(lastPlayhead_, !following_ ? Follow::None
                                           : rewind_   ? Follow::View
                                                       : Follow::Cursor);
    }
    else if (wasPlaying_)
    {
        // Stopped where it is (decided October 2026, after use), with rewind on or off, which
        // says only where the next play starts: the view staying and the cursor landing there,
        // without rewind where the engine says it stopped; but where the cursor was moved to
        // while playing, it stays; and with play-stop-back it goes back to where it was when
        // playback started.
        if (returning_ && playedFrom_)
        {
            view().stopPlayhead(std::nullopt);
            view().setCursor(*playedFrom_);
        }
        else
        {
            view().stopPlayhead(!following_ ? std::nullopt
                                : rewind_   ? std::optional{lastPlayhead_}
                                            : std::optional{player_->marker()});
            // Stopping in place is a jump, so '' goes back as Shift-Space does.
            if (following_ && playedFrom_ && view().cursor() != *playedFrom_)
            {
                jumpedFrom_ = playedFrom_;
            }
        }
        if (!player_->stopping())
        {
            returning_ = false;
        }
    }
    wasPlaying_ = playing || (player_ != nullptr && player_->stopping());
}

std::optional<std::pair<std::size_t, std::size_t>> Workspace::trackBlock() const
{
    const auto here = where();
    const auto cursor = view().cursor();
    if (!cursor.track || (here != Where::Tracks && here != Where::TrackList))
    {
        return std::nullopt;
    }
    const auto tracks = document_.song().tracks();
    const auto indexOf = [&](core::TrackId id)
    {
        return static_cast<std::size_t>(
            std::ranges::find_if(tracks, [&](const auto& t) { return t->id() == id; }) -
            tracks.begin());
    };
    auto first = indexOf(*cursor.track);
    auto last = first;
    if (selection_ && selection_->anchor.track)
    {
        const auto anchor = indexOf(*selection_->anchor.track);
        first = std::min(first, anchor);
        last = std::max(last, anchor);
    }
    return std::pair{first, last - first + 1};
}

std::vector<core::TrackId> Workspace::movingTracks() const
{
    std::vector<core::TrackId> ids;
    if (trackMove_)
    {
        const auto tracks = document_.song().tracks();
        for (std::size_t i = 0; i < trackMove_->count; ++i)
        {
            ids.push_back(tracks[trackMove_->first + i]->id());
        }
    }
    return ids;
}

void Workspace::moveTrackBlock(std::size_t first, std::size_t count, std::size_t to,
                               const std::string& mergeKey)
{
    const auto cursor = view().cursor();
    document_.edit(
        count == 1 ? "Move a track" : "Move tracks", view().place(),
        [&](core::Song& song) { song.moveTracks(first, count, to); }, mergeKey);
    view().refresh();
    view().setCursor(cursor);
}

bool Workspace::trackMoveKey(const Key& key)
{
    auto& move = *trackMove_;
    const auto last = document_.song().tracks().size() - move.count;
    const auto go = [&](std::size_t to)
    {
        if (to != move.first)
        {
            moveTrackBlock(move.first, move.count, to, move.mergeKey);
            move.first = to;
        }
    };
    const auto steps = [&] { return static_cast<std::size_t>(std::max(move.number, 1)); };
    const bool plain = (key.modifiers & (kCtrl | kAlt)) == 0;
    const bool g = std::exchange(move.g, false);
    if (key.named == NamedKey::Enter)
    {
        trackMove_.reset();
        endVisual();
    }
    else if (key.named == NamedKey::Escape)
    {
        go(move.start);
        trackMove_.reset();
    }
    else if (key.named == NamedKey::Up || (plain && key.character == U'k'))
    {
        go(move.first - std::min(move.first, steps()));
        move.number = 0;
    }
    else if (key.named == NamedKey::Down || (plain && key.character == U'j'))
    {
        go(std::min(move.first + steps(), last));
        move.number = 0;
    }
    else if (plain && key.character == U'G')
    {
        go(last);
        move.number = 0;
    }
    else if (plain && key.character == U'g')
    {
        if (g)
        {
            go(0);
        }
        move.g = !g;
        move.number = 0;
    }
    else if (plain && key.character >= U'0' && key.character <= U'9' &&
             (key.character != U'0' || move.number > 0))
    {
        move.number = std::min(move.number * 10 + static_cast<int>(key.character - U'0'), 9999);
    }
    else
    {
        move.number = 0;
    }
    return true;
}

CommandResult Workspace::insertEvent(std::string_view text)
{
    const auto cursor = view().cursor();
    const auto first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        // From its key: asks which, with the kinds to complete from.
        openPrompt("event-insert ");
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
    const core::Event event{cursor.tick, std::move(*parsed.data)};
    document_.edit("Insert an event", view().place(),
                   [&](core::Song& song)
                   {
                       song.updateEvents(*cursor.track,
                                         [&](core::EventList& events) { events.insert(event); });
                   });
    view().refresh();
    return {true, "Inserted at " + view().position(cursor.tick), false};
}

CommandResult Workspace::port(std::string_view name)
{
    const auto first = name.find_first_not_of(" \t");
    name = first == std::string_view::npos
               ? std::string_view{}
               : name.substr(first, name.find_last_not_of(" \t") - first + 1);
    if (!portFinder_)
    {
        return CommandResult::error("There are no MIDI ports here");
    }
    const auto id = view().cursor().track;
    if (!id)
    {
        return CommandResult::error("The conductor has no port; its tempo and meter aren't sent");
    }
    const auto found = portFinder_(name);
    const auto list = [](const std::vector<std::string>& names)
    {
        std::string text;
        for (const auto& n : names)
        {
            text += (text.empty() ? "" : ", ") + n;
        }
        return text;
    };
    const auto outputs = found.ports.empty() ? std::string{"there are no output ports"}
                                             : "outputs: " + list(found.ports);
    const auto tracks = document_.song().tracks();
    const auto at = std::ranges::find_if(tracks, [&](const auto& t) { return t->id() == *id; });
    const auto& track = **at;
    // The track's name, starting the message with a capital.
    auto who = track.name.empty() ? "track " + std::to_string(at - tracks.begin() + 1) : track.name;
    const auto trackName = who;
    who[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(who[0])));
    if (name.empty())
    {
        return {false,
                who + " plays to " +
                    (track.port.empty() ? std::string{"the default port"} : track.port) + "; " +
                    outputs,
                false};
    }
    if (!found.port)
    {
        return CommandResult::error(
            found.candidates.empty()
                ? "No output port matches \"" + std::string{name} + "\"; " + outputs
                : "\"" + std::string{name} + "\" could be " + list(found.candidates));
    }
    const auto& full = *found.port;
    document_.edit("Set port of " + trackName + " to " + full, view().place(), [&](core::Song& song)
                   { song.updateTrack(*id, [&](core::Track& t) { t.port = full; }); });
    view().refresh();
    return {true, who + " plays to " + full, false};
}

CommandResult Workspace::sectionHere(std::string_view command, int count)
{
    // On the cursor's section, from any view (, s, decided November 2026), as the strip's i, a
    // and J do there.
    const auto& song = document_.song();
    const auto section = song.sectionAt(view().cursor().tick);
    const auto last = song.sections().size() - 1;
    const auto name = [&](std::size_t i) { return song.sections()[i]->name(); };
    if (command == "section-join")
    {
        if (section == last)
        {
            return CommandResult::error(name(section) +
                                        " is the last section, with none after to join");
        }
        const auto joins = std::min<std::size_t>(
            static_cast<std::size_t>(std::max(std::max(count, 1) - 1, 1)), last - section);
        document_.edit("Join " + name(section) + " with " + name(section + 1) +
                           (joins > 1 ? " and the next " + std::to_string(joins - 1) : ""),
                       view().place(),
                       [&](core::Song& s)
                       {
                           for (std::size_t i = 0; i < joins; ++i)
                           {
                               s.joinSections(section);
                           }
                       });
        refresh();
        return {true};
    }
    // Asking its name first, with one to edit, so it goes in named, as one step; the cursor
    // then goes to it.
    const bool before = command == "section-insert-before";
    const auto at = before ? section : section + 1;
    const auto bars = count > 0 ? count : 4;
    askName("Insert a section " + std::string{before ? "before " : "after "} + name(section) +
                ", named: ",
            song.newSectionName(),
            [this, at, bars](const std::string& given)
            {
                document_.edit("Insert " + given, view().place(),
                               [&](core::Song& s) { s.insertEmptySection(at, given, bars); });
                refresh();
                auto cursor = view().cursor();
                cursor.tick = document_.song().sectionStart(at);
                view().setCursor(cursor);
            });
    return {true};
}

CommandResult Workspace::section(std::string_view name)
{
    const auto first = name.find_first_not_of(" \t");
    name = first == std::string_view::npos
               ? std::string_view{}
               : name.substr(first, name.find_last_not_of(" \t") - first + 1);
    auto& song = document_.song();
    const auto& meter = view().cursorMeter();
    const auto tick = meter.barStart(meter.toBarBeatTick(view().cursor().tick).bar);
    const auto at = positionText(meter, tick);
    const auto current = song.sectionAt(tick);
    if (tick >= song.end())
    {
        return CommandResult::error("There's no section after the song's end to split");
    }
    if (song.sectionStart(current) == tick)
    {
        return CommandResult::error(song.sections()[current]->name() + " starts at " + at +
                                    " already");
    }
    if (name.empty())
    {
        // Asking its name first, with one to edit, so it's split and named as one step.
        askName("Start a section at " + at + ", named: ", song.newSectionName(),
                [this](const std::string& given) { report(section(given)); });
        return {true};
    }
    const std::string wanted{name};
    std::optional<core::Song::Split> split;
    document_.edit("Start section " + wanted + " at " + at, view().place(),
                   [&](core::Song& s) { split = s.splitSection(tick, wanted); });
    refresh();
    if (!split)
    {
        return CommandResult::error("Can't start a section at " + at);
    }
    // The name it got, numbered if another section had it.
    const auto& named = document_.song().sections()[split->index]->name();
    std::string cut;
    if (split->conductor)
    {
        cut = "the conductor and the tracks following it";
    }
    for (const auto id : split->tracks)
    {
        const auto track = document_.song().track(id);
        cut += (cut.empty() ? "" : ", ") +
               (track->name.empty() ? std::string{"a track"} : track->name);
    }
    if (!cut.empty())
    {
        // A bar the new section starts inside ends short, as a remainder bar.
        return CommandResult{true, named + " starts at " + at + "; this cuts a bar short in " + cut,
                             true};
    }
    return {true, named + " starts at " + at, false};
}

namespace
{

std::string trackNameOf(const core::Song& song, core::TrackId id)
{
    const auto* track = song.track(id);
    return track == nullptr || track->name.empty() ? std::string{"A track"} : track->name;
}

} // namespace

CommandResult Workspace::label(std::string_view name)
{
    // :label NAME puts a label at the cursor's time, or renames the one there; alone, it asks
    // for the name, with the one there to edit, as :section-rename does.
    while (!name.empty() && name.front() == ' ')
    {
        name.remove_prefix(1);
    }
    while (!name.empty() && name.back() == ' ')
    {
        name.remove_suffix(1);
    }
    const auto tick = view().cursor().tick;
    const auto labels = document_.song().labels();
    const auto it = std::ranges::find(labels, tick, &core::Label::tick);
    if (name.empty())
    {
        openPrompt("label " + (it == labels.end() ? std::string{} : it->name));
        return {true};
    }
    const std::string text{name};
    if (it != labels.end() && it->name == text)
    {
        return {true, "The label here is already " + text, false};
    }
    const auto title =
        it == labels.end() ? "Label " + text : "Rename label " + it->name + " to " + text;
    document_.edit(title, view().place(), [&](core::Song& song) { song.setLabel(tick, text); });
    refresh();
    return {true, (it == labels.end() ? "Labelled " : "Renamed it ") + text, false};
}

CommandResult Workspace::deleteLabels()
{
    // The labels in the selection, or the one at the cursor's time; one undo step.
    const auto span = selection_ ? view().selectionSpan() : std::nullopt;
    const auto tick = view().cursor().tick;
    std::vector<core::Label> doomed;
    for (const auto& label : document_.song().labels())
    {
        if (span ? label.tick >= span->first && label.tick < span->second : label.tick == tick)
        {
            doomed.push_back(label);
        }
    }
    if (doomed.empty())
    {
        return CommandResult::error(
            span ? "There are no labels in " + view().selectionStatus().value_or("the selection")
                 : std::string{"There's no label here; ]l goes to the next"});
    }
    const auto title = doomed.size() == 1 ? "Delete label " + doomed.front().name
                                          : "Delete " + std::to_string(doomed.size()) + " labels";
    document_.edit(title, view().place(),
                   [&](core::Song& song)
                   {
                       for (const auto& label : doomed)
                       {
                           song.removeLabel(label.tick);
                       }
                   });
    refresh();
    return {true, "Deleted" + title.substr(6), false};
}

CommandResult Workspace::labelToSection()
{
    if (const auto span = selection_ ? view().selectionSpan() : std::nullopt)
    {
        return labelsToSections(span->first, span->second);
    }
    // The label at or before the cursor, its section starting at the bar line of its bar in
    // the conductor's bars, asking first unless the cursor is on it and it's on a bar line.
    const auto& song = document_.song();
    const auto& meter = song.meter();
    const auto tick = view().cursor().tick;
    const auto labels = song.labels();
    const auto after = std::ranges::upper_bound(labels, tick, {}, &core::Label::tick);
    if (after == labels.begin())
    {
        return CommandResult::error("There's no label at or before the cursor");
    }
    const auto label = *std::prev(after);
    const auto start = meter.barStart(meter.toBarBeatTick(label.tick).bar);
    const auto section = song.sectionAt(start);
    if (song.sectionStart(section) == start)
    {
        return CommandResult::error(label.name + " is at the start of " +
                                    song.sections()[section]->name() +
                                    ", so there's no section to start");
    }
    const auto at = positionText(meter, start);
    const auto make = [this, label, start, at]
    {
        std::optional<core::Song::Split> split;
        document_.edit("Start section " + label.name + " at " + at + ", from its label",
                       view().place(),
                       [&](core::Song& s)
                       {
                           split = s.splitSection(start, label.name);
                           s.removeLabel(label.tick);
                       });
        refresh();
        const auto& named = document_.song().sections()[split->index]->name();
        setMessage(named + " starts at " + at + ", from its label", MessageKind::Information);
    };
    if (label.tick == tick && label.tick == start)
    {
        make();
        return {true, {}, false};
    }
    ask(label.tick == start ? "Start a section from the label " + label.name + " at " + at + "?"
                            : "Start a section at " + at + " from the label " + label.name +
                                  " at " + positionText(meter, label.tick) + "?",
        make);
    return {true, {}, false};
}

CommandResult Workspace::labelsToSections(core::Tick from, core::Tick to)
{
    const auto labels = document_.song().labels();
    if (std::ranges::none_of(labels, [&](const core::Label& label)
                             { return label.tick >= from && label.tick < to; }))
    {
        return CommandResult::error("There are no labels in " +
                                    view().selectionStatus().value_or("the selection"));
    }
    auto run = std::make_shared<LabelRun>();
    run->from = view().cursor();
    run->first = from;
    run->last = to;
    runLabels(run);
    return {true, {}, false};
}

void Workspace::runLabels(std::shared_ptr<LabelRun> run)
{
    // On through the labels from where the run is: those on a bar line taken, those whose bar
    // already starts a section (or another label's) passed over, and for each of the others
    // the cursor going to it and a question, after which the run goes on.
    const auto& song = document_.song();
    const auto& meter = song.meter();
    const auto labels = song.labels();
    for (; run->next < labels.size(); ++run->next)
    {
        const auto label = labels[run->next];
        if (label.tick < run->first || label.tick >= run->last)
        {
            continue; // outside the selection
        }
        const auto start = meter.barStart(meter.toBarBeatTick(label.tick).bar);
        if (song.sectionStart(song.sectionAt(start)) == start)
        {
            ++run->atStarts;
            continue;
        }
        if (std::ranges::find(run->starts, start, &std::pair<core::Tick, std::string>::first) !=
            run->starts.end())
        {
            ++run->sharing;
            continue;
        }
        const auto take = [run, label, start]
        {
            run->starts.emplace_back(start, label.name);
            run->labels.push_back(label.tick);
        };
        if (label.tick == start)
        {
            take();
            continue;
        }
        auto cursor = view().cursor();
        cursor.tick = label.tick;
        view().setCursor(cursor);
        ++run->next;
        ask(
            "Start a section at " + positionText(meter, start) + " from the label " + label.name +
                " at " + positionText(meter, label.tick) + "?",
            [this, run, take]
            {
                take();
                runLabels(run);
            },
            [this, run]
            {
                ++run->declined;
                runLabels(run);
            },
            [this, run]
            {
                view().setCursor(run->from);
                refuse("Nothing done", MessageKind::Information);
            });
        return;
    }
    finishLabels(*run);
}

void Workspace::finishLabels(const LabelRun& run)
{
    // Every section started at once, as one undo step, the cursor back where it was.
    view().setCursor(run.from);
    std::string left;
    const auto add = [&](int n, const std::string& one, const std::string& many)
    {
        if (n > 0)
        {
            left += "; " + (n == 1 ? "1 was " + one : std::to_string(n) + " were " + many);
        }
    };
    add(run.atStarts, "at a section's start", "at sections' starts");
    add(run.sharing, "in a bar with another", "in bars with others");
    add(run.declined, "left as it was", "left as they were");
    if (run.starts.empty())
    {
        setMessage("No labels turned into sections" + left, MessageKind::Information);
        return;
    }
    const auto turned = run.starts.size();
    document_.edit("Turn " + std::to_string(turned) + (turned == 1 ? " label" : " labels") +
                       " into sections",
                   view().place(),
                   [&](core::Song& s)
                   {
                       for (const auto& [start, name] : run.starts)
                       {
                           s.splitSection(start, name);
                       }
                       for (const auto tick : run.labels)
                       {
                           s.removeLabel(tick);
                       }
                   });
    refresh();
    setMessage("Turned " + std::to_string(turned) +
                   (turned == 1 ? " label into a section" : " labels into sections") + left,
               MessageKind::Information);
}

CommandResult Workspace::barDelete(int count)
{
    if (const auto span = selection_ ? view().selectionSpan() : std::nullopt)
    {
        return deleteBars(span->first, span->second);
    }
    const auto& song = document_.song();
    const auto& meter = song.meter();
    const auto bar = meter.toBarBeatTick(view().cursor().tick).bar;
    const auto from = meter.barStart(bar);
    if (from >= song.end())
    {
        return CommandResult::error("There are no bars after the song's end to delete");
    }
    return deleteBars(from, std::min(meter.barStart(bar + std::max(count, 1)), song.end()));
}

CommandResult Workspace::deleteBars(core::Tick from, core::Tick to)
{
    // The conductor's bars from one bar line to another, on every track, done on a copy first,
    // which says whether it can be, as one undo step.
    const auto& song = document_.song();
    const auto& meter = song.meter();
    const auto bar = meter.toBarBeatTick(from).bar;
    const auto last = meter.toBarBeatTick(to).bar;
    if (meter.barStart(bar) != from || (to < song.end() && meter.barStart(last) != to))
    {
        return CommandResult::error(barsText(meter, from, to) +
                                    " aren't whole bars of the conductor's; take them out of "
                                    "each track instead");
    }
    auto n = last - bar;
    if (meter.barStart(last) != to)
    {
        ++n; // to the song's end, inside a bar
    }
    const auto bars = barsText(meter, from, to);
    auto edited = song;
    const auto edit = edited.removeBars(from, n);
    if (!edit.done)
    {
        if (edit.misaligned)
        {
            return CommandResult::error(trackNameOf(song, *edit.misaligned) +
                                        "'s bars don't line up with " + bars +
                                        "; delete them on each track instead");
        }
        return CommandResult::error("A song keeps at least one bar");
    }
    document_.edit("Delete " + bars + " on every track", view().place(),
                   [&](core::Song& s) { s = edited; });
    register_ = EventRegister{};
    register_.sections = edit.removed;
    refresh();
    return {true,
            "Deleted " + bars +
                " on every track; p on the section strip puts them back as a section",
            false};
}

CommandResult Workspace::askBarInsert(int count)
{
    // How many bars (1), then their meter (the one there), Enter taking each default.
    const auto& song = document_.song();
    const auto& meter = song.meter();
    const auto bar = meter.toBarBeatTick(view().cursor().tick).bar;
    if (meter.barStart(bar) >= song.end())
    {
        return CommandResult::error("Bars go in before a bar of the song");
    }
    const auto signature = meter.signatureAt(meter.barStart(bar));
    const auto here =
        std::to_string(signature.numerator) + "/" + std::to_string(signature.denominator);
    const auto before = "Insert bars before bar " + std::to_string(bar);
    const auto askMeter = [this, before, here](const std::string& bars)
    {
        askFor(
            before + ", " + bars + ", in the meter (" + here + "): ",
            [this, bars](const std::string& given) { report(barInsert(bars + " " + given)); },
            here);
    };
    if (count > 0)
    {
        askMeter(std::to_string(count));
        return {true};
    }
    askFor(before + ", how many (1): ", askMeter, "1");
    return {true};
}

CommandResult Workspace::barInsert(std::string_view arguments)
{
    // A number of bars and a meter, either or both, in either order.
    const auto& song = document_.song();
    std::int64_t n = 1;
    std::optional<core::TimeSignature> signature;
    std::size_t at = 0;
    while (true)
    {
        const auto start = arguments.find_first_not_of(" \t", at);
        if (start == std::string_view::npos)
        {
            break;
        }
        const auto stop = std::min(arguments.find_first_of(" \t", start), arguments.size());
        const auto word = arguments.substr(start, stop - start);
        at = stop;
        std::int64_t number = 0;
        const auto [ptr, ec] = std::from_chars(word.data(), word.data() + word.size(), number);
        if (ec == std::errc{} && ptr == word.data() + word.size() && number > 0)
        {
            n = number;
        }
        else if (const auto meter = signatureOf(word, song.ppqn()))
        {
            signature = meter;
        }
        else
        {
            return CommandResult::error(
                "bar-insert takes a number of bars and a meter, such as 2 3/4");
        }
    }
    const auto& meter = song.meter();
    const auto bar = meter.toBarBeatTick(view().cursor().tick).bar;
    const auto from = meter.barStart(bar);
    if (from >= song.end())
    {
        return CommandResult::error("Bars go in before a bar of the song");
    }
    const auto what = (n == 1 ? std::string{"a bar"} : std::to_string(n) + " bars") +
                      (signature ? " of " + std::to_string(signature->numerator) + "/" +
                                       std::to_string(signature->denominator)
                                 : std::string{});
    auto edited = song;
    const auto edit = edited.insertBars(from, n, signature);
    if (!edit.done)
    {
        if (edit.misaligned)
        {
            return CommandResult::error(trackNameOf(song, *edit.misaligned) +
                                        "'s bars don't line up with " + what +
                                        "; put them in on each track instead");
        }
        return CommandResult::error("Can't insert " + what + " here");
    }
    document_.edit("Insert " + what + " before bar " + std::to_string(bar) + " on every track",
                   view().place(), [&](core::Song& s) { s = edited; });
    refresh();
    if (signature)
    {
        chosenMeter(*signature);
    }
    return {true, {}, false};
}

void Workspace::chosenMeter(core::TimeSignature meter)
{
    // Offered from now on, unless it's built in.
    if (std::ranges::find(offeredMeters(), meter) == offeredMeters().end())
    {
        remembered_.typedMeter(meter);
        rememberedChanged();
    }
}

CommandResult Workspace::renameSection(std::string_view name)
{
    const auto first = name.find_first_not_of(" \t");
    const auto index = document_.song().sectionAt(view().cursor().tick);
    const auto old = document_.song().sections()[index]->name();
    if (first == std::string_view::npos)
    {
        // Asking for one, with its name to edit, as c on the strip does.
        openPrompt("section-rename " + old);
        return {true};
    }
    name = name.substr(first, name.find_last_not_of(" \t") - first + 1);
    document_.edit("Rename " + old + " to " + std::string{name}, view().place(),
                   [&](core::Song& song) { song.renameSection(index, std::string{name}); });
    refresh();
    // As renamed, numbered if another section had the name.
    return {true, "Renamed " + old + " to " + document_.song().sections()[index]->name(), false};
}

void Workspace::openPrompt(std::string line)
{
    prompt_.answer = {};
    mode_ = Mode::CommandLine;
    prompt_.line.open(decodeUtf8(line));
}

void Workspace::ask(std::string question, std::function<void()> yes, std::function<void()> no,
                    std::function<void()> stop)
{
    auto text = std::move(question) + (stop ? " (y/n, Escape stops)" : " (y/n)");
    setMessage(text, MessageKind::Warning);
    question_ = Question{std::move(yes), std::move(no), std::move(stop), std::move(text)};
}

void Workspace::playFrom(core::Tick tick)
{
    if (player_ == nullptr)
    {
        refuse("Nothing to play to", MessageKind::Warning);
        return;
    }
    player_->setMarker(tick);
    if (!player_->play())
    {
        refuse("The engine is busy and didn't take that: try again", MessageKind::Warning);
        return;
    }
    following_ = true;
    showPlayhead();
}

void Workspace::setRemembered(Remembered remembered, std::function<void(const Remembered&)> changed)
{
    remembered_ = std::move(remembered);
    rememberedChanged_ = std::move(changed);
}

void Workspace::rememberedChanged()
{
    if (rememberedChanged_)
    {
        rememberedChanged_(remembered_);
    }
}

} // namespace modal::ui
