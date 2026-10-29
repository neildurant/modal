// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/ui/key.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace modal::ui
{

// The modes keys are bound in, as in Vim. Only Normal has bindings so far.
enum class Mode : std::uint8_t
{
    Normal,
    Visual,
    Insert,
    OperatorPending, // after an operator, waiting for its motion
    CommandLine,
};

inline constexpr std::size_t kModeCount = static_cast<std::size_t>(Mode::CommandLine) + 1;

// What a sequence of keys does: a command, by name, and a short label for hints.
struct Binding
{
    std::string command;
    std::string hint;

    friend bool operator==(const Binding&, const Binding&) = default;
};

// Key sequences bound to commands, in each mode.
class Keymap
{
public:
    // Binds keys written in notation (see key_notation.hpp), replacing any binding of the
    // same keys; returns why not if the notation can't be read, or else nothing.
    std::string bind(Mode mode, std::string_view keys, Binding binding);
    void bind(Mode mode, std::vector<Key> keys, Binding binding);

    bool unbind(Mode mode, const std::vector<Key>& keys);

    // What some keys typed in a mode match: a binding of exactly those keys, if any, and
    // whether there are bindings of longer sequences starting with them.
    struct Match
    {
        const Binding* binding = nullptr;
        bool longer = false;
    };
    Match match(Mode mode, std::span<const Key> keys) const;

    // A mode's bindings, in the order of their keys.
    std::vector<std::pair<std::vector<Key>, Binding>> bindings(Mode mode) const;

    // A title for keys that start longer bindings, which the hint menu shows for them
    // ("Toggles" for <leader>t), and a mode's titles, in the order of their keys.
    void group(Mode mode, std::vector<Key> keys, std::string title);
    std::string groupTitle(Mode mode, std::span<const Key> keys) const;
    std::vector<std::pair<std::vector<Key>, std::string>> groups(Mode mode) const;

    friend bool operator==(const Keymap&, const Keymap&) = default;

private:
    struct KeysLess
    {
        bool operator()(const std::vector<Key>& a, const std::vector<Key>& b) const;
    };
    using Bindings = std::map<std::vector<Key>, Binding, KeysLess>;
    using Groups = std::map<std::vector<Key>, std::string, KeysLess>;

    std::array<Bindings, kModeCount> modes_;
    std::array<Groups, kModeCount> groups_;
};

// A command to run, with the count typed before its keys, or 0 if none was.
struct Action
{
    std::string command;
    int count = 0;

    friend bool operator==(const Action&, const Action&) = default;
};

// How long to wait, in milliseconds, after keys that are a binding and also start a longer
// one, before running the shorter: Vim's timeoutlen, until it's a setting.
inline constexpr std::int64_t kDefaultTimeout = 500;
inline constexpr int kMaxCount = 99999;

// Turns keys into commands through a keymap, as Vim does. In Normal, Visual and
// operator-pending mode, digits typed first are a count, except a 0 that would begin one. Keys that
// only start longer bindings wait for the next key; keys that are a binding and also start a longer
// one wait up to the timeout, then run. A key that continues no binding runs the keys before it, if
// they're a binding, and is then taken afresh; otherwise those keys and the count are given up.
// Escape gives up any count and keys waiting. Times are in milliseconds, from any fixed point. The
// keymap is passed with each key, and should be the same while keys are waiting.
class KeyDispatcher
{
public:
    explicit KeyDispatcher(std::int64_t timeout = kDefaultTimeout);

    // Handles a key typed in a mode, returning the commands to run, in order.
    std::vector<Action> key(const Keymap& keymap, Mode mode, const Key& key, std::int64_t now);

    // Runs keys that were waiting for a longer binding, once their time is up.
    std::vector<Action> timeout(const Keymap& keymap, std::int64_t now);

    // When timeout() should next be called, if anything is waiting for it.
    std::optional<std::int64_t> deadline() const { return deadline_; }

    void setTimeout(std::int64_t timeout) { timeout_ = timeout; }

    // The key that stands for <leader> in bindings, in Normal, Visual and operator-pending
    // mode: comma at first.
    void setLeader(const Key& leader) { leader_ = leader; }

    // The count and keys typed so far, for the status line, such as "3g"; empty if none.
    std::string pending() const;

private:
    void clear();

    std::int64_t timeout_;
    Key leader_ = Key::typed(U',');
    Mode mode_ = Mode::Normal; // the mode the waiting keys were typed in
    int count_ = 0;
    std::vector<Key> keys_;
    std::optional<std::int64_t> deadline_;
};

} // namespace modal::ui
