// SPDX-License-Identifier: GPL-3.0-or-later

// The workspace's prompt: the : line and the questions answered there, completion, the
// palette, and drawing them (split from workspace.cpp, November 2026, after a code review).

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

constexpr Style kCursor{Role::CursorText, Role::Cursor, kPlain};

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
    if (prompt_.palette)
    {
        return runPalette(command);
    }
    if (command == "command-line-next" || command == "command-line-previous")
    {
        const bool options = prompt_.answer && (prompt_.keyPrompt ||
                                                !transformSettings(prompt_.answerCommand).empty());
        return (!prompt_.answer || options) && complete(command == "command-line-next" ? 1 : -1);
    }
    if (prompt_.searchPrompt && (command == "command-line-run" || command == "command-line-cancel"))
    {
        // Back to Normal mode, or Visual mode if searching from it, the cursor moving with
        // the selection; Escape leaves the matches as they were.
        prompt_.searchPrompt = false;
        mode_ = selection_ ? Mode::Visual : Mode::Normal;
        const auto text = command == "command-line-run" ? prompt_.line.take() : std::string{};
        prompt_.line.clear();
        std::swap(prompt_.line, prompt_.searchLine);
        return command != "command-line-run" || search(text);
    }
    // While the matches show, above the line with the first nearest it, Up goes on through
    // them and Down back, as they look, stopping at the ends; from none chosen, Up takes the one
    // nearest the line and Down the top one shown (decided November 2026, after use, as going
    // back from none took the last, far off); Page Up and Down a popup of them.
    if (const auto shown = completions())
    {
        if (command == "command-line-older")
        {
            return chooseMatch(1);
        }
        if (command == "command-line-newer")
        {
            if (!shown->chosen)
            {
                chooseIndex(topShown());
                return true;
            }
            return chooseMatch(-1);
        }
        if (command == "command-line-page-down" || command == "command-line-page-up")
        {
            const auto page = static_cast<std::ptrdiff_t>(completionRows(shown->matches.size()));
            return chooseMatch(command == "command-line-page-up" ? page : -page);
        }
        if (command == "command-line-backspace" || command == "command-line-delete")
        {
            const auto edited = editLine(prompt_.line, command);
            refilter();
            return edited;
        }
    }
    prompt_.completing.reset();
    if (command == "command-line-run" && prompt_.answer)
    {
        // An answer to a question, kept out of the : prompt's history; a key in its own.
        mode_ = Mode::Normal;
        auto text = encodeUtf8(prompt_.line.text());
        prompt_.line.clear();
        if (std::exchange(prompt_.keyPrompt, false))
        {
            prompt_.line.open(decodeUtf8(text));
            prompt_.line.take();
            std::swap(prompt_.line, prompt_.keyLine);
        }
        if (text.find_first_not_of(' ') == std::string::npos)
        {
            text = std::exchange(prompt_.answerDefault, {});
        }
        if (const auto answer = std::exchange(prompt_.answer, {}); !text.empty())
        {
            answer(text);
        }
        endVisual(); // a selection the question was about
        return true;
    }
    if (command == "command-line-run")
    {
        mode_ = Mode::Normal;
        const auto text = prompt_.line.take();
        if (selection_ && !text.empty())
        {
            // From Visual mode: only commands that take a range act on the selection.
            const auto& words = text;
            const auto start = words.find_first_not_of(" \t");
            const auto name = start == std::string::npos
                                  ? std::string{}
                                  : words.substr(start, words.find_first_of(" \t!", start) - start);
            const auto* info = commands_.find(name);
            if (info == nullptr || !info->range)
            {
                endVisual();
                refuse(name + " doesn't take a selection", MessageKind::Warning);
                return true;
            }
        }
        if (lineHandler_ && !text.empty())
        {
            lineHandler_(text);
        }
        endVisual();
        return true;
    }
    // Escape gives it up. Backspace on an empty line does nothing, rather than closing it as
    // in Vim, so a press too many when clearing what's there can't throw it away, nor the next
    // go back a view (decided November 2026, after use); the same in every line typed into.
    if (command == "command-line-cancel")
    {
        mode_ = Mode::Normal;
        prompt_.answer = {};
        if (std::exchange(prompt_.keyPrompt, false))
        {
            prompt_.line.clear();
            std::swap(prompt_.line, prompt_.keyLine);
        }
        endVisual(); // a selection the question or the line was about
        return true;
    }
    return editLine(prompt_.line, command);
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
    {
        // The built-in ones and those the user has chosen before, as one list in order by
        // denominator then numerator, so the one wanted can be found by scanning (decided October
        // 2026); not the song's own, which may be remainder bars of no use.
        std::vector<core::TimeSignature> meters{offeredMeters().begin(), offeredMeters().end()};
        for (const auto meter : remembered_.meters())
        {
            if (std::ranges::find(meters, meter) == meters.end())
            {
                meters.push_back(meter);
            }
        }
        std::ranges::sort(meters, {}, [](core::TimeSignature meter)
                          { return std::pair{meter.denominator, meter.numerator}; });
        for (const auto meter : meters)
        {
            found.push_back(std::to_string(meter.numerator) + "/" +
                            std::to_string(meter.denominator));
        }
        return found;
    }
    case Choices::Length:
        return {"1/1",  "1/2",  "1/4",  "1/8",  "1/16",  "1/32", "1/2.",
                "1/4.", "1/8.", "1/4t", "1/8t", "1/16t", "grid"};
    case Choices::Key:
    {
        // The keys suggested for the notes, at the start; then the tonic, then the scale.
        const auto before = encodeUtf8(prompt_.line.text().substr(0, prompt_.line.cursor()));
        const auto start = before.find_first_not_of(' ');
        if (start == std::string::npos && !keySuggestions_.empty())
        {
            return keySuggestions_;
        }
        if (start == std::string::npos || before.find(' ', start) == std::string::npos)
        {
            for (const auto name : core::tonicNames())
            {
                found.emplace_back(name);
            }
            return found;
        }
        for (const auto& scale : scales())
        {
            if (std::ranges::find(found, scale.name) == found.end())
            {
                found.push_back(scale.name);
            }
        }
        return found;
    }
    case Choices::Query:
    {
        // After inside or outside, a key's tonic and then its scale; otherwise the keywords.
        std::vector<std::string> before;
        {
            const auto text = encodeUtf8(prompt_.line.text().substr(0, prompt_.line.cursor()));
            std::size_t at = 0;
            while (at < text.size())
            {
                const auto start = text.find_first_not_of(' ', at);
                if (start == std::string::npos)
                {
                    break;
                }
                const auto end = std::min(text.find(' ', start), text.size());
                before.push_back(text.substr(start, end - start));
                at = end;
            }
            if (!text.empty() && text.back() != ' ' && !before.empty())
            {
                before.pop_back(); // the word being typed
            }
        }
        const auto keyword = [&](std::size_t back)
        {
            return before.size() >= back && (before[before.size() - back] == "inside" ||
                                             before[before.size() - back] == "outside");
        };
        if (keyword(1))
        {
            for (const auto name : core::tonicNames())
            {
                found.emplace_back(name);
            }
            return found;
        }
        if (keyword(2))
        {
            for (const auto& scale : scales())
            {
                if (std::ranges::find(found, scale.name) == found.end())
                {
                    found.push_back(scale.name);
                }
            }
            return found;
        }
        return {"bend", "cc", "inside", "len", "outside", "vel"};
    }
    case Choices::Option:
    {
        // A setting being asked for, its values; at the : prompt, the names, NAME= to go on
        // with, and then the values of the one named.
        const auto options = transformSettings(optionsCommand());
        const auto addLengths = [&](std::string_view before)
        {
            for (const auto& length : choices(Choices::Length, {}))
            {
                if (length != "grid")
                {
                    found.push_back(std::string{before} + length);
                }
            }
        };
        if (prompt_.answer)
        {
            if (prompt_.answerSetting < options.size())
            {
                const auto& setting = options[prompt_.answerSetting];
                for (const auto value : setting.values)
                {
                    found.emplace_back(value);
                }
                if (setting.lengths)
                {
                    addLengths({});
                }
                if (setting.tracks)
                {
                    for (const auto& track : document_.song().tracks())
                    {
                        found.push_back(track->name);
                    }
                }
            }
            return found;
        }
        const auto equals = word.find('=');
        for (const auto& option : options)
        {
            if (option.name.empty())
            {
                continue;
            }
            if (equals == std::string_view::npos)
            {
                found.push_back(std::string{option.name} + "=");
                continue;
            }
            if (word.substr(0, equals) != option.name)
            {
                continue;
            }
            for (const auto value : option.values)
            {
                found.push_back(std::string{option.name} + "=" + std::string{value});
            }
            if (option.lengths)
            {
                addLengths(std::string{option.name} + "=");
            }
        }
        return found;
    }
    case Choices::EventKind:
        return {"bend",         "cc",       "copyright", "cue",   "instrument", "lyric",
                "polypressure", "pressure", "program",   "sysex", "tempo",      "text"};
    case Choices::FreezePart:
        return {"all", "transpose", "velocity", "quantise", "time"};
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
    if (mode_ != Mode::CommandLine || view().typing() || prompt_.palette || !prompt_.completing ||
        prompt_.completing->line != prompt_.line.text() || prompt_.completing->matches.size() < 2)
    {
        return std::nullopt;
    }
    return Completions{prompt_.completing->matches, prompt_.completing->chosen,
                       prompt_.completing->scroll};
}

