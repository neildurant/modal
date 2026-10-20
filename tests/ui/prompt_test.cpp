#include <modal/ui/prompt.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>

using namespace modal::ui;

namespace
{

constexpr std::array<CommandInfo, 2> kCommands{{
    {"cursor-down", "Down"},
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

} // namespace

TEST_CASE("the prompt runs config commands, and shows settings with set NAME?")
{
    Config config;
    ConfigReader reader{config, kRegistry};
    CHECK(runPromptLine(reader, config, "set fontsize=14", std::nullopt, kRegistry) ==
          PromptResult{});
    CHECK(config.settings.number("fontsize") == 14.0);
    CHECK(runPromptLine(reader, config, "nmap J cursor-down", std::nullopt, kRegistry) ==
          PromptResult{});

    CHECK(runPromptLine(reader, config, "set fontsize?", std::nullopt, kRegistry) ==
          PromptResult{"fontsize=14", false, false});
    CHECK(runPromptLine(reader, config, "  set fontsize? theme?", std::nullopt, kRegistry) ==
          PromptResult{"fontsize=14  theme=dark", false, false});
    CHECK(runPromptLine(reader, config, "set fontsiz?", std::nullopt, kRegistry) ==
          PromptResult{"unknown setting \"fontsiz\"", true, false});
    CHECK(runPromptLine(reader, config, "set fontsize=huge", std::nullopt, kRegistry) ==
          PromptResult{"fontsize must be a number from 4 to 72", true, false});
    CHECK(runPromptLine(reader, config, "frobnicate", std::nullopt, kRegistry) ==
          PromptResult{"unknown command \"frobnicate\"", true, false});
    CHECK(runPromptLine(reader, config, "   ", std::nullopt, kRegistry) == PromptResult{});
}

TEST_CASE("q and quit quit")
{
    Config config;
    ConfigReader reader{config, kRegistry};
    CHECK(runPromptLine(reader, config, "q", std::nullopt, kRegistry).quit);
    CHECK(runPromptLine(reader, config, "quit", std::nullopt, kRegistry).quit);
    CHECK_FALSE(runPromptLine(reader, config, "quite", std::nullopt, kRegistry).quit);
}

TEST_CASE("source with no file runs modalrc again, reporting its errors")
{
    const auto directory = std::filesystem::temp_directory_path() / "modal-prompt-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    Config config;
    ConfigReader reader{config, kRegistry, directory};

    CHECK(runPromptLine(reader, config, "source", directory, kRegistry)
              .message.starts_with("there's no modalrc in "));
    CHECK(runPromptLine(reader, config, "source", std::nullopt, kRegistry) ==
          PromptResult{"there's no modalrc", true, false});

    std::ofstream{directory / "modalrc"} << "set timeoutlen=321\n";
    CHECK(runPromptLine(reader, config, "source", directory, kRegistry) ==
          PromptResult{"ran modalrc again", false, false});
    CHECK(config.settings.integer("timeoutlen") == 321);

    std::ofstream{directory / "modalrc"} << "set nope=1\nset nada=2\n";
    CHECK(runPromptLine(reader, config, "source", directory, kRegistry) ==
          PromptResult{"modalrc:1: unknown setting \"nope\" (and 1 more)", true, false});

    // A file sourced by name, relative to the config directory, the same way.
    std::ofstream{directory / "more"} << "set fontsize=bad\n";
    CHECK(runPromptLine(reader, config, "source more", directory, kRegistry) ==
          PromptResult{"more:1: fontsize must be a number from 4 to 72", true, false});
    std::filesystem::remove_all(directory);
}

TEST_CASE("write-config writes the config, relative to the config directory")
{
    const auto directory = std::filesystem::temp_directory_path() / "modal-write-config-test";
    std::filesystem::remove_all(directory);
    Config config;
    ConfigReader reader{config, kRegistry, directory};
    runPromptLine(reader, config, "set fontsize=15", directory, kRegistry);

    const auto file = directory / "modalrc";
    CHECK(runPromptLine(reader, config, "write-config modalrc", directory, kRegistry) ==
          PromptResult{"wrote " + utf8Text(file), false, false});
    std::ifstream in{file};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    in.close();
    CHECK(text.find("\nset fontsize=15\n") != std::string::npos);

    const auto refused =
        runPromptLine(reader, config, "write-config modalrc", directory, kRegistry);
    CHECK(refused.warning);
    CHECK(refused.message.ends_with("exists already; write-config! overwrites it"));
    CHECK(runPromptLine(reader, config, "write-config! modalrc", directory, kRegistry).message ==
          "wrote " + utf8Text(file));
    CHECK(runPromptLine(reader, config, "write-config", directory, kRegistry) ==
          PromptResult{"write-config needs FILE", true, false});
    std::filesystem::remove_all(directory);
}
