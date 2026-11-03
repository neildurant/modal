// SPDX-License-Identifier: GPL-3.0-or-later

// The workspace's hint menu and list of keys, and keys shown as typed (split from
// workspace.cpp, November 2026, after a code review).

#include "workspace_commands.hpp"

#include <modal/core/pitch.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/completion.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/event_edit.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/key_notation.hpp>
#include <modal/ui/palette.hpp>
#include <modal/ui/settings.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <map>
#include <tuple>
#include <utility>

namespace modal::ui
{

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
// Left read together as h/←, and < as itself; the others in notation.
std::string hintKey(const Key& key)
{
    if (key.modifiers == kNoModifiers)
    {
        if (key.named == NamedKey::None && key.character == U'<')
        {
            return "<";
        }
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

void Workspace::noteWaiting(std::int64_t now)
{
    // Keys waiting for more, an operator waiting for its range, or a find for its note name,
    // start the wait for the hint menu, or show it at once with no delay.
    if (keys_.keys().empty() && !operator_ && !findWaiting())
    {
        waitingSince_.reset();
        hintsShown_ = false;
        return;
    }
    if (!waitingSince_ || !hintsShown_)
    {
        waitingSince_ = now;
        hintsShown_ = hintDelay_ == 0 || transformTarget_.has_value(); // a menu, so at once
    }
}

bool Workspace::findWaiting() const
{
    return (awaiting_ && awaiting_->command.starts_with("find-")) ||
           (finding_.naming && finding_.naming->redo);
}

std::optional<std::vector<Workspace::Hint>> Workspace::hints() const
{
    if (!hintsOn_ || !hintsShown_ || (keys_.keys().empty() && !operator_ && !findWaiting()))
    {
        return std::nullopt;
    }
    // A find's note name: the letters, then a sharp or flat (decided November 2026, so f
    // says what it's waiting for).
    if (awaiting_ && awaiting_->command.starts_with("find-"))
    {
        return std::vector<Hint>{{.keys = "c d e f g a b", .title = "a note name, any octave"},
                                 {.keys = "# b", .title = "then sharp or flat"}};
    }
    if (findWaiting())
    {
        return std::vector<Hint>{{.keys = "#", .title = "sharp"}, {.keys = "b", .title = "flat"}};
    }
    if (keys_.keys().empty())
    {
        auto hints = operatorHints();
        return hints.empty() ? std::nullopt : std::optional{std::move(hints)};
    }
    const auto mode = keys_.mode();
    const auto& keymap =
        mode == Mode::OperatorPending || mode == Mode::Visual ? operatorKeymapHere() : keymapHere();
    auto hints = hintsAfter(keymap, mode, keys_.keys());
    if (hints.empty())
    {
        return std::nullopt;
    }
    return hints;
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
            // Visual mode's keys are mostly Normal mode's, their groups' titles with them.
            auto title = keymap.groupTitle(mode, next);
            if (title.empty() && mode != Mode::Normal)
            {
                title = keymap.groupTitle(Mode::Normal, next);
            }
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
    for (const auto& [sequence, binding] : operatorKeymapHere().bindings(Mode::OperatorPending))
    {
        const auto& command = binding.command;
        const auto* info = commands_.find(command);
        const bool line = isOperator(command);
        if (sequence.size() > 2 || info == nullptr || !(line || info->motion || info->object) ||
            std::ranges::find(kMovesOnly, command) != kMovesOnly.end() || !offered(command))
        {
            continue;
        }
        if (line && command != operator_->name)
        {
            continue; // dd after d, yy after y, and no other operator
        }
        auto text = view().describeRange(operator_->name, line ? "operator-line" : command);
        if (!text)
        {
            continue;
        }
        auto written = shownKeys(sequence);
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
        const auto bindings = keymapHere().bindings(mode);
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
            const auto written = shownKeys(sequence);
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
            const auto section =
                binding.section.empty() ? std::string{"Other keys"} : binding.section;
            const auto* info = commands_.find(binding.command);
            auto name = !binding.hint.empty() ? binding.hint
                        : info != nullptr     ? titleOf(*info)
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
    const auto& keymap =
        mode == Mode::OperatorPending || mode == Mode::Visual ? operatorKeymapHere() : keymapHere();
    auto heading = pending();
    if (keys_.keys().empty() && operator_)
    {
        // After an operator: its key's hint, Delete or Yank.
        for (const auto& [keys, binding] : keymapHere().bindings(Mode::Normal))
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

std::string Workspace::shownKeys(const std::vector<Key>& keys) const
{
    std::string text;
    for (const auto& key : keys)
    {
        text += hintKey(keys_.typed(key));
    }
    return text;
}

} // namespace modal::ui