Workspace::CompletingWord Workspace::completingWord() const
{
    const auto& text = prompt_.line.text();
    const auto word = wordAt(text, prompt_.line.cursor());
    CompletingWord result{word.start,
                          encodeUtf8(text.substr(word.start, prompt_.line.cursor() - word.start)),
                          Choices::Command};
    if (prompt_.answer)
    {
        // A transform's question: its options, wherever they're typed; or a key.
        result.kind = prompt_.keyPrompt ? Choices::Key : Choices::Option;
        return result;
    }
    if (prompt_.searchPrompt)
    {
        result.kind = Choices::Query;
        return result;
    }
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
    { prompt_.line.replace(start, prompt_.line.cursor(), decodeUtf8(with)); };
    if (prompt_.completing && prompt_.completing->line == prompt_.line.text() &&
        prompt_.completing->matches.size() > 1)
    {
        const auto count = prompt_.completing->matches.size();
        const auto& chosen = prompt_.completing->chosen;
        // From none chosen, Tab takes the first, and Shift-Tab the top one shown.
        const auto to = !chosen  ? (by > 0 ? 0 : topShown())
                        : by > 0 ? (*chosen + 1) % count
                                 : (*chosen + count - 1) % count;
        chooseIndex(to);
        return true;
    }
    prompt_.completing.reset();
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
        // A setting's name goes on too, with =, ? or !.
        const bool goesOn =
            only.ends_with('=') || only.ends_with('/') ||
            (word.kind == Choices::Setting && only.find('=') == std::string::npos) ||
            (word.kind == Choices::Query && (only == "vel" || only == "len" || only == "cc"));
        replaceWord(word.start, only + (goesOn ? "" : " "));
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
    prompt_.completing = Completing{found, std::nullopt, word.start, prompt_.line.text(), 0};
    return true;
}

