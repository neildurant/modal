#include "support/edit_track.hpp"

#include <modal/core/event.hpp>
#include <modal/core/song.hpp>
#include <modal/ui/arrange_page.hpp>
#include <modal/ui/demo_song.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace modal;
using namespace modal::ui;

namespace
{

constexpr CellSize kCell{10, 20};

// The demo song's page in a main area of 80 by 22 cells: 60 columns of timeline, and 19
// tracks' rows below the ruler.
ArrangePage page80x22()
{
    ArrangePage page{demoSong()};
    page.resize({0, kCell.height, 80 * kCell.width, 22 * kCell.height}, kCell);
    page.zoomBy(1); // to a half note to a column, as these tests measure
    return page;
}

// Moves as the test view's default key bindings do, with a count (0 for none).
void type(ArrangePage& page, std::u32string_view keys, int count = 0)
{
    const int n = std::max(count, 1);
    for (const char32_t key : keys)
    {
        switch (key)
        {
        case U'h':
            page.moveBars(-n);
            break;
        case U'l':
            page.moveBars(n);
            break;
        case U'j':
            page.moveTracks(n);
            break;
        case U'k':
            page.moveTracks(-n);
            break;
        case U'0':
            page.goToBar(1);
            break;
        case U'$':
            page.goToLastBar();
            break;
        case U'G':
            count > 0 ? page.goToBar(count) : page.goToLastBar();
            break;
        case U'+':
            page.zoomBy(-n);
            break;
        case U'-':
            page.zoomBy(n);
            break;
        default:
            FAIL("no such key in these tests");
        }
    }
}

// The shapes in a role across a track's row, in the timeline region.
std::vector<Shape> rowShapes(const Region& timeline, int track, Role role)
{
    std::vector<Shape> shapes;
    const int top = (ArrangePage::kRulerRows + track) * kCell.height;
    for (const auto& shape : timeline.under)
    {
        if (shape.role == role && shape.rect.y >= top && shape.rect.y < top + kCell.height)
        {
            shapes.push_back(shape);
        }
    }
    return shapes;
}

bool isDensity(Role role)
{
    return role == Role::Density1 || role == Role::Density2 || role == Role::Density3 ||
           role == Role::Density4;
}

} // namespace

TEST_CASE("density goes from sparse to busy by the notes starting to a quarter note")
{
    const auto ppqn = core::kPpqn;
    const auto bar = core::NoteFraction{1, 1}.toTicks(ppqn); // four quarter notes
    CHECK(ArrangePage::density(0, bar, ppqn) == 0);
    CHECK(ArrangePage::density(1, bar, ppqn) == 1);
    CHECK(ArrangePage::density(2, bar, ppqn) == 1);
    CHECK(ArrangePage::density(3, bar, ppqn) == 2); // 0.75 to a quarter note
    CHECK(ArrangePage::density(5, bar, ppqn) == 2);
    CHECK(ArrangePage::density(6, bar, ppqn) == 3);
    CHECK(ArrangePage::density(11, bar, ppqn) == 3);
    CHECK(ArrangePage::density(12, bar, ppqn) == 4);
    CHECK(ArrangePage::density(64, bar, ppqn) == 4);
}

TEST_CASE("the arrangement's cursor moves by each track's bars, and between tracks in time")
{
    auto page = page80x22();
    const auto song = demoSong();
    const auto& drums = song.meter(song.tracks()[0]->id());

    type(page, U"l", 8); // to bar 9, the verse in 7/8
    CHECK(page.track() == 0);
    CHECK(page.bar() == 9);
    CHECK(page.time() == drums.barStart(9));

    // 32 quarter notes in, where the ostinato's bars start again with the verse: ten bars of
    // 3/4 and a 2/4 remainder bar make up the intro, so this is its bar 12.
    type(page, U"j", 6);
    CHECK(page.track() == 6);
    CHECK(page.bar() == 12);
    type(page, U"k", 6); // back where it was, since the time is kept
    CHECK(page.track() == 0);
    CHECK(page.bar() == 9);

    // Moving by the ostinato's bars moves the time: its remainder bar starts in the drums'
    // bar 8.
    type(page, U"j", 6);
    type(page, U"l");
    CHECK(page.bar() == 13);
    type(page, U"h", 2);
    CHECK(page.bar() == 11);
    type(page, U"k", 6);
    CHECK(page.bar() == 8);

    type(page, U"$");
    CHECK(page.bar() == 53);
    type(page, U"j", 6);
    CHECK(page.bar() == 68);
    type(page, U"$");
    CHECK(page.bar() == 69); // the last 3/4 bar, cut short by the song's end
    type(page, U"0");
    CHECK(page.bar() == 1);
    type(page, U"G", 20);
    CHECK(page.bar() == 20);
    type(page, U"G");
    CHECK(page.bar() == 69);

    type(page, U"j", 100);
    CHECK(page.track() == 13);
    type(page, U"h", 1000);
    CHECK(page.bar() == 1);
}

