#include <modal/ui/prompt.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

using namespace modal::ui;

namespace
{

constexpr std::array<CommandInfo, 2> kCommands{{
    {.name = "cursor-down", .description = "Down", .count = true},
    {.name = "cancel", .description = "Cancel"},
}};

// A config, its reader, and a registry with the prompt's commands and a few others.
struct Prompt
{
    explicit Prompt(std::optional<std::filesystem::path> directory = std::nullopt)
        : reader{config, registry, directory}
    {
        registry.add(kCommands);
        addPromptCommands(registry, reader, config, directory, [this] { quit = true; });
    }

    CommandResult run(std::string_view line) { return registry.runLine(line); }

    Config config;
    CommandRegistry registry;
    ConfigReader reader;
    bool quit = false;
};

CommandResult shows(std::string message)
{
    return {true, std::move(message), false};
}

} // namespace

TEST_CASE("the prompt runs config commands, and shows settings with set NAME?")
{
    Prompt prompt;
    CHECK(prompt.run("set fontsize=14") == CommandResult{true});
    CHECK(prompt.config.settings.number("fontsize") == 14.0);
    CHECK(prompt.run("nmap J cursor-down") == CommandResult{true});

    CHECK(prompt.run("set fontsize?") == shows("fontsize=14"));
    CHECK(prompt.run("  set fontsize? theme?") == shows("fontsize=14  theme=dark"));
    CHECK(prompt.run("set fontsiz?") == CommandResult::error("unknown setting \"fontsiz\""));
    CHECK(prompt.run("set fontsize=huge") ==
          CommandResult::error("fontsize must be a number from 4 to 72"));
    CHECK(prompt.run("frobnicate") == CommandResult::error("unknown command \"frobnicate\""));
    CHECK(prompt.run("   ") == CommandResult{});
}

TEST_CASE("the prompt runs any command, with a count for one that takes it")
{
    Prompt prompt;
    CHECK(prompt.run("cursor-down 3") == CommandResult::error("cursor-down can't be run here"));
    CHECK(prompt.run("cancel 3") == CommandResult::error("cancel takes nothing"));
}

TEST_CASE("q and quit quit")
{
    Prompt prompt;
    prompt.run("q");
    CHECK(prompt.quit);
    Prompt again;
    again.run("quit");
    CHECK(again.quit);
    Prompt typo;
    CHECK(typo.run("quite") == CommandResult::error("unknown command \"quite\""));
    CHECK_FALSE(typo.quit);
}

TEST_CASE("prompt commands can't go in a config file, nor be bound unless they're for keys")
{
    Prompt prompt;
    prompt.reader.runText("write-config x\nnmap Z set\nnmap ZZ quit\n", "modalrc", {});
    const auto& errors = prompt.reader.errors();
    REQUIRE(errors.size() == 2);
    CHECK(errors[0].message == "write-config can't be used in a config file");
    CHECK(errors[1].message == "set can't be bound to keys");
}

TEST_CASE("source with no file runs modalrc again, reporting its errors")
{
    const auto directory = std::filesystem::temp_directory_path() / "modal-prompt-test";
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    Prompt prompt{directory};

    CHECK(prompt.run("source").message.starts_with("there's no modalrc in "));
    CHECK(Prompt{}.run("source") == CommandResult::error("there's no modalrc"));

    std::ofstream{directory / "modalrc"} << "set timeoutlen=321\n";
    CHECK(prompt.run("source") == shows("ran modalrc again"));
    CHECK(prompt.config.settings.integer("timeoutlen") == 321);

    std::ofstream{directory / "modalrc"} << "set nope=1\nset nada=2\n";
    CHECK(prompt.run("source") ==
          CommandResult::error("modalrc:1: unknown setting \"nope\" (and 1 more)"));

    // A file sourced by name, relative to the config directory, the same way.
    std::ofstream{directory / "more"} << "set fontsize=bad\n";
    CHECK(prompt.run("source more") ==
          CommandResult::error("more:1: fontsize must be a number from 4 to 72"));
    std::filesystem::remove_all(directory);
}

TEST_CASE("write-config writes the config, relative to the config directory")
{
    const auto directory = std::filesystem::temp_directory_path() / "modal-write-config-test";
    std::filesystem::remove_all(directory);
    Prompt prompt{directory};
    prompt.run("set fontsize=15");

    const auto file = directory / "modalrc";
    CHECK(prompt.run("write-config modalrc") == shows("wrote " + utf8Text(file)));
    std::ifstream in{file};
    const std::string text{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    in.close();
    CHECK(text.find("\nset fontsize=15\n") != std::string::npos);
    // The commands keys can be bound to are listed; those that can't aren't.
    CHECK(text.find("\"   quit ") != std::string::npos);
    CHECK(text.find("\"   cursor-down ") != std::string::npos);
    CHECK(text.find("\"   write-config ") == std::string::npos);

    const auto refused = prompt.run("write-config modalrc");
    CHECK(refused.warning);
    CHECK(refused.message.ends_with("exists already; write-config! overwrites it"));
    CHECK(prompt.run("write-config! modalrc").message == "wrote " + utf8Text(file));
    CHECK(prompt.run("write-config") == CommandResult::error("write-config needs FILE"));
    std::filesystem::remove_all(directory);
}
