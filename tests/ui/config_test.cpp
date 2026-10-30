// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/config.hpp>
#include <modal/ui/event_list_view.hpp>
#include <modal/ui/style.hpp>
#include <modal/ui/themes.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace modal::ui;

namespace
{

constexpr std::array<CommandInfo, 3> kCommands{{
    {"cursor-down", "Down"},
    {"go-first", "First"},
    {"cancel", "Cancel"},
}};

// The commands, described, as the config sees them.
CommandRegistry registryOf(std::span<const CommandInfo> infos)
{
    CommandRegistry registry;
    registry.add(infos);
    return registry;
}

const CommandRegistry kRegistry = registryOf(kCommands);

// A directory of its own for a test's files, removed afterwards.
class TempDirectory
{
public:
    explicit TempDirectory(std::string_view name)
        : path_(std::filesystem::temp_directory_path() / utf8Path(name))
    {
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory() { std::filesystem::remove_all(path_); }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    const std::filesystem::path& path() const { return path_; }

    std::filesystem::path write(std::string_view name, std::string_view text) const
    {
        const auto file = path_ / utf8Path(name);
        std::filesystem::create_directories(file.parent_path());
        std::ofstream{file, std::ios::binary} << text;
        return file;
    }

private:
    std::filesystem::path path_;
};

const Binding* binding(const Config& config, Mode mode, std::vector<Key> keys)
{
    return config.keymap.match(mode, keys).binding;
}

} // namespace

TEST_CASE("command files set settings and bind keys, skipping blank lines and comments")
{
    Config config;
    ConfigReader reader{config, kRegistry};
    reader.runText(R"(" A comment, then a blank line

set timeoutlen=300
  set fontsize=14 minsize=100x30
set norewind port=Pro-800
nmap J cursor-down
nmap <C-g> go-first "Top of the song"
vmap j cursor-down
)",
                   "modalrc", {});
    CHECK(reader.errors().empty());
    CHECK(config.settings.integer("timeoutlen") == 300);
    CHECK(config.settings.number("fontsize") == 14.0);
    CHECK(config.settings.cells("minsize") == CellCount{100, 30});
    CHECK_FALSE(config.settings.flag("rewind"));
    CHECK(config.settings.text("port") == "Pro-800");
    CHECK(*binding(config, Mode::Normal, {Key::typed(U'J')}) == Binding{"cursor-down", ""});
    CHECK(*binding(config, Mode::Normal, {Key::typed(U'g', kCtrl)}) ==
          Binding{"go-first", "Top of the song"});
    CHECK(binding(config, Mode::Visual, {Key::typed(U'j')}) != nullptr);
    CHECK(binding(config, Mode::Normal, {Key::typed(U'j')}) == nullptr);
}

TEST_CASE("map commands unbind keys too")
{
    Config config;
    ConfigReader reader{config, kRegistry};
    reader.runText("nmap J cursor-down\nnunmap J\nnunmap J\n", "modalrc", {});
    CHECK(binding(config, Mode::Normal, {Key::typed(U'J')}) == nullptr);
    CHECK(reader.errors() == std::vector<ConfigError>{{"modalrc", 3, "no binding of J"}});
}

TEST_CASE("group commands give keys that start longer bindings a title")
{
    Config config;
    ConfigReader reader{config, kRegistry};
    reader.runText("ngroup <leader>t \"Toggles\"\nngroup g\nngroup <Nope> x\n", "modalrc", {});
    CHECK(config.keymap.groupTitle(
              Mode::Normal, std::vector{Key::of(NamedKey::Leader), Key::typed(U't')}) == "Toggles");
    REQUIRE(reader.errors().size() == 2);
    CHECK(reader.errors()[0].message == "ngroup needs KEYS \"TITLE\"");
}

