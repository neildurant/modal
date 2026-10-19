#include <modal/ui/prompt.hpp>

#include <cstddef>
#include <vector>

namespace modal::ui
{

namespace
{

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

// The errors since some count of them, as a warning, or else nothing.
PromptResult errorsSince(const ConfigReader& reader, std::size_t before)
{
    const auto& errors = reader.errors();
    if (errors.size() <= before)
    {
        return {};
    }
    const auto& first = errors[before];
    auto message = utf8Text(utf8Path(first.source).filename()) + ":" + std::to_string(first.line) +
                   ": " + first.message;
    if (errors.size() - before > 1)
    {
        message += " (and " + std::to_string(errors.size() - before - 1) + " more)";
    }
    return {message, true, false};
}

} // namespace

PromptResult runPromptLine(ConfigReader& reader, const Config& config, std::string_view line,
                           const std::optional<std::filesystem::path>& configDirectory)
{
    const auto parts = words(line);
    if (parts.empty())
    {
        return {};
    }
    const auto command = parts.front();
    if (command == "q" || command == "quit")
    {
        return {"", false, true};
    }
    if (command == "set" && parts.size() > 1 && parts[1].ends_with('?'))
    {
        std::string shown;
        for (std::size_t i = 1; i < parts.size(); ++i)
        {
            auto name = parts[i];
            name.remove_suffix(name.ends_with('?') ? 1 : 0);
            const auto* setting = config.settings.find(name);
            if (setting == nullptr)
            {
                return {"unknown setting \"" + std::string{name} + "\"", true, false};
            }
            shown +=
                (shown.empty() ? "" : "  ") + std::string{name} + "=" + formatValue(setting->value);
        }
        return {shown, false, false};
    }
    const auto before = reader.errors().size();
    if (command == "source" && parts.size() == 1)
    {
        if (!configDirectory || !std::filesystem::is_regular_file(*configDirectory / "modalrc"))
        {
            return {"there's no modalrc" +
                        (configDirectory ? " in " + utf8Text(*configDirectory) : std::string{}),
                    true, false};
        }
        reader.runFile(*configDirectory / "modalrc");
        auto result = errorsSince(reader, before);
        return result.warning ? result : PromptResult{"ran modalrc again", false, false};
    }
    if (auto error = reader.runCommand(line, configDirectory.value_or(std::filesystem::path{}));
        !error.empty())
    {
        return {std::move(error), true, false};
    }
    return errorsSince(reader, before); // from a file it ran
}

} // namespace modal::ui