std::size_t Workspace::topShown() const
{
    const auto& c = *prompt_.completing;
    return std::min(c.scroll + completionRows(c.matches.size()), c.matches.size()) - 1;
}

bool Workspace::chooseMatch(std::ptrdiff_t by)
{
    // From the one chosen, or with none yet from before the first, or going back, from above
    // the top one shown.
    const auto& c = *prompt_.completing;
    const auto count = static_cast<std::ptrdiff_t>(c.matches.size());
    const auto from = c.chosen ? static_cast<std::ptrdiff_t>(*c.chosen)
                      : by > 0 ? -1
                               : static_cast<std::ptrdiff_t>(topShown()) + 1;
    chooseIndex(static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(from + by, 0, count - 1)));
    return true;
}

void Workspace::chooseIndex(std::size_t to)
{
    // The line takes the match, and the columns shown move to keep it in view.
    auto& c = *prompt_.completing;
    c.chosen = to;
    prompt_.line.replace(c.start, prompt_.line.cursor(), decodeUtf8(c.matches[to]));
    c.line = prompt_.line.text();
    const auto rows = completionRows(c.matches.size());
    if (to < c.scroll)
    {
        c.scroll = to;
    }
    else if (to >= c.scroll + rows)
    {
        c.scroll = to - rows + 1;
    }
}

void Workspace::refilter()
{
    // The word's matches as it's typed, none chosen; gone when the word ends or none match.
    const auto& text = prompt_.line.text();
    const auto at = prompt_.line.cursor();
    const auto word = completingWord();
    const bool ended = at > 0 && (text[at - 1] == U' ' || text[at - 1] == U'\t');
    auto found = ended || word.kind == Choices::None
                     ? std::vector<std::string>{}
                     : matching(choices(word.kind, word.typed), word.typed);
    if (found.empty())
    {
        prompt_.completing.reset();
        return;
    }
    prompt_.completing = Completing{std::move(found), std::nullopt, word.start, text, 0};
}

