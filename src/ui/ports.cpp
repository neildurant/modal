// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/ports.hpp>

#include <algorithm>
#include <cctype>

namespace modal::ui
{

namespace
{

char lower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool equalIgnoringCase(std::string_view a, std::string_view b)
{
    return std::ranges::equal(a, b, {}, lower, lower);
}

bool containsIgnoringCase(std::string_view text, std::string_view part)
{
    return !std::ranges::search(text, part, {}, lower, lower).empty();
}

std::string quoted(std::string_view text)
{
    std::string out = "\"";
    for (const char c : text)
    {
        if (c == '"' || c == '\\')
        {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

// A line's words, a quoted one with \" and \\ inside, and key="value" pairs as one word with
// the value unquoted; or nothing if a quote isn't closed.
std::optional<std::vector<std::string>> words(std::string_view line)
{
    std::vector<std::string> result;
    std::size_t at = 0;
    while (at < line.size())
    {
        if (line[at] == ' ' || line[at] == '\t')
        {
            ++at;
            continue;
        }
        std::string word;
        while (at < line.size() && line[at] != ' ' && line[at] != '\t')
        {
            if (line[at] != '"')
            {
                word += line[at++];
                continue;
            }
            ++at; // a quoted part, to its closing quote
            bool closed = false;
            while (at < line.size())
            {
                const char c = line[at++];
                if (c == '"')
                {
                    closed = true;
                    break;
                }
                if (c == '\\' && at < line.size())
                {
                    word += line[at++];
                    continue;
                }
                word += c;
            }
            if (!closed)
            {
                return std::nullopt;
            }
        }
        result.push_back(std::move(word));
    }
    return result;
}

std::string_view directionWord(PortDirection direction)
{
    return direction == PortDirection::Input ? "input" : "output";
}

} // namespace

KnownPort* PortTable::findPort(PortDirection direction, std::string_view name)
{
    const auto it =
        std::ranges::find_if(ports_, [&](const KnownPort& port)
                             { return port.direction == direction && port.name == name; });
    return it == ports_.end() ? nullptr : &*it;
}

const KnownPort* PortTable::find(PortDirection direction, std::string_view name) const
{
    return const_cast<PortTable*>(this)->findPort(direction, name);
}

bool PortTable::detected(PortDirection direction, std::span<const std::string> names)
{
    bool added = false;
    for (auto& port : ports_)
    {
        if (port.direction == direction)
        {
            port.present = std::ranges::find(names, port.name) != names.end();
        }
    }
    for (const auto& name : names)
    {
        if (findPort(direction, name) != nullptr)
        {
            continue;
        }
        // After the last of its direction, so inputs stay before outputs.
        const auto after =
            std::ranges::find_if(ports_.rbegin(), ports_.rend(), [&](const KnownPort& port)
                                 { return port.direction == direction; });
        const auto at = after != ports_.rend()              ? after.base()
                        : direction == PortDirection::Input ? ports_.begin()
                                                            : ports_.end();
        ports_.insert(at, KnownPort{direction, name, {}, false, true});
        added = true;
    }
    return added;
}

bool PortTable::setAlias(PortDirection direction, std::string_view name, std::string alias)
{
    auto* port = findPort(direction, name);
    if (port == nullptr || port->alias == alias)
    {
        return false;
    }
    port->alias = std::move(alias);
    return true;
}

bool PortTable::setEnabled(PortDirection direction, std::string_view name, bool enabled)
{
    auto* port = findPort(direction, name);
    if (port == nullptr || port->enabled == enabled)
    {
        return false;
    }
    port->enabled = enabled;
    return true;
}

bool PortTable::forget(PortDirection direction, std::string_view name)
{
    const auto removed =
        std::erase_if(ports_, [&](const KnownPort& port)
                      { return port.direction == direction && port.name == name; });
    if (removed > 0 && defaultOf(direction) == name)
    {
        defaultOf(direction).clear();
    }
    return removed > 0;
}

bool PortTable::setDefault(PortDirection direction, std::string name)
{
    if (name == defaultOf(direction) || (!name.empty() && findPort(direction, name) == nullptr))
    {
        return false;
    }
    defaultOf(direction) = std::move(name);
    return true;
}

std::vector<std::string> PortTable::usable(PortDirection direction) const
{
    std::vector<std::string> names;
    for (const auto& port : ports_)
    {
        if (port.direction == direction && port.enabled && port.present)
        {
            names.push_back(port.name);
        }
    }
    return names;
}

std::vector<std::string> PortTable::offered(PortDirection direction) const
{
    std::vector<std::string> names;
    for (const auto& port : ports_)
    {
        if (port.direction == direction && port.enabled)
        {
            names.push_back(port.shown());
        }
    }
    return names;
}

PortResolution PortTable::resolve(PortDirection direction, std::string_view name) const
{
    PortResolution result;
    if (name.empty())
    {
        if (defaultPort(direction).empty())
        {
            result.problem = PortProblem::Missing;
            return result;
        }
        name = defaultPort(direction);
    }
    result.named = std::string{name};
    // An alias first, then a detected name, each as :port matches: exactly, then ignoring case,
    // then a part of one. A match that's several ports is none.
    std::vector<const KnownPort*> mine;
    for (const auto& port : ports_)
    {
        if (port.direction == direction)
        {
            mine.push_back(&port);
        }
    }
    const KnownPort* found = nullptr;
    const auto tryMatching = [&](auto matches)
    {
        const KnownPort* only = nullptr;
        int count = 0;
        for (const auto* port : mine)
        {
            if (matches(*port))
            {
                only = port;
                ++count;
            }
        }
        if (count == 1)
        {
            found = only;
        }
        return count > 0;
    };
    const bool matched =
        tryMatching([&](const KnownPort& p) { return !p.alias.empty() && p.alias == name; }) ||
        tryMatching([&](const KnownPort& p)
                    { return !p.alias.empty() && equalIgnoringCase(p.alias, name); }) ||
        tryMatching([&](const KnownPort& p) { return p.name == name; }) ||
        tryMatching([&](const KnownPort& p) { return equalIgnoringCase(p.name, name); }) ||
        tryMatching(
            [&](const KnownPort& p)
            {
                return containsIgnoringCase(p.shown(), name) || containsIgnoringCase(p.name, name);
            });
    if (!matched || found == nullptr)
    {
        result.problem = PortProblem::Missing;
    }
    else if (!found->enabled)
    {
        result.problem = PortProblem::Disabled;
    }
    else if (!found->present)
    {
        result.problem = PortProblem::Missing;
    }
    else
    {
        result.port = found->name;
    }
    return result;
}

std::string PortTable::text() const
{
    std::string text =
        "# The MIDI ports of this machine, as the Ports view (, v m) sets them. Written "
        "by Modal; safe to delete.\n";
    for (const auto& port : ports_)
    {
        text += std::string{directionWord(port.direction)} + " " + quoted(port.name) +
                (port.enabled ? " on" : " off");
        if (port.name == defaultPort(port.direction))
        {
            text += " default";
        }
        if (!port.alias.empty())
        {
            text += " alias=" + quoted(port.alias);
        }
        text += "\n";
    }
    return text;
}

PortTable PortTable::read(std::string_view text, std::vector<std::string>* errors)
{
    PortTable table;
    std::size_t at = 0;
    int number = 0;
    const auto fail = [&](std::string_view why)
    {
        if (errors != nullptr)
        {
            errors->push_back("ports:" + std::to_string(number) + ": " + std::string{why});
        }
    };
    while (at < text.size())
    {
        const auto end = std::min(text.find('\n', at), text.size());
        auto line = text.substr(at, end - at);
        at = end + 1;
        ++number;
        if (line.ends_with('\r'))
        {
            line.remove_suffix(1);
        }
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string_view::npos || line[first] == '#')
        {
            continue;
        }
        const auto parts = words(line);
        if (!parts || parts->size() < 3 || ((*parts)[0] != "input" && (*parts)[0] != "output"))
        {
            fail("not a port: " + std::string{line});
            continue;
        }
        const auto direction =
            (*parts)[0] == "input" ? PortDirection::Input : PortDirection::Output;
        KnownPort port{direction, (*parts)[1], {}, false, false};
        bool isDefault = false;
        bool understood = !port.name.empty();
        for (std::size_t i = 2; i < parts->size() && understood; ++i)
        {
            const auto& word = (*parts)[i];
            if (word == "on" || word == "off")
            {
                port.enabled = word == "on";
            }
            else if (word == "default")
            {
                isDefault = true;
            }
            else if (word.starts_with("alias="))
            {
                port.alias = word.substr(6);
            }
            else
            {
                understood = false;
            }
        }
        if (!understood || table.find(direction, port.name) != nullptr)
        {
            fail(understood ? "listed twice: " + port.name : "not a port: " + std::string{line});
            continue;
        }
        if (isDefault)
        {
            table.defaultOf(direction) = port.name;
        }
        const auto after = std::ranges::find_if(table.ports_.rbegin(), table.ports_.rend(),
                                                [&](const KnownPort& known)
                                                { return known.direction == direction; });
        const auto place = after != table.ports_.rend()        ? after.base()
                           : direction == PortDirection::Input ? table.ports_.begin()
                                                               : table.ports_.end();
        table.ports_.insert(place, std::move(port));
    }
    return table;
}

} // namespace modal::ui
