// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/block_glyphs.hpp>

#include <algorithm>
#include <array>

namespace modal::ui
{

namespace
{

// The lines from a box drawing character's centre to each edge: 0 none, 1 light, 2 heavy.
struct Arms
{
    char32_t character;
    std::uint8_t left;
    std::uint8_t right;
    std::uint8_t up;
    std::uint8_t down;
};

// Generated from the Unicode character names.
constexpr std::array kArms{
    Arms{0x2500, 1, 1, 0, 0}, // ─ Box Drawings Light Horizontal
    Arms{0x2501, 2, 2, 0, 0}, // ━ Box Drawings Heavy Horizontal
    Arms{0x2502, 0, 0, 1, 1}, // │ Box Drawings Light Vertical
    Arms{0x2503, 0, 0, 2, 2}, // ┃ Box Drawings Heavy Vertical
    Arms{0x250C, 0, 1, 0, 1}, // ┌ Box Drawings Light Down And Right
    Arms{0x250D, 0, 2, 0, 1}, // ┍ Box Drawings Down Light And Right Heavy
    Arms{0x250E, 0, 1, 0, 2}, // ┎ Box Drawings Down Heavy And Right Light
    Arms{0x250F, 0, 2, 0, 2}, // ┏ Box Drawings Heavy Down And Right
    Arms{0x2510, 1, 0, 0, 1}, // ┐ Box Drawings Light Down And Left
    Arms{0x2511, 2, 0, 0, 1}, // ┑ Box Drawings Down Light And Left Heavy
    Arms{0x2512, 1, 0, 0, 2}, // ┒ Box Drawings Down Heavy And Left Light
    Arms{0x2513, 2, 0, 0, 2}, // ┓ Box Drawings Heavy Down And Left
    Arms{0x2514, 0, 1, 1, 0}, // └ Box Drawings Light Up And Right
    Arms{0x2515, 0, 2, 1, 0}, // ┕ Box Drawings Up Light And Right Heavy
    Arms{0x2516, 0, 1, 2, 0}, // ┖ Box Drawings Up Heavy And Right Light
    Arms{0x2517, 0, 2, 2, 0}, // ┗ Box Drawings Heavy Up And Right
    Arms{0x2518, 1, 0, 1, 0}, // ┘ Box Drawings Light Up And Left
    Arms{0x2519, 2, 0, 1, 0}, // ┙ Box Drawings Up Light And Left Heavy
    Arms{0x251A, 1, 0, 2, 0}, // ┚ Box Drawings Up Heavy And Left Light
    Arms{0x251B, 2, 0, 2, 0}, // ┛ Box Drawings Heavy Up And Left
    Arms{0x251C, 0, 1, 1, 1}, // ├ Box Drawings Light Vertical And Right
    Arms{0x251D, 0, 2, 1, 1}, // ┝ Box Drawings Vertical Light And Right Heavy
    Arms{0x251E, 0, 1, 2, 1}, // ┞ Box Drawings Up Heavy And Right Down Light
    Arms{0x251F, 0, 1, 1, 2}, // ┟ Box Drawings Down Heavy And Right Up Light
    Arms{0x2520, 0, 1, 2, 2}, // ┠ Box Drawings Vertical Heavy And Right Light
    Arms{0x2521, 0, 2, 2, 1}, // ┡ Box Drawings Down Light And Right Up Heavy
    Arms{0x2522, 0, 2, 1, 2}, // ┢ Box Drawings Up Light And Right Down Heavy
    Arms{0x2523, 0, 2, 2, 2}, // ┣ Box Drawings Heavy Vertical And Right
    Arms{0x2524, 1, 0, 1, 1}, // ┤ Box Drawings Light Vertical And Left
    Arms{0x2525, 2, 0, 1, 1}, // ┥ Box Drawings Vertical Light And Left Heavy
    Arms{0x2526, 1, 0, 2, 1}, // ┦ Box Drawings Up Heavy And Left Down Light
    Arms{0x2527, 1, 0, 1, 2}, // ┧ Box Drawings Down Heavy And Left Up Light
    Arms{0x2528, 1, 0, 2, 2}, // ┨ Box Drawings Vertical Heavy And Left Light
    Arms{0x2529, 2, 0, 2, 1}, // ┩ Box Drawings Down Light And Left Up Heavy
    Arms{0x252A, 2, 0, 1, 2}, // ┪ Box Drawings Up Light And Left Down Heavy
    Arms{0x252B, 2, 0, 2, 2}, // ┫ Box Drawings Heavy Vertical And Left
    Arms{0x252C, 1, 1, 0, 1}, // ┬ Box Drawings Light Down And Horizontal
    Arms{0x252D, 2, 1, 0, 1}, // ┭ Box Drawings Left Heavy And Right Down Light
    Arms{0x252E, 1, 2, 0, 1}, // ┮ Box Drawings Right Heavy And Left Down Light
    Arms{0x252F, 2, 2, 0, 1}, // ┯ Box Drawings Down Light And Horizontal Heavy
    Arms{0x2530, 1, 1, 0, 2}, // ┰ Box Drawings Down Heavy And Horizontal Light
    Arms{0x2531, 2, 1, 0, 2}, // ┱ Box Drawings Right Light And Left Down Heavy
    Arms{0x2532, 1, 2, 0, 2}, // ┲ Box Drawings Left Light And Right Down Heavy
    Arms{0x2533, 2, 2, 0, 2}, // ┳ Box Drawings Heavy Down And Horizontal
    Arms{0x2534, 1, 1, 1, 0}, // ┴ Box Drawings Light Up And Horizontal
    Arms{0x2535, 2, 1, 1, 0}, // ┵ Box Drawings Left Heavy And Right Up Light
    Arms{0x2536, 1, 2, 1, 0}, // ┶ Box Drawings Right Heavy And Left Up Light
    Arms{0x2537, 2, 2, 1, 0}, // ┷ Box Drawings Up Light And Horizontal Heavy
    Arms{0x2538, 1, 1, 2, 0}, // ┸ Box Drawings Up Heavy And Horizontal Light
    Arms{0x2539, 2, 1, 2, 0}, // ┹ Box Drawings Right Light And Left Up Heavy
    Arms{0x253A, 1, 2, 2, 0}, // ┺ Box Drawings Left Light And Right Up Heavy
    Arms{0x253B, 2, 2, 2, 0}, // ┻ Box Drawings Heavy Up And Horizontal
    Arms{0x253C, 1, 1, 1, 1}, // ┼ Box Drawings Light Vertical And Horizontal
    Arms{0x253D, 2, 1, 1, 1}, // ┽ Box Drawings Left Heavy And Right Vertical Light
    Arms{0x253E, 1, 2, 1, 1}, // ┾ Box Drawings Right Heavy And Left Vertical Light
    Arms{0x253F, 2, 2, 1, 1}, // ┿ Box Drawings Vertical Light And Horizontal Heavy
    Arms{0x2540, 1, 1, 2, 1}, // ╀ Box Drawings Up Heavy And Down Horizontal Light
    Arms{0x2541, 1, 1, 1, 2}, // ╁ Box Drawings Down Heavy And Up Horizontal Light
    Arms{0x2542, 1, 1, 2, 2}, // ╂ Box Drawings Vertical Heavy And Horizontal Light
    Arms{0x2543, 2, 1, 2, 1}, // ╃ Box Drawings Left Up Heavy And Right Down Light
    Arms{0x2544, 1, 2, 2, 1}, // ╄ Box Drawings Right Up Heavy And Left Down Light
    Arms{0x2545, 2, 1, 1, 2}, // ╅ Box Drawings Left Down Heavy And Right Up Light
    Arms{0x2546, 1, 2, 1, 2}, // ╆ Box Drawings Right Down Heavy And Left Up Light
    Arms{0x2547, 2, 2, 2, 1}, // ╇ Box Drawings Down Light And Up Horizontal Heavy
    Arms{0x2548, 2, 2, 1, 2}, // ╈ Box Drawings Up Light And Down Horizontal Heavy
    Arms{0x2549, 2, 1, 2, 2}, // ╉ Box Drawings Right Light And Left Vertical Heavy
    Arms{0x254A, 1, 2, 2, 2}, // ╊ Box Drawings Left Light And Right Vertical Heavy
    Arms{0x254B, 2, 2, 2, 2}, // ╋ Box Drawings Heavy Vertical And Horizontal
    Arms{0x2574, 1, 0, 0, 0}, // ╴ Box Drawings Light Left
    Arms{0x2575, 0, 0, 1, 0}, // ╵ Box Drawings Light Up
    Arms{0x2576, 0, 1, 0, 0}, // ╶ Box Drawings Light Right
    Arms{0x2577, 0, 0, 0, 1}, // ╷ Box Drawings Light Down
    Arms{0x2578, 2, 0, 0, 0}, // ╸ Box Drawings Heavy Left
    Arms{0x2579, 0, 0, 2, 0}, // ╹ Box Drawings Heavy Up
    Arms{0x257A, 0, 2, 0, 0}, // ╺ Box Drawings Heavy Right
    Arms{0x257B, 0, 0, 0, 2}, // ╻ Box Drawings Heavy Down
    Arms{0x257C, 1, 2, 0, 0}, // ╼ Box Drawings Light Left And Heavy Right
    Arms{0x257D, 0, 0, 1, 2}, // ╽ Box Drawings Light Up And Heavy Down
    Arms{0x257E, 2, 1, 0, 0}, // ╾ Box Drawings Heavy Left And Light Right
    Arms{0x257F, 0, 0, 2, 1}, // ╿ Box Drawings Heavy Up And Light Down
};

// k eighths of a length, rounded to whole pixels.
int eighths(int length, int k)
{
    return (length * k + 4) / 8;
}

std::vector<CellRect> lines(const Arms& arms, int width, int height)
{
    const int light = std::max(1, eighths(width, 1));
    const auto thickness = [light](int weight) { return weight == 2 ? light * 2 : light; };

    // Each line runs past the centre to the far side of the widest line crossing it, so they
    // join without a notch.
    const int across = thickness(std::max<int>(arms.up, arms.down));   // vertical lines' width
    const int along = thickness(std::max<int>(arms.left, arms.right)); // horizontal lines' height
    const int middleX = (width - across) / 2;
    const int middleY = (height - along) / 2;

    std::vector<CellRect> rects;
    const auto horizontal = [&](int weight, int from, int to)
    {
        const int t = thickness(weight);
        rects.push_back({from, (height - t) / 2, to - from, t});
    };
    const auto vertical = [&](int weight, int from, int to)
    {
        const int t = thickness(weight);
        rects.push_back({(width - t) / 2, from, t, to - from});
    };
    if (arms.left != 0)
    {
        horizontal(arms.left, 0, middleX + across);
    }
    if (arms.right != 0)
    {
        horizontal(arms.right, middleX, width);
    }
    if (arms.up != 0)
    {
        vertical(arms.up, 0, middleY + along);
    }
    if (arms.down != 0)
    {
        vertical(arms.down, middleY, height);
    }
    return rects;
}

std::vector<CellRect> block(char32_t character, int width, int height)
{
    const int leftHalf = eighths(width, 4);
    const int topHalf = height - eighths(height, 4);
    const CellRect upperLeft{0, 0, leftHalf, topHalf};
    const CellRect upperRight{leftHalf, 0, width - leftHalf, topHalf};
    const CellRect lowerLeft{0, topHalf, leftHalf, height - topHalf};
    const CellRect lowerRight{leftHalf, topHalf, width - leftHalf, height - topHalf};

    if (character >= U'\u2581' && character <= U'\u2588') // lower one to eight eighths
    {
        const int h = eighths(height, static_cast<int>(character - U'\u2580'));
        return {{0, height - h, width, h}};
    }
    if (character >= U'\u2589' && character <= U'\u258F') // left seven to one eighths
    {
        return {{0, 0, eighths(width, static_cast<int>(U'\u2590' - character)), height}};
    }
    switch (character)
    {
    case U'\u2580': // upper half
        return {{0, 0, width, topHalf}};
    case U'\u2590': // right half
        return {{leftHalf, 0, width - leftHalf, height}};
    case U'\u2591': // light, medium and dark shade
        return {{0, 0, width, height, 64}};
    case U'\u2592':
        return {{0, 0, width, height, 128}};
    case U'\u2593':
        return {{0, 0, width, height, 192}};
    case U'\u2594': // upper one eighth
        return {{0, 0, width, eighths(height, 1)}};
    case U'\u2595': // right one eighth
    {
        const int w = eighths(width, 1);
        return {{width - w, 0, w, height}};
    }
    case U'\u2596':
        return {lowerLeft};
    case U'\u2597':
        return {lowerRight};
    case U'\u2598':
        return {upperLeft};
    case U'\u2599':
        return {upperLeft, lowerLeft, lowerRight};
    case U'\u259A':
        return {upperLeft, lowerRight};
    case U'\u259B':
        return {upperLeft, upperRight, lowerLeft};
    case U'\u259C':
        return {upperLeft, upperRight, lowerRight};
    case U'\u259D':
        return {upperRight};
    case U'\u259E':
        return {upperRight, lowerLeft};
    case U'\u259F':
        return {upperRight, lowerLeft, lowerRight};
    default:
        return {};
    }
}

} // namespace

std::vector<CellRect> blockGlyph(char32_t character, int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return {};
    }
    if (character >= U'\u2580' && character <= U'\u259F')
    {
        return block(character, width, height);
    }
    const auto it = std::ranges::find(kArms, character, &Arms::character);
    return it != kArms.end() ? lines(*it, width, height) : std::vector<CellRect>{};
}

} // namespace modal::ui