TEST_CASE("the arrangement scrolls in whole columns to keep the cursor's bar in view")
{
    auto page = page80x22();
    CHECK(page.leftTick() == 0);
    const auto column = ArrangePage::kZooms[page.zoom()].toTicks(core::kPpqn);

    type(page, U"$");
    CHECK(page.leftTick() > 0);
    CHECK(page.leftTick() % column == 0);
    // The song's end at the view's right edge: 205 quarter notes, in columns of two.
    CHECK(page.leftTick() == (103 - 60) * column);

    type(page, U"0");
    CHECK(page.leftTick() == 0);
}

TEST_CASE("zooming changes the time in a column, within its limits, and back to a quarter")
{
    ArrangePage page{demoSong()};
    page.resize({0, kCell.height, 80 * kCell.width, 22 * kCell.height}, kCell);
    CHECK(ArrangePage::kZooms[page.zoom()] == core::NoteFraction{1, 4});
    type(page, U"-");
    CHECK(page.zoom() == ArrangePage::kDefaultZoom + 1);
    type(page, U"-", 100);
    CHECK(page.zoom() == ArrangePage::kZooms.size() - 1);
    type(page, U"+", 100);
    CHECK(page.zoom() == 0);
    type(page, U"+");
    CHECK(page.zoom() == 0);
    page.resetZoom();
    CHECK(page.zoom() == ArrangePage::kDefaultZoom);
    type(page, U"+", 2);

    // Zoomed in, the view follows the cursor's bar, in the new zoom's columns.
    type(page, U"G", 30);
    const auto column = ArrangePage::kZooms[0].toTicks(core::kPpqn);
    CHECK(page.leftTick() % column == 0);
    CHECK(page.leftTick() <= page.time());
}

TEST_CASE("bars are drawn as blocks as wide as they are long, shaded by density")
{
    auto page = page80x22();
    Frame frame;
    page.addRegions(frame);
    REQUIRE(frame.size() == 2); // the headers, then the timeline
    const auto& timeline = frame[1];
    CHECK(timeline.bounds == Rect{ArrangePage::kHeaderColumns * kCell.width, kCell.height,
                                  60 * kCell.width, 22 * kCell.height});

    // Drums: eight bars of 4/4 at density 3, each two columns less a gap of one pixel, then
    // six bars of 7/8 at density 4, each one and three quarter columns.
    std::vector<Shape> blocks;
    for (const auto& shape : timeline.under)
    {
        const int top = ArrangePage::kRulerRows * kCell.height;
        if (isDensity(shape.role) && shape.rect.y >= top && shape.rect.y < top + kCell.height)
        {
            blocks.push_back(shape);
        }
    }
    REQUIRE(blocks.size() >= 14);
    CHECK(blocks[0].role == Role::Density3);
    CHECK(blocks[0].rect.x == 1);
    CHECK(blocks[0].rect.width == 19);
    CHECK(blocks[1].rect.x == 21);
    CHECK(blocks[8].role == Role::Density4);
    CHECK(blocks[8].rect.x == 161);
    CHECK(blocks[9].rect.x == 161 + 17); // 17.5 pixels on, rounded down

    // The bass is silent in the intro: a thin line in each bar.
    const auto bass = rowShapes(timeline, 1, Role::BeatLine);
    REQUIRE(bass.size() >= 8);
    CHECK(bass[0].rect.height == 1);

    // The empty tracks have dots.
    CHECK(rowShapes(timeline, 10, Role::Dim).size() >= 20);
    CHECK(std::ranges::none_of(timeline.under,
                               [](const Shape& shape)
                               {
                                   return isDensity(shape.role) &&
                                          shape.rect.y >=
                                              (ArrangePage::kRulerRows + 10) * kCell.height;
                               }));
}

