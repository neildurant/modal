// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/bar.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/key_notation.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <utility>

namespace modal::ui
{

namespace
{

constexpr Style kBarText{Role::BarText, Role::Bar, kPlain};
constexpr Style kCursor{Role::CursorText, Role::Cursor, kPlain};

constexpr CommandInfo atPrompt(std::string_view name, std::string_view description)
{
    return {.name = name, .description = description, .modes = modeBit(Mode::CommandLine)};
}

// Motions that only move the cursor, rather than landing on a boundary they name, so aren't
// ranges after an operator (see Ranges, objects and putting in the design): a bar's length on,
// the end of whatever the view's row is, and notes and chords, which objects take.
constexpr std::array<std::string_view, 8> kMovesOnly{
    "cursor-bar-next",  "cursor-bar-previous",  "cursor-first",      "cursor-last",
    "cursor-note-next", "cursor-note-previous", "cursor-chord-next", "cursor-chord-previous"};

// The commands every view shares, which the workspace runs itself.
constexpr std::array kSharedCommands{
    CommandInfo{.name = "operator-delete",
                .description = "Delete the events a motion or object takes (d]b, dib), or dd "
                               "this bar's",
                .count = true},
    CommandInfo{.name = "operator-yank",
                .description = "Yank the events a motion or object takes (y]b, yib), or yy "
                               "this bar's",
                .count = true},
    CommandInfo{.name = "operator-line",
                .description = "After an operator, this bar's events, or {count} bars' from it",
                .modes = modeBit(Mode::OperatorPending),
                .count = true},
    CommandInfo{.name = "object-bar-contents",
                .description = "After an operator, this bar's events (ib), or {count} bars' "
                               "from it",
                .modes = modeBit(Mode::OperatorPending),
                .count = true,
                .object = true},
    CommandInfo{.name = "object-bar",
                .description = "After an operator, this bar as time (ab), which d takes out, "
                               "closing the gap; or {count} bars from it",
                .modes = modeBit(Mode::OperatorPending),
                .count = true,
                .object = true},
    CommandInfo{.name = "object-note",
                .description = "After an operator, the note or event under the cursor (in)",
                .modes = modeBit(Mode::OperatorPending),
                .count = true,
                .object = true},
    CommandInfo{
        .name = "undo", .description = "Undo the last change, or {count} changes", .count = true},
    CommandInfo{.name = "redo",
                .description = "Redo the change undone last, or {count} changes",
                .count = true},
    CommandInfo{.name = "cancel", .description = "Cancel"},
    CommandInfo{.name = "play-toggle", .description = "Play from the play-from marker, or stop"},
    CommandInfo{.name = "play-cursor",
                .description = "Put the play-from marker at the cursor, and play from there"},
    CommandInfo{.name = "marker-set", .description = "Put the play-from marker at the cursor"},
    CommandInfo{.name = "panic",
                .description = "Stop, and send All Notes Off and Reset All Controllers"},
    CommandInfo{.name = "playhead-follow",
                .description = "Follow the playhead again, after moving away from it"},
    CommandInfo{.name = "port",
                .description = "Set the track's output port, by its name or part of it; or "
                               "with no name, show it and list the ports",
                .usage = "[NAME]",
                .keys = false},
    CommandInfo{.name = "section",
                .description = "Start a new section at the cursor's bar line, splitting the "
                               "section it's in; named Section and a number if no name is given",
                .usage = "[NAME]",
                .keys = false},
    CommandInfo{.name = "section-rename",
                .description = "Rename the section at the cursor",
                .usage = "NAME",
                .keys = false},
    CommandInfo{.name = "yank-this",
                .description = "Yank the note or event under the cursor, as yin does, or the "
                               "section on the section strip",
                .count = true},
    CommandInfo{.name = "rewind-toggle",
                .description = "Turn rewind on or off: whether the play-from marker stays put "
                               "when playback stops, or moves to where it stopped"},
    CommandInfo{.name = "grid-advance-toggle",
                .description = "Turn gridadvance on or off: whether O moves on by the grid step "
                               "or the note's length"},
    CommandInfo{.name = "command-line", .description = "Open the : command line"},
    CommandInfo{.name = "view-arrangement", .description = "Show the arrangement"},
    CommandInfo{.name = "view-piano-roll", .description = "Show the piano roll"},
    CommandInfo{.name = "view-events", .description = "Show the event list"},
    CommandInfo{.name = "view-back", .description = "Go back to the view shown before"},
    atPrompt("command-line-run", "Run the command line"),
    atPrompt("command-line-cancel", "Close the command line"),
    atPrompt("command-line-backspace", "Delete the character before the cursor"),
    atPrompt("command-line-delete", "Delete the character at the cursor"),
    atPrompt("command-line-left", "Move the cursor left"),
    atPrompt("command-line-right", "Move the cursor right"),
    atPrompt("command-line-start", "Move the cursor to the start"),
    atPrompt("command-line-end", "Move the cursor to the end"),
    atPrompt("command-line-clear", "Clear the command line"),
    atPrompt("command-line-older", "Recall the line run before"),
    atPrompt("command-line-newer", "Recall the line run after"),
};

bool isShared(std::string_view name)
{
    return std::ranges::find(kSharedCommands, name, &CommandInfo::name) != kSharedCommands.end();
}

} // namespace

std::optional<bool> editLine(CommandLine& line, std::string_view command)
{
    if (command == "command-line-backspace")
    {
        return line.backspace();
    }
    if (command == "command-line-delete")
    {
        return line.erase();
    }
    if (command == "command-line-older")
    {
        return line.older();
    }
    if (command == "command-line-newer")
    {
        return line.newer();
    }
    if (command == "command-line-left")
    {
        line.left();
    }
    else if (command == "command-line-right")
    {
        line.right();
    }
    else if (command == "command-line-start")
    {
        line.start();
    }
    else if (command == "command-line-end")
    {
        line.end();
    }
    else if (command == "command-line-clear")
    {
        line.clear();
    }
    else
    {
        return std::nullopt;
    }
    return true;
}

std::span<const CommandInfo> Workspace::commands()
{
    // The views' own commands, then the shared ones.
    static const std::vector<CommandInfo> all = []
    {
        std::vector<CommandInfo> result;
        const auto add = [&](std::span<const CommandInfo> commands)
        {
            for (const auto& info : commands)
            {
                if (std::ranges::find(result, info.name, &CommandInfo::name) == result.end())
                {
                    result.push_back(info);
                }
            }
        };
        add(EventListView::commands());
        add(ArrangeView::commands());
        add(PianoRollView::commands());
        add(kSharedCommands);
        return result;
    }();
    return all;
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
    , current_(eventList_.get())
    , keymap_(std::move(keymap))
    , keys_(timeout)
{
    for (const auto& info : commands())
    {
        registry.add(info,
                     [this, name = info.name](const CommandCall& call) -> CommandResult
                     {
                         if (name == "port")
                         {
                             return port(call.arguments);
                         }
                         if (name == "section")
                         {
                             return section(call.arguments);
                         }
                         if (name == "section-rename")
                         {
                             return renameSection(call.arguments);
                         }
                         if (auto result = view().runLine(name, call.arguments))
                         {
                             return *result;
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

void Workspace::buildOperatorKeymap()
{
    operatorKeymap_ = keymap_;
    for (const auto& [keys, binding] : keymap_.bindings(Mode::Normal))
    {
        const auto* info = commands_.find(binding.command);
        const bool again =
            binding.command == "operator-delete" || binding.command == "operator-yank";
        const bool wanted = again || binding.command == "cancel" || (info && info->motion);
        if (wanted && keymap_.match(Mode::OperatorPending, keys).binding == nullptr)
        {
            operatorKeymap_.bind(Mode::OperatorPending, keys,
                                 again ? Binding{"operator-line", binding.hint} : binding);
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
    std::string text;
    if (operator_)
    {
        text = (operator_->count > 0 ? std::to_string(operator_->count) : std::string{}) +
               (operator_->name == "operator-delete" ? "d" : "y");
    }
    return text + keys_.pending();
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
}

void Workspace::showSong()
{
    eventList_->showSong();
    arrange_->showSong();
    pianoRoll_->showSong();
    history_.clear();
    layOut();
}

ViewKind Workspace::shown() const
{
    return current_ == arrange_.get()     ? ViewKind::Arrangement
           : current_ == pianoRoll_.get() ? ViewKind::PianoRoll
                                          : ViewKind::EventList;
}

void Workspace::show(ViewKind kind)
{
    switch (kind)
    {
    case ViewKind::Arrangement:
        switchTo(*arrange_, true);
        break;
    case ViewKind::EventList:
        switchTo(*eventList_, true);
        break;
    case ViewKind::PianoRoll:
        switchTo(*pianoRoll_, true);
        break;
    }
}

void Workspace::open(ViewKind kind)
{
    switchTo(kind == ViewKind::Arrangement ? static_cast<View&>(*arrange_)
             : kind == ViewKind::PianoRoll ? static_cast<View&>(*pianoRoll_)
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
    lastTime_ = now;
    const bool hadMessage = !std::exchange(message_, std::string{}).empty();
    // A question asked takes the next key as its answer: y for yes, any other for no.
    if (auto yes = std::exchange(yes_, nullptr))
    {
        if (key.named == NamedKey::None && key.character == U'y' &&
            (key.modifiers & (kCtrl | kAlt)) == 0)
        {
            yes();
        }
        else
        {
            setMessage("Nothing done", MessageKind::Information);
        }
        return true;
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
                line_.insert(key.character);
            }
            return true;
        }
    }
    const auto before = pending();
    const auto& keymap = mode_ == Mode::OperatorPending ? operatorKeymap_ : keymap_;
    const auto actions = keys_.key(keymap, mode_, key, now);
    const bool changed = runAll(actions);
    // Keys after an operator that are no motion give the operator up, as in Vim.
    if (mode_ == Mode::OperatorPending && actions.empty() && keys_.pending().empty())
    {
        operator_.reset();
        mode_ = Mode::Normal;
    }
    return changed || hadMessage || pending() != before;
}

std::optional<std::int64_t> Workspace::deadline() const
{
    auto deadline = keys_.deadline();
    if (wasPlaying_)
    {
        const auto frame = lastTime_ + kFrameInterval;
        deadline = deadline ? std::min(*deadline, frame) : frame;
    }
    return deadline;
}

bool Workspace::wake(std::int64_t now)
{
    lastTime_ = now;
    const auto before = keys_.pending();
    const auto& keymap = mode_ == Mode::OperatorPending ? operatorKeymap_ : keymap_;
    bool changed = runAll(keys_.timeout(keymap, now));
    if (wasPlaying_)
    {
        player_->poll();
        showPlayhead();
        changed = true;
    }
    return changed || keys_.pending() != before;
}

bool Workspace::runAll(const std::vector<Action>& actions)
{
    bool changed = false;
    for (const auto& action : actions)
    {
        changed = run(action.command, action.count).changed || changed;
    }
    return changed;
}

CommandResult Workspace::run(std::string_view command, int count)
{
    auto result = commands_.run(command, count, mode_);
    if (!result.message.empty())
    {
        setMessage(result.message,
                   result.warning ? MessageKind::Warning : MessageKind::Information);
    }
    return result;
}

bool Workspace::runViewCommand(std::string_view command, int count)
{
    const auto cursor = view().cursor();
    if (const auto changed = view().run(command, count))
    {
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
        return *changed;
    }
    const auto* info = commands_.find(command);
    auto title = info != nullptr ? titleOf(*info) : std::string{command};
    setMessage(title + " isn't in " + std::string{view().name()}, MessageKind::Information);
    return true;
}

std::optional<bool> Workspace::runShared(std::string_view command, int count)
{
    const int n = std::max(count, 1);
    if (command.starts_with("command-line-"))
    {
        return runCommandLine(command);
    }
    if (command == "operator-delete" || command == "operator-yank")
    {
        operator_ = PendingOperator{std::string{command}, count};
        mode_ = Mode::OperatorPending;
        return true;
    }
    if (command == "undo" || command == "redo")
    {
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
    if (command == "play-toggle" || command == "play-cursor" || command == "marker-set" ||
        command == "panic")
    {
        return transport(command);
    }
    if (command == "playhead-follow")
    {
        following_ = true;
        showPlayhead();
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
        line_.open();
        return true;
    }
    if (command == "yank-this")
    {
        // The section strip yanks its section; everywhere else it's yin.
        if (const auto done = view().run(command, count))
        {
            return *done;
        }
        return view().operate("operator-yank", "object-note", count);
    }
    if (command == "view-arrangement" || command == "view-events" || command == "view-piano-roll")
    {
        show(command == "view-arrangement" ? ViewKind::Arrangement
             : command == "view-events"    ? ViewKind::EventList
                                           : ViewKind::PianoRoll);
        return true;
    }
    if (command == "view-back")
    {
        if (history_.empty())
        {
            setMessage("No view to go back to", MessageKind::Information);
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
    const auto op = *operator_;
    operator_.reset();
    mode_ = Mode::Normal;
    if (command == "cancel")
    {
        return true;
    }
    // Counts before the operator and after it multiply, as in Vim: 2d3j is d6j.
    const int total = op.count == 0 && count == 0 ? 0 : std::max(op.count, 1) * std::max(count, 1);
    if (command != "operator-line")
    {
        const auto* info = commands_.find(command);
        if (info == nullptr || !(info->motion || info->object))
        {
            return false;
        }
        if (std::ranges::find(kMovesOnly, command) != kMovesOnly.end())
        {
            setMessage(keysOf(command) + " only moves the cursor; for a range, use " +
                           keysOf("cursor-bar-line-next") + " or " + keysOf("cursor-section-next") +
                           ", or an object such as ib",
                       MessageKind::Information);
            return true;
        }
    }
    return view().operate(op.name, command, total);
}

std::string Workspace::keysOf(std::string_view command) const
{
    std::string shortest;
    for (const auto& [keys, binding] : keymap_.bindings(Mode::Normal))
    {
        if (binding.command == command)
        {
            auto text = formatKeys(keys);
            if (shortest.empty() || text.size() < shortest.size())
            {
                shortest = std::move(text);
            }
        }
    }
    return shortest.empty() ? std::string{command} : shortest;
}

bool Workspace::undoOrRedo(bool undo)
{
    const auto step = undo ? document_.undo() : document_.redo();
    if (!step)
    {
        setMessage(undo ? "Nothing to undo" : "Nothing to redo", MessageKind::Information);
        return true;
    }
    // Back where the change was made: its track, time and scroll.
    view().restore(step->place);
    setMessage((undo ? "Undid: " : "Redid: ") + step->title, MessageKind::Information);
    return true;
}

std::optional<bool> Workspace::runCommandLine(std::string_view command)
{
    if (mode_ != Mode::CommandLine)
    {
        return false;
    }
    if (view().typing())
    {
        return view().runTyping(command);
    }
    if (command == "command-line-run")
    {
        mode_ = Mode::Normal;
        const auto text = line_.take();
        if (lineHandler_ && !text.empty())
        {
            lineHandler_(text);
        }
        return true;
    }
    if (command == "command-line-cancel" ||
        (command == "command-line-backspace" && line_.text().empty())) // as in Vim
    {
        mode_ = Mode::Normal;
        return true;
    }
    return editLine(line_, command);
}

bool Workspace::transport(std::string_view command)
{
    if (player_ == nullptr)
    {
        setMessage("Nothing to play to", MessageKind::Warning);
        return true;
    }
    if (command == "play-toggle")
    {
        player_->toggle();
    }
    else if (command == "play-cursor" || command == "marker-set")
    {
        player_->setMarker(view().cursor().tick);
        if (command == "play-cursor")
        {
            player_->play();
            following_ = true;
        }
    }
    else
    {
        player_->panic();
    }
    showPlayhead();
    return true;
}

void Workspace::setPlayer(core::Player* player)
{
    player_ = player;
    if (player_ != nullptr)
    {
        player_->setRewind(rewind_);
    }
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
    }
    if (playing)
    {
        // Until the engine starts, the playhead is at the marker, where it will start.
        view().showPlayhead(player_->position().value_or(player_->marker()),
                            !following_ ? Follow::None
                            : rewind_   ? Follow::View
                                        : Follow::Cursor);
    }
    else if (wasPlaying_)
    {
        // Stopped: with rewind, or once moved away from the playhead, back to the cursor;
        // otherwise the cursor goes with the marker to where playback stopped, once the
        // engine says where.
        view().stopPlayhead(rewind_ || !following_ ? std::nullopt
                                                   : std::optional{player_->marker()});
    }
    wasPlaying_ = playing || (player_ != nullptr && player_->stopping());
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
    const std::string wanted = name.empty() ? song.newSectionName() : std::string{name};
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

CommandResult Workspace::renameSection(std::string_view name)
{
    const auto first = name.find_first_not_of(" \t");
    if (first == std::string_view::npos)
    {
        return CommandResult::error("section-rename needs a name");
    }
    name = name.substr(first, name.find_last_not_of(" \t") - first + 1);
    const auto index = document_.song().sectionAt(view().cursor().tick);
    const auto old = document_.song().sections()[index]->name();
    document_.edit("Rename " + old + " to " + std::string{name}, view().place(),
                   [&](core::Song& song) { song.renameSection(index, std::string{name}); });
    refresh();
    // As renamed, numbered if another section had the name.
    return {true, "Renamed " + old + " to " + document_.song().sections()[index]->name(), false};
}

void Workspace::openPrompt(std::string line)
{
    mode_ = Mode::CommandLine;
    line_.open(decodeUtf8(line));
}

void Workspace::ask(std::string question, std::function<void()> yes)
{
    setMessage(std::move(question) + " (y/n)", MessageKind::Warning);
    yes_ = std::move(yes);
}

void Workspace::playFrom(core::Tick tick)
{
    if (player_ == nullptr)
    {
        setMessage("Nothing to play to", MessageKind::Warning);
        return;
    }
    player_->setMarker(tick);
    player_->play();
    following_ = true;
    showPlayhead();
}

Frame Workspace::frame() const
{
    Frame frame;
    view().addRegions(frame);
    frame.push_back(mode_ == Mode::CommandLine && !view().typing() ? promptRegion()
                                                                   : statusRegion());
    return frame;
}

Region Workspace::promptRegion() const
{
    // ":" and the line, from where the cursor still fits, and the cursor as a cell of its own.
    const int margin = BarSpacing::forCell(cell_).margin;
    const auto& text = line_.text();
    const auto cursor = line_.cursor();
    const auto room =
        static_cast<std::size_t>(std::max((width_ - 2 * margin) / cell_.width - 2, 1));
    const std::size_t first = cursor >= room ? cursor - room + 1 : 0;
    Region region{
        .bounds = layout_.bottom[0], .background = Role::Bar, .text = {}, .under = {}, .over = {}};
    const auto x = [&](std::size_t column)
    { return margin + static_cast<int>(column) * cell_.width; };
    region.text.push_back({x(0), 0, U":", kBarText});
    region.text.push_back({x(1), 0, text.substr(first, cursor - first), kBarText});
    const char32_t under = cursor < text.size() ? text[cursor] : U' ';
    region.text.push_back({x(1 + cursor - first), 0, std::u32string(1, under), kCursor});
    if (cursor + 1 < text.size())
    {
        region.text.push_back({x(2 + cursor - first), 0, text.substr(cursor + 1), kBarText});
    }
    return region;
}

Region Workspace::statusRegion() const
{
    const Style label{Role::Dim, Role::Bar, kPlain};
    Bar bottom;
    bottom.left = {{view().typing() ? " EDIT " : " NORMAL ", {Role::ModeText, Role::Mode, kBold}}};
    if (!message_.empty())
    {
        const auto role = messageKind_ == MessageKind::Warning ? Role::Warning : Role::Message;
        bottom.left.push_back({"  " + message_, {role, Role::Bar, kPlain}});
    }
    else if (const auto at = player_ != nullptr ? player_->position() : std::nullopt)
    {
        // While playing, where the playhead is.
        bottom.left.push_back({"  ▶ " + view().timeStatus(*at), {Role::Accent, Role::Bar, kBold}});
    }
    else
    {
        bottom.left.push_back({"  " + view().cursorStatus(), kBarText});
    }
    if (const auto keys = pending(); !keys.empty())
    {
        bottom.right.push_back({keys + "   ", {Role::Accent, Role::Bar, kBold}});
    }
    if (player_ != nullptr)
    {
        bottom.right.push_back({"From ", label});
        bottom.right.push_back({view().position(player_->marker()) + "   ", kBarText});
    }
    bottom.right.push_back({rewind_ ? "Rewind on   " : "Rewind off   ", kBarText});
    // The song's file, and [+] if it has changes not saved, as in Vim.
    const auto& file = document_.file();
    const auto& imported = document_.importedFrom();
    const auto name = file       ? utf8Text(file->filename())
                      : imported ? utf8Text(imported->filename()) + " (imported)"
                                 : std::string{"No file"};
    bottom.right.push_back({name + (document_.modified() ? " [+]" : ""), kBarText});

    auto region = layoutBar(bottom, width_, cell_, BarSpacing::forCell(cell_));
    region.bounds = layout_.bottom[0];
    return region;
}

} // namespace modal::ui
