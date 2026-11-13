// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/prompt.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace modal::ui
{

namespace
{

constexpr std::array<CommandInfo, 1> kPromptCommands{{
    {.name = "write-config",
     .description = "Write the config to a file, to start a modalrc from; ! overwrites it; "
                    "with no file, choose one",
     .usage = "[FILE]",
     .bang = true,
     .keys = false,
     .choices = {Choices::ConfigFile}},
}};

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
}

} // namespace modal::ui