TEST_CASE("a command that fails is reported with its line, and the rest still run")
{
    Config config;
    ConfigReader reader{config, kRegistry};
    reader.runText("set fontsiz=14\r\n"
                   "set timeoutlen=lots\r\n"
                   "set fontsize\r\n"
                   "nmap J\r\n"
                   "nmap J nothing\r\n"
                   "nmap <Nope> cancel\r\n"
                   "xmap J cancel\r\n"
                   "frobnicate\r\n"
                   "nmap J cancel \"unclosed\r\n"
                   "set timeoutlen=250\r\n",
                   "modalrc", {});
    const std::vector<ConfigError> expected{
        {"modalrc", 1, "unknown setting \"fontsiz\""},
        {"modalrc", 2, "timeoutlen must be a whole number from 0 to 10000"},
        {"modalrc", 3, "set needs NAME=VALUE, not fontsize"},
        {"modalrc", 4, "nmap needs KEYS COMMAND, and perhaps \"HINT\""},
        {"modalrc", 5, "no command \"nothing\" to bind J to"},
        {"modalrc", 6, "unknown key <Nope>"},
        {"modalrc", 7, "unknown command \"xmap\""},
        {"modalrc", 8, "unknown command \"frobnicate\""},
        {"modalrc", 9, "no closing \" for a quoted argument"},
    };
    CHECK(reader.errors() == expected);
    CHECK(config.settings.integer("timeoutlen") == 250);
    CHECK(expected[1].text() == "modalrc:2: timeoutlen must be a whole number from 0 to 10000");
}

