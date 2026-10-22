// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/key_notation.hpp>
#include <modal/ui/keymap.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace modal::ui;

namespace
{

Keymap testKeymap()
{
    Keymap keymap;
    const std::vector<std::pair<std::string_view, std::string>> bindings{
        {"j", "down"},       {"0", "first"},  {"G", "last"},          {"gg", "top"},
        {"z", "centre"},     {"zz", "reset"}, {"<C-d>", "half-down"}, {"<Esc>", "cancel"},
        {"ZZ", "save-quit"}, {"ZQ", "quit"},
    };
    for (const auto& [keys, command] : bindings)
    {
        REQUIRE(keymap.bind(Mode::Normal, keys, {command, ""}).empty());
    }
    return keymap;
}

// Types keys written in notation, a millisecond apart from a time, returning the commands
// they run.
std::vector<Action> type(KeyDispatcher& dispatcher, const Keymap& keymap, std::string_view keys,
                         std::int64_t at = 0, Mode mode = Mode::Normal)
{
    const auto parsed = parseKeys(keys);
    REQUIRE(std::holds_alternative<std::vector<Key>>(parsed));
    std::vector<Action> actions;
    for (const auto& key : std::get<std::vector<Key>>(parsed))
    {
        auto more = dispatcher.key(keymap, mode, key, at++);
        actions.insert(actions.end(), more.begin(), more.end());
    }
    return actions;
}

using Actions = std::vector<Action>;

} // namespace

TEST_CASE("a keymap matches bindings, and says whether longer ones start with the keys")
{
    const auto keymap = testKeymap();
    const auto g = std::vector{Key::typed(U'g')};
    CHECK(keymap.match(Mode::Normal, g).binding == nullptr);
    CHECK(keymap.match(Mode::Normal, g).longer);

    const auto z = std::vector{Key::typed(U'z')};
    REQUIRE(keymap.match(Mode::Normal, z).binding != nullptr);
    CHECK(keymap.match(Mode::Normal, z).binding->command == "centre");
    CHECK(keymap.match(Mode::Normal, z).longer);

    const auto zz = std::vector{Key::typed(U'z'), Key::typed(U'z')};
    CHECK(keymap.match(Mode::Normal, zz).binding->command == "reset");
    CHECK_FALSE(keymap.match(Mode::Normal, zz).longer);

    CHECK(keymap.match(Mode::Visual, z).binding == nullptr); // each mode has its own

    Keymap copy = keymap;
    CHECK(copy.unbind(Mode::Normal, zz));
    CHECK_FALSE(copy.match(Mode::Normal, z).longer);
    CHECK_FALSE(copy.unbind(Mode::Normal, zz));

    CHECK(copy.bind(Mode::Normal, "<Nope>", {"x", ""}) == "unknown key <Nope>");
}

TEST_CASE("digits typed first are a count for the binding after them")
{
    const auto keymap = testKeymap();
    KeyDispatcher dispatcher;
    CHECK(type(dispatcher, keymap, "j") == Actions{{"down", 0}});
    CHECK(type(dispatcher, keymap, "12") == Actions{});
    CHECK(dispatcher.pending() == "12");
    CHECK(type(dispatcher, keymap, "j") == Actions{{"down", 12}});
    CHECK(dispatcher.pending().empty());

    CHECK(type(dispatcher, keymap, "0") == Actions{{"first", 0}}); // a 0 that would begin a count
    CHECK(type(dispatcher, keymap, "10<C-d>") == Actions{{"half-down", 10}});
    CHECK(type(dispatcher, keymap, "9999999j") == Actions{{"down", kMaxCount}});
}

TEST_CASE("keys that start longer bindings wait for the next key")
{
    const auto keymap = testKeymap();
    KeyDispatcher dispatcher;
    CHECK(type(dispatcher, keymap, "3g") == Actions{});
    CHECK(dispatcher.pending() == "3g");
    CHECK_FALSE(dispatcher.deadline()); // g alone is nothing, so no hurry
    CHECK(type(dispatcher, keymap, "g") == Actions{{"top", 3}});
    CHECK(dispatcher.pending().empty());

    CHECK(type(dispatcher, keymap, "ZQ") == Actions{{"quit", 0}});
}