TEST_CASE("markers are named in the ruler, with faint lines through it and every track")
{
    auto page = page80x22();
    Frame frame;
    page.addRegions(frame);
    const auto& timeline = frame[1];
    const int tracksTop = ArrangePage::kRulerRows * kCell.height;
    std::vector<int> lines;
    for (const auto& shape : timeline.under)
    {
        if (shape.role == Role::Marker && shape.rect.y < tracksTop)
        {
            if (shape.rect.y == 0)
            {
                lines.push_back(shape.rect.x);
            }
            CHECK(shape.rect.height == kCell.height); // a row of the ruler at a time
            CHECK(shape.alpha < 255);
        }
        else if (shape.role == Role::Marker)
        {
            CHECK(shape.rect.y == tracksTop);
            CHECK(shape.rect.height == 14 * kCell.height);
            CHECK(shape.alpha < 255);
        }
    }
    // Intro at 0, the verse at bar 9 (16 columns), the chorus 21 quarter notes later, and
    // the bridge 50 after that; the second chorus is out of view.
    CHECK(lines == std::vector<int>{0, 160, 265, 515});

    int names = 0;
    for (const auto& run : timeline.text)
    {
        names += run.y == 0 && run.style.foreground == Role::Marker ? 1 : 0;
    }
    CHECK(names == 4);
}

namespace
{

// The text of the runs on a row of the ruler, in order.
std::vector<std::u32string> rulerText(const Region& timeline, int row)
{
    std::vector<std::u32string> texts;
    for (const auto& run : timeline.text)
    {
        if (run.y == row * kCell.height)
        {
            texts.push_back(run.text);
        }
    }
    return texts;
}

} // namespace

TEST_CASE("marker names are cut short at the next marker, or left out if there's no room")
{
    auto page = page80x22();
    type(page, U"-", 2); // eight quarter notes to a column
    Frame frame;
    page.addRegions(frame);
    // The intro, verse and bridge have room for fewer than three characters and an
    // ellipsis; the choruses have room for four; the last marker runs on.
    CHECK(rulerText(frame[1], 0) == std::vector<std::u32string>{U"Chor…", U"Chor…", U"Outro"});
}

TEST_CASE("bars are numbered from bar 1 and each marker, every so many bars")
{
    auto page = page80x22();
    Frame frame;
    page.addRegions(frame);
    // The shortest bar, of 2/4, is ten pixels, and "53 " is thirty: every fourth bar, from
    // bar 1, the verse at 9, the chorus at 15 and the bridge at 28. Bar 27 would crowd 28.
    CHECK(rulerText(frame[1], 2) ==
          std::vector<std::u32string>{U"1", U"5", U"9", U"13", U"15", U"19", U"23", U"28", U"32"});

    type(page, U"-", 2); // a 2/4 bar is now 2.5 pixels: every sixteenth bar
    frame.clear();
    page.addRegions(frame);
    CHECK(rulerText(frame[1], 2) ==
          std::vector<std::u32string>{U"1", U"9", U"15", U"28", U"36", U"48"});
}

TEST_CASE("a meter change too short for its name is shown by a tick, not hiding the next")
{
    auto page = page80x22();
    Frame frame;
    page.addRegions(frame);
    // The single bar of 2/4 is ten pixels, too narrow for "2/4", so the bridge's 3/4 shows.
    CHECK(rulerText(frame[1], 1) == std::vector<std::u32string>{U"4/4", U"7/8", U"4/4", U"3/4"});
    std::vector<int> ticks;
    for (const auto& shape : frame[1].under)
    {
        if (shape.role == Role::Dim && shape.rect.y > kCell.height &&
            shape.rect.y < 2 * kCell.height)
        {
            ticks.push_back(shape.rect.x);
        }
    }
    CHECK(ticks == std::vector<int>{505}); // 101 quarter notes in

    type(page, U"+", 2); // a sixteenth to a column: the 2/4 bar is 80 pixels
    type(page, U"G", 27);
    frame.clear();
    page.addRegions(frame);
    const auto meters = rulerText(frame[1], 1);
    CHECK(std::ranges::find(meters, U"2/4") != meters.end());
}

TEST_CASE("an empty track's dots are on its bar lines")
{
    auto page = page80x22();
    Frame frame;
    page.addRegions(frame);
    const auto dots = rowShapes(frame[1], 10, Role::Dim);
    REQUIRE(dots.size() >= 2);
    CHECK(dots[0].rect.x == 0);
    CHECK(dots[1].rect.x == 20);
}

