#pragma once

#include <memory>
#include <optional>

namespace modal::platform
{

// Reads single key presses from the terminal without waiting for Enter, and without echoing
// them, while it exists; destroying it restores the terminal. Ctrl-C still raises SIGINT.
// If input isn't a terminal, no keys arrive.
class ConsoleKeys
{
public:
    ConsoleKeys();
    ~ConsoleKeys();

    ConsoleKeys(const ConsoleKeys&) = delete;
    ConsoleKeys& operator=(const ConsoleKeys&) = delete;

    // The next key pressed, if any, without waiting. Keys that aren't characters (arrows,
    // function keys) are skipped.
    std::optional<char> poll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace modal::platform