TEST_CASE("keys that are a binding and start a longer one wait only so long")
{
    const auto keymap = testKeymap();
    KeyDispatcher dispatcher{1000};
    CHECK(type(dispatcher, keymap, "2z", 5000) == Actions{});
    CHECK(dispatcher.deadline() == 5001 + 1000); // from the z
    CHECK(dispatcher.timeout(keymap, 5999) == Actions{});
    CHECK(dispatcher.pending() == "2z");
    CHECK(dispatcher.timeout(keymap, 6001) == Actions{{"centre", 2}});
    CHECK(dispatcher.pending().empty());
    CHECK_FALSE(dispatcher.deadline());
    CHECK(dispatcher.timeout(keymap, 7000) == Actions{}); // nothing waiting

    CHECK(type(dispatcher, keymap, "zz", 8000) == Actions{{"reset", 0}});
    CHECK_FALSE(dispatcher.deadline());
}

TEST_CASE("a key that continues no binding runs the keys before it if it can, then itself")
{
    const auto keymap = testKeymap();
    KeyDispatcher dispatcher;
    // z is a binding: it runs with the count, then j afresh.
    CHECK(type(dispatcher, keymap, "4zj") == Actions{{"centre", 4}, {"down", 0}});
    // g isn't: it and the count are given up, then j runs.
    CHECK(type(dispatcher, keymap, "4gj") == Actions{{"down", 0}});
    // The key taken afresh can begin a count or another sequence.
    CHECK(type(dispatcher, keymap, "g5") == Actions{});
    CHECK(dispatcher.pending() == "5");
    CHECK(type(dispatcher, keymap, "g") == Actions{});
    CHECK(dispatcher.pending() == "5g");
    CHECK(type(dispatcher, keymap, "z") == Actions{});
    CHECK(dispatcher.pending() == "z");
    CHECK(type(dispatcher, keymap, "z") == Actions{{"reset", 0}});

    // A key bound to nothing gives up the count.
    CHECK(type(dispatcher, keymap, "7x") == Actions{});
    CHECK(dispatcher.pending().empty());
}

TEST_CASE("Escape gives up a count and keys waiting, or else runs its binding")
{
    const auto keymap = testKeymap();
    KeyDispatcher dispatcher;
    CHECK(type(dispatcher, keymap, "3g<Esc>") == Actions{});
    CHECK(dispatcher.pending().empty());
    CHECK(type(dispatcher, keymap, "z<Esc>") == Actions{}); // even a binding waiting for more
    CHECK_FALSE(dispatcher.deadline());
    CHECK(type(dispatcher, keymap, "<Esc>") == Actions{{"cancel", 0}});
}

TEST_CASE("keys are matched in the mode the first of them was typed in")
{
    auto keymap = testKeymap();
    REQUIRE(keymap.bind(Mode::Visual, "j", {"visual-down", ""}).empty());
    KeyDispatcher dispatcher;
    CHECK(type(dispatcher, keymap, "j", 0, Mode::Visual) == Actions{{"visual-down", 0}});
    CHECK(type(dispatcher, keymap, "gg", 0, Mode::Visual) == Actions{});
    CHECK(type(dispatcher, keymap, "g", 0, Mode::Normal) == Actions{});
    CHECK(type(dispatcher, keymap, "g", 0, Mode::Visual) == Actions{{"top", 0}}); // begun in Normal
}

TEST_CASE("digits are a count only in the modes with counts, not in Insert or the command line")
{
    Keymap keymap;
    REQUIRE(keymap.bind(Mode::CommandLine, "1", {"one", ""}).empty());
    KeyDispatcher dispatcher;
    CHECK(type(dispatcher, keymap, "1", 0, Mode::CommandLine) == Actions{{"one", 0}});
    CHECK(type(dispatcher, keymap, "2", 0, Mode::Insert) == Actions{});
    CHECK(dispatcher.pending().empty());
}
