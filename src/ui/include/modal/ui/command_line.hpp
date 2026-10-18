#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// The line typed at the : prompt, with a cursor in it, and the lines run before, to recall,
// as in Vim's command line. History lasts the session.
class CommandLine
{
public:
    // The text, and the cursor's place in it, in characters from 0 to the text's length.
    const std::u32string& text() const { return text_; }
    std::size_t cursor() const { return cursor_; }

    // Starts a new, empty line.
    void open();

    void insert(char32_t character);
    // Deletes the character before the cursor, or after it; false if there's none.
    bool backspace();
    bool erase();
    void left();
    void right();
    void start();
    void end();
    void clear();

    // Recalls the line run before the one shown, or after it, returning to the line being
    // typed after the newest. False if there's none further.
    bool older();
    bool newer();

    // The line, as UTF-8, to run; it's added to the history, unless empty or the same as the
    // newest line there.
    std::string take();

private:
    void show(std::u32string text);

    std::u32string text_;
    std::size_t cursor_ = 0;
    std::vector<std::u32string> history_;
    std::size_t recalled_ = 0; // the history line shown, or history_.size() if none
    std::u32string draft_;     // the line being typed, kept while recalling others
};

} // namespace modal::ui
