#include <modal/ui/prompt.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>

using namespace modal::ui;

namespace
{

constexpr std::array<CommandInfo, 2> kCommands{{
    {"cursor-down", "Down"},
    {"cancel", "Cancel"},
}};

} // namespace

TEST_CASE("the prompt runs config commands, and shows settings with set NAME?")
{
    Config config;
    ConfigReader reader{config, kCommands};
    CHECK(runPromptLine(reader, config, "set fontsize=14", std::nullopt) == PromptResult{});
    CHECK(config.settings.number("fontsize") == 14.0);
    CHECK(runPromptLine(reader, config, "nmap J cursor-down", std::nullopt) == PromptResult{});

    CHECK(runPromptLine(reader, config, "set fontsize?", std::nullopt) ==
          PromptResult{"fontsize=14", false, false});
    CHECK(runPromptLine(reader, config, "  set fontsize? theme?", std::nullopt) ==
          PromptResult{"fontsize=14  theme=dark", false, false});
    CHECK(runPromptLine(reader, config, "set fontsiz?", std::nullopt) ==
          PromptResult{"unknown setting \"fontsiz\"", true, false});
    CHECK(runPromptLine(reader, config, "set fontsize=huge", std::nullopt) ==
          PromptResult{"fontsize must be a number from 4 to 72", true, false});
    CHECK(runPromptLine(reader, config, "frobnicate", std::nullopt) ==
          PromptResult{"unknown command \"frobnicate\"", true, false});
    CHECK(runPromptLine(reader, config, "   ", std::nullopt) == PromptResult{});
}

TEST_CASE("q and quit quit")
{
    Config config;
    ConfigReader reader{config, kCommands};
    CHECK(runPromptLine(reader, config, "q", std::nullopt).quit);
    CHECK(runPromptLine(reader, config, "quit", std::nullopt).quit);
    CHECK_FALSE(runPromptLine(reader, config, "quite", std::nullopt).quit);
}

TEST_CASE("source with no file runs modalrc again, reporting its errors")
{
    const auto directory = std::filesystem::temp_directory_path() / "modal-prompt-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    Config config;
    ConfigReader reader{config, kCommands, directory};

    CHECK(runPromptLine(reader, config, "source", directory)
              .message.starts_with("there's no modalrc in "));
    CHECK(runPromptLine(reader, config, "source", std::nullopt) ==
          PromptResult{"there's no modalrc", true, false});

    std::ofstream{directory / "modalrc"} << "set timeoutlen=321\n";
    CHECK(runPromptLine(reader, config, "source", directory) ==
          PromptResult{"ran modalrc again", false, false});
    CHECK(config.settings.integer("timeoutlen") == 321);

    std::ofstream{directory / "modalrc"} << "set nope=1\nset nada=2\n";
    CHECK(runPromptLine(reader, config, "source", directory) ==
          PromptResult{"modalrc:1: unknown setting \"nope\" (and 1 more)", true, false});

    // A file sourced by name, relative to the config directory, the same way.
    std::ofstream{directory / "more"} << "set fontsize=bad\n";
    CHECK(runPromptLine(reader, config, "source more", directory) ==
          PromptResult{"more:1: fontsize must be a number from 4 to 72", true, false});
    std::filesystem::remove_all(directory);
}
