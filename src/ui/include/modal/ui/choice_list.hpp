// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/ui/frame.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// A drop-down list of choices at a field (decided November 2026), opened with Enter on a field
// that has a fixed set of them, so what there is to choose from can be seen. Up and Down move
// through it, Enter takes the one highlighted and Escape leaves the field as it was. A long list
// of names, such as ports, narrows to those containing what's typed; a short one has nothing to
// type, j and k moving through it as Up and Down do and a letter going to the next choice
// starting with it. Fields with one show a down arrow at their right.
class ChoiceList
{
public:
    // The choices as they're shown, the one highlighted first, and whether typing narrows them.
    ChoiceList(std::vector<std::string> choices, std::size_t current, bool narrowing);

    // A choice that can't be used, as a port not found: shown with its reason after it, in the
    // warning colour.
    void warn(std::size_t index, std::string reason);
    // What it's a list of, for "No port matches".
    void name(std::string noun) { noun_ = std::move(noun); }

    bool narrowing() const { return narrowing_; }
    const std::u32string& typed() const { return typed_; }

    void type(char32_t character);
    // Runs a command-line command on the list: moving, and for a narrowing list Backspace and
    // clearing what's typed. False for those it doesn't take, as Enter and Escape.
    bool run(std::string_view command);

    // The choice highlighted, by its place in the list, or none when nothing matches.
    std::optional<std::size_t> chosen() const;
    // The message when Enter is pressed with nothing matching.
    std::string nothing() const { return "No " + noun_ + " matches that"; }

    // The list, below the field, or above it where there's no room below, as wide as the
    // longest choice; field and within are where the field is and where the list must fit.
    Region region(Rect field, Rect within, CellSize cell) const;

    // The arrow a field with a list shows at its right.
    static constexpr char32_t kArrow = U'▾';

private:
    std::vector<std::size_t> matches() const;
    void moveBy(std::ptrdiff_t by);

    std::vector<std::string> choices_;
    bool narrowing_ = false;
    std::u32string typed_;
    std::size_t chosen_ = 0; // among the matches
    std::size_t top_ = 0;    // the first match shown
    std::optional<std::size_t> warned_;
    std::string reason_;
    std::string noun_ = "choice";
};

} // namespace modal::ui
