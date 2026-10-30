// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/bar.hpp>
#include <modal/ui/completion.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/event_edit.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/key_notation.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <tuple>
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
    CommandInfo{.name = "operator-transpose-up",
                .description = "Transpose the notes a range or object takes up a semitone (>ib), "
                               "or {count} semitones (3>ib); >> this bar's",
                .count = true},
    CommandInfo{.name = "operator-transpose-down",
                .description = "Transpose the notes a range or object takes down a semitone "
                               "(<ib), or {count} semitones; << this bar's",
                .count = true},
    CommandInfo{.name = "operator-shift-later",
                .description = "Move the events a range or object takes later by the grid step "
                               "(t]b), or {count} steps; tt this bar's. In the piano roll, its "
                               "notes",
                .count = true},
    CommandInfo{.name = "operator-shift-earlier",
                .description = "Move the events a range or object takes earlier by the grid "
                               "step (T]b), or {count} steps; TT this bar's",
                .count = true},
    CommandInfo{.name = "operator-quantize",
                .description = "Quantise the starts of the notes a range or object takes to the "
                               "grid (qib); qq this bar's",
                .count = true},
    CommandInfo{.name = "operator-velocity",
                .description = "Set or change the velocity of the notes a range or object takes, "
                               "asking how: a value (80), a change (+10, -10) or a percentage "
                               "(80%); ^^ this bar's",
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
                .keys = false,
                .choices = {Choices::Port}},
    CommandInfo{.name = "section-split",
                .description = "Start a new section at the cursor's bar line, splitting the "
                               "section it's in; named Section and a number if no name is given",
                .usage = "[NAME]",
                .keys = false},
    CommandInfo{.name = "bar-delete",
                .description = "Delete the cursor's bar, or {count} bars from it, on every track, "
                               "closing up; they go in the register as a section",
                .count = true,
                .keys = false},
    CommandInfo{.name = "bar-insert",
                .description = "Insert an empty bar, or N, before the cursor's bar on every "
                               "track, in the meter there or one given (2 3/4)",
                .usage = "[N] [METER]",
                .keys = false,
                .choices = {Choices::Meter, Choices::Meter, Choices::Meter}},
    CommandInfo{.name = "marker-to-section",
                .description = "Start a section at the marker at or before the cursor, named "
                               "after it, at the bar line of its bar; the marker goes",
                .keys = false},
    CommandInfo{.name = "marker-to-section-all",
                .description = "Start a section at every marker, asking about those off a bar "
                               "line, as one undo step",
                .keys = false},
    CommandInfo{.name = "section-rename",
                .description = "Rename the section at the cursor",
                .usage = "NAME",
                .keys = false,
                .choices = {Choices::Section}},
    CommandInfo{.name = "yank-this",
                .description = "Yank the note or event under the cursor, as yin does, or the "
                               "section on the section strip",
                .count = true},
    CommandInfo{.name = "keys-show",
                .description = "Show the keys that do something here, in groups: q or Escape "
                               "closes it, j and k turn its pages"},
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
    atPrompt("command-line-next", "Set the field and go to the next, in the inspector"),
    atPrompt("command-line-previous", "Set the field and go to the one before, in the inspector"),
    atPrompt("command-line-page-down", "A screen on in the matches of completion"),
    atPrompt("command-line-page-up", "A screen back in the matches of completion"),
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
        // Where the views' commands do something, kept here beside each other, since a view
        // runs commands others describe; a test runs each binding offered in each place.
        using enum Where;
        const std::array<std::pair<std::string_view, std::uint8_t>, 56> where{{
            {"operator-velocity", onlyIn({Tracks, PianoRoll, EventList})},
            {"operator-transpose-up", onlyIn({Tracks, PianoRoll, EventList})},
            {"operator-transpose-down", onlyIn({Tracks, PianoRoll, EventList})},
            {"operator-shift-later", onlyIn({Tracks, PianoRoll, EventList})},
            {"operator-shift-earlier", onlyIn({Tracks, PianoRoll, EventList})},
            {"operator-quantize", onlyIn({Tracks, PianoRoll, EventList})},
            {"change", onlyIn({Strip})},
            {"insert-before", onlyIn({Strip})},
            {"insert-after", onlyIn({Strip})},
            {"go-section-strip", onlyIn({Tracks, Strip})},
            {"join", onlyIn({Strip, PianoRoll})},
            {"move-left", onlyIn({Strip, PianoRoll})},
            {"move-right", onlyIn({Strip, PianoRoll})},
            {"register-put-before", onlyIn({Tracks, Strip, PianoRoll})},
            {"zoom-in", onlyIn({Tracks, Strip, PianoRoll})},
            {"zoom-out", onlyIn({Tracks, Strip, PianoRoll})},
            {"zoom-reset", onlyIn({Tracks, Strip, PianoRoll})},
            {"field-edit", onlyIn({Tracks, Strip, EventList})},
            {"field-increase", onlyIn({Strip, PianoRoll, EventList})},
            {"field-decrease", onlyIn({Strip, PianoRoll, EventList})},
            {"field-increase-fine", onlyIn({Strip, PianoRoll, EventList})},
            {"field-decrease-fine", onlyIn({Strip, PianoRoll, EventList})},
            {"number-increase", onlyIn({Strip, PianoRoll, EventList})},
            {"number-decrease", onlyIn({Strip, PianoRoll, EventList})},
            {"note-add", onlyIn({PianoRoll, EventList})},
            {"cursor-chord-next", onlyIn({PianoRoll, EventList})},
            {"cursor-chord-previous", onlyIn({PianoRoll, EventList})},
            {"cursor-bar-next-scroll", onlyIn({Tracks, PianoRoll})},
            {"cursor-bar-previous-scroll", onlyIn({Tracks, PianoRoll})},
            {"cursor-bar-next", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-bar-previous", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-bar-line-next", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-bar-line-previous", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-section-next", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-section-previous", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-marker-next", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-marker-previous", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-left-scroll", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-right-scroll", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-up-scroll", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-down-scroll", onlyIn({Tracks, PianoRoll, EventList})},
            {"scroll-centre", onlyIn({Tracks, PianoRoll, EventList})},
            {"scroll-half-down", onlyIn({Tracks, PianoRoll, EventList})},
            {"scroll-half-up", onlyIn({Tracks, PianoRoll, EventList})},
            {"scroll-page-down", onlyIn({Tracks, PianoRoll, EventList})},
            {"scroll-page-up", onlyIn({Tracks, PianoRoll, EventList})},
            {"cursor-note-next", onlyIn({PianoRoll})},
            {"cursor-note-previous", onlyIn({PianoRoll})},
            {"octave-up", onlyIn({PianoRoll})},
            {"note-add-advance", onlyIn({PianoRoll})},
            {"default-note-set", onlyIn({PianoRoll})},
            {"grid-finer", onlyIn({PianoRoll})},
            {"grid-coarser", onlyIn({PianoRoll})},
            {"grid-triplets", onlyIn({PianoRoll})},
            {"grid", onlyIn({PianoRoll})},
            {"len", onlyIn({PianoRoll})},
        }};
        for (auto& info : result)
        {
            if (const auto it = std::ranges::find(
                    where, info.name, &std::pair<std::string_view, std::uint8_t>::first);
                it != where.end())
            {
                info.where = it->second;
            }
            else if (info.name.starts_with("move-"))
            {
                info.where = onlyIn({PianoRoll}); // the Alt keys move notes
            }
        }
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
                         if (name == "section-split")
                         {
                             return section(call.arguments);
                         }
                         if (name == "marker-to-section")
                         {
                             return markerToSection();
                         }
                         if (name == "marker-to-section-all")
                         {
                             return markerToSectionAll();
                         }
                         if (name == "bar-delete")
                         {
                             return barDelete(call.count);
                         }
                         if (name == "bar-insert")
                         {
                             return barInsert(call.arguments);
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
        const bool again = isOperator(binding.command);
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
        auto keys = keysOf(operator_->name);
        if (keys == "<lt>")
        {
            keys = "<";
        }
        text = (operator_->count > 0 ? std::to_string(operator_->count) : std::string{}) + keys;
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
    // A question asked takes the next key as its answer: y for yes, Escape to stop a run of
    // them, any other for no.
    if (auto question = std::exchange(question_, std::nullopt))
    {
        const bool escape = key.named == NamedKey::Escape;
        if (key.named == NamedKey::None && key.character == U'y' &&
            (key.modifiers & (kCtrl | kAlt)) == 0)
        {
            question->yes();
        }
        else if (escape && question->stop)
        {
            question->stop();
        }
        else if (!escape && question->no)
        {
            question->no();
        }
        else
        {
            setMessage("Nothing done", MessageKind::Information);
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
                line_.insert(key.character);
                if (listed)
                {
                    refilter(); // the matches follow what's typed
                }
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
    const bool hinted = hints().has_value();
    noteWaiting(now);
    return changed || hadMessage || pending() != before || hinted != hints().has_value();
}

void Workspace::noteWaiting(std::int64_t now)
{
    // Keys waiting for more, or an operator waiting for its range, start the wait for the hint
    // menu, or show it at once with no delay.
    if (keys_.keys().empty() && !operator_)
    {
        waitingSince_.reset();
        hintsShown_ = false;
        return;
    }
    if (!waitingSince_ || !hintsShown_)
    {
        waitingSince_ = now;
        hintsShown_ = hintDelay_ == 0;
    }
}

std::optional<std::vector<Workspace::Hint>> Workspace::hints() const
{
    if (!hintsOn_ || !hintsShown_ || (keys_.keys().empty() && !operator_))
    {
        return std::nullopt;
    }
    if (keys_.keys().empty())
    {
        auto hints = operatorHints();
        return hints.empty() ? std::nullopt : std::optional{std::move(hints)};
    }
    const auto mode = keys_.mode();
    const auto& keymap = mode == Mode::OperatorPending ? operatorKeymap_ : keymap_;
    auto hints = hintsAfter(keymap, mode, keys_.keys());
    if (hints.empty())
    {
        return std::nullopt;
    }
    return hints;
}

namespace
{

// The hint menu's order (decided October 2026, after Doom Emacs's): named keys first (<CR>,
// <Space>, <Tab>), then other characters, then letters, ignoring case, so t and T come together,
// t first.
auto hintOrder(const std::string& keys)
{
    const auto text = decodeUtf8(keys);
    const bool named = text.size() > 1 && text.front() == U'<';
    const char32_t first = text.empty() ? 0 : text.front();
    const bool letter = (first >= U'a' && first <= U'z') || (first >= U'A' && first <= U'Z');
    std::u32string folded = text;
    for (auto& c : folded)
    {
        if (c >= U'A' && c <= U'Z')
        {
            c = c - U'A' + U'a';
        }
    }
    return std::tuple{named ? 0 : letter ? 2 : 1, folded, text != folded};
}

// A key as the hint menu shows it: an arrow without modifiers as the arrow itself, so h and
// Left read together as h/←; the others in notation.
std::string hintKey(const Key& key)
{
    if (key.modifiers == kNoModifiers)
    {
        switch (key.named)
        {
        case NamedKey::Left:
            return "←";
        case NamedKey::Right:
            return "→";
        case NamedKey::Up:
            return "↑";
        case NamedKey::Down:
            return "↓";
        default:
            break;
        }
    }
    return formatKey(key);
}

// The keys of one command together, as h/← or <A-h>/<A-Left>: characters first, then
// characters with modifiers, then named keys, which sort first on their own.
void addKeys(std::vector<std::string>& keys, std::string more)
{
    keys.push_back(std::move(more));
    std::ranges::stable_sort(keys, {},
                             [](const std::string& k)
                             {
                                 const auto text = decodeUtf8(k);
                                 if (text.size() > 2 && text.front() == U'<')
                                 {
                                     const auto dash = text.find_last_of(U'-');
                                     const auto name =
                                         text.substr(dash == std::u32string::npos ? 1 : dash + 1);
                                     return name.size() == 2 ? 1 : 2; // a character and >
                                 }
                                 const bool arrow =
                                     text.size() == 1 && text[0] >= U'←' && text[0] <= U'↓';
                                 return arrow ? 2 : 0;
                             });
}

std::string joined(const std::vector<std::string>& keys)
{
    std::string text;
    for (const auto& k : keys)
    {
        text += (text.empty() ? "" : "/") + k;
    }
    return text;
}

} // namespace

Where Workspace::where() const
{
    switch (shown())
    {
    case ViewKind::Arrangement:
        return arrange_->onStrip() ? Where::Strip : Where::Tracks;
    case ViewKind::PianoRoll:
        return Where::PianoRoll;
    case ViewKind::EventList:
        break;
    }
    return Where::EventList;
}

bool Workspace::offered(std::string_view command) const
{
    const auto* info = commands_.find(command);
    return info == nullptr || info->where == 0 || (info->where & whereBit(where())) != 0;
}

std::vector<Workspace::Hint> Workspace::hintsAfter(const Keymap& keymap, Mode mode,
                                                   std::span<const Key> keys) const
{
    // Each key that can come next, once, with the title of its binding, or if it starts longer
    // ones, of its group; keys bound to one command together, as h/←.
    std::vector<Hint> hints;
    std::vector<std::vector<std::string>> keysOf; // each hint's keys
    std::vector<std::string> commands;            // and its command, if it's one
    std::vector<Key> previous;
    for (const auto& [sequence, binding] : keymap.bindings(mode))
    {
        if (sequence.size() <= keys.size() ||
            !std::ranges::equal(keys, std::span{sequence}.first(keys.size())) ||
            !offered(binding.command))
        {
            continue;
        }
        const std::vector<Key> next{
            sequence.begin(), sequence.begin() + static_cast<std::ptrdiff_t>(keys.size()) + 1};
        if (next == previous)
        {
            continue;
        }
        previous = next;
        const auto key = hintKey(keys_.typed(next.back()));
        Hint hint{.keys = key, .title = {}, .group = false};
        if (sequence.size() == next.size())
        {
            const auto* info = commands_.find(binding.command);
            hint.title = !binding.hint.empty() ? binding.hint
                         : info != nullptr     ? titleOf(*info)
                                               : binding.command;
            // A count, which the menu can't take, as N: "bar N".
            if (const auto at = hint.title.find("{count}"); at != std::string::npos)
            {
                hint.title.replace(at, 7, "N");
            }
        }
        // Keys that start longer bindings, some of which work here.
        const auto more = std::ranges::any_of(
            keymap.bindings(mode),
            [&](const auto& other)
            {
                return other.first.size() > next.size() &&
                       std::ranges::equal(next, std::span{other.first}.first(next.size())) &&
                       offered(other.second.command);
            });
        if (more)
        {
            hint.group = true;
            const auto title = keymap.groupTitle(mode, next);
            if (hint.title.empty())
            {
                hint.title = title.empty() ? std::string{"…"} : title;
            }
        }
        const auto command =
            !hint.group && sequence.size() == next.size() ? binding.command : std::string{};
        if (const auto same = std::ranges::find(commands, command);
            !command.empty() && same != commands.end())
        {
            addKeys(keysOf[static_cast<std::size_t>(same - commands.begin())], key);
            continue;
        }
        hints.push_back(std::move(hint));
        keysOf.push_back({key});
        commands.push_back(command);
    }
    for (std::size_t i = 0; i < hints.size(); ++i)
    {
        hints[i].keys = joined(keysOf[i]);
    }
    std::ranges::stable_sort(hints, {}, [](const Hint& h) { return hintOrder(h.keys); });
    return hints;
}

std::vector<Workspace::Hint> Workspace::operatorHints() const
{
    // After an operator, the ranges and objects it can take, as whole keys (]b, ib), with what
    // each would take in this view: the cursor's bar first, then the objects, then the ranges.
    // Keys that only move the cursor aren't ranges, so aren't shown.
    std::vector<std::pair<int, Hint>> ranked;
    std::vector<std::string> seen;                      // the commands, as ranked has them
    std::vector<std::vector<std::string>> alternatives; // and their keys
    for (const auto& [sequence, binding] : operatorKeymap_.bindings(Mode::OperatorPending))
    {
        const auto& command = binding.command;
        const auto* info = commands_.find(command);
        const bool line = command == "operator-line";
        if (sequence.size() > 2 || info == nullptr || !(line || info->motion || info->object) ||
            std::ranges::find(kMovesOnly, command) != kMovesOnly.end())
        {
            continue;
        }
        if (line && formatKeys(sequence) != keysOf(operator_->name))
        {
            continue; // dd after d, yy after y
        }
        auto text = view().describeRange(operator_->name, command);
        if (!text)
        {
            continue;
        }
        std::string written;
        for (const auto& k : sequence)
        {
            written += hintKey(keys_.typed(k));
        }
        // A command bound to several keys, shown once with them all, as l/→.
        if (const auto at = std::ranges::find(seen, command); at != seen.end())
        {
            addKeys(alternatives[static_cast<std::size_t>(at - seen.begin())], std::move(written));
            continue;
        }
        seen.push_back(command);
        alternatives.push_back({written});
        ranked.emplace_back(
            line           ? 0
            : info->object ? 1
                           : 2,
            Hint{.keys = std::move(written), .title = std::move(*text), .group = false});
    }
    for (std::size_t i = 0; i < ranked.size(); ++i)
    {
        ranked[i].second.keys = joined(alternatives[i]);
    }
    std::ranges::stable_sort(ranked, {}, [](const std::pair<int, Hint>& r)
                             { return std::tuple{r.first, hintOrder(r.second.keys)}; });
    std::vector<Hint> hints;
    for (auto& [rank, hint] : ranked)
    {
        hints.push_back(std::move(hint));
    }
    return hints;
}

std::vector<Workspace::KeyGroup> Workspace::keyList() const
{
    // Normal mode's bindings that do something here, and the objects after an operator, under
    // their sections, the sections in modalrc's order. A command shows once, with all its keys,
    // in the group its first binding is in, and commands doing the same here (= and + and
    // Ctrl-A stepping a field) together; a binding without a hint isn't listed beside one of its
    // command with one, as the keypad's and other keyboards' keys for bigger text aren't.
    struct Item
    {
        std::string title;
        std::vector<std::string> keys;
        std::vector<std::string> commands;
    };
    std::vector<std::pair<std::string, std::vector<Item>>> groups;
    for (const auto mode : {Mode::Normal, Mode::OperatorPending})
    {
        const auto bindings = keymap_.bindings(mode);
        const auto hinted = [&](const std::string& command)
        {
            return std::ranges::any_of(
                bindings, [&](const auto& b)
                { return b.second.command == command && !b.second.hint.empty(); });
        };
        for (const auto& [sequence, binding] : bindings)
        {
            if (!offered(binding.command) || binding.command == "cancel" ||
                (binding.hint.empty() && hinted(binding.command)))
            {
                continue;
            }
            std::string written;
            for (const auto& k : sequence)
            {
                written += hintKey(keys_.typed(k));
            }
            const auto placed = [&]
            {
                for (auto& [title, items] : groups)
                {
                    for (auto& item : items)
                    {
                        if (std::ranges::find(item.commands, binding.command) !=
                            item.commands.end())
                        {
                            addKeys(item.keys, written);
                            return true;
                        }
                    }
                }
                return false;
            };
            if (placed())
            {
                continue;
            }
            const auto relabelled = view().relabel(binding.command);
            const auto section = relabelled && !relabelled->first.empty() ? relabelled->first
                                 : binding.section.empty() ? std::string{"Other keys"}
                                                           : binding.section;
            const auto* info = commands_.find(binding.command);
            auto name = relabelled              ? relabelled->second
                        : !binding.hint.empty() ? binding.hint
                        : info != nullptr       ? titleOf(*info)
                                                : binding.command;
            if (const auto at = name.find("{count}"); at != std::string::npos)
            {
                name.replace(at, 7, "N");
            }
            auto group = std::ranges::find(groups, section,
                                           &std::pair<std::string, std::vector<Item>>::first);
            if (group == groups.end())
            {
                groups.emplace_back(section, std::vector<Item>{});
                group = std::prev(groups.end());
            }
            auto& items = group->second;
            if (const auto same = std::ranges::find(items, name, &Item::title); same != items.end())
            {
                addKeys(same->keys, written);
                same->commands.push_back(binding.command);
                continue;
            }
            items.push_back({std::move(name), {written}, {binding.command}});
        }
    }
    std::vector<KeyGroup> result;
    for (auto& [title, items] : groups)
    {
        KeyGroup group{title, {}};
        for (auto& item : items)
        {
            group.keys.push_back(
                {.keys = joined(item.keys), .title = std::move(item.title), .group = false});
        }
        std::ranges::stable_sort(group.keys, {}, [](const Hint& h) { return hintOrder(h.keys); });
        result.push_back(std::move(group));
    }
    // In the order modalrc's sections come in, those it has none for last.
    const auto& order = keymap_.sections();
    std::ranges::stable_sort(result, {},
                             [&](const KeyGroup& group)
                             {
                                 const auto at = std::ranges::find(order, group.title);
                                 return at == order.end()
                                            ? order.size()
                                            : static_cast<std::size_t>(at - order.begin());
                             });
    return result;
}

std::vector<std::vector<Workspace::KeyColumn>>
Workspace::keyPages(const std::vector<KeyGroup>& groups) const
{
    // Down each column and then across, a group's title never last in a column, as many
    // columns to a page as fit across, each as wide as what's in it.
    const int margin = BarSpacing::forCell(cell_).margin;
    const int room = std::max((width_ - 2 * margin) / cell_.width, 1);
    const int rows = std::max(layout_.main.height / cell_.height - 3, 2);
    std::vector<KeyColumn> columns(1);
    for (const auto& group : groups)
    {
        if (static_cast<int>(columns.back().lines.size()) + 2 > rows)
        {
            columns.emplace_back();
        }
        if (!columns.back().lines.empty())
        {
            columns.back().lines.push_back({nullptr, nullptr}); // a line between groups
        }
        columns.back().lines.push_back({&group, nullptr});
        for (const auto& key : group.keys)
        {
            if (static_cast<int>(columns.back().lines.size()) >= rows)
            {
                columns.emplace_back();
            }
            columns.back().lines.push_back({&group, &key});
        }
    }
    for (auto& column : columns)
    {
        for (const auto& line : column.lines)
        {
            if (line.key != nullptr)
            {
                column.keys =
                    std::max(column.keys, static_cast<int>(decodeUtf8(line.key->keys).size()));
                column.width =
                    std::max(column.width, static_cast<int>(decodeUtf8(line.key->title).size()));
            }
        }
        column.width += column.keys + 3;
        for (const auto& line : column.lines)
        {
            if (line.key == nullptr && line.group != nullptr)
            {
                column.width =
                    std::max(column.width, static_cast<int>(decodeUtf8(line.group->title).size()));
            }
        }
    }
    std::vector<std::vector<KeyColumn>> pages(1);
    int used = 0;
    for (auto& column : columns)
    {
        if (!pages.back().empty() && used + column.width > room)
        {
            pages.emplace_back();
            used = 0;
        }
        used += column.width + 4;
        pages.back().push_back(std::move(column));
    }
    return pages;
}

Region Workspace::keysRegion() const
{
    // Over the view, on a panel: what it is and how to close it, then the page of columns, the
    // groups' titles in the accent colour, keys as in the hint menu.
    const auto groups = keyList();
    const auto pages = keyPages(groups);
    const auto page =
        static_cast<std::size_t>(std::clamp(*keysPage_, 0, static_cast<int>(pages.size()) - 1));
    const int margin = BarSpacing::forCell(cell_).margin;
    Region region{
        .bounds = layout_.main, .background = Role::Panel, .text = {}, .under = {}, .over = {}};
    const Style headingStyle{Role::Dim, Role::Panel, kPlain};
    const Style groupStyle{Role::Accent, Role::Panel, kBold};
    const Style keyStyle{Role::HintKey, Role::Panel, kPlain};
    const Style colonStyle{Role::Dim, Role::Panel, kPlain};
    const Style titleStyle{Role::Text, Role::Panel, kPlain};
    const auto x = [&](int column) { return margin + column * cell_.width; };
    const int pad = cell_.height / 4;
    std::string heading = "Keys in " + std::string{view().name()} + ": q or Escape closes";
    if (pages.size() > 1)
    {
        heading += "; j and k turn the pages (" + std::to_string(page + 1) + " of " +
                   std::to_string(pages.size()) + ")";
    }
    region.text.push_back({x(0), pad, decodeUtf8(heading), headingStyle});
    int left = 0;
    for (const auto& column : pages[page])
    {
        int row = 2;
        for (const auto& line : column.lines)
        {
            const int y = pad + row * cell_.height;
            if (line.key != nullptr)
            {
                const auto keys = decodeUtf8(line.key->keys);
                region.text.push_back(
                    {x(left + column.keys - static_cast<int>(keys.size())), y, keys, keyStyle});
                region.text.push_back({x(left + column.keys), y, U" : ", colonStyle});
                region.text.push_back(
                    {x(left + column.keys + 3), y, decodeUtf8(line.key->title), titleStyle});
            }
            else if (line.group != nullptr)
            {
                region.text.push_back({x(left), y, decodeUtf8(line.group->title), groupStyle});
            }
            ++row;
        }
        left += column.width + 4;
    }
    return region;
}

Region Workspace::hintRegion(const std::vector<Hint>& hints) const
{
    // On a panel above the status line, after Doom Emacs's (decided October 2026): the keys
    // typed and their group's title, then each key that can follow, right-aligned in its
    // column, a colon, and its title, groups marked + in a colour of their own. Columns go down
    // six rows and then across, each as wide as what's in it, so the menu gathers at the left;
    // with more than fit across, it takes more rows, up to the main area's height.
    const int margin = BarSpacing::forCell(cell_).margin;
    const int room = std::max((width_ - 2 * margin) / cell_.width, 1);
    const int most = std::max(layout_.main.height / cell_.height - 2, 1);
    const auto count = static_cast<int>(hints.size());
    struct Column
    {
        int keys = 0;
        int width = 0;
    };
    const auto layOut = [&](int rows)
    {
        std::vector<Column> columns;
        for (int i = 0; i < count; ++i)
        {
            if (i % rows == 0)
            {
                columns.emplace_back();
            }
            const auto& hint = hints[static_cast<std::size_t>(i)];
            auto& column = columns.back();
            column.keys = std::max(column.keys, static_cast<int>(decodeUtf8(hint.keys).size()));
            column.width = std::max(column.width, static_cast<int>(decodeUtf8(hint.title).size()) +
                                                      (hint.group ? 1 : 0));
        }
        int total = 0;
        for (auto& column : columns)
        {
            column.width += column.keys + 3; // " : "
            total += column.width + 4;
        }
        return std::pair{columns, total - 4};
    };
    int rows = std::min(std::max(count, 1), std::min(6, most));
    auto [columns, total] = layOut(rows);
    while (total > room && rows < std::min(count, most))
    {
        std::tie(columns, total) = layOut(++rows);
    }

    const int pad = cell_.height / 4;
    const int height = (rows + 1) * cell_.height + 2 * pad;
    const auto& status = layout_.bottom[0];
    Region region{.bounds = {status.x, status.y - height, status.width, height},
                  .background = Role::Panel,
                  .text = {},
                  .under = {},
                  .over = {}};
    const Style keyStyle{Role::HintKey, Role::Panel, kPlain};
    const Style colonStyle{Role::Dim, Role::Panel, kPlain};
    const Style titleStyle{Role::Text, Role::Panel, kPlain};
    const Style groupStyle{Role::HintGroup, Role::Panel, kPlain};
    const Style headingStyle{Role::Dim, Role::Panel, kPlain};
    const auto x = [&](int column) { return margin + column * cell_.width; };
    const auto mode = keys_.mode();
    const auto& keymap = mode == Mode::OperatorPending ? operatorKeymap_ : keymap_;
    auto heading = pending();
    if (keys_.keys().empty() && operator_)
    {
        // After an operator: its key's hint, Delete or Yank.
        for (const auto& [keys, binding] : keymap_.bindings(Mode::Normal))
        {
            if (binding.command == operator_->name && !binding.hint.empty())
            {
                heading += "  " + binding.hint;
                break;
            }
        }
    }
    else if (const auto title = keymap.groupTitle(mode, keys_.keys()); !title.empty())
    {
        heading += "  " + title;
    }
    region.text.push_back({x(0), pad, decodeUtf8(heading), headingStyle});
    int left = 0;
    for (std::size_t c = 0; c < columns.size(); ++c)
    {
        const auto& column = columns[c];
        if (left + column.width > room && c > 0)
        {
            break; // more than fit, even so
        }
        for (int row = 0; row < rows; ++row)
        {
            const auto i = c * static_cast<std::size_t>(rows) + static_cast<std::size_t>(row);
            if (i >= hints.size())
            {
                break;
            }
            const auto& hint = hints[i];
            const auto keys = decodeUtf8(hint.keys);
            const int y = pad + (row + 1) * cell_.height;
            region.text.push_back(
                {x(left + column.keys - static_cast<int>(keys.size())), y, keys, keyStyle});
            region.text.push_back({x(left + column.keys), y, U" : ", colonStyle});
            region.text.push_back({x(left + column.keys + 3), y,
                                   decodeUtf8((hint.group ? "+" : "") + hint.title),
                                   hint.group ? groupStyle : titleStyle});
        }
        left += column.width + 4;
    }
    return region;
}

std::optional<std::int64_t> Workspace::deadline() const
{
    auto deadline = keys_.deadline();
    if (hintsOn_ && waitingSince_ && !hintsShown_)
    {
        const auto shown = *waitingSince_ + hintDelay_;
        deadline = deadline ? std::min(*deadline, shown) : shown;
    }
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
    if (waitingSince_ && !hintsShown_ && now >= *waitingSince_ + hintDelay_)
    {
        hintsShown_ = true;
        changed = changed || hints().has_value();
    }
    if (keys_.keys().empty() && !operator_)
    {
        waitingSince_.reset();
        hintsShown_ = false;
    }
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
    // Where it does something instead, rather than where it doesn't, which reads as if it
    // were missing: "Note default is for the piano roll".
    const auto* info = commands_.find(command);
    auto title = info != nullptr ? titleOf(*info) : std::string{command};
    std::vector<std::string> places;
    constexpr std::array<std::pair<Where, std::string_view>, 4> kPlaces{{
        {Where::Tracks, "the arrangement's tracks"},
        {Where::Strip, "the section strip"},
        {Where::PianoRoll, "the piano roll"},
        {Where::EventList, "the event list"},
    }};
    for (const auto& [place, name] : kPlaces)
    {
        if (info != nullptr && (info->where & whereBit(place)) != 0)
        {
            places.emplace_back(name);
        }
    }
    std::string text;
    for (std::size_t i = 0; i < places.size(); ++i)
    {
        text += (i == 0 ? "" : i + 1 == places.size() ? " and " : ", ") + places[i];
    }
    setMessage(places.empty() ? title + " does nothing here" : title + " is for " + text,
               MessageKind::Information);
    return true;
}

std::optional<bool> Workspace::runShared(std::string_view command, int count)
{
    const int n = std::max(count, 1);
    if (command.starts_with("command-line-"))
    {
        return runCommandLine(command);
    }
    if (isOperator(command))
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
        answer_ = {};
        return true;
    }
    if (command == "keys-show")
    {
        keysPage_ = 0;
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
    if (op.name != "operator-delete" && op.name != "operator-yank")
    {
        // The count before the operator is how far (3>ib, up three semitones), the one after
        // it the range's (>3ib, three bars up one).
        return transform(op.name, std::max(op.count, 1), command, count);
    }
    // Counts before the operator and after it multiply, as in Vim: 2d3j is d6j.
    const int total = op.count == 0 && count == 0 ? 0 : std::max(op.count, 1) * std::max(count, 1);
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
    if (command == "command-line-next" || command == "command-line-previous")
    {
        return !answer_ && complete(command == "command-line-next" ? 1 : -1);
    }
    // While the matches show, Down and Up go round them too, as Tab and Shift-Tab do; Left and
    // Right go across the columns, and Page Down and Up a screen of them.
    if (const auto shown = completions())
    {
        if (command == "command-line-newer" || command == "command-line-older")
        {
            return complete(command == "command-line-newer" ? 1 : -1);
        }
        const auto columns = matchColumns(shown->matches);
        const auto rows = static_cast<std::ptrdiff_t>(columns.rows);
        if (command == "command-line-right" || command == "command-line-left")
        {
            return chooseMatch(command == "command-line-right" ? rows : -rows);
        }
        if (command == "command-line-page-down" || command == "command-line-page-up")
        {
            const auto page =
                rows * static_cast<std::ptrdiff_t>(columnsShown(columns, shown->scroll));
            return chooseMatch(command == "command-line-page-down" ? page : -page);
        }
        if (command == "command-line-backspace" || command == "command-line-delete")
        {
            const auto edited = editLine(line_, command);
            refilter();
            return edited;
        }
    }
    completing_.reset();
    if (command == "command-line-run" && answer_)
    {
        // An answer to a question, kept out of the : prompt's history.
        mode_ = Mode::Normal;
        const auto text = encodeUtf8(line_.text());
        line_.clear();
        if (const auto answer = std::exchange(answer_, {}); !text.empty())
        {
            answer(text);
        }
        return true;
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
        answer_ = {};
        return true;
    }
    return editLine(line_, command);
}

void Workspace::setChoices(Choices kind, ChoiceSource source)
{
    sources_[static_cast<std::size_t>(kind)] = std::move(source);
}

std::vector<std::string> Workspace::choices(Choices kind, std::string_view word) const
{
    const auto& song = document_.song();
    std::vector<std::string> found;
    switch (kind)
    {
    case Choices::Command:
        // The names and aliases of the commands the prompt runs: not those only for the
        // prompt's own keys or after an operator.
        for (const auto& info : commands_.all())
        {
            if ((info.modes != 0 && (info.modes & modeBit(Mode::Normal)) == 0) ||
                !offered(info.name))
            {
                continue;
            }
            found.emplace_back(info.name);
            for (std::size_t at = 0; at < info.aliases.size();)
            {
                const auto end = std::min(info.aliases.find(' ', at), info.aliases.size());
                if (end > at)
                {
                    found.emplace_back(info.aliases.substr(at, end - at));
                }
                at = end + 1;
            }
        }
        std::ranges::sort(found);
        return found;
    case Choices::Section:
        for (const auto& section : song.sections())
        {
            found.push_back(section->name());
        }
        return found;
    case Choices::Role:
        for (std::size_t i = 0; i < kRoleCount; ++i)
        {
            found.emplace_back(roleName(static_cast<Role>(i)));
        }
        return found;
    case Choices::Meter:
        return {"2/4", "3/4", "4/4", "5/4", "6/4",  "7/4", "3/8",
                "5/8", "6/8", "7/8", "9/8", "11/8", "12/8"};
    case Choices::Length:
        return {"1/1",  "1/2",  "1/4",  "1/8",  "1/16",  "1/32", "1/2.",
                "1/4.", "1/8.", "1/4t", "1/8t", "1/16t", "grid"};
    case Choices::Port:
        if (portFinder_)
        {
            return portFinder_("").ports;
        }
        return {};
    default:
        break;
    }
    if (const auto& source = sources_[static_cast<std::size_t>(kind)])
    {
        return source(word);
    }
    return {};
}

std::optional<Workspace::Completions> Workspace::completions() const
{
    if (mode_ != Mode::CommandLine || view().typing() || !completing_ ||
        completing_->line != line_.text() || completing_->matches.size() < 2)
    {
        return std::nullopt;
    }
    return Completions{completing_->matches, completing_->chosen, completing_->scroll};
}

Workspace::CompletingWord Workspace::completingWord() const
{
    const auto& text = line_.text();
    const auto word = wordAt(text, line_.cursor());
    CompletingWord result{word.start,
                          encodeUtf8(text.substr(word.start, line_.cursor() - word.start)),
                          Choices::Command};
    if (word.index > 0)
    {
        const auto first = wordAt(text, 0);
        auto name = encodeUtf8(text.substr(first.start, first.end - first.start));
        if (name.size() > 1 && name.ends_with('!'))
        {
            name.pop_back();
        }
        const auto* info = commands_.find(name);
        // Not for a command that does nothing here.
        result.kind = info == nullptr || !offered(info->name)
                          ? Choices::None
                          : info->choices[std::min<std::size_t>(word.index - 1, 2)];
    }
    return result;
}

bool Workspace::complete(int by)
{
    // As zsh does (decided October 2026): Tab completes the word as far as its matches agree,
    // showing them, and then goes round them, Shift-Tab back; one match is completed with a space
    // after it, unless it goes on (set theme=, a folder/).
    const auto replaceWord = [this](std::size_t start, const std::string& with)
    { line_.replace(start, line_.cursor(), decodeUtf8(with)); };
    if (completing_ && completing_->line == line_.text() && completing_->matches.size() > 1)
    {
        const auto count = completing_->matches.size();
        const auto& chosen = completing_->chosen;
        const auto to = !chosen  ? (by > 0 ? 0 : count - 1)
                        : by > 0 ? (*chosen + 1) % count
                                 : (*chosen + count - 1) % count;
        chooseIndex(to);
        return true;
    }
    completing_.reset();
    const auto word = completingWord();
    if (word.kind == Choices::None)
    {
        return true;
    }
    const auto found = matching(choices(word.kind, word.typed), word.typed);
    if (found.empty())
    {
        return true;
    }
    if (found.size() == 1)
    {
        const auto& only = found.front();
        replaceWord(word.start, only + (only.ends_with('=') || only.ends_with('/') ? "" : " "));
        return true;
    }
    // With several, the first Tab only shows them, none chosen, so typing can narrow them (: Tab
    // lists every command); the next goes round them.
    const auto shared = sharedStart(found);
    if (decodeUtf8(shared).size() > decodeUtf8(word.typed).size() &&
        matching(std::span{&shared, 1}, word.typed).size() == 1)
    {
        replaceWord(word.start, shared);
    }
    completing_ = Completing{found, std::nullopt, word.start, line_.text(), 0};
    return true;
}

bool Workspace::chooseMatch(std::ptrdiff_t by)
{
    // From the one chosen, or with none yet from before the first (or after the last).
    const auto& c = *completing_;
    const auto count = static_cast<std::ptrdiff_t>(c.matches.size());
    const auto from = c.chosen ? static_cast<std::ptrdiff_t>(*c.chosen) : (by > 0 ? -1 : count);
    chooseIndex(static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(from + by, 0, count - 1)));
    return true;
}

void Workspace::chooseIndex(std::size_t to)
{
    // The line takes the match, and the columns shown move to keep it in view.
    auto& c = *completing_;
    c.chosen = to;
    line_.replace(c.start, line_.cursor(), decodeUtf8(c.matches[to]));
    c.line = line_.text();
    const auto columns = matchColumns(c.matches);
    const auto column = to / static_cast<std::size_t>(columns.rows);
    if (column < c.scroll)
    {
        c.scroll = column;
    }
    while (column >= c.scroll + columnsShown(columns, c.scroll))
    {
        ++c.scroll;
    }
}

void Workspace::refilter()
{
    // The word's matches as it's typed, none chosen; gone when the word ends or none match.
    const auto& text = line_.text();
    const auto at = line_.cursor();
    const auto word = completingWord();
    const bool ended = at > 0 && (text[at - 1] == U' ' || text[at - 1] == U'\t');
    auto found = ended || word.kind == Choices::None
                     ? std::vector<std::string>{}
                     : matching(choices(word.kind, word.typed), word.typed);
    if (found.empty())
    {
        completing_.reset();
        return;
    }
    completing_ = Completing{std::move(found), std::nullopt, word.start, text, 0};
}

Workspace::MatchColumns Workspace::matchColumns(const std::vector<std::string>& matches) const
{
    // Six rows down and then across, as the hint menu is; more rows while they don't fit
    // across, up to the main area's height, and then they scroll.
    const int margin = BarSpacing::forCell(cell_).margin;
    MatchColumns columns;
    columns.room = std::max((width_ - 2 * margin) / cell_.width, 1);
    const auto count = static_cast<int>(matches.size());
    const int most = std::max(layout_.main.height / cell_.height - 1, 1);
    const auto lay = [&](int rows)
    {
        std::vector<int> widths;
        int total = 0;
        for (int i = 0; i < count; ++i)
        {
            if (i % rows == 0)
            {
                widths.push_back(0);
            }
            widths.back() =
                std::max(widths.back(),
                         static_cast<int>(decodeUtf8(matches[static_cast<std::size_t>(i)]).size()));
        }
        for (const auto w : widths)
        {
            total += w + 3;
        }
        return std::pair{widths, total};
    };
    columns.rows = std::max(std::min(count, std::min(6, most)), 1);
    auto [widths, total] = lay(columns.rows);
    while (total > columns.room && columns.rows < std::min(count, most))
    {
        std::tie(widths, total) = lay(++columns.rows);
    }
    columns.widths = std::move(widths);
    return columns;
}

std::size_t Workspace::columnsShown(const MatchColumns& columns, std::size_t first) const
{
    // As many as fit, leaving room for the markers of more either side; at least one.
    int used = first > 0 ? 2 : 0;
    std::size_t shown = 0;
    for (auto c = first; c < columns.widths.size(); ++c)
    {
        const bool last = c + 1 == columns.widths.size();
        if (used + columns.widths[c] + (last ? 0 : 2) > columns.room && shown > 0)
        {
            break;
        }
        used += columns.widths[c] + 3;
        ++shown;
    }
    return std::max<std::size_t>(shown, 1);
}

Region Workspace::completionRegion(const Completions& shown) const
{
    // Above the prompt, as the hint menu is: the matches down and then across, each column as
    // wide as what's in it, the one chosen in the cursor's colours; with more than fit across,
    // those in view, a ◀ or ▶ at the side with more.
    const int margin = BarSpacing::forCell(cell_).margin;
    const auto columns = matchColumns(shown.matches);
    const auto count = columnsShown(columns, shown.scroll);
    const int pad = cell_.height / 4;
    const int height = columns.rows * cell_.height + 2 * pad;
    const auto& status = layout_.bottom[0];
    Region region{.bounds = {status.x, status.y - height, status.width, height},
                  .background = Role::Panel,
                  .text = {},
                  .under = {},
                  .over = {}};
    const Style plain{Role::Text, Role::Panel, kPlain};
    const Style chosen{Role::CursorText, Role::Cursor, kPlain};
    const Style more{Role::Dim, Role::Panel, kPlain};
    int left = 0;
    if (shown.scroll > 0)
    {
        region.text.push_back({margin, pad, U"◀", more});
        left = 2;
    }
    const auto end = std::min(shown.scroll + count, columns.widths.size());
    for (auto c = shown.scroll; c < end; ++c)
    {
        for (int row = 0; row < columns.rows; ++row)
        {
            const auto i =
                c * static_cast<std::size_t>(columns.rows) + static_cast<std::size_t>(row);
            if (i >= shown.matches.size())
            {
                break;
            }
            region.text.push_back({margin + left * cell_.width, pad + row * cell_.height,
                                   decodeUtf8(shown.matches[i]),
                                   shown.chosen == i ? chosen : plain});
        }
        left += columns.widths[c] + 3;
    }
    if (end < columns.widths.size())
    {
        region.text.push_back({margin + (columns.room - 1) * cell_.width, pad, U"▶", more});
    }
    return region;
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

namespace
{

std::string trackNameOf(const core::Song& song, core::TrackId id)
{
    const auto* track = song.track(id);
    return track == nullptr || track->name.empty() ? std::string{"A track"} : track->name;
}

} // namespace

CommandResult Workspace::markerToSection()
{
    // The marker at or before the cursor, its section starting at the bar line of its bar in
    // the conductor's bars, asking first unless the cursor is on it and it's on a bar line.
    const auto& song = document_.song();
    const auto& meter = song.meter();
    const auto tick = view().cursor().tick;
    const auto markers = song.markers();
    const auto after = std::ranges::upper_bound(markers, tick, {}, &core::Marker::tick);
    if (after == markers.begin())
    {
        return CommandResult::error("There's no marker at or before the cursor");
    }
    const auto marker = *std::prev(after);
    const auto start = meter.barStart(meter.toBarBeatTick(marker.tick).bar);
    const auto section = song.sectionAt(start);
    if (song.sectionStart(section) == start)
    {
        return CommandResult::error(marker.name + " is at the start of " +
                                    song.sections()[section]->name() +
                                    ", so there's no section to start");
    }
    const auto at = positionText(meter, start);
    const auto make = [this, marker, start, at]
    {
        std::optional<core::Song::Split> split;
        document_.edit("Start section " + marker.name + " at " + at + ", from its marker",
                       view().place(),
                       [&](core::Song& s)
                       {
                           split = s.splitSection(start, marker.name);
                           s.removeMarker(marker.tick);
                       });
        refresh();
        const auto& named = document_.song().sections()[split->index]->name();
        setMessage(named + " starts at " + at + ", from its marker", MessageKind::Information);
    };
    if (marker.tick == tick && marker.tick == start)
    {
        make();
        return {true, {}, false};
    }
    ask(marker.tick == start ? "Start a section from the marker " + marker.name + " at " + at + "?"
                             : "Start a section at " + at + " from the marker " + marker.name +
                                   " at " + positionText(meter, marker.tick) + "?",
        make);
    return {true, {}, false};
}

CommandResult Workspace::markerToSectionAll()
{
    if (document_.song().markers().empty())
    {
        return CommandResult::error("There are no markers to turn into sections");
    }
    auto run = std::make_shared<MarkerRun>();
    run->from = view().cursor();
    runMarkers(run);
    return {true, {}, false};
}

void Workspace::runMarkers(std::shared_ptr<MarkerRun> run)
{
    // On through the markers from where the run is: those on a bar line taken, those whose bar
    // already starts a section (or another marker's) passed over, and for each of the others
    // the cursor going to it and a question, after which the run goes on.
    const auto& song = document_.song();
    const auto& meter = song.meter();
    const auto markers = song.markers();
    for (; run->next < markers.size(); ++run->next)
    {
        const auto marker = markers[run->next];
        const auto start = meter.barStart(meter.toBarBeatTick(marker.tick).bar);
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
        const auto take = [run, marker, start]
        {
            run->starts.emplace_back(start, marker.name);
            run->markers.push_back(marker.tick);
        };
        if (marker.tick == start)
        {
            take();
            continue;
        }
        auto cursor = view().cursor();
        cursor.tick = marker.tick;
        view().setCursor(cursor);
        ++run->next;
        ask(
            "Start a section at " + positionText(meter, start) + " from the marker " + marker.name +
                " at " + positionText(meter, marker.tick) + "?",
            [this, run, take]
            {
                take();
                runMarkers(run);
            },
            [this, run]
            {
                ++run->declined;
                runMarkers(run);
            },
            [this, run]
            {
                view().setCursor(run->from);
                setMessage("Nothing done", MessageKind::Information);
            });
        return;
    }
    finishMarkers(*run);
}

void Workspace::finishMarkers(const MarkerRun& run)
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
        setMessage("No markers turned into sections" + left, MessageKind::Information);
        return;
    }
    const auto turned = run.starts.size();
    document_.edit("Turn " + std::to_string(turned) + (turned == 1 ? " marker" : " markers") +
                       " into sections",
                   view().place(),
                   [&](core::Song& s)
                   {
                       for (const auto& [start, name] : run.starts)
                       {
                           s.splitSection(start, name);
                       }
                       for (const auto tick : run.markers)
                       {
                           s.removeMarker(tick);
                       }
                   });
    refresh();
    setMessage("Turned " + std::to_string(turned) +
                   (turned == 1 ? " marker into a section" : " markers into sections") + left,
               MessageKind::Information);
}

CommandResult Workspace::barDelete(int count)
{
    // Done on a copy first, which says whether it can be, as one undo step.
    const auto& song = document_.song();
    const auto& meter = song.meter();
    const auto bar = meter.toBarBeatTick(view().cursor().tick).bar;
    const auto from = meter.barStart(bar);
    if (from >= song.end())
    {
        return CommandResult::error("There are no bars after the song's end to delete");
    }
    const auto n = std::max(count, 1);
    const auto bars = barsText(meter, from, std::min(meter.barStart(bar + n), song.end()));
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
    return {true, {}, false};
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
    answer_ = {};
    mode_ = Mode::CommandLine;
    line_.open(decodeUtf8(line));
}

void Workspace::ask(std::string question, std::function<void()> yes, std::function<void()> no,
                    std::function<void()> stop)
{
    setMessage(std::move(question) + (stop ? " (y/n, Escape stops)" : " (y/n)"),
               MessageKind::Warning);
    question_ = Question{std::move(yes), std::move(no), std::move(stop)};
}

void Workspace::askFor(std::string label, std::function<void(const std::string&)> answer)
{
    mode_ = Mode::CommandLine;
    line_.open();
    answerLabel_ = std::move(label);
    answer_ = std::move(answer);
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
    if (keysPage_)
    {
        frame.push_back(keysRegion());
    }
    if (const auto shown = completions())
    {
        frame.push_back(completionRegion(*shown));
    }
    if (const auto shown = hints())
    {
        frame.push_back(hintRegion(*shown));
    }
    if (const auto lines = messageLines(); !lines.empty())
    {
        frame.push_back(messageRegion(lines));
    }
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
    const auto room = static_cast<std::size_t>(
        std::max((width_ - 2 * margin) / cell_.width - 1 -
                     (answer_ ? static_cast<int>(decodeUtf8(answerLabel_).size()) : 1),
                 1));
    const std::size_t first = cursor >= room ? cursor - room + 1 : 0;
    Region region{
        .bounds = layout_.bottom[0], .background = Role::Bar, .text = {}, .under = {}, .over = {}};
    const auto x = [&](std::size_t column)
    { return margin + static_cast<int>(column) * cell_.width; };
    // Or a question's label, for its answer.
    const auto label = answer_ ? decodeUtf8(answerLabel_) : std::u32string{U":"};
    const auto lead = label.size();
    region.text.push_back({x(0), 0, label, kBarText});
    region.text.push_back({x(lead), 0, text.substr(first, cursor - first), kBarText});
    const char32_t under = cursor < text.size() ? text[cursor] : U' ';
    region.text.push_back({x(lead + cursor - first), 0, std::u32string(1, under), kCursor});
    if (cursor + 1 < text.size())
    {
        region.text.push_back({x(lead + 1 + cursor - first), 0, text.substr(cursor + 1), kBarText});
    }
    return region;
}

Region Workspace::statusRegion() const
{
    auto region = layoutBar(statusBar(), width_, cell_, BarSpacing::forCell(cell_));
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
    const auto& status = layout_.bottom[0];
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

Bar Workspace::statusBar() const
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
    return bottom;
}

} // namespace modal::ui
