// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/frame.hpp>
#include <modal/ui/inspector_edit.hpp>
#include <modal/ui/view.hpp>

#include <algorithm>
#include <cctype>
#include <utility>
#include <vector>

namespace modal::ui
{

namespace
{

// The text a field's editing starts from: its own, or its value less anything after it in
// brackets, a note number or note value.
std::string editText(const InspectorField& field)
{
    if (!field.editText.empty())
    {
        return field.editText;
    }
    return field.value.substr(0, field.value.find(" ("));
}

std::optional<std::size_t> indexOf(const Inspection& inspection, std::string_view heading)
{
    for (std::size_t i = 0; i < inspection.fields.size(); ++i)
    {
        if (inspection.fields[i].editable && inspection.fields[i].heading == heading)
        {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace

bool InspectorEditor::open(const Inspection& inspection, std::string_view heading)
{
    if (!heading.empty())
        last_ = heading;
    if (const auto last = indexOf(inspection, last_))
    {
        show(inspection, *last);
        return true;
    }
    const auto first = std::ranges::find_if(inspection.fields, &InspectorField::editable);
    if (first == inspection.fields.end())
    {
        return false;
    }
    show(inspection, static_cast<std::size_t>(first - inspection.fields.begin()));
    return true;
}

void InspectorEditor::show(const Inspection& inspection, std::size_t index)
{
    const auto& field = inspection.fields[index];
    heading_ = field.heading;
    last_ = field.heading;
    line_.open(decodeUtf8(editText(field)));
    fresh_ = true;
    list_.reset();
    choices_ = field.choices;
    shown_ = field.value;
    if (!field.choices.empty())
    {
        // Opening on what it's set to, by its words whatever their case.
        const auto lower = [](std::string text)
        {
            for (auto& c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        };
        const auto current = lower(editText(field));
        const auto at = std::ranges::find(field.choices, current, lower);
        list_.emplace(
            field.choices,
            static_cast<std::size_t>(at == field.choices.end() ? 0 : at - field.choices.begin()),
            field.narrowing);
        list_->name(lower(field.heading)); // "No port matches"
        current_ = list_->chosen();
    }
}

void InspectorEditor::move(const Inspection& inspection, int by)
{
    std::vector<std::size_t> editable;
    for (std::size_t i = 0; i < inspection.fields.size(); ++i)
    {
        if (inspection.fields[i].editable)
        {
            editable.push_back(i);
        }
    }
    if (editable.empty())
    {
        close();
        return;
    }
    const auto here = std::ranges::find_if(editable, [&](std::size_t i)
                                           { return inspection.fields[i].heading == *heading_; });
    const auto count = static_cast<std::ptrdiff_t>(editable.size());
    const auto at = here == editable.end() ? 0 : here - editable.begin();
    show(inspection, editable[static_cast<std::size_t>(((at + by) % count + count) % count)]);
}

void InspectorEditor::type(char32_t character)
{
    if (list_)
    {
        list_->type(character);
        return;
    }
    if (std::exchange(fresh_, false))
    {
        line_.clear(); // typing replaces the value, as in a spreadsheet
    }
    line_.insert(character);
}

std::optional<bool> InspectorEditor::run(std::string_view command, ViewHost& host,
                                         const Hooks& hooks)
{
    const auto leave = [&]
    {
        close();
        host.stopTyping();
        return true;
    };
    if (list_ && (command == "command-line-run" || command == "command-line-next" ||
                  command == "command-line-previous"))
    {
        // The one highlighted is set, unless it's what it was.
        const auto chosen = list_->chosen();
        if (!chosen)
        {
            host.refuse(list_->nothing(), MessageKind::Warning);
            return true;
        }
        if (chosen != current_)
        {
            if (auto why = hooks.set(*heading_, choices_[*chosen]))
            {
                host.refuse(std::move(*why), MessageKind::Warning);
                return true;
            }
        }
        if (command == "command-line-run")
        {
            return leave();
        }
        move(hooks.inspection(), command == "command-line-next" ? 1 : -1);
        return true;
    }
    if (list_ && command != "command-line-cancel")
    {
        list_->run(command);
        return true;
    }
    if (command == "command-line-run" || command == "command-line-next" ||
        command == "command-line-previous")
    {
        // What was typed is set, unless nothing was; a value that can't be keeps the field
        // open, saying why, to put right.
        if (!fresh_)
        {
            const auto text = encodeUtf8(line_.text());
            if (auto why = hooks.set(*heading_, text))
            {
                host.refuse(std::move(*why), MessageKind::Warning);
                return true;
            }
        }
        if (command == "command-line-run")
        {
            return leave();
        }
        move(hooks.inspection(), command == "command-line-next" ? 1 : -1);
        return true;
    }
    if (command == "command-line-cancel") // Backspace on an empty line does nothing
    {
        return leave();
    }
    if (command == "command-line-older" || command == "command-line-newer")
    {
        // A step of the value as it is, what was typed left aside.
        if (auto why = hooks.step(*heading_, command == "command-line-older" ? 1 : -1))
        {
            host.refuse(std::move(*why), MessageKind::Warning);
            return true;
        }
        const auto inspection = hooks.inspection();
        if (const auto index = indexOf(inspection, *heading_))
        {
            show(inspection, *index);
        }
        return true;
    }
    if (std::exchange(fresh_, false) &&
        (command == "command-line-backspace" || command == "command-line-delete"))
    {
        line_.clear(); // the value is highlighted whole, so these take it whole
        return true;
    }
    return editLine(line_, command); // other keys that edit the value keep it
}

} // namespace modal::ui