Region Workspace::completionRegion(const Completions& shown) const
{
    // A small popup above the word being completed, as wide as the longest match, growing up
    // from the line with the first match nearest it (decided November 2026, as lighter than the
    // palette); the one chosen in the cursor's colours, and a ▲ or ▼ where there are more.
    const int margin = BarSpacing::forCell(cell_).margin;
    const int pad = cell_.height / 4;
    const auto rows = completionRows(shown.matches.size());
    std::size_t widest = 0;
    for (const auto& match : shown.matches)
    {
        widest = std::max(widest, decodeUtf8(match).size());
    }
    const bool more = shown.matches.size() > rows;
    const int width = (static_cast<int>(widest) + (more ? 4 : 2)) * cell_.width;
    const int height = static_cast<int>(rows) * cell_.height + 2 * pad;
    // Over the word's first character on the line, kept within the window.
    const auto label = promptLabel().size();
    const auto room = static_cast<std::size_t>(
        std::max((width_ - 2 * margin) / cell_.width - 1 - static_cast<int>(label), 1));
    const auto cursor = prompt_.line.cursor();
    const std::size_t first = cursor >= room ? cursor - room + 1 : 0;
    const auto start = prompt_.completing ? prompt_.completing->start : 0;
    const int wordX =
        margin + static_cast<int>(label + (start > first ? start - first : 0) - 1) * cell_.width;
    const int x = std::clamp(wordX, 0, std::max(width_ - width, 0));
    const auto base = panelBase();
    Region region{.bounds = {x, base.y - height, width, height},
                  .background = Role::Panel,
                  .text = {},
                  .under = {},
                  .over = {}};
    const Style plain{Role::Text, Role::Panel, kPlain};
    const Style chosen{Role::CursorText, Role::Cursor, kPlain};
    const Style dim{Role::Dim, Role::Panel, kPlain};
    for (std::size_t r = 0; r < rows; ++r)
    {
        const auto i = shown.scroll + r;
        if (i >= shown.matches.size())
        {
            break;
        }
        const int y = pad + static_cast<int>(rows - 1 - r) * cell_.height; // the first lowest
        if (shown.chosen == i)
        {
            region.under.push_back({{0, y, width, cell_.height}, Role::Cursor, 255});
        }
        region.text.push_back(
            {cell_.width, y, decodeUtf8(shown.matches[i]), shown.chosen == i ? chosen : plain});
    }
    if (more)
    {
        const int side = width - 2 * cell_.width;
        if (shown.scroll + rows < shown.matches.size())
        {
            region.text.push_back({side, pad, U"▲", dim});
        }
        if (shown.scroll > 0)
        {
            region.text.push_back(
                {side, pad + static_cast<int>(rows - 1) * cell_.height, U"▼", dim});
        }
    }
    return region;
}

void Workspace::askFor(std::string label, std::function<void(const std::string&)> answer,
                       std::string defaultAnswer)
{
    mode_ = Mode::CommandLine;
    prompt_.line.open();
    prompt_.answerLabel = std::move(label);
    prompt_.answer = std::move(answer);
    prompt_.answerDefault = std::move(defaultAnswer);
    prompt_.answerCommand.clear();
    prompt_.answerSetting = 0;
}

void Workspace::askName(std::string label, std::string name,
                        std::function<void(const std::string&)> answer)
{
    askFor(std::move(label), std::move(answer));
    prompt_.line.open(decodeUtf8(name));
}

std::vector<PaletteItem> Workspace::paletteItems() const
{
    // The commands that can be run from Normal mode, the palette apart, with their keys.
    std::map<std::string, std::vector<std::string>, std::less<>> keys;
    for (const auto& [sequence, binding] : keymap_.bindings(Mode::Normal))
    {
        keys[binding.command].push_back(shownKeys(sequence));
    }
    std::vector<PaletteItem> items;
    const auto now = clock_();
    for (const auto& info : commands_.all())
    {
        const bool normal = info.modes == 0 || (info.modes & modeBit(Mode::Normal)) != 0;
        if (!normal || info.object || !info.palette || info.name == "palette")
        {
            continue;
        }
        items.push_back({.name = std::string{info.name},
                         .title = titleOf(info),
                         .description = std::string{describedIn(info, where())},
                         .aliases = std::string{info.aliases},
                         .offered = offered(info.name),
                         .keys = {},
                         .usage = std::string{info.usage},
                         .rank = remembered_.rank(info.name, now)});
        if (const auto bound = keys.find(info.name); bound != keys.end())
        {
            items.back().keys = bound->second;
        }
    }
    return items;
}

std::optional<char> Workspace::palettePrefix() const
{
    // A first character that searches something else than the commands.
    const auto& text = prompt_.line.text();
    if (!text.empty() &&
        std::u32string_view{U"@#'?"}.find(text.front()) != std::u32string_view::npos)
    {
        return static_cast<char>(text.front());
    }
    return std::nullopt;
}

