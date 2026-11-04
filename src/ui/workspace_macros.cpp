// SPDX-License-Identifier: GPL-3.0-or-later

// The workspace's macros (Qa ... Q, @a) and . repeating the last change (split from
// workspace.cpp, November 2026, after a code review).

#include "workspace_commands.hpp"

#include <modal/core/pitch.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/completion.hpp>
#include <modal/ui/config.hpp>
#include <modal/ui/event_edit.hpp>
#include <modal/ui/event_text.hpp>
#include <modal/ui/key_notation.hpp>
#include <modal/ui/palette.hpp>
#include <modal/ui/settings.hpp>
#include <modal/ui/view_parts.hpp>
#include <modal/ui/window_commands.hpp>
#include <modal/ui/workspace.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <charconv>
#include <map>
#include <tuple>
#include <utility>

namespace modal::ui
{

bool Workspace::recordChange(const Key& key, std::int64_t now)
{
    // The keys of each change, for . to repeat (decided for milestone 6, as Vim's .): those
    // typed from Normal mode until it's back there, with nothing waiting, if the song changed
    // meanwhile; not undo, redo or . itself, nor while . or a macro replays them.
    if (repeat_.replaying)
    {
        return handleKey(key, now);
    }
    // An operator's find that a sharp or flat may yet change (dfB) is done at any other key,
    // the change with it, the key not being part of it; Escape undoes it (decided November
    // 2026), leaving nothing to repeat.
    bool changed = false;
    const bool accidental = key.named == NamedKey::None && (key.modifiers & (kCtrl | kAlt)) == 0 &&
                            (key.character == U'#' || key.character == U'b');
    if (finding_.naming && finding_.naming->redo && key.named == NamedKey::Escape)
    {
        lastTime_ = now;
        cancelNaming();
        repeat_.changeKeys.clear();
        return true;
    }
    if (finding_.naming && finding_.naming->redo && !accidental)
    {
        lastTime_ = now;
        settleNaming();
        changed = true;
        endChange();
    }
    if (repeat_.changeKeys.empty())
    {
        repeat_.versionBefore = document_.version();
    }
    repeat_.changeKeys.push_back(key);
    changed = handleKey(key, now) || changed;
    endChange();
    return changed;
}

void Workspace::endChange()
{
    // A command waiting for its letter (@a, "ap) isn't idle yet, so . repeats the whole of it.
    const bool idle = mode_ == Mode::Normal && keys_.pending().empty() && !operator_ &&
                      !question_ && !cutNext_ && !awaiting_ && !registerName_ && !finding_.naming;
    if (idle && !repeat_.changeKeys.empty())
    {
        if (document_.version() != repeat_.versionBefore && !repeat_.notAChange)
        {
            repeat_.lastChange = std::move(repeat_.changeKeys);
        }
        repeat_.changeKeys.clear();
        repeat_.notAChange = false;
    }
}

std::string Workspace::macroName(char32_t name)
{
    return "@" + encodeUtf8(std::u32string(1, name));
}

bool Workspace::endRecording()
{
    // What was typed, without the keys that ended it, kept in the remembered state.
    macros_.recorded.resize(std::min(macros_.recordedBefore, macros_.recorded.size()));
    const auto name = std::exchange(macros_.recording, 0);
    const auto count = macros_.recorded.size();
    remembered_.setMacro(name, std::exchange(macros_.recorded, {}));
    rememberedChanged();
    setMessage(count == 0 ? macroName(name) + " is empty"
                          : "Recorded " + macroName(name) + ", " + std::to_string(count) +
                                (count == 1 ? " key" : " keys"),
               MessageKind::Information);
    return true;
}

bool Workspace::playMacro(char32_t name, int count)
{
    // Its keys again as typed, count times, as one undo step (decided October 2026, as Vim's
    // @). A command failing in it, by a warning, a key that does nothing here or a motion that
    // can't move, stops it and any playing it, what was done staying, to undo or carry on
    // from; macros playing macros stop kMaxMacroDepth deep.
    const auto* keys = remembered_.macro(name);
    if (keys == nullptr)
    {
        setMessage(macroName(name) + " is empty", MessageKind::Information);
        macros_.failed = macros_.depth > 0;
        return true;
    }
    if (macros_.depth >= kMaxMacroDepth)
    {
        setMessage("Macros playing macros stopped, " + std::to_string(kMaxMacroDepth) + " deep",
                   MessageKind::Warning);
        return true;
    }
    macros_.last = name;
    const auto macro = *keys; // as it was, whatever's recorded meanwhile
    // Changes in it are changes for . in it, as typed; after it, . repeats the @ itself.
    auto changeKeys = std::exchange(repeat_.changeKeys, {});
    const auto lastChange = repeat_.lastChange;
    const auto versionBefore = repeat_.versionBefore;
    const bool notAChange = std::exchange(repeat_.notAChange, false);
    const bool replaying = std::exchange(repeat_.replaying, false);
    ++macros_.depth;
    document_.beginGroup({}, view().place());
    for (int i = 0; i < std::max(count, 1) && !macros_.failed; ++i)
    {
        for (const auto& key : macro)
        {
            if (macros_.failed)
            {
                break;
            }
            recordChange(key, lastTime_);
        }
        // Keys left waiting for a longer binding run, as when their time is up.
        if (const auto deadline = keys_.deadline(); deadline && !macros_.failed)
        {
            const auto& keymap = mode_ == Mode::OperatorPending || mode_ == Mode::Visual
                                     ? operatorKeymapHere()
                                     : keymapHere();
            runAll(keys_.timeout(keymap, *deadline));
        }
        settleNaming(); // its undo group ending within the macro's
    }
    document_.endGroup();
    if (--macros_.depth == 0)
    {
        macros_.failed = false;
    }
    repeat_.changeKeys = std::move(changeKeys);
    repeat_.lastChange = lastChange;
    repeat_.versionBefore = versionBefore;
    repeat_.notAChange = notAChange;
    repeat_.replaying = replaying;
    refresh();
    return true;
}

bool Workspace::sayMacros()
{
    // One a line, as the registers: "@a : 3>>j".
    std::vector<std::pair<std::string, std::string>> lines;
    for (const auto& [name, keys] : remembered_.macros())
    {
        lines.emplace_back(macroName(name), formatKeys(keys));
    }
    if (lines.empty())
    {
        setMessage("No macros recorded", MessageKind::Information);
        return true;
    }
    listing_ = std::move(lines);
    return true;
}

bool Workspace::repeatChange(int count)
{
    // The last change's keys again, as one undo step; a count replaces the one it began with.
    repeat_.notAChange = true;
    if (repeat_.lastChange.empty())
    {
        setMessage("No change yet to repeat", MessageKind::Information);
        return true;
    }
    std::vector<Key> keys = repeat_.lastChange;
    if (count > 0)
    {
        const auto digit = [](const Key& key)
        {
            return key.named == NamedKey::None && key.modifiers == kNoModifiers &&
                   key.character >= U'0' && key.character <= U'9';
        };
        const auto counted = std::ranges::find_if_not(keys, digit);
        keys.erase(keys.begin(), counted);
        const auto digits = std::to_string(count);
        for (auto i = digits.size(); i-- > 0;)
        {
            keys.insert(keys.begin(), Key::typed(static_cast<char32_t>(digits[i])));
        }
    }
    repeat_.replaying = true;
    document_.beginGroup({}, view().place());
    for (const auto& key : keys)
    {
        handleKey(key, lastTime_);
    }
    settleNaming();
    document_.endGroup();
    repeat_.replaying = false;
    refresh();
    return true;
}

} // namespace modal::ui
