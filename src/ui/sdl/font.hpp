#pragma once

#include <modal/ui/sdl/window.hpp>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace modal::ui::sdl
{

// The built-in font, JetBrains Mono NL (see assets/fonts).
std::span<const unsigned char> builtInFont();

// A monospace font at one size, with its glyphs rendered into textures (an atlas) as they're
// first drawn, so each cell after that is one copy from a texture.
class Font
{
public:
    // Opens a font for a renderer, at a size in points scaled by the display's scale, or says
    // why it couldn't.
    static std::variant<std::unique_ptr<Font>, std::string>
    open(SDL_Renderer* renderer, const FontOptions& options, float displayScale);

    ~Font();

    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    CellSize cell() const { return cell_; }

    // Draws a character in the cell whose top left corner is at (x, y), in pixels. A
    // character the font doesn't have is drawn as U+FFFD, or ? if it hasn't that either.
    void draw(char32_t character, float x, float y, Colour colour);

private:
    // Where a glyph is in the atlas; no page for a glyph that draws nothing, such as a space.
    struct Slot
    {
        SDL_Texture* page = nullptr;
        SDL_FRect rect{};
    };

    Font(SDL_Renderer* renderer, TTF_Font* font, CellSize cell);
    const Slot& slot(char32_t character);
    Slot render(char32_t character);

    // Copies a white glyph, in ARGB8888, into the atlas, and frees it.
    Slot store(SDL_Surface* glyph);

    static constexpr int kPageCells = 32; // a page holds 32 x 32 glyphs

    SDL_Renderer* renderer_;
    TTF_Font* font_;
    CellSize cell_;
    std::unordered_map<char32_t, Slot> slots_;
    std::vector<SDL_Texture*> pages_;
    int used_ = 0; // cells used in the last page
};

} // namespace modal::ui::sdl