std::vector<PaletteItem> Workspace::prefixItems(char prefix) const
{
    // @ sections, # tracks, ' labels and ? settings (decided October 2026), each with what
    // helps choose it: its bars and meter, its port and channel, where it is, its value.
    const auto& song = document_.song();
    std::vector<PaletteItem> items;
    const auto item = [&](std::string id, std::string title, std::string about)
    {
        items.push_back({.name = std::move(id),
                         .title = std::move(title),
                         .description = std::move(about),
                         .aliases = "",
                         .offered = true,
                         .keys = {},
                         .usage = {},
                         .rank = 0});
    };
    if (prefix == '@')
    {
        for (std::size_t i = 0; i < song.sections().size(); ++i)
        {
            const auto start = song.sectionStart(i);
            item(std::to_string(i), song.sections()[i]->name(),
                 barsText(song.meter(), start, song.sectionStart(i + 1)) + ", " +
                     std::to_string(song.meter().signatureAt(start).numerator) + "/" +
                     std::to_string(song.meter().signatureAt(start).denominator));
        }
    }
    else if (prefix == '#')
    {
        // The conductor first, as the event list's track 0 (decided November 2026).
        item("conductor", "Conductor", "track 0, the tempos, meters, sections and labels");
        const auto tracks = song.tracks();
        for (std::size_t i = 0; i < tracks.size(); ++i)
        {
            const auto& track = *tracks[i];
            item(std::to_string(i),
                 track.name.empty() ? "Track " + std::to_string(i + 1) : track.name,
                 "track " + std::to_string(i + 1) + ", " +
                     (track.port.empty() ? std::string{"the default port"} : track.port) +
                     ", channel " + std::to_string(track.channel));
        }
    }
    else if (prefix == '\'')
    {
        const auto labels = song.labels();
        for (std::size_t i = 0; i < labels.size(); ++i)
        {
            item(std::to_string(i), labels[i].name, positionText(song.meter(), labels[i].tick));
        }
        for (const auto& mark : song.marks())
        {
            item("mark " + std::string(1, mark.letter), "Mark " + std::string(1, mark.letter),
                 markAbout(mark));
        }
    }
    else if (prefix == '?' && settings_ != nullptr)
    {
        for (const auto& setting : settings_->all())
        {
            item(std::string{setting.name}, std::string{setting.name},
                 formatValue(setting.value) + "  " + std::string{setting.description});
        }
    }
    return items;
}

bool Workspace::inSongOrder() const
{
    // Sections, tracks and labels, listed from the top down as the song has them (decided
    // October 2026), rather than ranked from the line up as commands are.
    const auto prefix = palettePrefix();
    return prefix && *prefix != '?';
}

std::vector<PaletteItem> Workspace::paletteShown() const
{
    const auto prefix = palettePrefix();
    const auto items = prefix ? prefixItems(*prefix) : paletteItems();
    auto query = encodeUtf8(prompt_.line.text());
    if (prefix)
    {
        query.erase(0, 1);
    }
    std::vector<PaletteItem> shown;
    for (const auto i : paletteMatches(items, query, inSongOrder()))
    {
        shown.push_back(items[i]);
    }
    return shown;
}

bool Workspace::runPrefixItem(char prefix, const PaletteItem& item)
{
    // Going to a section, a track or a label, or a setting on the : prompt to change.
    const auto& song = document_.song();
    if (prefix == '?')
    {
        const auto* setting = settings_ != nullptr ? settings_->find(item.name) : nullptr;
        prompt_.line.open(
            decodeUtf8("set " + item.name + "=" +
                       (setting != nullptr ? formatValue(setting->value) : std::string{})));
        return true;
    }
    prompt_.line.clear();
    mode_ = selection_ ? Mode::Visual : Mode::Normal;
    if (item.name.starts_with("mark "))
    {
        const bool done = goToMark(static_cast<char32_t>(item.name.back()), 0);
        return done;
    }
    if (item.name == "conductor")
    {
        return run("view-conductor", 0).changed;
    }
    const auto index = static_cast<std::size_t>(std::stoul(item.name));
    auto cursor = view().cursor();
    if (!selection_)
    {
        jumpedFrom_ = cursor;
    }
    if (prefix == '@' && index < song.sections().size())
    {
        cursor.tick = song.sectionStart(index);
    }
    else if (prefix == '#' && index < song.tracks().size())
    {
        cursor.track = song.tracks()[index]->id();
    }
    else if (prefix == '\'' && index < song.labels().size())
    {
        cursor.tick = song.labels()[index].tick;
    }
    view().setCursor(cursor);
    return true;
}

std::string Workspace::keysFor(std::string_view command) const
{
    auto keys = keysOf(command);
    return keys == command ? std::string{} : keys;
}

