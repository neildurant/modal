// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/commands.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace modal::ui;

namespace
{

// A registry with a few commands, recording how they were called.
struct Recorded
{
    CommandRegistry registry;
    std::vector<std::string> calls;

    Recorded()
    {
        const auto record = [this](std::string name)
        {
            return [this, name](const CommandCall& call)
            {
                calls.push_back(name + " " + std::to_string(call.count) + (call.bang ? "!" : "") +
                                " [" + std::string{call.arguments} + "]");
                return CommandResult{true, {}, false};
            };
        };
        registry.add({.name = "cursor-down", .description = "Down", .count = true},
                     record("cursor-down"));
        registry.add({.name = "quit", .description = "Quit", .aliases = "q exit"}, record("quit"));
        registry.add({.name = "write-config",
                      .description = "Write",
                      .usage = "FILE",
                      .bang = true,
                      .keys = false},
                     record("write-config"));
        registry.add({.name = "command-line-left",
                      .description = "Left",
                      .modes = modeBit(Mode::CommandLine)},
                     record("command-line-left"));
        registry.add({.name = "described", .description = "No handler here"});
    }
};

} // namespace

TEST_CASE("commands are found by name or alias, in the order added")
{
    const Recorded r;
    REQUIRE(r.registry.find("quit"));
    CHECK(r.registry.find("q") == r.registry.find("quit"));
    CHECK(r.registry.find("exit") == r.registry.find("quit"));
    CHECK(r.registry.find("qu") == nullptr);
    CHECK(r.registry.find("write-config!") == nullptr);

    const auto all = r.registry.all();
    REQUIRE(all.size() == 5);
    CHECK(all[0].name == "cursor-down");
    CHECK(all[4].name == "described");
}

TEST_CASE("a command can be described first, and given its handler later")
{
    Recorded r;
    CHECK(r.registry.run("described", 0, Mode::Normal).warning);
    r.registry.add({.name = "described", .description = "Now it runs"},
                   [](const CommandCall&) { return CommandResult{true, "ran", false}; });
    CHECK(r.registry.run("described", 0, Mode::Normal) == CommandResult{true, "ran", false});
    CHECK(r.registry.find("described")->description == "Now it runs");
    CHECK(r.registry.all().size() == 5);
}

TEST_CASE("a command's title comes from its name unless it has one")
{
    CHECK(titleOf({.name = "cursor-left", .description = ""}) == "Cursor left");
    CHECK(titleOf({.name = "go-first", .description = "", .title = "Go to the start"}) ==
          "Go to the start");
}

TEST_CASE("keys run commands with a count, in the modes they work in")
{
    Recorded r;
    CHECK(r.registry.run("cursor-down", 3, Mode::Normal).changed);
    CHECK(r.registry.run("command-line-left", 0, Mode::CommandLine).changed);
    CHECK(r.calls == std::vector<std::string>{"cursor-down 3 []", "command-line-left 0 []"});

    CHECK(r.registry.run("command-line-left", 0, Mode::Normal) ==
          CommandResult::error("command-line-left works only in command-line mode"));
    CHECK(r.registry.run("nothing", 0, Mode::Normal) ==
          CommandResult::error("unknown command \"nothing\""));
    CHECK(r.registry.run("described", 0, Mode::Normal) ==
          CommandResult::error("described can't be run here"));
}

TEST_CASE("the : line runs a command by name or alias, with a !, arguments or a count")
{
    Recorded r;
    CHECK(r.registry.runLine("  cursor-down  ").changed);
    CHECK(r.registry.runLine("cursor-down 3").changed);
    CHECK(r.registry.runLine("q").changed);
    CHECK(r.registry.runLine("write-config  my config ").changed);
    CHECK(r.registry.runLine("write-config! modalrc").changed);
    CHECK(r.calls == std::vector<std::string>{"cursor-down 0 []", "cursor-down 3 []", "quit 0 []",
                                              "write-config 0 [my config]",
                                              "write-config 0! [modalrc]"});

    CHECK(r.registry.runLine("") == CommandResult{});
    CHECK(r.registry.runLine("cursor-down 0") ==
          CommandResult::error("cursor-down takes only a count"));
    CHECK(r.registry.runLine("cursor-down x") ==
          CommandResult::error("cursor-down takes only a count"));
    CHECK(r.registry.runLine("quit now") == CommandResult::error("quit takes nothing"));
    CHECK(r.registry.runLine("quit!") == CommandResult::error("quit doesn't take !"));
    CHECK(r.registry.runLine("frobnicate") ==
          CommandResult::error("unknown command \"frobnicate\""));
    CHECK(r.registry.runLine("command-line-left") ==
          CommandResult::error("command-line-left works only in command-line mode"));
}
