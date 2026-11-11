// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/choice_list.hpp>
#include <modal/ui/style.hpp>

#include <algorithm>
#include <cctype>
#include <utility>

namespace modal::ui
{

namespace
{

constexpr std::size_t kShown = 8;

std::u32string folded(std::u32string text)
{
    for (auto& c : text)
    {
        c = c < 128 ? static_cast<char32_t>(std::tolower(static_cast<int>(c))) : c;
    }
    return text;
}

} // namespace

ChoiceList::ChoiceList(std::vector<std::string> choices, std::size_t current, bool narrowing)
    : choices_(std::move(choices))
    , narrowing_(narrowing)
    , chosen_(std::min(current, choices_.empty() ? 0 : choices_.size() - 1))
{
    top_ = chosen_ >= kShown ? chosen_ - kShown + 1 : 0;
}

void ChoiceList::warn(std::size_t index, std::string reason)
{
    warned_ = index;
    reason_ = std::move(reason);
}

std::vector<std::size_t> ChoiceList::matches() const
{
    const auto wanted = folded(typed_);
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < choices_.size(); ++i)
    {
        if (folded(decodeUtf8(choices_[i])).find(wanted) != std::u32string::npos)
        {
            result.push_back(i);
        }
    }
    return result;
}

std::optional<std::size_t> ChoiceList::chosen() const
{
    const auto all = matches();
    if (chosen_ >= all.size())
    {
        return std::nullopt;
    }
    return all[chosen_];
}

void ChoiceList::moveBy(std::ptrdiff_t by)
{
    const auto all = matches();
    if (all.empty())
    {
        return;
    }
    const auto last = static_cast<std::ptrdiff_t>(all.size()) - 1;
    chosen_ = static_cast<std::size_t>(
        std::clamp(static_cast<std::ptrdiff_t>(chosen_) + by, std::ptrdiff_t{0}, last));
    top_ = std::min(top_, chosen_);
    top_ = chosen_ >= top_ + kShown ? chosen_ - kShown + 1 : top_;
}

void ChoiceList::type(char32_t character)
{
    if (narrowing_)
    {
        typed_ += character;
        chosen_ = 0;
        top_ = 0;
        return;
    }
    if (character == U'j' || character == U'k')
    {
        moveBy(character == U'j' ? 1 : -1);
        return;
    }
    // The next choice after the one highlighted that starts with the letter, round to the first.
    const auto letter = folded(std::u32string(1, character));
    for (std::size_t step = 1; step <= choices_.size(); ++step)
    {
        const auto i = (chosen_ + step) % choices_.size();
        if (folded(decodeUtf8(choices_[i])).starts_with(letter))
        {
            moveBy(static_cast<std::ptrdiff_t>(i) - static_cast<std::ptrdiff_t>(chosen_));
            return;
        }
    }
}

bool ChoiceList::run(std::string_view command)
{
    if (command == "command-line-older")
    {
        moveBy(-1);
    }
    else if (command == "command-line-newer")
    {
        moveBy(1);
    }
    else if (command == "command-line-page-up")
    {
        moveBy(-static_cast<std::ptrdiff_t>(kShown));
    }
    else if (command == "command-line-page-down")
    {
        moveBy(static_cast<std::ptrdiff_t>(kShown));
    }
    else if (narrowing_ && (command == "command-line-backspace" || command == "command-line-clear"))
    {
        if (command == "command-line-clear")
        {
            typed_.clear();
        }
        else if (!typed_.empty())
        {
            typed_.pop_back();
        }
        chosen_ = 0;
        top_ = 0;
    }
    else
    {
        return false;
    }
    return true;
}

Region ChoiceList::region(Rect field, Rect within, CellSize cell) const
{
    const auto all = matches();
    const auto rows = std::clamp<std::size_t>(all.size(), 1, kShown);
    const auto label = [&](std::size_t match)
    {
        const auto i = all[match];
        return decodeUtf8(i == warned_ ? choices_[i] + " " + reason_ : choices_[i]);
    };

    // As wide as the longest choice, or the field if that's wider.
    auto widest = static_cast<std::size_t>(std::max(field.width / cell.width, 0));
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        widest = std::max(widest, label(i).size() + 2);
    }
    const int width = std::min(static_cast<int>(widest) * cell.width, within.width);
    const int x = std::clamp(field.x, within.x, within.x + within.width - width);
    const int height = static_cast<int>(rows) * cell.height;
    const int below = field.y + cell.height;
    const int y =
        below + height <= within.y + within.height ? below : std::max(field.y - height, within.y);

    Region list{.bounds = {x, y, width, height},
                .background = Role::Panel,
                .text = {},
                .under = {},
                .over = {}};
    if (all.empty())
    {
        list.text.push_back({cell.width,
                             0,
                             decodeUtf8("No " + noun_ + " matches"),
                             {Role::Dim, Role::Panel, kPlain}});
    }
    for (std::size_t i = top_; i < all.size() && i < top_ + rows; ++i)
    {
        auto text = label(i);
        const bool here = i == chosen_;
        const bool warned = all[i] == warned_;
        text.insert(0, 1, U' ');
        text.resize(static_cast<std::size_t>(width / cell.width), U' ');
        list.text.push_back({0, static_cast<int>(i - top_) * cell.height, std::move(text),
                             here     ? Style{Role::CursorText, Role::Cursor, kPlain}
                             : warned ? Style{Role::Warning, Role::Panel, kPlain}
                                      : Style{Role::Text, Role::Panel, kPlain}});
    }
    return list;
}

} // namespace modal::ui
