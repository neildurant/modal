// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/ui/choice_list.hpp>
#include <modal/ui/command_line.hpp>
#include <modal/ui/inspector.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

class ViewHost;

// Editing the inspector's fields with e (decided October 2026): one field at a time, typed
// into as the event list's cells are, typing replacing the value and the editing keys keeping
// it. Tab and Shift-Tab set it and go on to the next or previous field that can be edited,
// round from the last to the first; Enter sets it and leaves; Escape leaves it as it was, the
// fields set before it staying set. Up and Down step the value where it can be stepped. Each
// field set is an undo step of its own. It opens on the field it was last on, so going through
// notes changing one property is e, a value and Enter each time. A field with a fixed set of
// choices opens a drop-down list of them instead (decided November 2026), Enter or Tab setting
// the one highlighted.
class InspectorEditor
{
public:
    // What a view does with a field: sets it from what was typed, or steps it, saying why not
    // if it can't. Then the inspection as it is after.
    struct Hooks
    {
        std::function<Inspection()> inspection;
        std::function<std::optional<std::string>(const std::string& heading,
                                                 const std::string& text)>
            set;
        std::function<std::optional<std::string>(const std::string& heading, int steps)> step;
    };

    // Starts editing, on the field it was last on if there is one, or else the first that can be;
    // false if none can.
    bool open(const Inspection& inspection, std::string_view heading = {});
    void close()
    {
        heading_.reset();
        list_.reset();
    }
    bool active() const { return heading_.has_value(); }

    // The field being edited, by its heading, and what's typed in it.
    const std::string& heading() const { return *heading_; }
    const CommandLine& line() const { return line_; }
    // Whether the next character typed replaces the value, shown by highlighting all of it.
    bool replacing() const { return fresh_; }
    // The list being chosen from, on a field with choices.
    const ChoiceList* list() const { return list_ ? &*list_ : nullptr; }
    // What the field is set to as editing started, shown in it while the list is open.
    const std::string& shown() const { return shown_; }

    void type(char32_t character);

    // Runs a command-line command on the field, as a view's runTyping does.
    std::optional<bool> run(std::string_view command, ViewHost& host, const Hooks& hooks);

private:
    // Goes to the field with a heading, or by places among those that can be edited.
    void show(const Inspection& inspection, std::size_t index);
    void move(const Inspection& inspection, int by);

    std::optional<std::string> heading_;
    std::string last_; // the heading of the field it was last on
    CommandLine line_;
    bool fresh_ = true; // typing, Backspace and Delete replace the value; other keys keep it
    std::optional<ChoiceList> list_;
    std::vector<std::string> choices_;
    std::string shown_;
    std::optional<std::size_t> current_; // the choice it was on as the list opened
};

} // namespace modal::ui
