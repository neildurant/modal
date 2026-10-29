// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/config.hpp>
#include <modal/ui/key_notation.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>
#include <utility>
#include <variant>

namespace modal::ui
{

// Built in from assets/config by modal_embed_file.
std::span<const unsigned char> builtInModalrcBytes();

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

ConfigReader::ConfigReader(Config& config, const CommandRegistry& commands,
                           std::optional<std::filesystem::path> configDirectory)
    : config_(config)
    , commands_(commands)
    , configDirectory_(std::move(configDirectory))
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
    const auto rest = line.substr(static_cast<std::size_t>(first - line.begin()));
    const auto commandName = rest.substr(0, rest.find_first_of(" \t"));
    const bool mapping = commandName.ends_with("map") || commandName.ends_with("group");
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
                // An on-or-off setting: rewind, norewind or rewind!.
                if (auto error = config_.settings.setFlag(argument); !error.empty())
                {
                    return error;
                }
                continue;
            }
            const auto name = std::string_view{argument}.substr(0, equals);
            const auto previous = name == "theme" ? config_.settings.text("theme") : std::string{};
            if (auto error =
                    config_.settings.set(name, std::string_view{argument}.substr(equals + 1));
                !error.empty())
            {
                return error;
            }
            if (name == "theme")
            {
                if (auto error = useTheme(config_.settings.text("theme"), previous); !error.empty())
                {
                    return error;
                }
            }
        }
        return {};
    }
    if (command == "source")
    {
        return source(arguments, directory);
    }
    if (command == "highlight")
    {
        return highlight(config_.theme, arguments);
    }
    if (command.size() >= 4 && (command.substr(1) == "map" || command.substr(1) == "unmap"))
    {
        return map(command, arguments);
    }
    if (command.size() == 6 && command.substr(1) == "group")
    {
        return group(command, arguments);
    }
    if (commands_.find(command) != nullptr)
    {
        return command + " can't be used in a config file";
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
    const auto* target = commands_.find(arguments[1]);
    if (target == nullptr)
    {
        return "no command \"" + arguments[1] + "\" to bind " + arguments[0] + " to";
    }
    if (!target->keys)
    {
        return arguments[1] + " can't be bound to keys";
    }
    config_.keymap.bind(it->mode, std::move(sequence),
                        {arguments[1], arguments.size() > 2 ? arguments[2] : std::string{}});
    return {};
}

