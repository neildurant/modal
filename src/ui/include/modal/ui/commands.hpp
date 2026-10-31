// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/ui/keymap.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
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
// The kinds of things a command's arguments are, for completion at the : prompt; each has one
// source of its choices (decided October 2026).
enum class Choices : std::uint8_t
{
    None,
    Command,    // a command's name or alias
    Setting,    // a setting's name, or NAME=VALUE with its values
    Role,       // a theme role
    Port,       // an output port
    File,       // a file or folder, from the current directory
    ConfigFile, // a file or folder in the config directory
    Section,    // a section's name
    Meter,      // a time signature, such as 3/4
    Length,     // a note length, such as 1/8 or 1/16t
};
inline constexpr std::size_t kChoicesCount = static_cast<std::size_t>(Choices::Length) + 1;

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
    bool motion = false;           // moves the cursor, so an operator can take it (d]b)
    bool object = false;           // a text object, which only an operator takes (dib)
    bool range = false;            // at the : prompt from Visual mode, acts on the selection
    std::uint8_t where = 0;        // where it does something, as whereBit makes it; 0 everywhere
    // The kinds of its arguments, for completion: the first, second and third, the third for
    // any after it too, as set takes settings one after another.
    std::array<Choices, 3> choices = {};
};

// Where a command does something, for the hint menu and the palette to offer only what works
// there (decided October 2026): each view, the arrangement's section strip apart from its tracks.
enum class Where : std::uint8_t
{
    Tracks, // the arrangement's tracks
    Strip,  // its section strip
    PianoRoll,
    EventList,
};

constexpr std::uint8_t whereBit(Where where)
{
    return static_cast<std::uint8_t>(1U << static_cast<unsigned>(where));
}

// Places together, as a command's where.
constexpr std::uint8_t onlyIn(std::initializer_list<Where> places)
{
    std::uint8_t bits = 0;
    for (const auto place : places)
    {
        bits = static_cast<std::uint8_t>(bits | whereBit(place));
    }
    return bits;
}

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
    // Adds a command, whose name and aliases must be new; or, for a command described
    // without a handler, gives it one. So commands can be described before what runs them
    // exists, as when the config, which binds keys to them, is read before the views.
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
