#include <modal/ui/commands.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <system_error>

namespace modal::ui
{

namespace
{

constexpr std::array<std::string_view, kModeCount> kModeNames{"Normal", "Visual", "Insert",
                                                              "operator-pending", "command-line"};

bool isSpace(char c)
{
    return c == ' ' || c == '\t';
}

std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && isSpace(text.front()))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && isSpace(text.back()))
    {
        text.remove_suffix(1);
    }
    return text;
}

// Whether a name is the command's or one of its aliases.
bool names(const CommandInfo& info, std::string_view name)
{
    if (info.name == name)
    {
        return true;
    }
    std::string_view aliases = info.aliases;
    while (!aliases.empty())
    {
        const auto space = aliases.find(' ');
        if (aliases.substr(0, space) == name)
        {
            return true;
        }
        aliases.remove_prefix(space == std::string_view::npos ? aliases.size() : space + 1);
    }
    return false;
}

} // namespace

std::string titleOf(const CommandInfo& info)
{
    if (!info.title.empty())
    {
        return std::string{info.title};
    }
    std::string title{info.name};
    std::ranges::replace(title, '-', ' ');
    if (!title.empty() && title[0] >= 'a' && title[0] <= 'z')
    {
        title[0] = static_cast<char>(title[0] - 'a' + 'A');
    }
    return title;
}

void CommandRegistry::add(const CommandInfo& info, CommandHandler handler)
{
    const auto it =
        std::ranges::find(entries_, info.name, [](const Entry& e) { return e.info.name; });
    if (it != entries_.end() && !it->handler && handler)
    {
        it->info = info;
        it->handler = std::move(handler);
        return;
    }
    assert(entry(info.name) == nullptr);
    entries_.push_back({info, std::move(handler)});
}

void CommandRegistry::add(std::span<const CommandInfo> infos)
{
    for (const auto& info : infos)
    {
        add(info);
    }
}

const CommandInfo* CommandRegistry::find(std::string_view name) const
{
    const auto* found = entry(name);
    return found ? &found->info : nullptr;
}

std::vector<CommandInfo> CommandRegistry::all() const
{
    std::vector<CommandInfo> infos;
    for (const auto& e : entries_)
    {
        infos.push_back(e.info);
    }
    return infos;
}

CommandResult CommandRegistry::run(std::string_view name, int count, Mode mode) const
{
    const auto* found = entry(name);
    if (found == nullptr)
    {
        return CommandResult::error("unknown command \"" + std::string{name} + "\"");
    }
    return call(*found, {.count = count}, mode);
}

CommandResult CommandRegistry::runLine(std::string_view line, Mode mode) const
{
    line = trimmed(line);
    if (line.empty())
    {
        return {};
    }
    const auto end = std::ranges::find_if(line, isSpace) - line.begin();
    auto name = line.substr(0, static_cast<std::size_t>(end));
    const bool bang = name.size() > 1 && name.ends_with('!');
    name.remove_suffix(bang ? 1 : 0);
    const auto arguments = trimmed(line.substr(static_cast<std::size_t>(end)));

    const auto* found = entry(name);
    if (found == nullptr)
    {
        return CommandResult::error("unknown command \"" + std::string{name} + "\"");
    }
    const auto& info = found->info;
    if (bang && !info.bang)
    {
        return CommandResult::error(std::string{info.name} + " doesn't take !");
    }
    CommandCall call{.bang = bang, .arguments = arguments, .line = line};
    if (info.usage.empty() && !arguments.empty())
    {
        int count = 0;
        const auto* last = arguments.data() + arguments.size();
        const auto [ptr, error] = std::from_chars(arguments.data(), last, count);
        if (!info.count || error != std::errc{} || ptr != last || count < 1)
        {
            return CommandResult::error(std::string{info.name} +
                                        (info.count ? " takes only a count" : " takes nothing"));
        }
        call.count = count;
        call.arguments = {};
    }
    return this->call(*found, call, mode);
}

const CommandRegistry::Entry* CommandRegistry::entry(std::string_view name) const
{
    const auto it =
        std::ranges::find_if(entries_, [name](const Entry& e) { return names(e.info, name); });
    return it == entries_.end() ? nullptr : &*it;
}

CommandResult CommandRegistry::call(const Entry& entry, const CommandCall& call, Mode mode) const
{
    const auto& info = entry.info;
    if (info.modes != 0 && (info.modes & modeBit(mode)) == 0)
    {
        std::string modes;
        for (std::size_t i = 0; i < kModeCount; ++i)
        {
            if ((info.modes & modeBit(static_cast<Mode>(i))) != 0)
            {
                modes += (modes.empty() ? "" : " or ") + std::string{kModeNames[i]};
            }
        }
        return CommandResult::error(std::string{info.name} + " works only in " + modes + " mode");
    }
    if (!entry.handler)
    {
        return CommandResult::error(std::string{info.name} + " can't be run here");
    }
    return entry.handler(call);
}

} // namespace modal::ui