bool Workspace::runPalette(std::string_view command)
{
    // Enter runs the command chosen, or with an argument it must have, puts it on the : prompt,
    // its choices showing; Tab puts it there as it is to edit; Up and Down, and Ctrl-p and
    // Ctrl-n, go up and down the list; Escape closes it; the rest edit what's typed.
    const auto shown = paletteShown();
    const auto last = shown.empty() ? 0 : shown.size() - 1;
    if (command == "command-line-cancel")
    {
        // Back to the mode it was opened from, a selection kept.
        prompt_.palette.reset();
        prompt_.line.clear();
        mode_ = selection_ ? Mode::Visual : Mode::Normal;
        return true;
    }
    if (command == "command-line-older" || command == "command-line-newer" ||
        command == "command-line-page-up" || command == "command-line-page-down")
    {
        // Up and Down the way the list looks, from the best, or the song's first, at the top
        // under the line (decided November 2026, after use, as Doom Emacs's M-x has it).
        const auto page = static_cast<std::ptrdiff_t>(paletteRows());
        const std::ptrdiff_t by = command == "command-line-older"     ? -1
                                  : command == "command-line-newer"   ? 1
                                  : command == "command-line-page-up" ? -page
                                                                      : page;
        // From none chosen, the best.
        prompt_.palette->chosen =
            !prompt_.palette->chosen
                ? 0
                : static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(
                      static_cast<std::ptrdiff_t>(*prompt_.palette->chosen) + by, 0,
                      static_cast<std::ptrdiff_t>(last)));
        return true;
    }
    if (command == "command-line-start" || command == "command-line-end")
    {
        // Home and End: the top and the bottom of the list.
        prompt_.palette->chosen = command == "command-line-start" ? 0 : last;
        return true;
    }
    if (command == "command-line-run" || command == "command-line-next")
    {
        if (shown.empty() || !prompt_.palette->chosen)
        {
            return false; // nothing chosen yet
        }
        const auto& item = shown[std::min(*prompt_.palette->chosen, last)];
        if (const auto prefix = palettePrefix())
        {
            prompt_.palette.reset();
            return runPrefixItem(*prefix, item);
        }
        const auto* info = commands_.find(item.name);
        prompt_.palette.reset();
        if (command == "command-line-run")
        {
            remembered_.ran(item.name, clock_());
            rememberedChanged();
        }
        // Taking arguments, even ones it can do without, it goes to the prompt, their hint
        // showing (decided October 2026, after use), so what it takes isn't missed; those it must
        // have with their choices showing too.
        const bool takes = info != nullptr && !info->usage.empty();
        const bool needs = takes && !info->usage.starts_with("[");
        if (command == "command-line-next" || takes)
        {
            prompt_.line.open(decodeUtf8(item.name + " "));
            if (needs)
            {
                complete(1);
            }
            return true;
        }
        // As its keys would run it in the mode the palette was opened from: in Visual mode on
        // the selection, as an operator or a command taking a range does.
        prompt_.line.clear();
        mode_ = selection_ ? Mode::Visual : Mode::Normal;
        const auto ran = run(item.name, 0);
        if (const auto keys = keysFor(item.name); message_.empty() && !keys.empty())
        {
            setMessage("Also: " + keys, MessageKind::Information); // teaching the binding
        }
        return ran.changed;
    }
    const auto edited = editLine(prompt_.line, command);
    prompt_.palette->chosen =
        prompt_.line.text().empty() ? std::nullopt : std::optional<std::size_t>{0};
    return edited.value_or(false);
}

std::size_t Workspace::paletteRows() const
{
    // As many as set, or as fit above the message row with the line above them.
    const auto wanted = settings_ != nullptr ? settings_->integer("palettelines") : 10;
    const int pad = cell_.height / 4;
    const int room = (messageRow().y - cell_.height - 2 * pad) / std::max(cell_.height, 1);
    return static_cast<std::size_t>(
        std::max<std::int64_t>(std::min<std::int64_t>(wanted, room), 1));
}

