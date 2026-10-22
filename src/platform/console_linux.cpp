// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/platform/console.hpp>

#include <termios.h>
#include <unistd.h>

namespace modal::platform
{

struct ConsoleKeys::Impl
{
    termios saved{};
};

ConsoleKeys::ConsoleKeys()
{
    termios mode{};
    if (isatty(STDIN_FILENO) == 0 || tcgetattr(STDIN_FILENO, &mode) != 0)
    {
        return;
    }
    impl_ = std::make_unique<Impl>(Impl{mode});
    mode.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO); // ISIG stays, for Ctrl-C
    mode.c_cc[VMIN] = 0;
    mode.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &mode);
}

ConsoleKeys::~ConsoleKeys()
{
    if (impl_)
    {
        tcsetattr(STDIN_FILENO, TCSANOW, &impl_->saved);
    }
}

std::optional<char> ConsoleKeys::poll()
{
    constexpr char kEscape = 0x1B;
    const auto next = [&]() -> std::optional<char>
    {
        char key = 0;
        if (impl_ && read(STDIN_FILENO, &key, 1) == 1)
        {
            return key;
        }
        return std::nullopt;
    };

    for (auto key = next(); key; key = next())
    {
        if (*key != kEscape)
        {
            return key;
        }
        // Arrow and function keys arrive as Esc [ or Esc O and more, all at once; a lone Esc
        // is the key itself.
        const auto introducer = next();
        if (!introducer)
        {
            return key;
        }
        if (*introducer != '[' && *introducer != 'O')
        {
            return introducer; // Alt and a key: take the key
        }
        for (auto part = next(); part && (*part < 0x40 || *part > 0x7E); part = next())
        {
        }
    }
    return std::nullopt;
}

} // namespace modal::platform