TEST_CASE("quotes keep spaces in arguments, as in a shell, but not in keys")
{
    Config config;
    ConfigReader reader{config, kRegistry};
    CHECK(reader.runCommand(R"(nmap J cursor-down "Say \"down\" \\ go")", {}).empty());
    CHECK(binding(config, Mode::Normal, {Key::typed(U'J')})->hint == R"(Say "down" \ go)");
    CHECK(reader.runCommand(R"(set font="C:/My Fonts/Mono.ttf")", {}).empty());
    CHECK(config.settings.text("font") == "C:/My Fonts/Mono.ttf");

    // A map's keys are as they are: " is a key, as Vim's register key is.
    CHECK(reader.runCommand(R"(nmap "a cancel "Cancel")", {}).empty());
    CHECK(*binding(config, Mode::Normal, {Key::typed(U'"'), Key::typed(U'a')}) ==
          Binding{"cancel", "Cancel"});
}

TEST_CASE("files run other files, relative to themselves, in UTF-8 names")
{
    const TempDirectory directory{"modal-config-test"};
    const auto modalrc = directory.write("modalrc", "\xEF\xBB\xBF" // a byte order mark
                                                    "source more/keys\n"
                                                    "source missing\n");
    directory.write("more/keys", "nmap J cursor-down\nsource ../thème\n");
    directory.write("thème", "set timeoutlen=123\n");

    Config config;
    ConfigReader reader{config, kRegistry};
    reader.runFile(modalrc);
    CHECK(binding(config, Mode::Normal, {Key::typed(U'J')}) != nullptr);
    CHECK(config.settings.integer("timeoutlen") == 123);
    REQUIRE(reader.errors().size() == 1);
    CHECK(reader.errors()[0].line == 2);
    CHECK(reader.errors()[0].message.starts_with("can't read "));

    reader.runFile(directory.path() / "nothing here");
    CHECK(reader.errors().back().message == "can't be read");
}

TEST_CASE("files that source each other stop, and say so")
{
    const TempDirectory directory{"modal-config-loop-test"};
    const auto file = directory.write("loop", "source loop\n");
    Config config;
    ConfigReader reader{config, kRegistry};
    reader.runFile(file);
    REQUIRE_FALSE(reader.errors().empty());
    CHECK(reader.errors().front().message == "files source each other more than 16 deep");
}

TEST_CASE("highlight sets a role's colour, and themes are command files of highlights")
{
    Config config;
    ConfigReader reader{config, kRegistry};
    CHECK(config.theme[Role::Background] == Colour{0x1a, 0x1b, 0x26}); // dark, to begin with
    CHECK(reader.runCommand("highlight cursor-text #FFEEDD", {}).empty());
    CHECK(config.theme[Role::CursorText] == Colour{0xff, 0xee, 0xdd});
    CHECK(reader.runCommand("highlight cursor", {}) == "highlight needs ROLE #RRGGBB");
    CHECK(reader.runCommand("highlight curser #000000", {}) == "unknown role \"curser\"");
    CHECK(reader.runCommand("highlight cursor black", {}) ==
          "a colour is written #rrggbb, not black");

    CHECK(reader.runCommand("set theme=light", {}).empty());
    CHECK(config.theme[Role::Background] == Colour{0xe1, 0xe2, 0xe7});
    CHECK(config.theme[Role::CursorText] == Colour{0xe1, 0xe2, 0xe7}); // the override's gone
}

TEST_CASE("a theme in the config directory comes before a built-in one, from dark")
{
    const TempDirectory directory{"modal-theme-test"};
    directory.write("themes/light", "highlight background #000001\n");
    directory.write("themes/mine", "highlight text #000002\nhighlight nope #000000\n");
    Config config;
    ConfigReader reader{config, kRegistry, directory.path()};

    CHECK(reader.runCommand("set theme=light", {}).empty());
    CHECK(config.theme[Role::Background] == Colour{0, 0, 1});
    CHECK(config.theme[Role::Text] == builtInTheme("dark")->operator[](Role::Text));

    CHECK(reader.runCommand("set theme=mine", {}).empty());
    CHECK(config.theme[Role::Text] == Colour{0, 0, 2});
    CHECK(config.theme[Role::Background] == Colour{0x1a, 0x1b, 0x26});
    REQUIRE(reader.errors().size() == 1); // from the theme's own file
    CHECK(reader.errors()[0].line == 2);
    CHECK(reader.errors()[0].source.ends_with("mine"));

    const auto before = config.theme;
    const auto error = reader.runCommand("set theme=nowhere", {});
    CAPTURE(error);
    CHECK(error.starts_with("no theme \"nowhere\" in "));
    CHECK(config.settings.text("theme") == "mine"); // as it was
    CHECK(config.theme == before);
}

TEST_CASE("the built-in themes give every role a colour of its own reading")
{
    for (const auto name : builtInThemeNames())
    {
        CAPTURE(name);
        const auto text = builtInThemeText(name);
        REQUIRE(text);
        // Every role is named once, and reading it with ConfigReader agrees.
        for (std::size_t i = 0; i < kRoleCount; ++i)
        {
            const auto role = std::string{roleName(static_cast<Role>(i))};
            CHECK(text->find("highlight " + role + " ") != std::string_view::npos);
        }
        Config config;
        ConfigReader reader{config, kRegistry};
        reader.runText(*text, name, {});
        CHECK(reader.errors().empty());
        CHECK(config.theme == *builtInTheme(name));
    }
    CHECK_FALSE(builtInTheme("nope"));
}

TEST_CASE("roles and colours read back as they're written")
{
    for (std::size_t i = 0; i < kRoleCount; ++i)
    {
        const auto role = static_cast<Role>(i);
        CHECK(roleFromName(roleName(role)) == role);
    }
    CHECK_FALSE(roleFromName("Cursor"));
    CHECK(formatColour({0x1a, 0xb0, 0xff}) == "#1ab0ff");
    CHECK(parseColour("#1AB0ff") == Colour{0x1a, 0xb0, 0xff});
    CHECK_FALSE(parseColour("1ab0ff"));
    CHECK_FALSE(parseColour("#1ab0f"));
    CHECK_FALSE(parseColour("#1ab0fg"));
}

TEST_CASE("the default config changes nothing, and uncommented gives the defaults")
{
    // The commands of the built-in modalrc: the event list's and the window's.
    CommandRegistry commands;
    commands.add(Workspace::commands());
    commands.add(windowCommands());
    const auto text = defaultConfigText(commands);
    CHECK(text.find("\"set timeoutlen=500\n") != std::string::npos);
    CHECK(text.find("\"set font=\"\"\n") != std::string::npos);
    CHECK(text.find("\"nmap gg") != std::string::npos);
    CHECK(text.find("\"highlight cursor-text") != std::string::npos);
    // Each command on a line of its own, with its description in a column after the names.
    const auto listed = text.find("\"   scroll-half-down ");
    REQUIRE(listed != std::string::npos);
    CHECK(text.substr(listed, text.find('\n', listed) - listed)
              .ends_with(" Scroll down half a page"));

    Config defaults;
    ConfigReader defaultReader{defaults, commands};
    defaultReader.runText(builtInModalrc(), "built-in modalrc", {});
    REQUIRE(defaultReader.errors().empty());

    // As written, every line is a comment.
    Config config;
    ConfigReader reader{config, commands};
    reader.runText(text, "written", {});
    CHECK(reader.errors().empty());
    CHECK(config.keymap == Keymap{});

    // Uncommented, the commands give the defaults: settings, bindings and colours.
    std::string uncommented;
    for (std::size_t start = 0; start < text.size();)
    {
        const auto end = text.find('\n', start);
        auto line = text.substr(start, end - start);
        start = end + 1;
        for (const auto* command : {"\"set ", "\"nmap ", "\"omap ", "\"cmap ", "\"ngroup ",
                                    "\"keysection ", "\"highlight "})
        {
            if (line.starts_with(command))
            {
                line.erase(0, 1);
            }
        }
        uncommented += line + "\n";
    }
    Config written;
    ConfigReader writtenReader{written, commands};
    writtenReader.runText(uncommented, "uncommented", {});
    CHECK(writtenReader.errors().empty());
    CHECK(written.keymap == defaults.keymap);
    CHECK(written.theme == defaults.theme);
    for (const auto& setting : defaults.settings.all())
    {
        CAPTURE(setting.name);
        CHECK(written.settings.find(setting.name)->value == setting.value);
    }
}

TEST_CASE("the config text has what's changed as commands, and reads back the same")
{
    CommandRegistry commands;
    commands.add(Workspace::commands());
    commands.add(windowCommands());
    Config config;
    ConfigReader reader{config, commands};
    runStartupFiles(reader, std::nullopt);
    reader.runText("set fontsize=14 theme=light font=\"C:/My Fonts/x.ttf\"\n"
                   "nmap Q go-first \"Top \\\"row\\\"\"\n"
                   "nmap G go-first\n"
                   "nunmap <PageUp>\n"
                   "highlight cursor #ff0000\n",
                   "modalrc", {});
    REQUIRE(reader.errors().empty());

    const auto text = configText(config, commands, std::nullopt);
    const auto has = [&](std::string_view line)
    { return text.find("\n" + std::string{line} + "\n") != std::string::npos; };
    CHECK(has("set fontsize=14"));
    CHECK(has("set theme=light"));
    CHECK(has("set font=\"C:/My Fonts/x.ttf\""));
    CHECK(has("\"set timeoutlen=500")); // unchanged, so commented out
    CHECK(has("nmap Q go-first \"Top \\\"row\\\"\""));
    CHECK(has("nmap G go-first"));
    CHECK(has("nunmap <PageUp>"));
    CHECK_FALSE(has("nmap gg go-first")); // as built in
    CHECK(text.find("The light theme's:") != std::string::npos);
    CHECK(has("\"highlight background  #e1e2e7")); // the light theme's, commented out
    CHECK(has("highlight cursor      #ff0000"));

    // Read back from the defaults, it gives the same config.
    Config again;
    ConfigReader againReader{again, commands};
    runStartupFiles(againReader, std::nullopt);
    againReader.runText(text, "written", {});
    CHECK(againReader.errors().empty());
    CHECK(again.keymap == config.keymap);
    CHECK(again.theme == config.theme);
    for (const auto& setting : config.settings.all())
    {
        CAPTURE(setting.name);
        CHECK(again.settings.find(setting.name)->value == setting.value);
    }
}
