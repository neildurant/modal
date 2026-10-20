#include <modal/ui/bar.hpp>
#include <modal/ui/cell_grid.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/event_list_view.hpp>
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

// The columns' headings and widths in cells, with a space after each.
struct ColumnInfo
{
    const char* heading;
    int width;
};

constexpr std::array<ColumnInfo, kColumnCount> kColumns{{
    {"Position", 0}, // as wide as the song's positions need
    {"Event", 14},
    {"Data", 20},
    {"Value", 7},
    {"Length", 14},
    {"Release", 8},
}};

constexpr int kMargin = 1; // a cell before the first column

// A command that takes a count, and one that works only at the : prompt.
constexpr CommandInfo counted(std::string_view name, std::string_view description)
{
    return {.name = name, .description = description, .count = true};
}

constexpr CommandInfo atPrompt(std::string_view name, std::string_view description)
{
    return {.name = name, .description = description, .modes = modeBit(Mode::CommandLine)};
}

constexpr std::array kCommands{
    counted("cursor-left", "Move to the field before"),
    counted("cursor-right", "Move to the next field"),
    counted("cursor-down", "Move down an event"),
    counted("cursor-up", "Move up an event"),
    CommandInfo{.name = "cursor-first", .description = "Move to the first field"},
    CommandInfo{.name = "cursor-last", .description = "Move to the last field"},
    counted("go-first", "Go to the first event, or bar {count}"),
    counted("go-last", "Go to the end, or bar {count}"),
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

EventListView::EventListView(CommandRegistry& registry, const core::Document& document,
                             Keymap keymap, std::int64_t timeout)
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
            line_.insert(key.character);
            return true;
        }
    }
    const auto before = keys_.pending();
    const bool changed = runAll(keys_.key(keymap_, mode_, key, now));
    return changed || hadMessage || keys_.pending() != before;
}

bool EventListView::wake(std::int64_t now)
{
    const auto before = keys_.pending();
    const bool changed = runAll(keys_.timeout(keymap_, now));
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

std::optional<bool> EventListView::runOwn(std::string_view command, int count)
{
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
    if (command == "cursor-down" || command == "cursor-up")
    {
        moveRows(command == "cursor-down" ? n : -n);
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

std::optional<bool> EventListView::runCommandLine(std::string_view command)
{
    if (mode_ != Mode::CommandLine)
    {
        return false;
    }
    if (command == "command-line-run")
    {
        mode_ = Mode::Normal;
        const auto line = line_.take();
        if (lineHandler_ && !line.empty())
        {
            lineHandler_(line);
        }
        return true;
    }
    if (command == "command-line-cancel" ||
        (command == "command-line-backspace" && line_.text().empty())) // as in Vim
    {
        mode_ = Mode::Normal;
        return true;
    }
    if (command == "command-line-backspace")
    {
        return line_.backspace();
    }
    if (command == "command-line-delete")
    {
        return line_.erase();
    }
    if (command == "command-line-older")
    {
        return line_.older();
    }
    if (command == "command-line-newer")
    {
        return line_.newer();
    }
    if (command == "command-line-left")
    {
        line_.left();
    }
    else if (command == "command-line-right")
    {
        line_.right();
    }
    else if (command == "command-line-start")
    {
        line_.start();
    }
    else if (command == "command-line-end")
    {
        line_.end();
    }
    else if (command == "command-line-clear")
    {
        line_.clear();
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
        grid.write(x[c], 0, kColumns[c].heading, kHeading);
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
            if (row == row_ && column == cursorField)
            {
                grid.fill(x[c] - 1, line,
                          std::max(static_cast<int>(decodeUtf8(shown).size()), 1) + 2, 1,
                          {U' ', kCursor});
                grid.write(x[c], line, shown, kCursor);
            }
            else
            {
                grid.write(x[c], line, shown, style);
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
    bottom.left = {{" NORMAL ", {Role::ModeText, Role::Mode, kBold}}};
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
    if (mode_ == Mode::CommandLine)
    {
        frame.push_back(promptRegion());
        return;
    }
    auto bottomRegion = layoutBar(bottom, width_, cell_, spacing);
    bottomRegion.bounds = layout_.bottom[0];
    frame.push_back(std::move(bottomRegion));
}

} // namespace modal::ui