Region Workspace::paletteRegion() const
{
    // Under its line, which is at the top (decided November 2026, after use, as Doom Emacs's
    // M-x has it): the best first, or in song order, each with its keys and what it does; the
    // one chosen in the cursor's colours; those that do nothing here dimmed; a ▲ or ▼ at the side
    // where there are more. Always as high, so the line stays put as the matches narrow.
    const auto shown = paletteShown();
    // The row chosen, as an index, or past the matches if none is.
    const auto chosen = prompt_.palette->chosen ? std::min(*prompt_.palette->chosen,
                                                           shown.empty() ? 0 : shown.size() - 1)
                                                : shown.size();
    const auto rows = paletteRows();
    const auto first = chosen < shown.size() && chosen >= rows ? chosen - rows + 1 : 0;
    const int margin = BarSpacing::forCell(cell_).margin;
    const int pad = cell_.height / 4;
    const int height = static_cast<int>(rows) * cell_.height + 2 * pad;
    const auto status = messageRow();
    Region region{.bounds = {status.x, status.y - height, status.width, height},
                  .background = Role::Panel,
                  .text = {},
                  .under = {},
                  .over = {}};
    if (shown.empty())
    {
        region.text.push_back(
            {margin, pad, U"No command matches", {Role::Dim, Role::Panel, kPlain}});
        return region;
    }
    const auto count = std::min(rows, shown.size() - first);
    std::size_t titleWidth = 0;
    std::size_t keysWidth = 0;
    std::vector<std::string> keys;
    for (auto i = first; i < first + count; ++i)
    {
        keys.push_back(keysFor(shown[i].name));
        titleWidth = std::max(
            titleWidth, decodeUtf8(shown[i].title).size() +
                            (shown[i].usage.empty() ? 0 : 1 + decodeUtf8(shown[i].usage).size()));
        keysWidth = std::max(keysWidth, decodeUtf8(keys.back()).size());
    }
    const auto x = [&](std::size_t column)
    { return margin + static_cast<int>(column) * cell_.width; };
    for (std::size_t r = 0; r < count; ++r)
    {
        const auto i = first + r;
        const auto& item = shown[i];
        const int y = pad + static_cast<int>(r) * cell_.height;
        const bool isChosen = i == chosen;
        const auto background = isChosen ? Role::Cursor : Role::Panel;
        if (isChosen)
        {
            region.under.push_back({{0, y, region.bounds.width, cell_.height}, Role::Cursor, 255});
        }
        const auto role = [&](Role own)
        {
            return isChosen ? Role::CursorText : item.offered ? own : Role::Dim;
        };
        region.text.push_back(
            {x(0), y, decodeUtf8(item.title), {role(Role::Text), background, kPlain}});
        if (!item.usage.empty())
        {
            region.text.push_back({x(decodeUtf8(item.title).size() + 1),
                                   y,
                                   decodeUtf8(item.usage),
                                   {isChosen ? Role::CursorText : Role::Dim, background, kPlain}});
        }
        region.text.push_back(
            {x(titleWidth + 2), y, decodeUtf8(keys[r]), {role(Role::HintKey), background, kPlain}});
        region.text.push_back({x(titleWidth + keysWidth + 4),
                               y,
                               decodeUtf8(item.description),
                               {isChosen ? Role::CursorText : Role::Dim, background, kPlain}});
    }
    // More before the first shown, or after the last.
    const int side = region.bounds.width - margin - cell_.width;
    const Style more{Role::Dim, Role::Panel, kPlain};
    if (first > 0)
    {
        region.text.push_back({side, pad, U"▲", more});
    }
    if (first + count < shown.size())
    {
        region.text.push_back({side, pad + static_cast<int>(rows - 1) * cell_.height, U"▼", more});
    }
    return region;
}

std::u32string Workspace::promptLabel() const
{
    // ":", or a question's label, for its answer; or from Visual mode, what's selected, which
    // the line acts on.
    if (prompt_.answer)
    {
        return decodeUtf8(prompt_.answerLabel);
    }
    if (prompt_.searchPrompt)
    {
        return U"/";
    }
    if (prompt_.palette)
    {
        return decodeUtf8(selection_ ? "Command on " +
                                           view().selectionStatus().value_or("selection") + ": "
                                     : std::string{"Command: "});
    }
    if (selection_)
    {
        return decodeUtf8(view().selectionStatus().value_or("selection") + ": ");
    }
    return U":";
}

bool Workspace::promptRowShown() const
{
    return mode_ == Mode::CommandLine && !view().typing() && !prompt_.palette;
}

Rect Workspace::panelBase() const
{
    return messageRow();
}

