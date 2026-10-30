// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/ui/commands.hpp>
#include <modal/ui/keymap.hpp>
#include <modal/ui/settings.hpp>
#include <modal/ui/style.hpp>
#include <modal/ui/themes.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// A path from UTF-8 text, and back, the same on every system.
std::filesystem::path utf8Path(std::string_view text);
std::string utf8Text(const std::filesystem::path& path);

// What command files change.
struct Config
{
    Settings settings;
    Keymap keymap;
    Theme theme = *builtInTheme("dark");
};

// A command that couldn't be run, where it was.
struct ConfigError
{
    std::string source; // the file, as named when run
    int line = 0;       // counting from 1
    std::string message;

    // The error as "source:line: message".
    std::string text() const;

    friend bool operator==(const ConfigError&, const ConfigError&) = default;
};

// Runs command files, such as modalrc, as Vim runs a vimrc: a command a line, with blank
// lines and lines starting with " (comments) skipped. The commands are those that exist so
// far (milestone 3):
//
//   set NAME=VALUE...             sets settings (see Settings); set theme=NAME starts again
//                                 from the dark theme, then runs themes/NAME from the config
//                                 directory, or else the built-in theme of that name
//   highlight ROLE #RRGGBB        sets a role's colour (see roleName)
//   nmap KEYS COMMAND ["HINT"]    binds keys in Normal mode; vmap, imap, omap and cmap bind
//                                 them in Visual, Insert, operator-pending and command-line
//                                 mode; keys are in notation (see key_notation.hpp)
//   nunmap KEYS                   unbinds them, and so on for the other modes
//   source FILE                   runs another file, relative to the one running it
//
// Arguments are separated by spaces. As in a shell, text in double quotes keeps its spaces,
// with \" and \\ for a quote and a backslash, so set font="C:/My Fonts/Mono.ttf" works; but
// a map command's keys are taken as they are, since " is a key. A command that fails is
// reported with its file and line, and the rest still run.
class ConfigReader
{
public:
    // The commands keys can be bound to, and the directory themes are found in.
    ConfigReader(Config& config, const CommandRegistry& commands,
                 std::optional<std::filesystem::path> configDirectory = std::nullopt);

    // Runs a file's commands. If it can't be read, that's an error.
    void runFile(const std::filesystem::path& file);

    // Runs commands from text, as if from a file of that name in a directory, which
    // relative files are found from.
    void runText(std::string_view text, std::string_view source,
                 const std::filesystem::path& directory);

    // Runs one command, returning why not if it fails, or else nothing.
    std::string runCommand(std::string_view line, const std::filesystem::path& directory);

    // The errors so far, in the order they happened.
    const std::vector<ConfigError>& errors() const { return errors_; }

private:
    std::string map(std::string_view command, const std::vector<std::string>& arguments);
    std::string group(std::string_view command, const std::vector<std::string>& arguments);
    std::string source(const std::vector<std::string>& arguments,
                       const std::filesystem::path& directory);
    std::string useTheme(std::string name, const std::string& previous);

    Config& config_;
    const CommandRegistry& commands_;
    std::optional<std::filesystem::path> configDirectory_;
    std::vector<ConfigError> errors_;
    int depth_ = 0;       // of files sourcing others, which stops at a limit
    std::string section_; // keysection's, for the bindings after it in the file
};

// A command file of every setting, key binding and colour, with what each does, to start a
// modalrc from (decided in milestone 3): those changed from their defaults as commands,
// and the rest commented out at their defaults, so a user changes only what they want and
// keeps later versions' defaults for the rest. Settings are compared with their defaults,
// key bindings with the built-in modalrc's (changes listed as map and unmap commands), and
// colours with the chosen theme's. The commands keys can be bound to are listed too.
std::string configText(const Config& config, const CommandRegistry& commands,
                       const std::optional<std::filesystem::path>& configDirectory);

// The config text of a config at its defaults, with the built-in modalrc's bindings.
std::string defaultConfigText(const CommandRegistry& commands);

// Writes text to a file, making its directory if needed, unless the file exists and
// overwrite is false. Returns why not, or else nothing.
std::string writeConfigFile(const std::filesystem::path& file, std::string_view text,
                            bool overwrite);

// The commands a command file takes, described for the command registry, so the : prompt
// runs them too and they're listed with the rest. ConfigReader runs them itself.
std::span<const CommandInfo> configCommands();

// The built-in command file, assets/config/modalrc, which holds the default key bindings.
std::string_view builtInModalrc();

// Runs the command files a session starts with: the built-in modalrc, then modalrc in the
// config directory, if there's one.
void runStartupFiles(ConfigReader& reader,
                     const std::optional<std::filesystem::path>& configDirectory);

} // namespace modal::ui
