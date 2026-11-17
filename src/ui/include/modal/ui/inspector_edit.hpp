// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
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

// Editing fields one at a time (decided October 2026), typed into as the event list's cells
// are, typing replacing the value and the editing keys keeping it. Up and Down step the value
// where it can be stepped. A field with a fixed set of choices has a drop-down list of them
// (decided November 2026).
//
// It works two ways. Editing a table's cell in place (the Tracks, Ports and Metronome tables):
// Tab and Shift-Tab set the field and go on, Enter sets it and leaves, Escape leaves it as it
// was, each field set being an undo step of its own, and a list opens with the field.
//
// Editing in an inspector panel, with e (decided 17 November 2026, after use, the same in
// every view): its changes are made at once, to see and hear, and are one undo step. Enter
// sets the field and stays on it; Tab and Shift-Tab set it and go round the fields and the
// panel's Keep and Cancel buttons, where Enter presses the button; Ctrl-Enter keeps from
// anywhere; Escape puts back what's typed, closes an open list, or with nothing to put back
// cancels, taking every change since the panel opened back. A choice field's list stays
// closed, Up and Down stepping through the choices, until Enter or Alt-Down opens it; Enter
// there chooses and closes it.
class InspectorEditor
{
public:
    // What a view does with a field: sets it from what was typed, or steps it, saying why not
    // if it can't. Then the inspection as it is after. And, in a panel, what to do once it's
    // kept or cancelled, the editing having closed.
    struct Hooks
    {
        std::function<Inspection()> inspection;
        std::function<std::optional<std::string>(const std::string& heading,
                                                 const std::string& text)>
            set;
        std::function<std::optional<std::string>(const std::string& heading, int steps)> step;
        std::function<void(bool kept)> finished = {};
    };

    // The panel's buttons, which Tab reaches after its fields.
    static constexpr std::string_view kKeep = "Keep";
    static constexpr std::string_view kCancel = "Cancel";

    // Starts editing a table's cell, on the field it was last on if there is one, or else the
    // first that can be; false if none can.
    bool open(const Inspection& inspection, std::string_view heading = {});
    // Starts editing in an inspector panel, its changes to the document a group of its own,
    // unless the caller has begun one already, as making a ramp does.
    bool openPanel(const Inspection& inspection, core::Document& document,
                   const core::EditPlace& place, std::string_view heading = {}, bool begun = false);
    void close()
    {
        heading_.reset();
        list_.reset();
    }
    bool active() const { return heading_.has_value(); }
    // Whether it's editing in a panel, and on one of its buttons.
    bool panel() const { return document_ != nullptr && active(); }
    bool onButton() const { return active() && (*heading_ == kKeep || *heading_ == kCancel); }
    // Takes a panel's changes back and closes it, without a word, as closing the window does.
    void abandon();

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
    // Goes to the field with a heading, or by places among those that can be edited, and in a
    // panel its buttons.
    void show(const Inspection& inspection, std::size_t index);
    void showButton(std::string_view button);
    void move(const Inspection& inspection, int by);
    void openList();
    // The field again as it is now, what was typed going.
    void reshow(const Inspection& inspection);
    std::optional<bool> runPanel(std::string_view command, ViewHost& host, const Hooks& hooks);
    // Sets what's typed or chosen, if anything, saying why not if it can't be.
    bool setHere(ViewHost& host, const Hooks& hooks);
    bool finish(bool keep, ViewHost& host, const Hooks& hooks);

    std::optional<std::string> heading_;
    std::string last_; // the heading of the field it was last on
    CommandLine line_;
    bool fresh_ = true; // typing, Backspace and Delete replace the value; other keys keep it
    std::optional<ChoiceList> list_;
    std::vector<std::string> choices_;
    bool narrowing_ = false;
    std::string shown_;
    std::optional<std::size_t> current_; // the choice the field is on
    core::Document* document_ = nullptr; // while editing in a panel
};

// What the status line says while a panel is being edited.
inline constexpr std::string_view kPanelHint =
    "Enter sets; Tab goes on; Ctrl-Enter keeps, Escape cancels";

} // namespace modal::ui
