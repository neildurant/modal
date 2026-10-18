#pragma once

#include <modal/ui/key.hpp>

#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace modal::ui
{

// Keys written in Vim's notation, as in key bindings, the status line and hints. A typed
// character stands for itself ("j", "$", "é"), except "<", written <lt>, and space, written
// <Space>. Other keys and modified keys are written in angle brackets, with any of the
// modifiers C- (Ctrl), A- or M- (Alt) and S- (Shift) before a key name or a character:
// <Esc>, <Tab>, <CR> (or <Enter>, <Return>), <BS>, <Del>, <Insert>, <Up>, <Down>, <Left>,
// <Right>, <Home>, <End>, <PageUp>, <PageDown>, <F1> to <F12>, the keypad's <kPlus>,
// <kMinus> and <k0> to <k9> (which arrive only with Ctrl or Alt), <C-d>, <C-S-d>, <S-Tab>,
// <A-x>. Names and modifiers are read in any case; with Ctrl or Alt a letter is read in
// either case too, since <C-D> is Ctrl-d as in Vim, and Ctrl-Shift-d is written <C-S-d>.
// <S-a> is read as A, the character it types.

// The keys a string of notation stands for, or why it can't be read.
std::variant<std::vector<Key>, std::string> parseKeys(std::string_view text);

// Keys in notation that parseKeys reads back.
std::string formatKey(const Key& key);
std::string formatKeys(const std::vector<Key>& keys);

} // namespace modal::ui
