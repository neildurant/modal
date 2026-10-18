#pragma once

#include <modal/ui/frame.hpp>
#include <modal/ui/key.hpp>
#include <modal/ui/style.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>

namespace modal::ui::sdl
{

struct FontOptions
{
    std::string file; // a TrueType or OpenType file, or empty for the built-in font
    float points = 12.0f;

    friend bool operator==(const FontOptions&, const FontOptions&) = default;
};

struct WindowOptions
{
    std::string title = "Modal";

    // The size to open at: in cells of its font if columns and rows are set, so that it's the
    // same size relative to its text on any display, or else in the display's units.
    int columns = 0;
    int rows = 0;
    int width = 1280;
    int height = 800;

    // Draws off screen, through SDL's offscreen video driver and software renderer, so tests
    // run without a display.
    bool offscreen = false;

    FontOptions font;

    // The smallest the window can be made, in cells of its font; 0 for no limit.
    int minimumColumns = 0;
    int minimumRows = 0;
};

// A window with a renderer and a monospace font, which draws frames of regions. SDL's video
// subsystem is initialised while it exists, so there is one at a time.
class Window
{
public:
    // Opens a window, or says why it couldn't.
    static std::variant<std::unique_ptr<Window>, std::string> open(const WindowOptions& options);

    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Draws a frame and shows it: the window in the theme's background, then each region in
    // turn, clipped to its bounds (see Region).
    void draw(const Frame& frame, const Theme& theme);

    // Changes the font, or says why it couldn't and keeps the one it had. The minimum size
    // follows the new cell size, and the window grows if it is now below it.
    std::string setFont(const FontOptions& font);
    CellSize cellSize() const;

    // Sets the smallest the window can be made, in cells; 0 for no limit. If the window is
    // smaller, it grows to fit, as far as the screen allows.
    void setMinimumCells(int columns, int rows);

    // Handles events until the window is closed, calling redraw (which draws a frame) only
    // when the window needs one: when it is shown, resized or uncovered, or moved to a display
    // with another scale (when the font is sized again for it first), or when a key press
    // passed to key says it changed what's shown.
    void run(const std::function<void(Window&)>& redraw,
             const std::function<bool(const Key&)>& key = {});

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

    void resizeToCells(int columns, int rows);
    void applyMinimumSize();
    void drawRegion(const Region& region, const Theme& theme);

    std::unique_ptr<Impl> impl_;
};

// Shows an error in a message box, for when there's no console to print it to.
void showError(const std::string& title, const std::string& message);

} // namespace modal::ui::sdl
