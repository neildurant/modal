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

std::string lowered(std::string text)
{
    for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
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

bool InspectorEditor::openPanel(const Inspection& inspection, core::Document& document,
                                const core::EditPlace& place, std::string_view heading, bool begun)
{
    document_ = &document;
    if (!open(inspection, heading))
    {
        document_ = nullptr;
        if (begun)
        {
            document.cancelGroup();
        }
        return false;
    }
    if (!begun)
    {
        document.beginGroup({}, place);
    }
    return true;
}

void InspectorEditor::abandon()
{
    if (document_ != nullptr)
    {
        document_->cancelGroup();
        document_ = nullptr;
    }
    close();
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
    narrowing_ = field.narrowing;
    shown_ = field.value;
    current_.reset();
    if (!field.choices.empty())
    {
        // On what it's set to, by its words whatever their case.
        const auto at = std::ranges::find(field.choices, lowered(editText(field)), lowered);
        current_ =
            static_cast<std::size_t>(at == field.choices.end() ? 0 : at - field.choices.begin());
        // In a table's cell the list opens with the field; in a panel, when asked for.
        if (document_ == nullptr)
        {
            openList();
        }
    }
}

void InspectorEditor::showButton(std::string_view button)
{
    heading_ = std::string{button};
    line_.open({});
    fresh_ = true;
    list_.reset();
    choices_.clear();
    current_.reset();
    shown_.clear();
}

void InspectorEditor::openList()
{
    if (choices_.empty())
    {
        return;
    }
    list_.emplace(choices_, current_.value_or(0), narrowing_);
    list_->name(lowered(*heading_)); // "No port matches"
    current_ = list_->chosen();
}

void InspectorEditor::move(const Inspection& inspection, int by)
{
    // The fields that can be edited, then in a panel its buttons.
    std::vector<std::string> stops;
    for (const auto& field : inspection.fields)
    {
        if (field.editable)
        {
            stops.push_back(field.heading);
        }
    }
    if (stops.empty())
    {
        close();
        return;
    }
    if (document_ != nullptr)
    {
        stops.emplace_back(kKeep);
        stops.emplace_back(kCancel);
    }
    const auto here = std::ranges::find(stops, *heading_);
    const auto count = static_cast<std::ptrdiff_t>(stops.size());
    const auto at = here == stops.end() ? 0 : here - stops.begin();
    const auto& to = stops[static_cast<std::size_t>(((at + by) % count + count) % count)];
    if (const auto index = indexOf(inspection, to))
    {
        show(inspection, *index);
    }
    else
    {
        showButton(to);
    }
}

void InspectorEditor::reshow(const Inspection& inspection)
{
    if (!heading_ || onButton())
    {
        return;
    }
    if (const auto index = indexOf(inspection, *heading_))
    {
        show(inspection, *index);
    }
    else
    {
        move(inspection, 0); // gone, as a curve's amount does with another shape
    }
}

void InspectorEditor::type(char32_t character)
{
    if (onButton())
    {
        return;
    }
    if (!list_ && document_ != nullptr && !choices_.empty())
    {
        openList(); // a letter on a choice field opens its list at what it starts
    }
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

bool InspectorEditor::setHere(ViewHost& host, const Hooks& hooks)
{
    if (onButton())
    {
        return true;
    }
    std::optional<std::string> why;
    if (list_)
    {
        const auto chosen = list_->chosen();
        if (!chosen)
        {
            host.refuse(list_->nothing(), MessageKind::Warning);
            return false;
        }
        if (chosen != current_)
        {
            why = hooks.set(*heading_, choices_[*chosen]);
        }
    }
    else if (!fresh_)
    {
        why = hooks.set(*heading_, encodeUtf8(line_.text()));
    }
    if (why)
    {
        host.refuse(std::move(*why), MessageKind::Warning);
        return false;
    }
    return true;
}

bool InspectorEditor::finish(bool keep, ViewHost& host, const Hooks& hooks)
{
    auto* document = std::exchange(document_, nullptr);
    if (keep)
    {
        document->endGroup();
    }
    else if (!document->cancelGroup())
    {
        document_ = document;
        host.refuse(document->takeRefusal().value_or("The changes can't be taken back now"),
                    MessageKind::Warning);
        return true;
    }
    close();
    host.stopTyping();
    if (hooks.finished)
    {
        hooks.finished(keep);
    }
    return true;
}

std::optional<bool> InspectorEditor::runPanel(std::string_view command, ViewHost& host,
                                              const Hooks& hooks)
{
    if (command == "command-line-keep")
    {
        return !setHere(host, hooks) || finish(true, host, hooks);
    }
    if (command == "command-line-cancel")
    {
        if (list_)
        {
            list_.reset(); // the field as it was, the list closed
        }
        else if (!onButton() && !fresh_)
        {
            reshow(hooks.inspection());
        }
        else
        {
            finish(false, host, hooks);
        }
        return true;
    }
    if (command == "command-line-run")
    {
        if (onButton())
        {
            return finish(*heading_ == kKeep, host, hooks);
        }
        if (!list_ && !choices_.empty())
        {
            openList();
            return true;
        }
        if (setHere(host, hooks))
        {
            reshow(hooks.inspection());
        }
        return true;
    }
    if (command == "command-line-list")
    {
        if (!list_)
        {
            openList();
        }
        return true;
    }
    if (command == "command-line-next" || command == "command-line-previous")
    {
        if (setHere(host, hooks))
        {
            move(hooks.inspection(), command == "command-line-next" ? 1 : -1);
        }
        return true;
    }
    if (list_)
    {
        list_->run(command);
        return true;
    }
    if (onButton())
    {
        return true;
    }
    if (command == "command-line-older" || command == "command-line-newer")
    {
        // A step of the value as it is, what was typed left aside; on a choice field, the
        // choice before or after, round from the last to the first.
        const int by = command == "command-line-older" ? 1 : -1;
        std::optional<std::string> why;
        if (!choices_.empty())
        {
            const auto size = static_cast<int>(choices_.size());
            const auto at = static_cast<int>(current_.value_or(0));
            const auto next = static_cast<std::size_t>(((at - by) % size + size) % size);
            why = hooks.set(*heading_, choices_[next]);
        }
        else
        {
            why = hooks.step(*heading_, by);
        }
        if (why)
        {
            host.refuse(std::move(*why), MessageKind::Warning);
            return true;
        }
        reshow(hooks.inspection());
        return true;
    }
    if (std::exchange(fresh_, false) &&
        (command == "command-line-backspace" || command == "command-line-delete"))
    {
        line_.clear(); // the value is highlighted whole, so these take it whole
        return true;
    }
    return editLine(line_, command);
}

std::optional<bool> InspectorEditor::run(std::string_view command, ViewHost& host,
                                         const Hooks& hooks)
{
    if (document_ != nullptr)
    {
        return runPanel(command, host, hooks);
    }
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
