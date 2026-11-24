// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
#include <modal/core/player.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace modal::core
{

// The control-side lifetime of a document's recording take. Document owns song history;
// Player owns transport and input. They outlive the session while a take is pending.
// The session exclusively owns the document's preview and edit guard during that take.
class RecordingSession
{
public:
    explicit RecordingSession(Document& document);
    ~RecordingSession();
    RecordingSession(const RecordingSession&) = delete;
    RecordingSession& operator=(const RecordingSession&) = delete;

    enum class State
    {
        Idle,
        Recording, // includes count-in and pre-roll
        Finishing, // stop requested or punched out, through acknowledgement and the undo commit
    };

    State state() const;
    bool pending() const { return take_.has_value(); }
    Document& document() { return document_; }
    const Document& document() const { return document_; }
    std::span<const TrackId> tracks() const;

    // A failed start installs no hooks and leaves the document and history unchanged. Given a
    // punch range, the pass records only that, playback carrying on past its end.
    bool start(Player& player, std::optional<ArmedSpan> punch = std::nullopt);
    bool stop();

    // Refreshes the live take only in its recording phase, as the playhead advances.
    bool refreshPreview();
    bool inputLost() const;

    struct Completion
    {
        bool changed;
        std::vector<TrackId> tracks;
        std::size_t inputLost;
    };

    // Never waits for the engine. Once stopped, polls for its acknowledgement, collects the
    // last input, removes the hooks and commits at most one undo step. Later calls do nothing.
    // The UI supplies the undo title and place, and presents the completion's status.
    std::optional<Completion> finish(const EditPlace& place, std::string title);

    // Shared by commands, delayed dialog answers and native window closing. This depends
    // on the take's lifetime, independently of whether a preview is currently displayed.
    std::optional<std::string> fileOperationRefusal() const;

    // Views must check before using preview event indices to edit the underlying song.
    std::optional<std::string> eventEditRefusal(TrackId track) const;

private:
    struct Take
    {
        Player* player;
        std::vector<TrackId> tracks;
        std::size_t inputLost;
    };

    void clearHooks();
    std::optional<std::string> guard(const Song& before, const Song& after) const;

    Document& document_;
    std::optional<Take> take_;
};

} // namespace modal::core
