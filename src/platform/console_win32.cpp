#include <modal/platform/console.hpp>

#include <conio.h>

namespace modal::platform
{

// The console needs no mode change: _getch() reads a key without echo or Enter.
struct ConsoleKeys::Impl
{
};

ConsoleKeys::ConsoleKeys() = default;
ConsoleKeys::~ConsoleKeys() = default;

std::optional<char> ConsoleKeys::poll()
{
    while (_kbhit() != 0)
    {
        const int key = _getch();
        if (key == 0 || key == 0xE0)
        {
            _getch(); // the second half of an arrow or function key
            continue;
        }
        return static_cast<char>(key);
    }
    return std::nullopt;
}

} // namespace modal::platform
