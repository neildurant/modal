#include <modal/ui/bar.hpp>
#include <modal/ui/cell_grid.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/event_edit.hpp>
#include <modal/ui/event_list_view.hpp>
#include <modal/ui/time_motions.hpp>
#include <modal/ui/window_commands.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <utility>

namespace modal::ui
{

namespace
{

constexpr Style kPlainText{Role::Text, Role::Background, kPlain};
constexpr Style kDimText{Role::Dim, Role::Background, kPlain};
constexpr Style kHeading{Role::BarText, Role::Bar, kPlain};
constexpr Style kCursor{Role::CursorText, Role::Cursor, kPlain};
constexpr Style kBarText{Role::BarText, Role::Bar, kPlain};

// The columns' headings and widths in cells, with two spaces after each, and whether
// they're right-justified, as numbers are.
struct ColumnInfo
{
    const char* heading;
    int width;
    bool right;
};

constexpr std::array<ColumnInfo, kColumnCount> kColumns{{
    {"Position", 0, false}, // as wide as the song's positions need
    {"Event", 13, false},
    {"Data", 18, false},
    {"Vel/Value", 11, true},
    {"Length", 19, false},
    {"Release", 9, true},
}}; // all within the smallest window, 80 cells

// Where text starts in a column: at its left, or right-justified to two cells before the
// next column.
int textX(int x, int width, bool right, std::string_view text)
{
    return right ? x + width - 2 - static_cast<int>(decodeUtf8(text).size()) : x;
}

constexpr int kMargin = 1; // a cell before the first column

// A command that takes a count, and one that works only at the : prompt.
constexpr CommandInfo counted(std::string_view name, std::string_view description)
{
    return {.name = name, .description = description, .count = true};
}

// A motion, which also takes a count.
constexpr CommandInfo motion(std::string_view name, std::string_view description)
{
    return {.name = name, .description = description, .count = true, .motion = true};
}

constexpr CommandInfo atPrompt(std::string_view name, std::string_view description)
{
    return {.name = name, .description = description, .modes = modeBit(Mode::CommandLine)};
}

constexpr std::array kCommands{
    counted("cursor-left", "Move to the field before"),
    counted("cursor-right", "Move to the next field"),
    motion("cursor-down", "Move down an event"),
    motion("cursor-up", "Move up an event"),
    CommandInfo{.name = "cursor-first", .description = "Move to the first field"},
    CommandInfo{.name = "cursor-last", .description = "Move to the last field"},
    motion("cursor-beat-next", "Move to the event at or after the next beat"),
    motion("cursor-beat-previous", "Move to the event at or after the beat before"),
    motion("cursor-bar-next", "Move to the event at or after the same place a bar on"),
    motion("cursor-bar-previous", "Move to the event at or after the same place a bar back"),
    motion("cursor-bar-line-next", "Move to the event at or after the next bar line"),
    motion("cursor-bar-line-previous", "Move to the event at or after the bar line before"),
    motion("cursor-chord-next", "Move to the next chord, or event"),
    motion("cursor-chord-previous", "Move to the chord, or event, before"),
    motion("cursor-section-next", "Move to the next section's start"),
    motion("cursor-section-previous", "Move to the section's start, or the one before"),
    motion("cursor-marker-next", "Move to the next marker"),
    motion("cursor-marker-previous", "Move to the marker before"),
    motion("go-first", "Go to the first event, or bar {count}"),
    motion("go-last", "Go to the end, or bar {count}"),
    counted("cursor-left-scroll", "Move to the field before"),
    counted("cursor-right-scroll", "Move to the next field"),
    counted("cursor-down-scroll", "Move down, scrolling to keep the cursor in place"),
    counted("cursor-up-scroll", "Move up, scrolling to keep the cursor in place"),
    counted("scroll-half-down", "Scroll down half a page"),
    counted("scroll-half-up", "Scroll up half a page"),
    counted("scroll-page-down", "Scroll down a page"),
    counted("scroll-page-up", "Scroll up a page"),
    CommandInfo{.name = "scroll-centre", .description = "Scroll the cursor to the middle"},
    counted("track-next", "Show the next track's list, or track {count}'s"),
    counted("track-previous", "Show the track's list before, {count} tracks back"),
    CommandInfo{.name = "field-edit",
                .description = "Edit the field under the cursor, typing a value or a change"},
    counted("field-increase", "Step the field under the cursor up, {count} steps"),
    counted("field-decrease", "Step the field under the cursor down, {count} steps"),
    counted("operator-delete", "Delete the events a motion goes over (d}), or dd this event"),
    counted("operator-yank", "Yank the events a motion goes over (y}), or yy this event"),
    CommandInfo{.name = "operator-line",
                .description = "After an operator, this event, or {count} events from it",
                .modes = modeBit(Mode::OperatorPending),
                .count = true},
    counted("event-delete", "Delete this event, or {count} events from it"),
    CommandInfo{.name = "register-put",
                .description = "Put the yanked or deleted events, the first at the cursor's time"},
    counted("undo", "Undo the last change, or {count} changes"),
    counted("redo", "Redo the change undone last, or {count} changes"),
    CommandInfo{.name = "cancel", .description = "Cancel"},
    CommandInfo{.name = "command-line", .description = "Open the : command line"},
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

// Text to show in a cell of a width, without control characters, cut short with an
// ellipsis if it's too long.
std::string fitted(std::string_view text, int width)
{
    auto characters = decodeUtf8(text);
    for (auto& c : characters)
    {
        c = c < U' ' || c == 0x7F ? U'·' : c;
    }
    if (width > 1 && static_cast<int>(characters.size()) > width - 1)
    {
        characters.resize(static_cast<std::size_t>(width - 2));
        characters += U'…';
    }
    return encodeUtf8(characters);
}

std::string signatureText(core::TimeSignature signature)
{
    return std::to_string(signature.numerator) + "/" + std::to_string(signature.denominator);
}

} // namespace

std::span<const CommandInfo> EventListView::commands()
{
    return kCommands;
}

Keymap EventListView::defaultKeymap()
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

EventListView::EventListView(CommandRegistry& registry, core::Document& document, Keymap keymap,
                             std::int64_t timeout)
    : commands_(registry)
    , document_(document)
    , track_(document.song().tracks().empty() ? 0 : 1)
    , list_(listOf(track_))
    , keymap_(std::move(keymap))
    , keys_(timeout)
{
    for (const auto& info : commands())
    {
        registry.add(info, [this, name = info.name](const CommandCall& call)
                     { return CommandResult{runOwn(name, call.count).value_or(false)}; });
    }
    buildOperatorKeymap();
}

void EventListView::setKeymap(Keymap keymap)
{
    keymap_ = std::move(keymap);
    buildOperatorKeymap();
}

void EventListView::buildOperatorKeymap()
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

std::string EventListView::pending() const
{
    std::string text;
    if (operator_)
    {
        text = (operator_->count > 0 ? std::to_string(operator_->count) : std::string{}) +
               (operator_->name == "operator-delete" ? "d" : "y");
    }
    return text + keys_.pending();
}

void EventListView::resize(int width, int height, CellSize cell)
{
    width_ = width;
    height_ = height;
    cell_ = {std::max(cell.width, 1), std::max(cell.height, 1)};
    layout_ = layoutWindow(width, height, cell_, 1, 1);
    follow();
}

int EventListView::viewRows() const
{
    return std::max(layout_.fullRows - 1, 1); // below the headings
}

Column EventListView::field() const
{
    const auto fields = list_.text(row_).fields;
    // The field kept to, or the last before it, or the first.
    Column best = fields.front();
    for (const auto f : fields)
    {
        if (f <= wanted_)
        {
            best = f;
        }
    }
    return best;
}

void EventListView::refresh()
{
    // The same time, and as far down the rows at that time as can be.
    const auto tick = list_.rows()[row_].tick;
    const auto below = row_ - list_.rowAt(tick);
    const auto tracks = document_.song().tracks().size();
    showTrack(std::min(track_, tracks), tick);
    const auto rows = list_.rows();
    while (row_ + 1 < rows.size() && rows[row_ + 1].tick == tick &&
           row_ - list_.rowAt(tick) < below)
    {
        ++row_;
    }
    follow();
}

EventList EventListView::listOf(std::size_t track) const
{
    const auto& song = document_.song();
    return EventList{song,
                     track == 0 ? std::nullopt : std::optional{song.tracks()[track - 1]->id()}};
}

void EventListView::showTrack(std::size_t track, core::Tick tick)
{
    track_ = track;
    list_ = listOf(track);
    row_ = list_.rowAt(tick);
    follow();
}

bool EventListView::handle(const Key& key, std::int64_t now)
{
    const bool hadMessage = !std::exchange(message_, std::string{}).empty();
    // At the prompt, a character that starts no command-line binding is typed.
    if (mode_ == Mode::CommandLine && keys_.pending().empty() && key.named == NamedKey::None &&
        (key.modifiers & (kCtrl | kAlt)) == 0)
    {
        const std::vector<Key> keys{key};
        const auto match = keymap_.match(Mode::CommandLine, keys);
        if (match.binding == nullptr && !match.longer)
        {
            auto& line = activeLine();
            if (field_ && std::exchange(field_->fresh, false))
            {
                line.clear(); // typing replaces the value, as in a spreadsheet
            }
            line.insert(key.character);
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

bool EventListView::wake(std::int64_t now)
{
    const auto before = keys_.pending();
    const auto& keymap = mode_ == Mode::OperatorPending ? operatorKeymap_ : keymap_;
    const bool changed = runAll(keys_.timeout(keymap, now));
    return changed || keys_.pending() != before;
}

bool EventListView::runAll(const std::vector<Action>& actions)
{
    bool changed = false;
    for (const auto& action : actions)
    {
        changed = run(action.command, action.count).changed || changed;
    }
    return changed;
}

CommandResult EventListView::run(std::string_view command, int count)
{
    auto result = commands_.run(command, count, mode_);
    if (!result.message.empty())
    {
        setMessage(result.message,
                   result.warning ? MessageKind::Warning : MessageKind::Information);
    }
    return result;
}

void EventListView::moveRows(std::int64_t by)
{
    const auto last = static_cast<std::int64_t>(list_.rows().size()) - 1;
    row_ = static_cast<std::size_t>(
        std::clamp(static_cast<std::int64_t>(row_) + by, std::int64_t{0}, last));
}

void EventListView::moveFields(int by)
{
    const auto fields = list_.text(row_).fields;
    const auto at = std::ranges::find(fields, field()) - fields.begin();
    const auto to =
        std::clamp<std::ptrdiff_t>(at + by, 0, static_cast<std::ptrdiff_t>(fields.size()) - 1);
    wanted_ = fields[static_cast<std::size_t>(to)];
}

void EventListView::follow()
{
    const auto rows = static_cast<std::size_t>(viewRows());
    const auto count = list_.rows().size();
    top_ = std::min(top_, count > rows ? count - rows : 0);
    if (row_ < top_)
    {
        top_ = row_;
    }
    else if (row_ >= top_ + rows)
    {
        top_ = row_ - rows + 1;
    }
}

void EventListView::land(core::Tick target, bool forward,
                         const std::function<core::Tick(core::Tick)>& again)
{
    // Going back means earlier in time, not just an earlier row at the same time.
    const auto from = list_.rows()[row_].tick;
    auto row = list_.rowAt(target);
    while (!forward && list_.rows()[row].tick >= from && target > 0)
    {
        target = again(target);
        row = list_.rowAt(target);
    }
    if (forward || list_.rows()[row].tick < from)
    {
        row_ = row;
    }
}

std::vector<std::size_t> EventListView::chordStarts() const
{
    const auto& song = document_.song();
    const auto rows = list_.rows();
    std::vector<std::size_t> starts;
    std::optional<core::Tick> first;
    for (std::size_t i = 0; i + 1 < rows.size(); ++i) // not the end
    {
        if (track_ != 0 && rows[i].kind != RowKind::Event)
        {
            continue;
        }
        if (!first || !together(song, *first, rows[i].tick, chordWindow_))
        {
            starts.push_back(i);
            first = rows[i].tick;
        }
    }
    return starts;
}

std::optional<bool> EventListView::runOwn(std::string_view command, int count)
{
    if (mode_ == Mode::OperatorPending)
    {
        return runOperator(command, count);
    }
    const bool keepsBarOffset = command == "cursor-bar-next" || command == "cursor-bar-previous";
    if (!keepsBarOffset)
    {
        barOffset_.reset();
    }
    const int n = std::max(count, 1);
    const auto rows = static_cast<std::int64_t>(viewRows());
    const auto scroll = [&](std::int64_t by)
    {
        const auto last = static_cast<std::int64_t>(list_.rows().size()) - 1;
        const auto maxTop = std::max(last + 1 - rows, std::int64_t{0});
        top_ = static_cast<std::size_t>(
            std::clamp(static_cast<std::int64_t>(top_) + by, std::int64_t{0}, maxTop));
        moveRows(by);
    };
    const auto barRow = [&](int bar) { return list_.rowAt(list_.meter().barStart(bar)); };

    if (command.starts_with("command-line-"))
    {
        return runCommandLine(command);
    }
    const auto& meter = list_.meter();
    const auto tick = list_.rows()[row_].tick;
    const auto& song = document_.song();

    if (command == "cursor-down" || command == "cursor-up")
    {
        moveRows(command == "cursor-down" ? n : -n);
    }
    else if (command == "cursor-beat-next")
    {
        land(beatAfter(meter, tick, n), true, {});
    }
    else if (command == "cursor-beat-previous")
    {
        land(beatBefore(meter, tick, n), false,
             [&](core::Tick t) { return beatBefore(meter, t, 1); });
    }
    else if (keepsBarOffset)
    {
        const auto offset = barOffset_.value_or(offsetInBar(meter, tick));
        if (command == "cursor-bar-next")
        {
            land(sameInBar(meter, tick, n, offset), true, {});
        }
        else
        {
            land(sameInBar(meter, tick, -n, offset), false,
                 [&](core::Tick t) { return sameInBar(meter, t, -1, offset); });
        }
        barOffset_ = offset;
    }
    else if (command == "cursor-bar-line-next")
    {
        land(barLineAfter(meter, tick, n), true, {});
    }
    else if (command == "cursor-bar-line-previous")
    {
        land(barLineBefore(meter, tick, n), false,
             [&](core::Tick t) { return barLineBefore(meter, t, 1); });
    }
    else if (command == "cursor-chord-next" || command == "cursor-chord-previous")
    {
        const auto starts = chordStarts();
        // The chord the cursor is in: the last starting at or before its row.
        const auto after = std::ranges::upper_bound(starts, row_);
        auto at = after - starts.begin() - 1;
        if (command == "cursor-chord-next")
        {
            at += n;
        }
        else
        {
            at -= at >= 0 && starts[static_cast<std::size_t>(at)] == row_ ? n : n - 1;
        }
        if (at >= static_cast<std::ptrdiff_t>(starts.size()))
        {
            row_ = list_.rows().size() - 1; // past the last chord, the end
        }
        else if (!starts.empty())
        {
            const auto to = starts[static_cast<std::size_t>(std::max<std::ptrdiff_t>(at, 0))];
            if (command == "cursor-chord-next" || to < row_) // never back to a later row
            {
                row_ = to;
            }
        }
    }
    else if (command.starts_with("cursor-section-") || command.starts_with("cursor-marker-"))
    {
        const bool sections = command.starts_with("cursor-section-");
        std::vector<core::Tick> ticks;
        if (sections)
        {
            for (std::size_t i = 0; i < song.sections().size(); ++i)
            {
                ticks.push_back(song.sectionStart(i));
            }
        }
        else
        {
            for (const auto& marker : song.markers())
            {
                ticks.push_back(marker.tick);
            }
        }
        const auto to =
            command.ends_with("-next") ? nextOf(ticks, tick, n) : previousOf(ticks, tick, n);
        if (!to)
        {
            return false;
        }
        // On its row: a section's is the first at its tick; a marker's comes after any
        // section, meter or tempo change there.
        row_ = list_.rowAt(*to);
        const auto kind = sections ? RowKind::Section : RowKind::Marker;
        while (list_.rows()[row_].kind != kind)
        {
            ++row_;
        }
    }
    else if (command == "cursor-left" || command == "cursor-left-scroll")
    {
        moveFields(-n);
    }
    else if (command == "cursor-right" || command == "cursor-right-scroll")
    {
        moveFields(n);
    }
    else if (command == "cursor-first" || command == "cursor-last")
    {
        const auto fields = list_.text(row_).fields;
        wanted_ = command == "cursor-first" ? fields.front() : fields.back();
    }
    else if (command == "go-first")
    {
        row_ = count > 0 ? barRow(count) : 0;
    }
    else if (command == "go-last")
    {
        row_ = count > 0 ? barRow(count) : list_.rows().size() - 1;
    }
    else if (command == "cursor-down-scroll" || command == "cursor-up-scroll")
    {
        scroll(command == "cursor-down-scroll" ? n : -n);
    }
    else if (command.starts_with("scroll-half-") || command.starts_with("scroll-page-"))
    {
        const bool half = command.starts_with("scroll-half-");
        const auto page = std::max(half ? rows / 2 : rows - 2, std::int64_t{1}); // as in Vim
        scroll(n * page * (command.ends_with("down") ? 1 : -1));
    }
    else if (command == "scroll-centre")
    {
        top_ = row_ > static_cast<std::size_t>(rows / 2) ? row_ - static_cast<std::size_t>(rows / 2)
                                                         : 0;
    }
    else if (command == "track-next" || command == "track-previous")
    {
        const auto tracks = static_cast<std::int64_t>(document_.song().tracks().size()) + 1;
        std::int64_t to = static_cast<std::int64_t>(track_);
        if (command == "track-next")
        {
            to = count > 0 ? std::min<std::int64_t>(count, tracks - 1) : (to + 1) % tracks;
        }
        else
        {
            to = ((to - n) % tracks + tracks) % tracks;
        }
        showTrack(static_cast<std::size_t>(to), list_.rows()[row_].tick);
        return true;
    }
    else if (command == "operator-delete" || command == "operator-yank")
    {
        operator_ = PendingOperator{std::string{command}, count};
        mode_ = Mode::OperatorPending;
        return true;
    }
    else if (command == "event-delete")
    {
        return operate("operator-delete", row_, row_ + static_cast<std::size_t>(n) - 1);
    }
    else if (command == "register-put")
    {
        return put();
    }
    else if (command == "field-edit")
    {
        return openField();
    }
    else if (command == "field-increase" || command == "field-decrease")
    {
        return stepFieldBy(command == "field-increase" ? n : -n);
    }
    else if (command == "undo" || command == "redo")
    {
        bool changed = false;
        for (int i = 0; i < n; ++i)
        {
            changed = undoOrRedo(command == "undo") || changed;
        }
        return changed;
    }
    else if (command == "cancel")
    {
        return false;
    }
    else if (command == "command-line")
    {
        mode_ = Mode::CommandLine;
        line_.open();
        return true;
    }
    else
    {
        return std::nullopt;
    }
    follow();
    return true;
}

std::optional<bool> EventListView::runOperator(std::string_view command, int count)
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
    if (command == "operator-line")
    {
        return operate(op.name, row_, row_ + static_cast<std::size_t>(std::max(total, 1)) - 1);
    }
    const auto* info = commands_.find(command);
    if (info == nullptr || !info->motion)
    {
        return false;
    }
    const auto from = row_;
    runOwn(command, total);
    const auto to = row_;
    row_ = from;
    // Moving between rows takes them all, the one landed on too, as Vim's line motions do;
    // the others stop before the row they land on.
    const bool whole = command == "cursor-down" || command == "cursor-up" ||
                       command == "go-first" || command == "go-last";
    if (whole)
    {
        return operate(op.name, from, to);
    }
    if (to == from)
    {
        return false;
    }
    return to > from ? operate(op.name, from, to - 1) : operate(op.name, to, from - 1);
}

bool EventListView::operate(std::string_view op, std::size_t from, std::size_t to)
{
    if (track_ == 0)
    {
        setMessage("The conductor's list can't be edited yet", MessageKind::Warning);
        return true;
    }
    if (from > to)
    {
        std::swap(from, to);
    }
    to = std::min(to, list_.rows().size() - 1);
    // The track's own events among the rows; the conductor's rows and the end aren't touched.
    std::vector<std::size_t> indices;
    for (auto r = from; r <= to; ++r)
    {
        if (list_.rows()[r].kind == RowKind::Event)
        {
            indices.push_back(list_.rows()[r].index);
        }
    }
    if (indices.empty())
    {
        return false;
    }
    const auto id = *list_.track();
    const auto& events = document_.song().events(id);
    const auto first = events[indices.front()].tick;
    register_.clear();
    for (const auto i : indices)
    {
        auto event = events[i];
        event.tick -= first;
        register_.push_back(std::move(event));
    }
    const auto what =
        indices.size() == 1 ? std::string{"1 event"} : std::to_string(indices.size()) + " events";
    if (op == "operator-yank")
    {
        setMessage(what + " yanked", MessageKind::Information);
        return true;
    }
    edit("Delete " + what + " in " + trackName(),
         [&](core::Song& song)
         {
             song.updateEvents(id,
                               [&](core::EventList& list)
                               {
                                   for (auto it = indices.rbegin(); it != indices.rend(); ++it)
                                   {
                                       list.erase(*it);
                                   }
                               });
         });
    // On what followed, as in Vim.
    row_ = std::min(from, list_.rows().size() - 1);
    follow();
    return true;
}

bool EventListView::put()
{
    if (track_ == 0)
    {
        setMessage("The conductor's list can't be edited yet", MessageKind::Warning);
        return true;
    }
    if (register_.empty())
    {
        setMessage("Nothing yanked or deleted to put", MessageKind::Warning);
        return true;
    }
    const auto at = list_.rows()[row_].tick;
    auto added = register_;
    for (auto& event : added)
    {
        event.tick += at;
    }
    const auto what =
        added.size() == 1 ? std::string{"1 event"} : std::to_string(added.size()) + " events";
    const auto id = *list_.track();
    edit("Put " + what + " in " + trackName(),
         [&](core::Song& song)
         {
             song.updateEvents(id, [&](core::EventList& events)
                               { events = core::merged(events, added); });
         });
    return true;
}

namespace
{

// What a field is called in undo titles.
std::string fieldName(Column field, const core::Event& event)
{
    switch (field)
    {
    case Column::Position:
        return "position";
    case Column::Data:
        if (event.isNote() || std::holds_alternative<core::PolyPressure>(event.data))
        {
            return "pitch";
        }
        if (std::holds_alternative<core::ControlChange>(event.data))
        {
            return "controller";
        }
        if (std::holds_alternative<core::ProgramChange>(event.data))
        {
            return "program";
        }
        return "text";
    case Column::Value:
        return event.isNote() ? "velocity" : "value";
    case Column::Length:
        return "length";
    case Column::Release:
        return "release velocity";
    case Column::Event:
        break;
    }
    return "field";
}

} // namespace

bool EventListView::openField()
{
    const auto& row = list_.rows()[row_];
    if (row.kind == RowKind::End)
    {
        setMessage("Nothing to edit here", MessageKind::Information);
        return true;
    }
    if (track_ == 0 || row.dim)
    {
        setMessage(row.dim ? "That's the conductor's: edit it in its list (gT)"
                           : "The conductor's list can't be edited yet",
                   MessageKind::Warning);
        return true;
    }
    // The value as shown, less any note value or note number after it in brackets.
    const auto column = field();
    auto text = list_.text(row_)[column];
    text = text.substr(0, text.find(" ("));
    field_ = FieldPrompt{row_, column, true};
    fieldLine_.open(decodeUtf8(text));
    mode_ = Mode::CommandLine;
    return true;
}

bool EventListView::finishField()
{
    const auto prompt = *field_;
    const auto text = fieldLine_.take();
    const auto& row = list_.rows()[prompt.row];
    const auto& event = document_.song().events(*list_.track())[row.index];
    const auto result = setField(event, prompt.column, text, list_.meter());
    std::optional<std::string> error;
    if (const auto* why = std::get_if<std::string>(&result))
    {
        error = *why;
    }
    else
    {
        error = replaceEvent(prompt.row, std::get<core::Event>(result), prompt.column);
    }
    if (error)
    {
        // The prompt stays open, with what was typed, to put right.
        setMessage(*error, MessageKind::Warning);
        fieldLine_.open(decodeUtf8(text));
        field_->fresh = false;
        return true;
    }
    field_.reset();
    mode_ = Mode::Normal;
    return true;
}

bool EventListView::stepFieldBy(int steps)
{
    const auto& row = list_.rows()[row_];
    if (track_ == 0 || row.kind != RowKind::Event)
    {
        setMessage(row.dim       ? "That's the conductor's: edit it in its list (gT)"
                   : track_ == 0 ? "The conductor's list can't be edited yet"
                                 : "Nothing to change here",
                   MessageKind::Warning);
        return true;
    }
    const auto column = field();
    const auto& event = document_.song().events(*list_.track())[row.index];
    const auto result = stepField(event, column, steps, list_.meter());
    std::optional<std::string> error;
    if (const auto* why = std::get_if<std::string>(&result))
    {
        error = *why;
    }
    else
    {
        error = replaceEvent(row_, std::get<core::Event>(result), column);
    }
    if (error)
    {
        setMessage(*error, MessageKind::Warning);
    }
    return true;
}

std::optional<std::string> EventListView::replaceEvent(std::size_t row, const core::Event& changed,
                                                       Column field)
{
    const auto id = *list_.track();
    const auto index = list_.rows()[row].index;
    const auto& events = document_.song().events(id);
    if (changed == events[index])
    {
        return std::nullopt;
    }
    if (auto why = overlap(events, index, changed, list_.meter()))
    {
        return why;
    }
    const auto title = "Set " + fieldName(field, changed) + " in " + trackName();
    edit(title,
         [&](core::Song& song)
         {
             song.updateEvents(id,
                               [&](core::EventList& list)
                               {
                                   list.erase(index);
                                   list.insert(changed);
                               });
         });
    // The cursor follows the event, as a change of position moves it.
    const auto rows = list_.rows();
    const auto& now = document_.song().events(id);
    for (auto r = list_.rowAt(changed.tick); r < rows.size() && rows[r].tick == changed.tick; ++r)
    {
        if (rows[r].kind == RowKind::Event && now[rows[r].index] == changed)
        {
            row_ = r;
            break;
        }
    }
    follow();
    return std::nullopt;
}

bool EventListView::undoOrRedo(bool undo)
{
    const auto step = undo ? document_.undo() : document_.redo();
    if (!step)
    {
        setMessage(undo ? "Nothing to undo" : "Nothing to redo", MessageKind::Information);
        return true;
    }
    // Back where the change was made: its track, time and scroll.
    const auto tracks = document_.song().tracks();
    if (!step->place.track)
    {
        track_ = 0;
    }
    else
    {
        const auto it = std::ranges::find_if(tracks, [&](const auto& track)
                                             { return track->id() == *step->place.track; });
        track_ = it != tracks.end() ? static_cast<std::size_t>(it - tracks.begin()) + 1
                                    : std::min(track_, tracks.size());
    }
    list_ = listOf(track_);
    row_ = list_.rowAt(step->place.tick);
    const auto& view = step->place.view;
    if (view.size() == 2)
    {
        top_ = static_cast<std::size_t>(std::max<std::int64_t>(view[0], 0));
        const auto rows = list_.rows();
        for (std::int64_t i = 0;
             i < view[1] && row_ + 1 < rows.size() && rows[row_ + 1].tick == step->place.tick; ++i)
        {
            ++row_;
        }
    }
    follow();
    setMessage((undo ? "Undid: " : "Redid: ") + step->title, MessageKind::Information);
    return true;
}

bool EventListView::edit(std::string title, const std::function<void(core::Song&)>& change)
{
    const bool changed = document_.edit(std::move(title), place(), change);
    refresh();
    return changed;
}

core::EditPlace EventListView::place() const
{
    // The view's state: the top row, and how far down the rows at the cursor's time it is.
    const auto tick = list_.rows()[row_].tick;
    return {list_.track(),
            tick,
            {static_cast<std::int64_t>(top_), static_cast<std::int64_t>(row_ - list_.rowAt(tick))}};
}

std::string EventListView::trackName() const
{
    if (track_ == 0)
    {
        return "the conductor";
    }
    const auto& name = document_.song().tracks()[track_ - 1]->name;
    return name.empty() ? "track " + std::to_string(track_) : name;
}

std::optional<bool> EventListView::runCommandLine(std::string_view command)
{
    if (mode_ != Mode::CommandLine)
    {
        return false;
    }
    auto& line = activeLine();
    if (command == "command-line-run")
    {
        if (field_)
        {
            return finishField();
        }
        mode_ = Mode::Normal;
        const auto text = line.take();
        if (lineHandler_ && !text.empty())
        {
            lineHandler_(text);
        }
        return true;
    }
    if (command == "command-line-cancel" ||
        (command == "command-line-backspace" && line.text().empty())) // as in Vim
    {
        field_.reset();
        mode_ = Mode::Normal;
        return true;
    }
    if (field_)
    {
        field_->fresh = false; // keys that edit the value keep it
    }
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

Frame EventListView::frame() const
{
    Frame frame;
    frame.push_back(listRegion());
    addBars(frame);
    return frame;
}

Region EventListView::listRegion() const
{
    const Rect& main = layout_.main;
    const int rows = (main.height + cell_.height - 1) / cell_.height;
    CellGrid grid{layout_.columns, rows};

    // The columns' places: the position as wide as the song's last needs.
    std::array<int, kColumnCount> x{};
    std::array<int, kColumnCount> width{};
    int at = kMargin;
    for (std::size_t c = 0; c < kColumnCount; ++c)
    {
        width[c] = kColumns[c].width;
        if (c == 0)
        {
            const auto end = list_.text(list_.rows().size() - 1)[Column::Position];
            width[c] = std::max(static_cast<int>(end.size()) + 3, 10);
        }
        x[c] = at;
        at += width[c];
    }

    // The headings stay put as the list scrolls.
    grid.fill(0, 0, layout_.columns, 1, {U' ', kHeading});
    for (std::size_t c = 0; c < kColumnCount; ++c)
    {
        const auto& column = kColumns[c];
        grid.write(textX(x[c], width[c], column.right, column.heading), 0, column.heading,
                   kHeading);
    }

    const auto cursorField = field();
    for (int line = 1; line < rows; ++line)
    {
        const auto row = top_ + static_cast<std::size_t>(line - 1);
        if (row >= list_.rows().size())
        {
            break;
        }
        const auto text = list_.text(row);
        const auto style = list_.rows()[row].dim ? kDimText : kPlainText;
        for (std::size_t c = 0; c < kColumnCount; ++c)
        {
            const auto column = static_cast<Column>(c);
            // The last column with text may run on, such as a name or SysEx bytes.
            const bool runsOn = column == Column::Data && text[Column::Value].empty() &&
                                text[Column::Length].empty();
            const int room = runsOn ? layout_.columns - x[c] : width[c];
            const auto shown = fitted(text.cells[c], room);
            const int start = textX(x[c], width[c], kColumns[c].right, shown);
            if (field_ && row == field_->row && column == field_->column)
            {
                // The field being edited: what's typed, left-justified, with its cursor.
                const Style editing{Role::Text, Role::Selection, kPlain};
                const auto& typed = fieldLine_.text();
                const int span = std::max(width[c] - 1, static_cast<int>(typed.size()) + 1);
                grid.fill(x[c] - 1, line, span + 1, 1, {U' ', editing});
                grid.write(x[c], line, encodeUtf8(typed), editing);
                const auto cursor = fieldLine_.cursor();
                grid.set(x[c] + static_cast<int>(cursor), line,
                         {cursor < typed.size() ? typed[cursor] : U' ', kCursor});
            }
            else if (row == row_ && column == cursorField)
            {
                grid.fill(start - 1, line,
                          std::max(static_cast<int>(decodeUtf8(shown).size()), 1) + 2, 1,
                          {U' ', kCursor});
                grid.write(start, line, shown, kCursor);
            }
            else
            {
                grid.write(start, line, shown, style);
            }
        }
    }
    return ui::gridRegion(grid, main, cell_, Role::Background);
}

Region EventListView::promptRegion() const
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

void EventListView::addBars(Frame& frame) const
{
    const auto& song = document_.song();
    const auto tick = list_.rows()[row_].tick;
    const auto section = song.sectionAt(tick);
    const auto& meter = list_.meter();

    Bar top;
    if (track_ == 0)
    {
        top.left = {{"0 Conductor", {Role::Accent, Role::Bar, kBold}}};
    }
    else
    {
        const auto& track = *song.tracks()[track_ - 1];
        top.left = {{std::to_string(track_) + " " + track.name, {Role::Accent, Role::Bar, kBold}},
                    {"   ch " + std::to_string(track.channel), kBarText}};
        if (!track.port.empty())
        {
            top.left.push_back({"   " + track.port, kBarText});
        }
    }
    const auto first = meter.toBarBeatTick(song.sectionStart(section)).bar;
    const auto last = meter.toBarBeatTick(song.sectionStart(section + 1) - 1).bar;
    top.right = {{song.sections()[section]->name() + " · bars " + std::to_string(first) + "–" +
                      std::to_string(last),
                  kBarText}};

    Bar bottom;
    bottom.left = {{field_ ? " EDIT " : " NORMAL ", {Role::ModeText, Role::Mode, kBold}}};
    if (!message_.empty())
    {
        const auto role = messageKind_ == MessageKind::Warning ? Role::Warning : Role::Message;
        bottom.left.push_back({"  " + message_, {role, Role::Bar, kPlain}});
    }
    else
    {
        bottom.left.push_back({"  " + list_.text(row_)[Column::Position] + "  " +
                                   signatureText(meter.signatureAt(tick)) + "  " +
                                   song.sections()[section]->name(),
                               kBarText});
    }
    if (const auto pending = keys_.pending(); !pending.empty())
    {
        bottom.right.push_back({pending + "   ", {Role::Accent, Role::Bar, kBold}});
    }

    const auto spacing = BarSpacing::forCell(cell_);
    auto topRegion = layoutBar(top, width_, cell_, spacing);
    topRegion.bounds = layout_.top[0];
    frame.push_back(std::move(topRegion));
    if (mode_ == Mode::CommandLine && !field_)
    {
        frame.push_back(promptRegion());
        return;
    }
    auto bottomRegion = layoutBar(bottom, width_, cell_, spacing);
    bottomRegion.bounds = layout_.bottom[0];
    frame.push_back(std::move(bottomRegion));
}

} // namespace modal::ui
