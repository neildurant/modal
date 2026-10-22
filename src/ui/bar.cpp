// SPDX-License-Identifier: GPL-3.0-or-later

#include <modal/ui/bar.hpp>

#include <algorithm>
#include <utility>

namespace modal::ui
{

namespace
{

constexpr char32_t kEllipsis = 0x2026;

struct Piece
{
    std::u32string text;
    Style style;
};

std::vector<Piece> decode(const std::vector<Segment>& segments)
{
    std::vector<Piece> pieces;
    for (const auto& segment : segments)
    {
        auto text = decodeUtf8(segment.text);
        if (!text.empty())
        {
            pieces.push_back({std::move(text), segment.style});
        }
    }
    return pieces;
}

int characters(const std::vector<Piece>& pieces)
{
    int count = 0;
    for (const auto& piece : pieces)
    {
        count += static_cast<int>(piece.text.size());
    }
    return count;
}

// Keeps what fits in a number of characters from the start, cutting the last piece that
// doesn't fit short with an ellipsis, or dropping it if too little of it would show.
std::vector<Piece> keepStart(std::vector<Piece> pieces, int room)
{
    std::vector<Piece> kept;
    for (auto& piece : pieces)
    {
        const auto length = static_cast<int>(piece.text.size());
        if (length <= room)
        {
            room -= length;
            kept.push_back(std::move(piece));
            continue;
        }
        const int shown = std::min(room - 1, length - 1); // leaving room for the ellipsis
        if (shown >= kMinSegmentCharacters)
        {
            piece.text.resize(static_cast<std::size_t>(shown));
            piece.text.push_back(kEllipsis);
            kept.push_back(std::move(piece));
        }
        break;
    }
    return kept;
}

// As keepStart, from the end: the first piece that doesn't fit is cut from its start.
std::vector<Piece> keepEnd(std::vector<Piece> pieces, int room)
{
    std::ranges::reverse(pieces);
    for (auto& piece : pieces)
    {
        std::ranges::reverse(piece.text);
    }
    auto kept = keepStart(std::move(pieces), room);
    std::ranges::reverse(kept);
    for (auto& piece : kept)
    {
        std::ranges::reverse(piece.text);
    }
    return kept;
}

void place(Region& region, const std::vector<Piece>& pieces, int x, CellSize cell)
{
    for (const auto& piece : pieces)
    {
        region.text.push_back({x, 0, piece.text, piece.style});
        x += static_cast<int>(piece.text.size()) * cell.width;
    }
}

} // namespace

Region layoutBar(const Bar& bar, int width, CellSize cell, BarSpacing spacing)
{
    Region region{.bounds = {0, 0, width, cell.height},
                  .background = bar.background,
                  .text = {},
                  .under = {},
                  .over = {}};
    if (cell.width <= 0)
    {
        return region;
    }
    const auto columnsIn = [&](int pixels) { return std::max(pixels, 0) / cell.width; };

    // The right group, cut from its left end if it can't fit at all.
    auto right = keepEnd(decode(bar.right), columnsIn(width - 2 * spacing.margin));
    const int rightWidth = characters(right) * cell.width;
    const int rightX = width - spacing.margin - rightWidth;
    const int leftLimit = right.empty() ? width - spacing.margin : rightX - spacing.gap;

    auto left = decode(bar.left);
    auto centre = decode(bar.centre);
    const int leftWidth = characters(left) * cell.width;
    const int centreWidth = characters(centre) * cell.width;
    const int centreX = (width - centreWidth) / 2;
    const bool centreFits =
        !centre.empty() &&
        centreX >= spacing.margin + leftWidth + (left.empty() ? 0 : spacing.gap) &&
        centreX + centreWidth <= leftLimit;

    left = keepStart(std::move(left), columnsIn(leftLimit - spacing.margin));
    place(region, left, spacing.margin, cell);
    if (centreFits)
    {
        place(region, centre, centreX, cell);
    }
    place(region, right, rightX, cell);
    return region;
}

} // namespace modal::ui
