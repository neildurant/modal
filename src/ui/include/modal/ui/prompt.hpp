#pragma once

#include <modal/ui/config.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace modal::ui
{

// What a line run at the : prompt did: a message to show, and whether it's a warning, and
// whether to quit.
struct PromptResult
{
    std::string message;
    bool warning = false;
    bool quit = false;

    friend bool operator==(const PromptResult&, const PromptResult&) = default;
};

// Runs a line from the : prompt: any command a command file takes (see ConfigReader), and
//
//   set NAME?...        shows settings' values, as in Vim
//   source              with no file, runs modalrc in the config directory again
//   q, quit             quits
//
// Errors, including those in files a command runs, come back as a warning: the first, and
// how many more.
PromptResult runPromptLine(ConfigReader& reader, const Config& config, std::string_view line,
                           const std::optional<std::filesystem::path>& configDirectory);

} // namespace modal::ui
