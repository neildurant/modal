// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/base16.hpp>
#include <modal/ui/prompt.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace modal::ui
{

namespace
{

constexpr std::array<CommandInfo, 2> kPromptCommands{{
    {.name = "write-config",
     .description = "Write the config to a file, to start a modalrc from; ! overwrites it; "
                    "with no file, choose one",
     .usage = "[FILE]",
     .bang = true,
     .keys = false,
     .choices = {Choices::ConfigFile}},
    {.name = "theme-import",
     .description = "Make a base16 or base24 scheme a theme of your own, themes/NAME in the "
                    "config directory, to change as you like, and use it; ! overwrites one",
     .usage = "FILE [NAME]",
     .bang = true,
     .keys = false,
     .choices = {Choices::ConfigFile, Choices::None}},
}};

// A theme's name from a scheme's file: its name without .yaml, spaces made hyphens, so set
// theme= takes it as one word.
std::string themeNameOf(const std::filesystem::path& file)
{
    auto name = utf8Text(file.stem());
    std::ranges::replace(name, ' ', '-');
    return name;
}

std::vector<std::string_view> words(std::string_view line)
{
    std::vector<std::string_view> result;
    std::size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t'))
        {
            ++i;
        }
        const auto start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t')
        {
            ++i;
        }
        if (i > start)
        {
            result.push_back(line.substr(start, i - start));
        }
    }
    return result;
}

// The errors since some count of them, as a warning: the first, and how many more.
CommandResult errorsSince(const ConfigReader& reader, std::size_t before)
{
    const auto& errors = reader.errors();
    if (errors.size() <= before)
    {
        return {true};
    }
    const auto& first = errors[before];
    auto message = utf8Text(utf8Path(first.source).filename()) + ":" + std::to_string(first.line) +
                   ": " + first.message;
    if (errors.size() - before > 1)
    {
        message += " (and " + std::to_string(errors.size() - before - 1) + " more)";
    }
    return CommandResult::error(std::move(message));
}

// set NAME?...: the settings' values, as in Vim.
CommandResult showSettings(const Config& config, std::span<const std::string_view> names)
{
    std::string shown;
    for (auto name : names)
    {
        name.remove_suffix(name.ends_with('?') ? 1 : 0);
        const auto* setting = config.settings.find(name);
        if (setting == nullptr)
        {
            return CommandResult::error("unknown setting \"" + std::string{name} + "\"");
        }
        shown +=
            (shown.empty() ? "" : "  ") + std::string{name} + "=" + formatValue(setting->value);
    }
    return {true, shown, false};
}

} // namespace

std::span<const CommandInfo> promptCommands()
{
    return kPromptCommands;
}

