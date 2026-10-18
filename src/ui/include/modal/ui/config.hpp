#pragma once

#include <modal/ui/keymap.hpp>
#include <modal/ui/settings.hpp>

#include <filesystem>
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
//   set NAME=VALUE...             sets settings (see Settings)
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
    // The commands keys can be bound to, by name.
    ConfigReader(Config& config, std::span<const std::string_view> commands);

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
    std::string source(const std::vector<std::string>& arguments,
                       const std::filesystem::path& directory);

    Config& config_;
    std::span<const std::string_view> commands_;
    std::vector<ConfigError> errors_;
    int depth_ = 0; // of files sourcing others, which stops at a limit
};

} // namespace modal::ui