Region Workspace::promptRegion() const
{
    // ":" and the line, from where the cursor still fits, and the cursor as a cell of its own;
    // on the message row (decided 12 November 2026), or above the palette's matches with how
    // many there are at its right (decided November 2026, so nothing covers the status line).
    const int margin = BarSpacing::forCell(cell_).margin;
    const auto& text = prompt_.line.text();
    const auto cursor = prompt_.line.cursor();
    const auto label = promptLabel();
    const auto room = static_cast<std::size_t>(
        std::max((width_ - 2 * margin) / cell_.width - 1 - static_cast<int>(label.size()), 1));
    const std::size_t first = cursor >= room ? cursor - room + 1 : 0;
    auto bounds = messageRow();
    std::string count;
    if (prompt_.palette)
    {
        const int pad = cell_.height / 4;
        bounds.y -= cell_.height + static_cast<int>(paletteRows()) * cell_.height + 2 * pad;
        const auto matches = paletteShown().size();
        count = prompt_.palette->chosen && matches > 0
                    ? std::to_string(std::min(*prompt_.palette->chosen, matches - 1) + 1) + "/" +
                          std::to_string(matches)
                    : std::to_string(matches);
    }
    // On the message row, its colour; over the palette, the bars'.
    const auto background = prompt_.palette ? Role::Bar : Role::MessageLine;
    const Style plain{Role::BarText, background, kPlain};
    Region region{.bounds = bounds, .background = background, .text = {}, .under = {}, .over = {}};
    const auto x = [&](std::size_t column)
    { return margin + static_cast<int>(column) * cell_.width; };
    const auto lead = label.size();
    region.text.push_back({x(0), 0, label, plain});
    region.text.push_back({x(lead), 0, text.substr(first, cursor - first), plain});
    const char32_t under = cursor < text.size() ? text[cursor] : U' ';
    region.text.push_back({x(lead + cursor - first), 0, std::u32string(1, under), kCursor});
    if (cursor + 1 < text.size())
    {
        region.text.push_back({x(lead + 1 + cursor - first), 0, text.substr(cursor + 1), plain});
    }
    // The command's arguments not yet typed, dimmed after the line ([N] [METER]).
    if (const auto hint = argumentHint(); !hint.empty())
    {
        const auto end = std::max(text.size(), cursor + 1);
        const bool spaced = !text.empty() && text.back() == U' ';
        region.text.push_back({x(lead + end - first + (spaced ? 0 : 1)),
                               0,
                               decodeUtf8(hint),
                               {Role::Dim, background, kPlain}});
    }
    // The palette's count, as its position among the matches once one is chosen: 3/120.
    if (!count.empty())
    {
        region.text.push_back({width_ - margin - static_cast<int>(count.size()) * cell_.width,
                               0,
                               decodeUtf8(count),
                               {Role::Dim, background, kPlain}});
    }
    return region;
}

std::string Workspace::argumentHint() const
{
    // At the : prompt, once the line holds a command and a space: its arguments from the one
    // being typed on, as its usage writes them, brackets for those it can do without (decided
    // October 2026, so :bar-insert says it takes [N] [METER]).
    if (prompt_.answer)
    {
        // A transform's setting: how it's written, until something's typed.
        const auto settings = transformSettings(prompt_.answerCommand);
        return prompt_.line.text().empty() && prompt_.answerSetting < settings.size()
                   ? std::string{settings[prompt_.answerSetting].form}
                   : std::string{};
    }
    if (prompt_.searchPrompt)
    {
        // What a search can look for, until something's typed (decided November 2026).
        return prompt_.line.text().empty()
                   ? "C4 F# >A3 A3-A5 vel<60 len<1/16 cc1=127 bend inside KEY outside KEY"
                   : std::string{};
    }
    if (prompt_.palette || view().typing())
    {
        return {};
    }
    const auto text = encodeUtf8(prompt_.line.text());
    const auto space = text.find(' ');
    if (space == std::string::npos)
    {
        return {};
    }
    const auto* info = commands_.find(std::string_view{text}.substr(0, space));
    if (info == nullptr || info->usage.empty())
    {
        return {};
    }
    // The arguments typed, or being typed: words after the name, the last only once a space
    // follows it.
    std::size_t typed = 0;
    for (std::size_t at = space; at < text.size();)
    {
        const auto start = text.find_first_not_of(' ', at);
        if (start == std::string::npos)
        {
            break;
        }
        const auto stop = std::min(text.find(' ', start), text.size());
        typed += stop < text.size() ? 1 : 0;
        at = stop;
    }
    std::vector<std::string_view> words;
    const auto usage = info->usage;
    for (std::size_t at = 0; at < usage.size();)
    {
        const auto start = usage.find_first_not_of(' ', at);
        if (start == std::string_view::npos)
        {
            break;
        }
        const auto stop = std::min(usage.find(' ', start), usage.size());
        words.push_back(usage.substr(start, stop - start));
        at = stop;
    }
    const bool typing = !text.ends_with(' ');
    std::string hint;
    for (auto i = typed + (typing ? 1 : 0); i < words.size(); ++i)
    {
        hint += (hint.empty() ? "" : " ") + std::string{words[i]};
    }
    return hint;
}

} // namespace modal::ui
