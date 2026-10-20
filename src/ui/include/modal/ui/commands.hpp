#pragma once

#include <modal/ui/keymap.hpp>

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// A set of modes, as bits.
using ModeSet = std::uint8_t;

constexpr ModeSet modeBit(Mode mode)
{
    return static_cast<ModeSet>(1u << static_cast<unsigned>(mode));
}

// What a command is: its name, what it does, and how it's called. Every action is described
// once like this, in the registry, so the keymap, the : line, --write-config's list of
// commands, and later completion, the hint menu and the palette all draw on one list.
struct CommandInfo
{
    std::string_view name = {};        // lower case with hyphens, by what it acts on: cursor-left
    std::string_view description = {}; // what it does, in a line

    std::string_view title = {};   // for menus; if empty, made from the name (see titleOf)
    std::string_view aliases = {}; // other names, separated by spaces, such as q for quit
    std::string_view usage = {};   // its arguments, such as FILE, if it takes any
    ModeSet modes = 0;             // the modes it works in, or 0 for any
    bool count = false;            // takes a count, from keys or as its argument (:cursor-down 3)
    bool bang = false;             // takes a !, as write-config! does
    bool config = false;           // can be used in a config file
    bool keys = true;              // can be bound to keys
};

// A command's title: the one given, or else its name as words, cursor-left as "Cursor left".
std::string titleOf(const CommandInfo& info);

// How a command is being run.
struct CommandCall
{
    int count = 0;                   // 0 for none
    bool bang = false;               // after its name, as in write-config!
    std::string_view arguments = {}; // the rest of the line, from the : line
    std::string_view line = {};      // the whole line, from the : line
};

// What running a command did: whether what's shown changed, and a message to show, perhaps
// as a warning.
struct CommandResult
{
    bool changed = false;
    std::string message = {};
    bool warning = false;

    static CommandResult error(std::string message) { return {true, std::move(message), true}; }

    friend bool operator==(const CommandResult&, const CommandResult&) = default;
};

using CommandHandler = std::function<CommandResult(const CommandCall&)>;

// Every command, with what runs it. A command can be described without a handler, to bind
// keys to and list, where nothing can run it (as for the built-in modalrc's defaults).
class CommandRegistry
{
public:
    // Adds a command; its name and aliases must be new.
    void add(const CommandInfo& info, CommandHandler handler = {});
    void add(std::span<const CommandInfo> infos);

    // The command with a name or alias, or null.
    const CommandInfo* find(std::string_view name) const;

    // Every command, in the order added.
    std::vector<CommandInfo> all() const;

    // Runs a command by name with a count (0 for none), as keys do, in a mode.
    CommandResult run(std::string_view name, int count, Mode mode) const;

    // Runs a line from the : prompt: a command's name or alias, perhaps with a !, then its
    // arguments, or a count for a command that takes one.
    CommandResult runLine(std::string_view line, Mode mode = Mode::Normal) const;

private:
    struct Entry
    {
        CommandInfo info;
        CommandHandler handler;
    };

    const Entry* entry(std::string_view name) const;
    CommandResult call(const Entry& entry, const CommandCall& call, Mode mode) const;

    std::vector<Entry> entries_;
};

} // namespace modal::ui