void addPromptCommands(CommandRegistry& registry, ConfigReader& reader, const Config& config,
                       std::optional<std::filesystem::path> configDirectory, FileHost* host,
                       std::optional<std::filesystem::path> home)
{
    const auto directory = configDirectory.value_or(std::filesystem::path{});

    // The config commands, run by the reader as in a command file, but for set NAME? and
    // source with no file.
    for (const auto& info : configCommands())
    {
        registry.add(info,
                     [&reader, &config, configDirectory, directory,
                      name = info.name](const CommandCall& call) -> CommandResult
                     {
                         const auto arguments = words(call.arguments);
                         if (name == "set" && !arguments.empty() && arguments[0].ends_with('?'))
                         {
                             return showSettings(config, arguments);
                         }
                         const auto before = reader.errors().size();
                         if (name == "source" && arguments.empty())
                         {
                             if (!configDirectory ||
                                 !std::filesystem::is_regular_file(*configDirectory / "modalrc"))
                             {
                                 return CommandResult::error(
                                     "there's no modalrc" +
                                     (configDirectory ? " in " + utf8Text(*configDirectory)
                                                      : std::string{}));
                             }
                             reader.runFile(*configDirectory / "modalrc");
                             auto result = errorsSince(reader, before);
                             return result.warning
                                        ? result
                                        : CommandResult{true, "ran modalrc again", false};
                         }
                         if (auto error = reader.runCommand(call.line, directory); !error.empty())
                         {
                             return CommandResult::error(std::move(error));
                         }
                         return errorsSince(reader, before); // from a file it ran
                     });
    }

    const auto write =
        [&registry, &config, configDirectory](const std::filesystem::path& file, bool overwrite)
    {
        if (auto error =
                writeConfigFile(file, configText(config, registry, configDirectory), overwrite);
            !error.empty())
        {
            return CommandResult::error(error + (overwrite ? "" : "; write-config! overwrites it"));
        }
        return CommandResult{true, "wrote " + utf8Text(file), false};
    };
    registry.add(kPromptCommands[0],
                 [write, directory, host, home](const CommandCall& call)
                 {
                     // The rest of the line, so a path in quotes can have spaces.
                     auto text = std::string_view{call.arguments};
                     const auto first = text.find_first_not_of(" \t");
                     text = first == std::string_view::npos
                                ? std::string_view{}
                                : text.substr(first, text.find_last_not_of(" \t") - first + 1);
                     if (text.empty())
                     {
                         if (host == nullptr)
                         {
                             return CommandResult::error("write-config needs FILE");
                         }
                         host->chooseSave(FileHost::FileKind::Config, directory / "modalrc",
                                          [write, host](std::optional<std::filesystem::path> file)
                                          {
                                              if (file)
                                              {
                                                  host->report(write(*file, true));
                                              }
                                          });
                         return CommandResult{};
                     }
                     // The ! meant to overwrite, put after the file rather than the command.
                     if (text.ends_with('!'))
                     {
                         return CommandResult::error("! goes after the command: write-config! " +
                                                     std::string{text.substr(0, text.size() - 1)} +
                                                     " overwrites it");
                     }
                     return write(typedPath(text, directory, home), call.bang);
                 });

    // theme-import FILE [NAME] (decided 24 November 2026): a scheme written out as a theme,
    // a highlight line for each role, which is then the theme.
    registry.add(
        kPromptCommands[1],
        [&reader, configDirectory, directory, home](const CommandCall& call) -> CommandResult
        {
            // FILE in quotes if it has spaces, then perhaps NAME.
            std::vector<std::string_view> arguments;
            std::string_view rest = call.arguments;
            rest.remove_prefix(std::min(rest.find_first_not_of(" \t"), rest.size()));
            if (rest.starts_with('"'))
            {
                const auto close = rest.find('"', 1);
                const auto end = close == std::string_view::npos ? rest.size() : close + 1;
                arguments.push_back(rest.substr(0, end));
                for (const auto word : words(rest.substr(end)))
                {
                    arguments.push_back(word);
                }
            }
            else
            {
                arguments = words(rest);
            }
            if (arguments.empty() || arguments.size() > 2)
            {
                return CommandResult::error("theme-import needs FILE, and a NAME if you like");
            }
            if (!configDirectory)
            {
                return CommandResult::error("there's no config directory to keep themes in");
            }
            // A relative FILE from the config directory, as write-config's and source's.
            const auto file = typedPath(arguments[0], directory, home);
            std::ifstream in{file, std::ios::binary};
            if (!in)
            {
                return CommandResult::error("can't read " + utf8Text(file));
            }
            const std::string text{std::istreambuf_iterator<char>{in},
                                   std::istreambuf_iterator<char>{}};
            const auto read = readBase16(text);
            if (!read.scheme)
            {
                return CommandResult::error(utf8Text(file.filename()) + ": " + read.error);
            }
            const auto name = arguments.size() > 1 ? std::string{arguments[1]} : themeNameOf(file);
            if (name.empty() || name.find_first_of("/\\ \"") != std::string::npos)
            {
                return CommandResult::error(
                    "a theme's name is one word, without slashes or quotes");
            }
            const auto theme = *configDirectory / "themes" / utf8Path(name);
            if (auto error = writeConfigFile(theme, base16ThemeText(*read.scheme), call.bang);
                !error.empty())
            {
                return CommandResult::error(error +
                                            (call.bang ? "" : "; theme-import! overwrites it"));
            }
            if (auto error = reader.runCommand("set theme=" + name, directory); !error.empty())
            {
                return CommandResult::error(std::move(error));
            }
            return {true,
                    "imported " + (read.scheme->name.empty() ? name : read.scheme->name) +
                        " as themes/" + name + ", the theme now",
                    false};
        });
}

} // namespace modal::ui