std::string ConfigReader::group(std::string_view command, const std::vector<std::string>& arguments)
{
    const auto it = std::ranges::find(kMapModes, command.front(), &MapCommand::prefix);
    if (it == kMapModes.end())
    {
        return "unknown command \"" + std::string{command} + "\"";
    }
    if (arguments.size() != 2)
    {
        return std::string{command} + " needs KEYS \"TITLE\"";
    }
    auto keys = parseKeys(arguments[0]);
    if (auto* error = std::get_if<std::string>(&keys))
    {
        return std::move(*error);
    }
    config_.keymap.group(it->mode, std::move(std::get<std::vector<Key>>(keys)), arguments[1]);
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

std::string ConfigReader::useTheme(std::string name, const std::string& previous)
{
    if (configDirectory_)
    {
        const auto file = *configDirectory_ / "themes" / utf8Path(name);
        if (std::filesystem::is_regular_file(file))
        {
            config_.theme = *builtInTheme("dark");
            runFile(file);
            return {};
        }
    }
    if (const auto theme = builtInTheme(name))
    {
        config_.theme = *theme;
        return {};
    }
    config_.settings.set("theme", previous);
    return "no theme \"" + name + "\"" +
           (configDirectory_ ? " in " + utf8Text(*configDirectory_ / "themes") : std::string{}) +
           ", nor built in";
}

namespace
{

constexpr CommandInfo configCommand(std::string_view name, std::string_view description,
                                    std::string_view usage)
{
    return {
        .name = name, .description = description, .usage = usage, .config = true, .keys = false};
}

constexpr std::string_view kMapUsage = "KEYS COMMAND [\"HINT\"]";

constexpr std::array kConfigCommands{
    configCommand("set", "Set settings; set NAME? shows one", "NAME=VALUE..."),
    configCommand("highlight", "Set a theme role's colour", "ROLE #rrggbb"),
    configCommand("nmap", "Bind keys to a command in Normal mode", kMapUsage),
    configCommand("vmap", "Bind keys to a command in Visual mode", kMapUsage),
    configCommand("imap", "Bind keys to a command in Insert mode", kMapUsage),
    configCommand("omap", "Bind keys to a command in operator-pending mode", kMapUsage),
    configCommand("cmap", "Bind keys to a command in command-line mode", kMapUsage),
    configCommand("ngroup",
                  "Give keys that start longer bindings a title in the hint menu, in "
                  "Normal mode",
                  "KEYS \"TITLE\""),
    configCommand("vgroup", "Give keys a title in the hint menu, in Visual mode", "KEYS \"TITLE\""),
    configCommand("igroup", "Give keys a title in the hint menu, in Insert mode", "KEYS \"TITLE\""),
    configCommand("ogroup", "Give keys a title in the hint menu, in operator-pending mode",
                  "KEYS \"TITLE\""),
    configCommand("cgroup", "Give keys a title in the hint menu, in command-line mode",
                  "KEYS \"TITLE\""),
    configCommand("nunmap", "Unbind keys in Normal mode", "KEYS"),
    configCommand("vunmap", "Unbind keys in Visual mode", "KEYS"),
    configCommand("iunmap", "Unbind keys in Insert mode", "KEYS"),
    configCommand("ounmap", "Unbind keys in operator-pending mode", "KEYS"),
    configCommand("cunmap", "Unbind keys in command-line mode", "KEYS"),
    configCommand("source", "Run a command file; at the : prompt, modalrc again if none", "FILE"),
};

} // namespace

std::span<const CommandInfo> configCommands()
{
    return kConfigCommands;
}

std::string_view builtInModalrc()
{
    const auto bytes = builtInModalrcBytes();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

void runStartupFiles(ConfigReader& reader,
                     const std::optional<std::filesystem::path>& configDirectory)
{
    reader.runText(builtInModalrc(), "built-in modalrc",
                   configDirectory.value_or(std::filesystem::path{}));
    if (configDirectory && std::filesystem::is_regular_file(*configDirectory / "modalrc"))
    {
        reader.runFile(*configDirectory / "modalrc");
    }
}

namespace
{

// Text as comment lines, wrapped at spaces to fit kCommentWidth characters.
constexpr std::size_t kCommentWidth = 90;

std::string wrappedComment(std::string_view text)
{
    std::string out;
    std::string line = "\"";
    while (!text.empty())
    {
        const auto end = text.find(' ');
        const auto word = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (line.size() > 1 && line.size() + 1 + word.size() > kCommentWidth)
        {
            out += line + "\n";
            line = "\"";
        }
        line += " ";
        line += word;
    }
    return out + line + "\n";
}

// A built-in file's lines after its opening comment, with commands commented out.
std::string commentedOut(std::string_view text)
{
    std::string out;
    bool opening = true; // the comment the file starts with, up to a blank line
    while (!text.empty())
    {
        const auto end = text.find('\n');
        const auto line = text.substr(0, end);
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        if (opening)
        {
            opening = !line.empty();
            continue;
        }
        if (!line.empty() && !line.starts_with('"'))
        {
            out += '"';
        }
        out += line;
        out += '\n';
    }
    return out;
}

} // namespace

namespace
{

// A value or hint in double quotes, if it needs them, with \" and \\ inside.
std::string quotedIfNeeded(std::string_view text, bool always)
{
    if (!always && !text.empty() && text.find_first_of(" \t\"\\") == std::string_view::npos)
    {
        return std::string{text};
    }
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

// The lines that change a default keymap into another: map commands for bindings added or
// changed, unmap commands for those removed.
std::string keymapChanges(const Keymap& keymap, const Keymap& defaults)
{
    std::string text;
    for (const auto& [prefix, mode] : kMapModes)
    {
        const auto current = keymap.bindings(mode);
        const auto before = defaults.bindings(mode);
        for (const auto& binding : current)
        {
            if (std::ranges::find(before, binding) == before.end())
            {
                text += std::string(1, prefix) + "map " + formatKeys(binding.first) + " " +
                        binding.second.command;
                if (!binding.second.hint.empty())
                {
                    text += " " + quotedIfNeeded(binding.second.hint, true);
                }
                text += "\n";
            }
        }
        for (const auto& binding : before)
        {
            if (std::ranges::find(current, binding.first,
                                  &std::pair<std::vector<Key>, Binding>::first) == current.end())
            {
                text += std::string(1, prefix) + "unmap " + formatKeys(binding.first) + "\n";
            }
        }
        const auto titles = defaults.groups(mode);
        for (const auto& [keys, title] : keymap.groups(mode))
        {
            if (std::ranges::find(titles, std::pair{keys, title}) == titles.end())
            {
                text += std::string(1, prefix) + "group " + formatKeys(keys) + " " +
                        quotedIfNeeded(title, true) + "\n";
            }
        }
    }
    return text;
}

} // namespace

std::string configText(const Config& config, const CommandRegistry& commands,
                       const std::optional<std::filesystem::path>& configDirectory)
{
    std::string text =
        "\" Modal's configuration: every setting, key binding and colour. Those changed from\n"
        "\" their defaults are commands; the rest are commented out at their defaults, to\n"
        "\" uncomment and change as you want, and otherwise follow later versions' defaults.\n"
        "\" Put it in modalrc in the config directory.\n"
        "\"\n"
        "\" A line starting with \" is a comment. Keys are written as in Vim: <C-d>, <Esc>,\n"
        "\" <S-Tab>, <lt> for <, <Space>.\n\n"
        "\" Settings\n\n";
    for (const auto& setting : config.settings.all())
    {
        text += wrappedComment(setting.description);
        const bool changed = setting.value != setting.defaultValue;
        text += std::string{changed ? "" : "\""} + "set " + std::string{setting.name} + "=" +
                quotedIfNeeded(formatValue(setting.value), false) + "\n\n";
    }

    text +=
        "\" Key bindings: nmap KEYS COMMAND \"HINT\" binds keys in Normal mode, and nunmap KEYS\n"
        "\" unbinds them; vmap, imap, omap and cmap are for the other modes. <leader> is the key\n"
        "\" the leader setting names. ngroup KEYS \"TITLE\" gives keys that start longer ones a\n"
        "\" title in the hint menu. The built-in ones:\n\n";
    text += commentedOut(builtInModalrc());
    Config defaults;
    ConfigReader reader{defaults, commands};
    reader.runText(builtInModalrc(), "built-in modalrc", {});
    if (const auto changes = keymapChanges(config.keymap, defaults.keymap); !changes.empty())
    {
        text += "\n\" Changes to the built-in key bindings\n" + changes;
    }
    text += "\n\" The commands keys can be bound to:\n\"\n";
    std::vector<CommandInfo> bindable;
    std::ranges::copy_if(commands.all(), std::back_inserter(bindable),
                         [](const CommandInfo& command) { return command.keys; });
    std::size_t width = 0;
    for (const auto& command : bindable)
    {
        width = std::max(width, command.name.size());
    }
    for (const auto& command : bindable)
    {
        text += "\"   " + std::string{command.name} +
                std::string(width - command.name.size() + 2, ' ') +
                std::string{command.description} + "\n";
    }

    // The colours of the theme chosen, to compare the config's with.
    const auto& themeName = config.settings.text("theme");
    Config base;
    ConfigReader themeReader{base, commands, configDirectory};
    themeReader.runCommand("set theme=" + quotedIfNeeded(themeName, false), {});
    text += "\n\" Colours: highlight ROLE #rrggbb sets a role's colour, after the theme's. The " +
            base.settings.text("theme") + " theme's:\n\n";
    std::string changes;
    for (std::size_t i = 0; i < kRoleCount; ++i)
    {
        const auto role = static_cast<Role>(i);
        const auto name = std::string{roleName(role)};
        const auto padding = std::string(name.size() < 12 ? 12 - name.size() : 1, ' ');
        text += "\"highlight " + name + padding + formatColour(base.theme[role]) + "\n";
        if (config.theme[role] != base.theme[role])
        {
            changes += "highlight " + name + padding + formatColour(config.theme[role]) + "\n";
        }
    }
    if (!changes.empty())
    {
        text += "\n\" Changes to the theme's colours\n" + changes;
    }
    return text;
}

std::string defaultConfigText(const CommandRegistry& commands)
{
    Config defaults;
    ConfigReader reader{defaults, commands};
    reader.runText(builtInModalrc(), "built-in modalrc", {});
    return configText(defaults, commands, std::nullopt);
}

std::string writeConfigFile(const std::filesystem::path& file, std::string_view text,
                            bool overwrite)
{
    std::error_code ignored;
    if (!overwrite && std::filesystem::exists(file, ignored))
    {
        return utf8Text(file) + " exists already";
    }
    if (file.has_parent_path())
    {
        std::filesystem::create_directories(file.parent_path(), ignored);
    }
    std::ofstream out{file, std::ios::binary};
    out << text;
    if (!out.flush())
    {
        return "can't write " + utf8Text(file);
    }
    return {};
}

} // namespace modal::ui
