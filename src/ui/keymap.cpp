// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/key_notation.hpp>
#include <modal/ui/keymap.hpp>

#include <algorithm>
#include <tuple>
#include <utility>
#include <variant>

namespace modal::ui
{

namespace
{

auto order(const Key& key)
{
    return std::tuple{key.character, key.named, key.modifiers};
}

} // namespace

bool Keymap::KeysLess::operator()(const std::vector<Key>& a, const std::vector<Key>& b) const
{
    return std::ranges::lexicographical_compare(a, b, [](const Key& x, const Key& y)
                                                { return order(x) < order(y); });
}

std::string Keymap::bind(Mode mode, std::string_view keys, Binding binding)
{
    auto parsed = parseKeys(keys);
    if (auto* error = std::get_if<std::string>(&parsed))
    {
        return std::move(*error);
    }
    bind(mode, std::move(std::get<std::vector<Key>>(parsed)), std::move(binding));
    return {};
}

void Keymap::bind(Mode mode, std::vector<Key> keys, Binding binding)
{
    modes_[static_cast<std::size_t>(mode)].insert_or_assign(std::move(keys), std::move(binding));
}

bool Keymap::unbind(Mode mode, const std::vector<Key>& keys)
{
    return modes_[static_cast<std::size_t>(mode)].erase(keys) > 0;
}

std::vector<std::pair<std::vector<Key>, Binding>> Keymap::bindings(Mode mode) const
{
    const auto& bindings = modes_[static_cast<std::size_t>(mode)];
    return {bindings.begin(), bindings.end()};
}

void Keymap::group(Mode mode, std::vector<Key> keys, std::string title)
{
    groups_[static_cast<std::size_t>(mode)].insert_or_assign(std::move(keys), std::move(title));
}

std::string Keymap::groupTitle(Mode mode, std::span<const Key> keys) const
{
    const auto& groups = groups_[static_cast<std::size_t>(mode)];
    const auto it = groups.find(std::vector<Key>{keys.begin(), keys.end()});
    return it == groups.end() ? std::string{} : it->second;
}

std::vector<std::pair<std::vector<Key>, std::string>> Keymap::groups(Mode mode) const
{
    const auto& groups = groups_[static_cast<std::size_t>(mode)];
    return {groups.begin(), groups.end()};
}

Keymap::Match Keymap::match(Mode mode, std::span<const Key> keys) const
{
    const auto& bindings = modes_[static_cast<std::size_t>(mode)];
    const std::vector<Key> wanted{keys.begin(), keys.end()};
    Match match;
    auto it = bindings.lower_bound(wanted);
    if (it != bindings.end() && it->first == wanted)
    {
        match.binding = &it->second;
        ++it;
    }
    // Longer sequences starting with these keys sort straight after them.
    match.longer = it != bindings.end() && it->first.size() > wanted.size() &&
                   std::ranges::equal(wanted, std::span{it->first}.first(wanted.size()));
    return match;
}

KeyDispatcher::KeyDispatcher(std::int64_t timeout)
    : timeout_(timeout)
{
}

void KeyDispatcher::clear()
{
    count_ = 0;
    keys_.clear();
    deadline_.reset();
}

std::vector<Action> KeyDispatcher::key(const Keymap& keymap, Mode mode, const Key& typed,
                                       std::int64_t now)
{
    // The leader setting's key is <leader> in bindings, where keys are mapped to commands.
    const bool leads =
        mode == Mode::Normal || mode == Mode::Visual || mode == Mode::OperatorPending;
    const auto key = leads && typed == leader_ ? Key::of(NamedKey::Leader) : typed;
    if (key == Key::of(NamedKey::Escape) && (count_ > 0 || !keys_.empty()))
    {
        clear();
        return {};
    }
    const char32_t c = key.character;
    const bool digit =
        key.named == NamedKey::None && key.modifiers == kNoModifiers && c >= U'0' && c <= U'9';
    const bool counts =
        mode == Mode::Normal || mode == Mode::Visual || mode == Mode::OperatorPending;
    if (keys_.empty() && counts && digit && (c != U'0' || count_ > 0))
    {
        count_ = std::min(count_ * 10 + static_cast<int>(c - U'0'), kMaxCount);
        return {};
    }

    if (keys_.empty())
    {
        mode_ = mode;
    }
    keys_.push_back(key);
    const auto match = keymap.match(mode_, keys_);
    if (match.longer)
    {
        // Wait for the next key, or only so long if these keys are a binding already.
        deadline_ = match.binding != nullptr ? std::optional{now + timeout_} : std::nullopt;
        return {};
    }
    if (match.binding != nullptr)
    {
        std::vector<Action> actions{{match.binding->command, count_}};
        clear();
        return actions;
    }

    // These keys continue no binding: run the keys before the last if they're one, then
    // take the last afresh.
    keys_.pop_back();
    if (keys_.empty())
    {
        clear(); // a key bound to nothing
        return {};
    }
    std::vector<Action> actions;
    if (const auto* binding = keymap.match(mode_, keys_).binding)
    {
        actions.push_back({binding->command, count_});
    }
    clear();
    auto more = this->key(keymap, mode, typed, now);
    actions.insert(actions.end(), more.begin(), more.end());
    return actions;
}

std::vector<Action> KeyDispatcher::timeout(const Keymap& keymap, std::int64_t now)
{
    if (!deadline_ || now < *deadline_)
    {
        return {};
    }
    std::vector<Action> actions;
    if (const auto* binding = keymap.match(mode_, keys_).binding)
    {
        actions.push_back({binding->command, count_});
    }
    clear();
    return actions;
}

std::string KeyDispatcher::pending() const
{
    return (count_ > 0 ? std::to_string(count_) : std::string{}) + formatKeys(keys_);
}

} // namespace modal::ui
