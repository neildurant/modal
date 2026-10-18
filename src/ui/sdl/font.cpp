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

constexpr char32_t kReplacement = 0xFFFD;
constexpr char32_t kBoldBit = 0x80000000;

TTF_Font* openFile(const FontOptions& options, bool bold)
{
    if (options.file.empty())
    {
        const auto bytes = bold ? builtInBoldFont() : builtInFont();
        return TTF_OpenFontIO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true, 12.0f);
    }
    TTF_Font* font = TTF_OpenFont(options.file.c_str(), 12.0f);
    if (font != nullptr && bold)
    {
        TTF_SetFontStyle(font, TTF_STYLE_BOLD);
    }
    return font;
}

} // namespace

std::variant<std::unique_ptr<Font>, std::string>
Font::open(SDL_Renderer* renderer, const FontOptions& options, float displayScale)
{
    std::array<TTF_Font*, 2> fonts{openFile(options, false), nullptr};
    if (fonts[0] != nullptr)
    {
        fonts[1] = openFile(options, true);
    }
    const auto fail = [&](const std::string& what)
    {
        const std::string error = SDL_GetError();
        for (auto* font : fonts)
        {
            if (font != nullptr)
            {
                TTF_CloseFont(font);
            }
        }
        return what + ": " + error;
    };
    if (fonts[0] == nullptr || fonts[1] == nullptr)
    {
        return fail(std::string{"can't open the font"} +
                    (options.file.empty() ? "" : " " + options.file));
    }

    const auto dpi = static_cast<int>(std::lround(kBaseDpi * std::max(displayScale, 0.25f)));
    int advance = 0;
    if (!TTF_SetFontSizeDPI(fonts[0], options.points, dpi, dpi) ||
        !TTF_SetFontSizeDPI(fonts[1], options.points, dpi, dpi) ||
        !TTF_GetGlyphMetrics(fonts[0], 'M', nullptr, nullptr, nullptr, nullptr, &advance) ||
        advance <= 0)
    {
        return fail("can't size the font");
    }
    // The cell is the font's whole height, ascent to descent, so box drawing joins up.
    const CellSize cell{advance, std::max(TTF_GetFontHeight(fonts[0]), 1)};
    std::unique_ptr<Font> font{new Font{renderer, fonts[0], fonts[1], cell}};
    font->underlineThickness_ = std::max(1, (cell.width + 4) / 8);
    font->underlineTop_ = std::min(TTF_GetFontAscent(fonts[0]) + font->underlineThickness_,
                                   cell.height - font->underlineThickness_);
    return font;
}

Font::Font(SDL_Renderer* renderer, TTF_Font* regular, TTF_Font* bold, CellSize cell)
    : renderer_(renderer)
    , fonts_{regular, bold}
    , cell_(cell)
{
}

Font::~Font()
{
    for (auto* page : pages_)
    {
        SDL_DestroyTexture(page);
    }
    for (auto* font : fonts_)
    {
        TTF_CloseFont(font);
    }
}

void Font::draw(char32_t character, bool bold, float x, float y, Colour colour)
{
    const auto& found = slot(character, bold);
    if (found.page == nullptr)
    {
        return;
    }
    SDL_SetTextureColorMod(found.page, colour.r, colour.g, colour.b);
    const SDL_FRect target{x, y, found.rect.w, found.rect.h};
    SDL_RenderTexture(renderer_, found.page, &found.rect, &target);
}

const Font::Slot& Font::slot(char32_t character, bool bold)
{
    const char32_t key = bold ? (character | kBoldBit) : character;
    if (const auto it = slots_.find(key); it != slots_.end())
    {
        return it->second;
    }
    return slots_.emplace(key, render(character, bold)).first->second;
}

Font::Slot Font::render(char32_t character, bool bold)
{
    if (character == U' ')
    {
        return {};
    }
    // Box drawing and blocks are the same in either weight.
    if (const auto rects = blockGlyph(character, cell_.width, cell_.height); !rects.empty())
    {
        if (bold)
        {
            return slot(character, false);
        }
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

    TTF_Font* font = fonts_[bold ? 1 : 0];
    if (!TTF_FontHasGlyph(font, character))
    {
        if (character == U'?')
        {
            return {};
        }
        const bool replacement = character != kReplacement && TTF_FontHasGlyph(font, kReplacement);
        return slot(replacement ? kReplacement : U'?', bold);
    }

    // As one-character text rather than a glyph, so it keeps its side bearing and baseline.
    char utf8[5] = {};
    SDL_UCS4ToUTF8(character, utf8);
    SDL_Surface* rendered = TTF_RenderText_Blended(font, utf8, 0, SDL_Color{255, 255, 255, 255});
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
