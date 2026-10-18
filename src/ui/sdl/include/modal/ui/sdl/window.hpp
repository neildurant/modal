#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace modal::ui::sdl
{

struct Colour
{
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend bool operator==(Colour, Colour) = default;
};

struct WindowOptions
{
    std::string title = "Modal";
    int width = 1280; // in the display's logical units, before any scaling
    int height = 800;

    // Draws off screen, through SDL's offscreen video driver and software renderer, so tests
    // run without a display.
    bool offscreen = false;
};

// A window with a renderer. SDL's video subsystem is initialised while it exists, so there
// is one at a time.
class Window
{
public:
    // Opens a window, or says why it couldn't.
    static std::variant<std::unique_ptr<Window>, std::string> open(const WindowOptions& options);

    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Draws a frame and shows it: fills the window with a colour, then calls draw.
    void drawFrame(Colour background, const std::function<void()>& draw = {});

    // Handles events until the window is closed, calling redraw (which draws a frame) only
    // when the window needs one: when it is shown, resized or uncovered.
    void run(const std::function<void(Window&)>& redraw);

    // For offscreen windows, the colour of a pixel in the last frame drawn, in pixels.
    std::optional<Colour> pixel(int x, int y) const;

    // The size of the window's drawing area, in pixels.
    int pixelWidth() const;
    int pixelHeight() const;

private:
    struct Impl;
    explicit Window(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

// Shows an error in a message box, for when there's no console to print it to.
void showError(const std::string& title, const std::string& message);

} // namespace modal::ui::sdl
