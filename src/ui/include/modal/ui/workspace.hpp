// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
#include <modal/core/player.hpp>
#include <modal/core/transforms.hpp>
#include <modal/ui/arrange_view.hpp>
#include <modal/ui/bar.hpp>
#include <modal/ui/command_line.hpp>
#include <modal/ui/commands.hpp>
#include <modal/ui/event_list_view.hpp>
#include <modal/ui/frame.hpp>
#include <modal/ui/key.hpp>
#include <modal/ui/keymap.hpp>
#include <modal/ui/layout.hpp>
#include <modal/ui/piano_roll_view.hpp>
#include <modal/ui/view.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace modal::ui
{

// Whether a command is an operator, waiting for a range or object: d, y, > and the others.
bool isOperator(std::string_view command);

// The window's contents: the views of the song, one shown at a time, with what they share
// (decided for milestone 5). The workspace handles keys through the keymap, with counts and
// pending operators, runs the commands every view has (the : prompt, playing, undo, :port)
// and hands the rest to the view shown; it draws that view, then the status line, or the :
// prompt, at the bottom.
class Workspace final : private ViewHost
{
public:
    // Every command the workspace and its views run.
    static std::span<const CommandInfo> commands();

    // The keymap of the built-in modalrc alone, whose bindings run the workspace's commands
    // and the window's (see windowCommands).
    static Keymap defaultKeymap();

    // Shows the document's song, in the event list from track 1 (or the conductor's list, if
    // there are no tracks), until another view is shown. Gives the registry what runs the commands,
    // which keys then run through it with the registry's others, such as the window's. The registry
    // and the document outlive the workspace, which can't be copied or moved, since the registry
    // runs its commands on it.
    Workspace(CommandRegistry& registry, core::Document& document, Keymap keymap = defaultKeymap(),
              std::int64_t timeout = kDefaultTimeout);
    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;
    ~Workspace() override;

    EventListView& eventList() { return *eventList_; }
    const EventListView& eventList() const { return *eventList_; }
    ArrangeView& arrangement() { return *arrange_; }
    const ArrangeView& arrangement() const { return *arrange_; }
    PianoRollView& pianoRoll() { return *pianoRoll_; }
    const PianoRollView& pianoRoll() const { return *pianoRoll_; }

    // Shows a view, its cursor where the cursor was in the view shown before, which Backspace
    // goes back to (decided for milestone 5).
    void show(ViewKind kind) override;
    void open(ViewKind kind) override;
    ViewKind shown() const;

    // Fits the views to the window, scrolling if the cursor would be out of view.
    void resize(int width, int height, CellSize cell);

    // Handles a key pressed at a time in milliseconds, through the keymap, returning whether
    // what's shown changed.
    bool handle(const Key& key, std::int64_t now);

    // When wake should next be called, for keys waiting on a timeout, or for the next frame
    // while playing; and the call, which returns whether what's shown changed.
    std::optional<std::int64_t> deadline() const;
    bool wake(std::int64_t now);

    // How often to draw a frame while playing, in milliseconds; the display's refresh rate
    // may hold it back further.
    static constexpr std::int64_t kFrameInterval = 8;

    Frame frame() const;

    // Runs a command through the registry with a count (0 for none), as a key bound to it
    // does, showing any message it gives.
    CommandResult run(std::string_view command, int count);

    // Shows the document's song as it is now, keeping the cursor's place; or a song new to
    // the document, opened or new, from its start.
    void refresh();
    void showSong();

    // The player the transport commands play the song with, or none, when they do nothing.
    // It must outlive the workspace.
    void setPlayer(core::Player* player);

    // Whether the play-from marker stays put when playback stops (the rewind setting), and the
    // cursor stays where it was; otherwise the marker moves to where playback stops, and the
    // cursor follows the playhead there.
    void setRewind(bool rewind);

    // How far ~ moves notes at most, either way: in time (humanizetime) and in velocity
    // (humanizevel).
    void setHumanize(std::chrono::milliseconds time, int velocity)
    {
        humanizeTime_ = time;
        humanizeVelocity_ = velocity;
    }
    bool rewind() const { return rewind_; }

    // What a port name given to :port refers to: the output port's full name, or else the
    // ports it could be when several match equally well (none when none match); and every
    // output port there is. An empty name only lists them.
    struct PortLookup
    {
        std::optional<std::string> port;
        std::vector<std::string> candidates;
        std::vector<std::string> ports;
    };
    using PortFinder = std::function<PortLookup(std::string_view name)>;
    void setPortFinder(PortFinder finder) { portFinder_ = std::move(finder); }

    // Where completion at the : prompt finds the choices of a kind of argument, from the word
    // typed so far (the workspace has those of commands, sections, roles, meters, lengths and
    // ports; the program the settings' and files'), and the choices it shows, while there are
    // several, with the one chosen.
    using ChoiceSource = std::function<std::vector<std::string>(std::string_view word)>;
    void setChoices(Choices kind, ChoiceSource source);
    std::vector<std::string> choices(Choices kind, std::string_view word) const;
    struct Completions
    {
        std::vector<std::string> matches;
        std::optional<std::size_t> chosen;
        std::size_t scroll = 0;
    };
    std::optional<Completions> completions() const;

    // What runs a line entered at the : prompt, such as the program's command runner.
    using LineHandler = std::function<void(const std::string& line)>;
    void setLineHandler(LineHandler handler) { lineHandler_ = std::move(handler); }

    void setKeymap(Keymap keymap);
    void setTimeout(std::int64_t timeout) { keys_.setTimeout(timeout); }
    void setLeader(const Key& leader) { keys_.setLeader(leader); }

    // The hint menu (decided October 2026): after keys that start longer bindings, once
    // they've waited hintdelay, what can follow them, with each one's title. Off with hints off.
    void setHints(bool on, std::int64_t delay)
    {
        hintsOn_ = on;
        hintDelay_ = delay;
    }
    struct Hint
    {
        std::string keys;   // the next key, as typed
        std::string title;  // the binding's hint or command's title, or a group's title
        bool group = false; // it starts longer bindings, so leads to more hints
        friend bool operator==(const Hint&, const Hint&) = default;
    };
    // What the hint menu shows now, if it's showing.
    std::optional<std::vector<Hint>> hints() const;

    // The list of keys (, ?, decided October 2026): every binding that does something where the
    // cursor is, under the built-in modalrc's sections, over the view; its pages, if it needs
    // more than one, and whether it's showing.
    struct KeyGroup
    {
        std::string title;
        std::vector<Hint> keys;
    };
    std::vector<KeyGroup> keyList() const;
    bool showingKeys() const { return keysPage_.has_value(); }

    // Where the cursor is, and whether a command does something there, so the hint menu
    // offers it.
    Where where() const;
    bool offered(std::string_view command) const;

    // Normal; OperatorPending after d or y, waiting for a motion; or CommandLine at the :
    // prompt, or while a view takes typing, as when editing a field in place.
    Mode mode() const { return mode_; }
    const CommandLine& commandLine() const { return line_; }
    std::string pending() const; // the operator, count and keys typed so far

    // What the unnamed register holds: an area's events, yanked or deleted; and those of its
    // first track alone.
    const EventRegister& registerArea() const { return register_; }
    const std::vector<core::Event>& registerEvents() const;

    // Shows a message in the status line until the next key.
    void setMessage(std::string message, MessageKind kind) override
    {
        message_ = std::move(message);
        messageKind_ = kind;
    }
    const std::string& message() const { return message_; }

private:
    void startTyping() override { mode_ = Mode::CommandLine; }
    void openPrompt(std::string line) override;
    void ask(std::string question, std::function<void()> yes, std::function<void()> no = {},
             std::function<void()> stop = {}) override;
    void playFrom(core::Tick tick) override;
    std::optional<Selection> selection() const override { return selection_; }
    void stopTyping() override { mode_ = Mode::Normal; }
    EventRegister& eventRegister() override { return register_; }
    const core::Player* player() const override { return player_; }

    View& view() const { return *current_; }

    // Shows a view at the cursor, remembering the one before for Backspace, or not when going
    // back; and lays it out in the window.
    void switchTo(View& view, bool remember, bool fresh = false);
    void layOut();

    bool runAll(const std::vector<Action>& actions);

    // Runs a command the workspace has itself, returning whether what's shown changed, or
    // nothing if it isn't one; or a view's command, through the view shown.
    std::optional<bool> runShared(std::string_view command, int count);
    bool runViewCommand(std::string_view command, int count);
    std::optional<bool> runCommandLine(std::string_view command);

    // The keymap in operator-pending mode: the Normal mode's motions, the operator's own keys
    // again for the cursor's place (dd), and Escape, unless omap binds those keys.
    void buildOperatorKeymap();

    // Runs a motion or the operator's own key after an operator, then the operator.
    bool runOperator(std::string_view command, int count);
    // das and yas away from the section strip: the cursor's section and as many after it as
    // the count says less one, taken out of the song or yanked, as x and Y do on the strip.
    bool operateOnSections(std::string_view op, int count);
    // The operators other than d and y, over what a range or object takes in the view shown,
    // by an amount (semitones, grid steps) the count before the operator gives (decided for
    // milestone 6); and changing the events each track's part of it takes, as one undo step,
    // or saying why it can't be.
    bool transform(std::string_view op, int amount, std::string_view motion, int count);
    // Quantises the notes' starts, and the pedals with them if asked.
    bool quantize(const OperatorTarget& target, bool pedals);
    using EventsChange =
        std::function<core::Transformed(const core::EventList&, const OperatorTarget::Part&)>;
    bool changeEvents(std::string title, const OperatorTarget& target, const EventsChange& change);
    // The shortest keys bound to a command in Normal mode, as typed, or its name if none are.
    std::string keysOf(std::string_view command) const;
    // Keys as shown in the hint menu and messages: the leader as typed, < as itself.
    std::string shownKeys(const std::vector<Key>& keys) const;

    bool undoOrRedo(bool undo);

    // Runs a transport command; then shows the playhead, or where playback stopped.
    bool transport(std::string_view command);
    void showPlayhead();

    // :port NAME sets the cursor's track's output port; :port alone says what it is.
    CommandResult port(std::string_view name);

    // :section-split NAME starts a new section at the bar line of the cursor's bar, in the view's
    // own bars; :section-rename NAME renames the section at the cursor.
    CommandResult section(std::string_view name);
    // Bars on every track, at the cursor's bar in the conductor's bars.
    CommandResult barDelete(int count);
    // The conductor's bars from one bar line to another, on every track, closing up.
    CommandResult deleteBars(core::Tick from, core::Tick to);
    CommandResult barInsert(std::string_view arguments);
    // Markers made section starts, the one at or before the cursor, or all of them.
    CommandResult markerToSection();
    // :marker-to-section on a selection: every marker from one time to another.
    CommandResult markersToSections(core::Tick from, core::Tick to);
    CommandResult renameSection(std::string_view name);

    Region promptRegion() const;
    Region statusRegion() const;
    // The status line, without the message when it is shown above it instead.
    Bar statusBar(bool withMessage = true) const;
    // A message too long for the status line, shown whole, wrapped, in a panel above it until
    // the next key (decided October 2026); its lines, none if it fits.
    std::vector<std::string> messageLines() const;
    Region messageRegion(const std::vector<std::string>& lines) const;

    CommandRegistry& commands_;
    core::Document& document_;
    std::unique_ptr<EventListView> eventList_;
    std::unique_ptr<ArrangeView> arrange_;
    std::unique_ptr<PianoRollView> pianoRoll_;
    View* current_ = nullptr;
    std::vector<View*> history_; // the views shown before, for Backspace
    Keymap keymap_;
    Keymap operatorKeymap_;
    KeyDispatcher keys_;
    struct PendingOperator
    {
        std::string name; // operator-delete, operator-yank, operator-quantize and so on
        int count;
    };
    std::optional<PendingOperator> operator_;
    // The Visual selection, and the last one with the cursor at its end, for gv.
    std::optional<Selection> selection_;
    std::optional<std::pair<Selection, SongCursor>> lastSelection_;
    // A command in Visual mode, or nothing to run it as in Normal mode; and ending it.
    std::optional<bool> runVisual(std::string_view command, int count);
    // What the prompt shows before the line.
    std::u32string promptLabel() const;
    bool operateOnSelection(std::string_view command, int count);
    bool putOverSelection();
    void endVisual();
    // A question asked: what it runs for y, for another key, and for Escape.
    struct Question
    {
        std::function<void()> yes;
        std::function<void()> no;
        std::function<void()> stop;
    };
    std::optional<Question> question_;
    // A question answered at the prompt, such as ^'s velocity: its label, shown in place of
    // the :, and what runs with the line typed.
    void askFor(std::string label, std::function<void(const std::string&)> answer);
    std::string answerLabel_;
    std::function<void(const std::string&)> answer_;
    std::chrono::milliseconds humanizeTime_{10};
    int humanizeVelocity_ = 8;
    std::mt19937_64 random_{std::random_device{}()};
    // :marker-to-section on a selection, going through its markers, asking about those off a
    // bar line.
    struct MarkerRun
    {
        SongCursor from; // where the cursor goes back to
        std::size_t next = 0;
        std::vector<std::pair<core::Tick, std::string>> starts; // a section's start, its name
        std::vector<core::Tick> markers;                        // the markers turned
        int atStarts = 0;
        int sharing = 0;
        int declined = 0;
        core::Tick first = 0; // the selection's span, outside which markers are passed over
        core::Tick last = 0;
    };
    void runMarkers(std::shared_ptr<MarkerRun> run);
    void finishMarkers(const MarkerRun& run);
    EventRegister register_;
    std::string message_;
    MessageKind messageKind_ = MessageKind::Information;
    LineHandler lineHandler_;
    PortFinder portFinder_;
    Mode mode_ = Mode::Normal;
    CommandLine line_;
    int width_ = 0;
    int height_ = 0;
    CellSize cell_{1, 1};
    WindowLayout layout_;
    core::Player* player_ = nullptr;
    bool rewind_ = true;
    bool wasPlaying_ = false;   // or stopping, until the player knows where it stopped
    bool following_ = true;     // the views following the playhead, until moved away from it
    std::int64_t lastTime_ = 0; // of the last key or wake-up
    bool hintsOn_ = true;
    std::int64_t hintDelay_ = 400;
    std::optional<std::int64_t> waitingSince_; // when keys started longer bindings
    bool hintsShown_ = false;
    void noteWaiting(std::int64_t now);
    std::vector<Hint> hintsAfter(const Keymap& keymap, Mode mode, std::span<const Key> keys) const;
    std::vector<Hint> operatorHints() const;
    std::optional<int> keysPage_; // the list of keys' page, while it shows
    std::array<ChoiceSource, kChoicesCount> sources_;
    // Completing at the prompt: the matches, the one chosen as Tab goes round them, where the
    // word they replace starts, and the line as completion left it, to know it's going on.
    struct Completing
    {
        std::vector<std::string> matches;
        std::optional<std::size_t> chosen;
        std::size_t start = 0;
        std::u32string line;
        std::size_t scroll = 0; // the first column shown, when they don't all fit across
    };
    std::optional<Completing> completing_;
    // The word at the prompt's cursor to complete: where it starts, what's typed of it, and
    // the kind of thing it is, None if nothing completes there.
    struct CompletingWord
    {
        std::size_t start = 0;
        std::string typed;
        Choices kind = Choices::None;
    };
    CompletingWord completingWord() const;
    bool complete(int by);
    // Chooses the match some places on or back, as the arrow keys and Page Down and Up do,
    // stopping at the ends.
    bool chooseMatch(std::ptrdiff_t by);
    void chooseIndex(std::size_t to);
    // After the line is edited while the matches show, they're the matches of the word now.
    void refilter();
    // The matches in columns: how many rows, each column's width, and how many fit across from
    // the first shown.
    struct MatchColumns
    {
        int rows = 1;
        std::vector<int> widths;
        int room = 1;
    };
    MatchColumns matchColumns(const std::vector<std::string>& matches) const;
    std::size_t columnsShown(const MatchColumns& columns, std::size_t first) const;
    Region completionRegion(const Completions& shown) const;
    // The list of keys laid out in pages of columns, each a run of lines: a group's title, or
    // a key and its title.
    struct KeyLine
    {
        const KeyGroup* group;
        const Hint* key; // or null for the group's title
    };
    struct KeyColumn
    {
        std::vector<KeyLine> lines;
        int keys = 0;
        int width = 0;
    };
    std::vector<std::vector<KeyColumn>> keyPages(const std::vector<KeyGroup>& groups) const;
    Region keysRegion() const;
    Region hintRegion(const std::vector<Hint>& hints) const;
};

} // namespace modal::ui
