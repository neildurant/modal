// SPDX-License-Identifier: GPL-3.0-or-later

#include "support/ppqn.hpp"

#include <modal/core/document.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace modal::core;

namespace
{

// A document with a track, and a way to add a note to it as an edit.
struct Edited
{
    explicit Edited(Ppqn ppqn = kPpqn)
        : document{[&]
                   {
                       Song song{ppqn};
                       song.addTrack("Bass");
                       return song;
                   }()}
    {
    }

    TrackId bass() const { return document.song().tracks()[0]->id(); }

    bool addNote(Tick tick)
    {
        return document.edit("Add a note in Bass", {bass(), tick, {1, 2}},
                             [&](Song& song)
                             {
                                 song.updateEvents(bass(), [&](EventList& events)
                                                   { events.insert({tick, Note{40, 100, 1}}); });
                             });
    }

    std::size_t notes() const { return document.song().events(bass()).size(); }

    Document document;
};

} // namespace

TEST_CASE("an edit is a step to undo and redo, which go back to where it was made")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Edited e{ppqn};
    CHECK_FALSE(e.document.canUndo());
    CHECK_FALSE(e.document.undo());

    REQUIRE(e.addNote(ppqn.quarter()));
    REQUIRE(e.addNote(2 * ppqn.quarter()));
    CHECK(e.notes() == 2);

    const auto undone = e.document.undo();
    REQUIRE(undone);
    CHECK(undone->title == "Add a note in Bass");
    CHECK(undone->place == EditPlace{e.bass(), 2 * ppqn.quarter(), {1, 2}});
    CHECK(e.notes() == 1);
    CHECK(e.document.undo());
    CHECK(e.notes() == 0);
    CHECK_FALSE(e.document.undo());

    const auto redone = e.document.redo();
    REQUIRE(redone);
    CHECK(redone->place.tick == ppqn.quarter());
    CHECK(e.notes() == 1);
    CHECK(e.document.redo());
    CHECK(e.notes() == 2);
    CHECK_FALSE(e.document.redo());

    // The flattened timeline is rebuilt each time.
    CHECK(e.document.song().events(e.bass())[1].tick == 2 * ppqn.quarter());
}

TEST_CASE("an edit after undoing drops what could have been redone")
{
    Edited e;
    e.addNote(0);
    e.addNote(1);
    e.document.undo();
    e.addNote(2);
    CHECK_FALSE(e.document.canRedo());
    e.document.undo();
    CHECK(e.notes() == 1);
    CHECK(e.document.song().events(e.bass())[0].tick == 0);
}

TEST_CASE("an edit that changes nothing is no step")
{
    Edited e;
    CHECK_FALSE(e.document.edit("Nothing", {}, [](Song&) {}));
    CHECK_FALSE(
        e.document.edit("Rename to the same name", {}, [&](Song& song)
                        { song.updateTrack(e.bass(), [](Track& t) { t.name = "Bass"; }); }));
    CHECK_FALSE(e.document.canUndo());

    // Every other change to the song is one, settings and names too.
    CHECK(e.document.edit("Mute Bass", {}, [&](Song& song)
                          { song.updateTrack(e.bass(), [](Track& t) { t.mute = true; }); }));
    CHECK(e.document.edit("Rename track 1 to Low", {}, [&](Song& song)
                          { song.updateTrack(e.bass(), [](Track& t) { t.name = "Low"; }); }));
    e.document.undo();
    e.document.undo();
    CHECK_FALSE(e.document.song().tracks()[0]->mute);
}

TEST_CASE("a group of edits is one step")
{
    Edited e;
    e.document.beginGroup("Record 3 notes", {e.bass(), 0, {}});
    e.addNote(0);
    e.document.beginGroup("Inner", {}); // nested: the outer group's title counts
    e.addNote(1);
    e.document.endGroup();
    e.addNote(2);
    CHECK_FALSE(e.document.undo()); // not while grouping
    e.document.endGroup();
    CHECK(e.notes() == 3);

    const auto undone = e.document.undo();
    REQUIRE(undone);
    CHECK(undone->title == "Record 3 notes");
    CHECK(e.notes() == 0);
    CHECK_FALSE(e.document.canUndo());

    // A group whose edits change nothing in the end is no step.
    e.document.beginGroup("Add and remove", {});
    e.addNote(5);
    e.document.edit("Remove it", {}, [&](Song& song)
                    { song.updateEvents(e.bass(), [](EventList& events) { events.erase(0); }); });
    e.document.endGroup();
    CHECK_FALSE(e.document.canUndo());
    CHECK(e.document.canRedo());
}

