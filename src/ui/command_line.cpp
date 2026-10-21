#include <modal/ui/command_line.hpp>
#include <modal/ui/frame.hpp>

#include <utility>

namespace modal::ui
{

void CommandLine::open()
{
    text_.clear();
    cursor_ = 0;
    recalled_ = history_.size();
    draft_.clear();
}

void CommandLine::open(std::u32string text)
{
    open();
    text_ = std::move(text);
    cursor_ = text_.size();
}

void CommandLine::insert(char32_t character)
{
    text_.insert(cursor_++, 1, character);
}

bool CommandLine::backspace()
{
    if (cursor_ == 0)
    {
        return false;
    }
    text_.erase(--cursor_, 1);
    return true;
}

bool CommandLine::erase()
{
    if (cursor_ == text_.size())
    {
        return false;
    }
    text_.erase(cursor_, 1);
    return true;
}

void CommandLine::left()
{
    cursor_ -= cursor_ > 0 ? 1 : 0;
}

void CommandLine::right()
{
    cursor_ += cursor_ < text_.size() ? 1 : 0;
}

void CommandLine::start()
{
    cursor_ = 0;
}

void CommandLine::end()
{
    cursor_ = text_.size();
}

void CommandLine::clear()
{
    text_.clear();
    cursor_ = 0;
}

void CommandLine::show(std::u32string text)
{
    text_ = std::move(text);
    cursor_ = text_.size();
}

bool CommandLine::older()
{
    if (recalled_ == 0)
    {
        return false;
    }
    if (recalled_ == history_.size())
    {
        draft_ = text_;
    }
    show(history_[--recalled_]);
    return true;
}

bool CommandLine::newer()
{
    if (recalled_ >= history_.size())
    {
        return false;
    }
    ++recalled_;
    show(recalled_ == history_.size() ? draft_ : history_[recalled_]);
    return true;
}

std::string CommandLine::take()
{
    auto line = std::exchange(text_, {});
    cursor_ = 0;
    if (!line.empty() && (history_.empty() || history_.back() != line))
    {
        history_.push_back(line);
    }
    recalled_ = history_.size();
    return encodeUtf8(line);
}

} // namespace modal::ui
