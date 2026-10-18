#include <modal/ui/config.hpp>
#include <modal/ui/key_notation.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <utility>
#include <variant>

namespace modal::ui
{

namespace
{

constexpr int kMaxDepth = 16; // files sourcing files, so a loop is reported, not followed

struct MapCommand
{
    char prefix;
    Mode mode;
};

constexpr std::array<MapCommand, 5> kMapModes{{
    {'n', Mode::Normal},
    {'v', Mode::Visual},
    {'i', Mode::Insert},
    {'o', Mode::OperatorPending},
    {'c', Mode::CommandLine},
}};

bool isSpace(char c)
{
    return c == ' ' || c == '\t';
}

// A line split into words at spaces, as a shell does: in double quotes, spaces are kept,
// and \" and \\ are a quote and a backslash, so set font="C:/My Fonts/Mono.ttf" is two
// words. The first few words are taken as they are, without quotes, for words such as
// key bindings' keys, where " is a key. Returns why not if a quote isn't closed.
std::variant<std::vector<std::string>, std::string> split(std::string_view line, int plainWords)
{
    std::vector<std::string> words;
    std::size_t i = 0;
    while (true)
    {
        while (i < line.size() && isSpace(line[i]))
        {
            ++i;
        }
        if (i == line.size())
        {
            return words;
        }
        const bool plain = static_cast<int>(words.size()) < plainWords;
        std::string word;
        bool quoted = false;
        while (i < line.size() && (quoted || !isSpace(line[i])))
        {
            const char c = line[i++];
            if (plain)
            {
                word += c;
            }
            else if (c == '"')
            {
                quoted = !quoted;
            }
            else if (quoted && c == '\\' && i < line.size() && (line[i] == '"' || line[i] == '\\'))
            {
                word += line[i++];
            }
            else
            {
                word += c;
            }
        }
        if (quoted)
        {
            return std::string{"no closing \" for a quoted argument"};
        }
        words.push_back(std::move(word));
    }
}

std::optional<std::string> readFile(const std::filesystem::path& file)
{
    std::ifstream in{file, std::ios::binary};
    if (!in)
    {
        return std::nullopt;
    }
    std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    if (text.starts_with("\xEF\xBB\xBF")) // a UTF-8 byte order mark, as some editors write
    {
        text.erase(0, 3);
    }
    return text;
}

} // namespace

std::filesystem::path utf8Path(std::string_view text)
{
    return std::filesystem::path{
        std::u8string{reinterpret_cast<const char8_t*>(text.data()), text.size()}};
}

std::string utf8Text(const std::filesystem::path& path)
{
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string ConfigError::text() const
{
    return source + ":" + std::to_string(line) + ": " + message;
}

ConfigReader::ConfigReader(Config& config, std::span<const std::string_view> commands)
    : config_(config)
    , commands_(commands)
{
}

void ConfigReader::runFile(const std::filesystem::path& file)
{
    const auto text = readFile(file);
    if (!text)
    {
        errors_.push_back({utf8Text(file), 0, "can't be read"});
        return;
    }
    runText(*text, utf8Text(file), file.parent_path());
}

void ConfigReader::runText(std::string_view text, std::string_view source,
                           const std::filesystem::path& directory)
{
    int number = 0;
    while (!text.empty())
    {
        ++number;
        const auto end = text.find('\n');
        auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.ends_with('\r'))
        {
            line.remove_suffix(1);
        }
        if (auto error = runCommand(line, directory); !error.empty())
        {
            errors_.push_back({std::string{source}, number, std::move(error)});
        }
    }
}

std::string ConfigReader::runCommand(std::string_view line, const std::filesystem::path& directory)
{
    const auto first = std::ranges::find_if_not(line, isSpace);
    if (first == line.end() || *first == '"')
    {
        return {}; // blank, or a comment
    }
    // A map command's keys are taken as they are, since " is a key.
    const auto name = line.substr(static_cast<std::size_t>(first - line.begin()));
    const auto commandName = name.substr(0, name.find_first_of(" 	"));
    const bool mapping = commandName.ends_with("map");
    auto split = ui::split(line, mapping ? 2 : 1);
    if (auto* error = std::get_if<std::string>(&split))
    {
        return std::move(*error);
    }
    auto& words = std::get<std::vector<std::string>>(split);
    const std::string command = words.front();
    const std::vector<std::string> arguments{words.begin() + 1, words.end()};

    if (command == "set")
    {
        if (arguments.empty())
        {
            return "set needs NAME=VALUE";
        }
        for (const auto& argument : arguments)
        {
            const auto equals = argument.find('=');
            if (equals == std::string::npos)
            {
                return "set needs NAME=VALUE, not " + argument;
            }
            if (auto error = config_.settings.set(std::string_view{argument}.substr(0, equals),
                                                  std::string_view{argument}.substr(equals + 1));
                !error.empty())
            {
                return error;
            }
        }
        return {};
    }
    if (command == "source")
    {
        return source(arguments, directory);
    }
    if (command.size() >= 4 && (command.substr(1) == "map" || command.substr(1) == "unmap"))
    {
        return map(command, arguments);
    }
    return "unknown command \"" + command + "\"";
}

std::string ConfigReader::map(std::string_view command, const std::vector<std::string>& arguments)
{
    const auto it = std::ranges::find(kMapModes, command.front(), &MapCommand::prefix);
    if (it == kMapModes.end())
    {
        return "unknown command \"" + std::string{command} + "\"";
    }
    const bool unmap = command.substr(1) == "unmap";
    const std::string usage =
        unmap ? std::string{command} + " needs KEYS"
              : std::string{command} + " needs KEYS COMMAND, and perhaps \"HINT\"";
    if (arguments.empty() || arguments.size() > (unmap ? 1u : 3u) ||
        (!unmap && arguments.size() < 2))
    {
        return usage;
    }
    auto keys = parseKeys(arguments[0]);
    if (auto* error = std::get_if<std::string>(&keys))
    {
        return std::move(*error);
    }
    auto& sequence = std::get<std::vector<Key>>(keys);
    if (unmap)
    {
        return config_.keymap.unbind(it->mode, sequence) ? std::string{}
                                                         : "no binding of " + arguments[0];
    }
    if (std::ranges::find(commands_, arguments[1]) == commands_.end())
    {
        return "no command \"" + arguments[1] + "\" to bind " + arguments[0] + " to";
    }
    config_.keymap.bind(it->mode, std::move(sequence),
                        {arguments[1], arguments.size() > 2 ? arguments[2] : std::string{}});
    return {};
}

std::string ConfigReader::source(const std::vector<std::string>& arguments,
                                 const std::filesystem::path& directory)
{
    if (arguments.size() != 1)
    {
        return "source needs FILE";
    }
    if (depth_ >= kMaxDepth)
    {
        return "files source each other more than " + std::to_string(kMaxDepth) + " deep";
    }
    const auto file = directory / utf8Path(arguments[0]);
    const auto text = readFile(file);
    if (!text)
    {
        return "can't read " + utf8Text(file);
    }
    ++depth_;
    runText(*text, utf8Text(file), file.parent_path());
    --depth_;
    return {};
}

} // namespace modal::ui