TEST_CASE("the cursor boxes its bar on all four sides")
{
    auto page = page80x22();
    Frame frame;
    page.addRegions(frame);
    std::vector<Rect> sides;
    for (const auto& shape : frame[1].over)
    {
        if (shape.role == Role::Cursor)
        {
            sides.push_back(shape.rect);
        }
    }
    // Bar 1 of the drums, from 0 to the end of the gap after it at 21.
    const int y = ArrangePage::kRulerRows * kCell.height;
    CHECK(sides ==
          std::vector<Rect>{{0, y, 21, 2}, {0, y + 18, 21, 2}, {0, y, 1, 20}, {20, y, 1, 20}});
}

TEST_CASE("a marker's line through the ruler breaks only where a label sits across it")
{
    // A marker halfway through bar 1, which its meter and number run across.
    core::Song song;
    const auto id = song.addTrack("Piano");
    song.updateEvents(id, [&](core::EventList& events)
                      { events.insert({0, core::Note{60, 100, song.ppqn().quarter()}}); });
    song.setMarker(core::NoteFraction{1, 2}.toTicks(song.ppqn()), "Middle");
    ArrangePage page{song};
    page.resize({0, kCell.height, 80 * kCell.width, 22 * kCell.height}, kCell);
    page.zoomBy(1); // a half note to a column, so the marker is ten pixels in
    Frame frame;
    page.addRegions(frame);

    std::vector<int> rows;
    for (const auto& shape : frame[1].under)
    {
        if (shape.role == Role::Marker && shape.rect.x == 10 &&
            shape.rect.y < ArrangePage::kRulerRows * kCell.height)
        {
            rows.push_back(shape.rect.y / kCell.height);
        }
    }
    CHECK(rows == std::vector<int>{0}); // beside its name, but not through "4/4" or "1"
}

TEST_CASE("narrow bars lose their gaps")
{
    auto page = page80x22();
    type(page, U"-", 2); // two whole notes to a column: a bar of 4/4 is five pixels
    Frame frame;
    page.addRegions(frame);
    const auto drums = rowShapes(frame[1], 0, Role::Density3);
    REQUIRE(drums.size() >= 2);
    CHECK(drums[0].rect.x == 0);
    CHECK(drums[0].rect.width == 5);
    CHECK(drums[1].rect.x == 5);
}

TEST_CASE("centring puts the cursor's bar and track in the middle of the view")
{
    auto page = page80x22(); // 60 columns of two quarter notes, 19 tracks' rows
    type(page, U"G", 30);    // bar 30, from 109 quarter notes, of three
    CHECK(page.leftTick() == 0);
    page.centre();
    // The bar's middle, 110.5 quarter notes in, less 60 quarter notes, in whole columns.
    const auto column = ArrangePage::kZooms[page.zoom()].toTicks(core::kPpqn);
    CHECK(page.leftTick() == 25 * column);
    CHECK(page.topTrack() == 0); // all 14 tracks fit

    type(page, U"0");
    page.centre();
    CHECK(page.leftTick() == 0); // no further left than the start
}

TEST_CASE("the arrangement's status shows the track, bar, meter and zoom")
{
    auto page = page80x22();
    type(page, U"l", 8);
    type(page, U"j", 6);
    CHECK(page.status() == "track 7  bar 12/69  3/4  zoom 1/2");
}

TEST_CASE("scrolling by bars keeps the cursor exactly where it is on screen")
{
    auto page = page80x22(); // 60 columns of two quarter notes: 120 quarter notes in view
    const auto song = demoSong();
    const auto& ostinato = song.meter(song.tracks()[6]->id()); // 3/4, from each section

    // At the start the view can't scroll left, so the cursor just moves.
    page.scrollBars(-1);
    CHECK(page.bar() == 1);
    CHECK(page.leftTick() == 0);

    // In the middle, the view moves as far as the cursor, by bars that aren't whole columns.
    type(page, U"j", 6);
    page.goToBar(10);
    const auto onScreen = [&] { return page.time() - page.leftTick(); };
    const auto before = onScreen();
    page.scrollBars(1);
    CHECK(page.bar() == 11);
    CHECK(page.leftTick() == ostinato.barStart(11) - ostinato.barStart(10)); // 3 quarters
    CHECK(onScreen() == before);
    page.scrollBars(3);
    CHECK(page.bar() == 14);
    CHECK(onScreen() == before);
    page.scrollBars(-4);
    CHECK(page.leftTick() == 0);
    CHECK(onScreen() == before);

    // Near the end, the view scrolls only as far as the song's end at its right edge (86
    // quarter notes in, the last 120 of 205 rounded up to columns), and the cursor moves on.
    page.goToBar(40);
    page.scrollBars(100);
    CHECK(page.bar() == 69);
    CHECK(page.leftTick() == 86 * core::kPpqn.quarter());
}