TEST_CASE("the song is modified until saved, and unmodified when undone back to it")
{
    Edited e;
    CHECK_FALSE(e.document.modified());
    e.addNote(0);
    CHECK(e.document.modified());
    e.document.undo();
    CHECK_FALSE(e.document.modified());
    e.document.redo();
    CHECK(e.document.modified());

    e.document.markSaved("song.modal");
    CHECK_FALSE(e.document.modified());
    CHECK(e.document.file() == std::filesystem::path{"song.modal"});
    e.document.undo();
    CHECK(e.document.modified());
    e.document.redo();
    CHECK_FALSE(e.document.modified());

    // Saved, then undone and changed another way, it can't get back to the saved version.
    e.document.undo();
    e.addNote(7);
    CHECK(e.document.modified());

    e.document.replace(Song{}, std::nullopt);
    CHECK_FALSE(e.document.modified());
    CHECK_FALSE(e.document.canUndo());
    CHECK_FALSE(e.document.file());
}

TEST_CASE("an imported song remembers where it came from, until saved to a file of its own")
{
    Edited e;
    e.document.replace(Song{}, std::nullopt, "song.mid");
    CHECK_FALSE(e.document.file());
    CHECK(e.document.importedFrom() == std::filesystem::path{"song.mid"});
    e.document.markSaved();
    CHECK(e.document.importedFrom());
    e.document.markSaved("song.modal");
    CHECK(e.document.file() == std::filesystem::path{"song.modal"});
    CHECK_FALSE(e.document.importedFrom());
    e.document.replace(Song{}, std::nullopt);
    CHECK_FALSE(e.document.importedFrom());
}

TEST_CASE("only so many steps are kept")
{
    Edited e;
    e.document.setUndoLevels(3);
    for (Tick tick = 0; tick < 5; ++tick)
    {
        e.addNote(tick);
    }
    int undone = 0;
    while (e.document.undo())
    {
        ++undone;
    }
    CHECK(undone == 3);
    CHECK(e.notes() == 2);
    CHECK(e.document.modified()); // the loaded version is gone

    // Fewer steps drops the oldest, but not what can be redone.
    e.document.setUndoLevels(1);
    CHECK(e.document.redo());
    CHECK(e.document.redo());
    CHECK(e.document.undo());
    CHECK_FALSE(e.document.undo());
    CHECK(e.notes() == 3);
}

TEST_CASE("past versions share what an edit didn't touch")
{
    const auto ppqn = MODAL_ANY_PPQN();
    Edited e{ppqn};
    e.document.edit("Split", {}, [&](Song& song) { song.splitSection(4 * ppqn.whole(), "Verse"); });
    const auto before = e.document.song().sections()[0];
    e.addNote(5 * ppqn.whole()); // in the verse
    CHECK(e.document.song().sections()[0] == before);
    e.document.undo();
    CHECK(e.document.song().sections()[0] == before);
}

TEST_CASE("undo never brings back a track ID that's been given out")
{
    Edited e;
    TrackId added{0};
    e.document.edit("Add track", {}, [&](Song& song) { added = song.addTrack("Keys"); });
    e.document.undo();
    TrackId again{0};
    e.document.edit("Add track", {}, [&](Song& song) { again = song.addTrack("Pad"); });
    CHECK(again != added);
}

TEST_CASE("the listener hears every change to the song")
{
    Edited e;
    std::vector<std::size_t> heard;
    e.document.setListener([&](const Song& song)
                           { heard.push_back(song.events(song.tracks()[0]->id()).size()); });
    e.addNote(0);
    e.addNote(1);
    e.document.undo();
    e.document.redo();
    e.document.edit("Nothing", {}, [](Song&) {});
    CHECK(heard == std::vector<std::size_t>{1, 2, 1, 2});
}
