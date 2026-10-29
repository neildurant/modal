// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/frame.hpp>
#include <modal/ui/inspector_edit.hpp>
#include <modal/ui/view.hpp>

#include <algorithm>
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

bool InspectorEditor::open(const Inspection& inspection)
{
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
                host.setMessage(std::move(*why), MessageKind::Warning);
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
    if (command == "command-line-cancel" ||
        (command == "command-line-backspace" && line_.text().empty())) // as in Vim
    {
        return leave();
    }
    if (command == "command-line-older" || command == "command-line-newer")
    {
        // A step of the value as it is, what was typed left aside.
        if (auto why = hooks.step(*heading_, command == "command-line-older" ? 1 : -1))
        {
            host.setMessage(std::move(*why), MessageKind::Warning);
            return true;
        }
        const auto inspection = hooks.inspection();
        if (const auto index = indexOf(inspection, *heading_))
        {
            show(inspection, *index);
        }
        return true;
    }
    fresh_ = false; // keys that edit the value keep it
    return editLine(line_, command);
}

} // namespace modal::ui
