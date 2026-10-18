#include "font.hpp"

#include <modal/ui/block_glyphs.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace modal::ui::sdl
{

namespace
{

// Font sizes are in points at 96 dots per inch, before the display's scale, as on Windows.
constexpr float kBaseDpi = 96.0f;

TTF_Font* openFile(const FontOptions& options)
{
    if (options.file.empty())
    {
        const auto bytes = builtInFont();
        return TTF_OpenFontIO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true, 12.0f);
    }
    return TTF_OpenFont(options.file.c_str(), 12.0f);
}

} // namespace

std::variant<std::unique_ptr<Font>, std::string>
Font::open(SDL_Renderer* renderer, const FontOptions& options, float displayScale)
{
    TTF_Font* font = openFile(options);
    if (font == nullptr)
    {
        return std::string{"can't open the font"} +
               (options.file.empty() ? "" : " " + options.file) + ": " + SDL_GetError();
    }
    const auto dpi = static_cast<int>(std::lround(kBaseDpi * std::max(displayScale, 0.25f)));
    int advance = 0;
    if (!TTF_SetFontSizeDPI(font, options.points, dpi, dpi) ||
        !TTF_GetGlyphMetrics(font, 'M', nullptr, nullptr, nullptr, nullptr, &advance) ||
        advance <= 0)
    {
        const std::string error = SDL_GetError();
        TTF_CloseFont(font);
        return "can't size the font: " + error;
    }
    // The cell is the font's whole height, ascent to descent, so box drawing joins up.
    const CellSize cell{advance, std::max(TTF_GetFontHeight(font), 1)};
    return std::unique_ptr<Font>{new Font{renderer, font, cell}};
}

Font::Font(SDL_Renderer* renderer, TTF_Font* font, CellSize cell)
    : renderer_(renderer)
    , font_(font)
    , cell_(cell)
{
}

Font::~Font()
{
    for (auto* page : pages_)
    {
        SDL_DestroyTexture(page);
    }
    TTF_CloseFont(font_);
}

void Font::draw(char32_t character, float x, float y, Colour colour)
{
    const auto& found = slot(character);
    if (found.page == nullptr)
    {
        return;
    }
    SDL_SetTextureColorMod(found.page, colour.r, colour.g, colour.b);
    const SDL_FRect target{x, y, found.rect.w, found.rect.h};
    SDL_RenderTexture(renderer_, found.page, &found.rect, &target);
}

const Font::Slot& Font::slot(char32_t character)
{
    if (const auto it = slots_.find(character); it != slots_.end())
    {
        return it->second;
    }
    return slots_.emplace(character, render(character)).first->second;
}

Font::Slot Font::render(char32_t character)
{
    if (character == U' ')
    {
        return {};
    }
    if (const auto rects = blockGlyph(character, cell_.width, cell_.height); !rects.empty())
    {
        SDL_Surface* surface =
            SDL_CreateSurface(cell_.width, cell_.height, SDL_PIXELFORMAT_ARGB8888);
        if (surface == nullptr)
        {
            return {};
        }
        SDL_FillSurfaceRect(surface, nullptr, 0); // transparent
        for (const auto& r : rects)
        {
            const SDL_Rect rect{r.x, r.y, r.width, r.height};
            SDL_FillSurfaceRect(surface, &rect,
                                SDL_MapSurfaceRGBA(surface, 255, 255, 255, r.alpha));
        }
        return store(surface);
    }
    if (!TTF_FontHasGlyph(font_, character))
    {
        if (character == U'?')
        {
            return {};
        }
        const bool replacement = character != U'\uFFFD' && TTF_FontHasGlyph(font_, U'\uFFFD');
        return slot(replacement ? U'\uFFFD' : U'?');
    }

    // As one-character text rather than a glyph, so it keeps its side bearing and baseline.
    char utf8[5] = {};
    SDL_UCS4ToUTF8(character, utf8);
    SDL_Surface* rendered = TTF_RenderText_Blended(font_, utf8, 0, SDL_Color{255, 255, 255, 255});
    if (rendered == nullptr)
    {
        return {};
    }
    SDL_Surface* converted = SDL_ConvertSurface(rendered, SDL_PIXELFORMAT_ARGB8888);
    SDL_DestroySurface(rendered);
    if (converted == nullptr)
    {
        return {};
    }
    return store(converted);
}

Font::Slot Font::store(SDL_Surface* glyph)
{
    if (pages_.empty() || used_ == kPageCells * kPageCells)
    {
        SDL_Texture* page =
            SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC,
                              cell_.width * kPageCells, cell_.height * kPageCells);
        if (page == nullptr)
        {
            SDL_DestroySurface(glyph);
            return {};
        }
        SDL_SetTextureBlendMode(page, SDL_BLENDMODE_BLEND);
        pages_.push_back(page);
        used_ = 0;
    }
    const SDL_Rect rect{(used_ % kPageCells) * cell_.width, (used_ / kPageCells) * cell_.height,
                        std::min(glyph->w, cell_.width), std::min(glyph->h, cell_.height)};
    ++used_;
    SDL_UpdateTexture(pages_.back(), &rect, glyph->pixels, glyph->pitch);
    SDL_DestroySurface(glyph);
    return {pages_.back(), SDL_FRect{static_cast<float>(rect.x), static_cast<float>(rect.y),
                                     static_cast<float>(rect.w), static_cast<float>(rect.h)}};
}

} // namespace modal::ui::sdl
