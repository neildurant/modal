#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

struct FontOptions
{
    std::string file; // a TrueType or OpenType file, or empty for the built-in font
    float points = 12.0f;

    friend bool operator==(const FontOptions&, const FontOptions&) = default;
};

// The size of a grid cell, in pixels.
struct CellSize
{
    int width = 0;
    int height = 0;

    friend bool operator==(CellSize, CellSize) = default;
};

struct WindowOptions
{
    std::string title = "Modal";
    int width = 1280; // in the display's logical units, before any scaling
    int height = 800;

    // Draws off screen, through SDL's offscreen video driver and software renderer, so tests
    // run without a display.
    bool offscreen = false;

    FontOptions font;
};

// A window with a renderer and a monospace font, drawn as a grid of cells. SDL's video
// subsystem is initialised while it exists, so there is one at a time.
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

    // Draws UTF-8 text from a cell, one character to a cell, during drawFrame.
    void drawText(int column, int row, std::string_view text, Colour colour);

    // Changes the font, or says why it couldn't and keeps the one it had.
    std::string setFont(const FontOptions& font);
    CellSize cellSize() const;

    // Handles events until the window is closed, calling redraw (which draws a frame) only
    // when the window needs one: when it is shown, resized or uncovered, or moved to a display
    // with another scale (when the font is sized again for it first).
    void run(const std::function<void(Window&)>& redraw);

    // For offscreen windows, the colour of a pixel in the last frame drawn, in pixels.
    std::optional<Colour> pixel(int x, int y) const;

    // For offscreen windows, saves the last frame drawn as a BMP file; false if it can't.
    bool saveFrame(const std::string& path) const;

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
