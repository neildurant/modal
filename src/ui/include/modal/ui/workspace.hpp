// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <modal/core/document.hpp>
#include <modal/core/freeze.hpp>
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
#include <modal/ui/palette.hpp>
#include <modal/ui/piano_roll_view.hpp>
#include <modal/ui/remembered.hpp>
#include <modal/ui/tracks_view.hpp>
#include <modal/ui/view.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace modal::ui
{

class Settings;

// Whether a command is an operator, waiting for a range or object: d, y, > and the others.
bool isOperator(std::string_view command);

// A transform's settings, each asked for in turn (decided November 2026): its name, as NAME=VALUE
// at the : prompt (none for the amount, which comes first there), the question, the default
// Enter takes, how it's written, dimmed after the answer, and the values Tab offers, note
// lengths too where it takes one.
struct TransformSetting
{
    std::string_view name;
    std::string_view question;
    std::string_view answer;
    std::string_view form = {};
    std::vector<std::string_view> values = {};
    bool lengths = false;
    bool tracks = false; // the tracks' names, offered too
};

std::span<const TransformSetting> transformSettings(std::string_view command);

// How the transform menu's quantise was answered: a grid (or each track's own), how far, the
// swing and what's moved.
struct Quantizing
{
    std::optional<core::NoteFraction> grid = std::nullopt;
    int swing = 50;
    core::QuantizeHow::Move move = core::QuantizeHow::Move::Starts;
    int strength = 100;
};

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

    // One definition for each transform (decided November 2026, after a code review): its
    // command; how it asks, for nothing, a key and the rest, its settings in turn or one
    // thing (the question's start, {} for what it takes, examples when it has no default, and
    // the default, {first} for the range's first note and {search} for the search's query);
    // and the function that does it.
    struct TransformSpec
    {
        enum class Asks : std::uint8_t
        {
            Nothing,
            Once,
            InTurn,
            Key,
        };
        using Apply = bool (Workspace::*)(std::string_view name, const OperatorTarget& target,
                                          const std::string& text);

        std::string_view name;
        std::string_view aliases;
        std::string_view usage;
        std::string_view description;
        Asks asks = Asks::Once;
        std::string_view label = {};
        std::string_view examples = {};
        std::string_view answer = {};
        std::vector<TransformSetting> settings = {};
        Apply apply = nullptr;
    };
    static std::span<const TransformSpec> transformSpecs();
    static const TransformSpec* findTransform(std::string_view name);

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

    TracksView& tracks() { return *tracks_; }
    const TracksView& tracks() const { return *tracks_; }

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

    // MIDI input has arrived, as another thread says: takes what the engine has passed back,
    // and again shortly after, for what it hadn't yet. Returns whether what's shown changed.
    bool inputWoken(std::int64_t now);

    // How long after input arrives to take it again, by when the engine will have passed back
    // anything it hadn't yet.
    static constexpr std::int64_t kInputSettle = 10; // milliseconds

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
    // cursor stays where it was; otherwise the play-from marker moves to where playback stops, and
    // the cursor follows the playhead there.
    void setRewind(bool rewind);
    void setQuantizeMarks(bool marks) { quantizeMarks_ = marks; }

    // How far ~ moves notes at most, either way: in time (humanizetime) and in velocity
    // (humanizevel).
    void setHumanize(std::chrono::milliseconds time, int velocity)
    {
        humanizeTime_ = time;
        humanizeVelocity_ = velocity;
    }
    bool rewind() const { return rewind_; }
    // The modalrc's own scales, beside the built-in ones, for keys.
    void setScales(std::vector<core::Scale> scales) { userScales_ = std::move(scales); }
    std::vector<core::Scale> scales() const;
    // How long before a change the pedalling tools lift the pedal, and after it press it.
    void setPedalTiming(std::chrono::milliseconds lift, std::chrono::milliseconds press)
    {
        pedalLift_ = lift;
        pedalPress_ = press;
    }
    // How far apart notes can start and still be a chord, for the chord tools.
    void setChordWindow(std::chrono::milliseconds window) { chordWindow_ = window; }
    // How velocity changes keep within their limits: the velclip and velfloor settings.
    void setVelocityLimits(core::VelocityLimits limits) { velocityLimits_ = limits; }
    // Whether q moves the pedalling with its notes, keeps it where it is, or asks (the
    // quantizepedal setting, decided November 2026); the menu's quantise always asks.
    enum class QuantizePedal
    {
        Follow,
        Keep,
        Ask,
    };
    void setQuantizePedal(QuantizePedal pedal) { quantizePedal_ = pedal; }

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

    // The settings, for the palette's ? to list with their values.
    core::NoteFraction timingUnit(core::NoteFraction grid) const override;
    void setSettings(const Settings* settings) { settings_ = settings; }

    // What's remembered between sessions, as read from the state directory, and what to call
    // when it changes, to write it there; and the clock it's kept by, seconds since 1970 (the
    // system's, unless a test sets one).
    void setRemembered(Remembered remembered, std::function<void(const Remembered&)> changed);
    const Remembered& remembered() const { return remembered_; }
    void setClock(std::function<std::int64_t()> clock) { clock_ = std::move(clock); }
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
    // A toggle's name and what it's set to now, for the hint menu, such as "Rewind: on"; none
    // for a command that isn't one, or a track's with no track to say of.
    std::optional<std::string> toggleTitle(std::string_view command) const;

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
    // The list laid out in pages of columns, each a run of lines: a group's title (continued,
    // when a group too long for a whole column goes on in the next), or a key and its title.
    struct KeyLine
    {
        const KeyGroup* group;
        const Hint* key; // or null for the group's title
        bool continued = false;
    };
    struct KeyColumn
    {
        std::vector<KeyLine> lines;
        int keys = 0;
        int width = 0;
    };
    std::vector<std::vector<KeyColumn>> keyPages(const std::vector<KeyGroup>& groups) const;

    // Where the cursor is, and whether a command does something there, so the hint menu
    // offers it.
    Where where() const;
    bool offered(std::string_view command) const;
    // An object not offered where the cursor is, saying where it is.
    bool refuseObject(std::string_view command);
    void sayWholeRows();

    // Normal; OperatorPending after d or y, waiting for a motion; or CommandLine at the :
    // prompt, or while a view takes typing, as when editing a field in place.
    Mode mode() const { return mode_; }
    const CommandLine& commandLine() const { return prompt_.line; }
    std::string pending() const; // the operator, count and keys typed so far
    // What a listing (:registers) shows, a name and what it is a line, until the next key.
    const std::vector<std::pair<std::string, std::string>>& listing() const { return listing_; }

    // What the unnamed register holds: an area's events, yanked or deleted; and those of its
    // first track alone.
    const EventRegister& registerArea() const { return register_; }
    const std::vector<core::Event>& registerEvents() const;

    // Shows a message in the status line until the next key.
    std::optional<core::EventQuery> findQuery() const override { return finding_.query; }
    std::optional<core::EventQuery> searchQuery() const override { return searching_.query; }
    void searchWrapped(bool forward) override;
    std::optional<core::Tick> jumpTime() const override
    {
        return jumpTime_ >= 0 ? std::optional{jumpTime_} : std::nullopt;
    }

    // Report a registry command's outcome, independently of its message's severity.
    void report(const CommandResult& result);
    void setMessage(std::string message, MessageKind kind) override
    {
        message_ = std::move(message);
        messageKind_ = kind;
    }
    void refuse(std::string message, MessageKind kind = MessageKind::Warning) override
    {
        setMessage(std::move(message), kind);
        refused_ = true;
        macros_.failed = macros_.failed || macros_.depth > 0;
    }
    // Whether the last key's command was refused or given up, rather than done.
    bool refused() const { return refused_; }
    const std::string& message() const { return message_; }
    MessageKind messageKind() const { return messageKind_; }

private:
    CommandResult trackFreeze(std::string_view arguments);
    CommandResult applyFreeze(std::span<const core::TrackId> tracks, core::FreezePart part);
    using FreezePreparation = std::variant<std::vector<core::FrozenTrack>, std::string>;
    FreezePreparation prepareFreeze(std::span<const core::TrackId> tracks,
                                    core::FreezePart part) const;
    const std::array<std::optional<std::string>, 5>& freezeReasons() const;
    bool freezeChoiceKey(const Key& key);
    Region freezeChoiceRegion() const;
    struct FreezeChoice
    {
        std::vector<core::TrackId> tracks;
        std::size_t chosen = 0;
        // Preview invalidates with the song or transport, rather than transforming every
        // track again on each frame. Enter prepares the chosen operation afresh.
        mutable std::optional<std::uint64_t> version = std::nullopt;
        mutable bool bypass = false;
        mutable bool busy = false;
        mutable std::array<std::optional<std::string>, 5> reasons{};
    };
    std::optional<FreezeChoice> freezeChoice_;
    void startTyping() override { mode_ = Mode::CommandLine; }
    void openPrompt(std::string line) override;
    void askName(std::string label, std::string name,
                 std::function<void(const std::string&)> answer) override;
    void ask(std::string question, std::function<void()> yes, std::function<void()> no = {},
             std::function<void()> stop = {}) override;
    void playFrom(core::Tick tick) override;
    std::optional<Selection> selection() const override { return selection_; }
    void moveSelection(core::Tick by, int pitches) override;
    void chosenMeter(core::TimeSignature meter) override;
    void stopTyping() override
    {
        mode_ = selection_ && where() == Where::TrackList ? Mode::Visual : Mode::Normal;
    }
    EventRegister& eventRegister() override { return register_; }
    const core::Player* player() const override { return player_; }
    std::vector<core::TrackId> movingTracks() const override;
    bool quantizeMarks() const override { return quantizeMarks_; }
    std::vector<std::string> outputPorts() const override
    {
        return portFinder_ ? portFinder_("").ports : std::vector<std::string>{};
    }

    View& view() const { return *current_; }

    // Shows a view at the cursor, remembering the one before for Backspace, or not when going
    // back; and lays it out in the window.
    void switchTo(View& view, bool remember, bool fresh = false);
    void layOut();

    bool runAll(const std::vector<Action>& actions);

    // Takes the MIDI input waiting, reporting any lost; returns whether what's shown changed.
    bool takeInput();

    // Runs a command the workspace has itself, returning whether what's shown changed, or
    // nothing if it isn't one; or a view's command, through the view shown.
    std::optional<bool> runShared(std::string_view command, int count);
    bool runViewCommand(std::string_view command, int count);
    // Says where a command does something, for one run where it does nothing.
    bool sayWhereFor(std::string_view command);
    bool fromKeys_ = false;      // running commands typed as keys, not from the command line
    std::vector<Key> typedKeys_; // and the keys, as the bindings have them
    std::optional<bool> runCommandLine(std::string_view command);

    // The keymap in operator-pending mode: the Normal mode's motions, the operator's own keys
    // again for the cursor's place (dd), and Escape, unless omap binds those keys.
    void buildOperatorKeymap();
    // The keymaps for where the cursor is: of keys bound to several commands, the one doing
    // something here (see Keymap::resolved); kept until the place or the keymap changes.
    const Keymap& keymapHere() const;
    const Keymap& operatorKeymapHere() const;
    void resolveKeymaps() const;
    mutable std::optional<Where> resolvedFor_;
    mutable Keymap keymapHere_;
    mutable Keymap operatorKeymapHere_;

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
    bool quantize(const OperatorTarget& target, bool pedals, const Quantizing& how = {});
    using Answers = std::vector<std::pair<std::string, std::string>>;
    bool quantizeAsked(std::string_view name, const OperatorTarget& target,
                       const std::string& text);
    bool thinAsked(std::string_view name, const OperatorTarget& target, const std::string& text);
    bool splitAsked(std::string_view name, const OperatorTarget& target, const std::string& given);
    bool remapAsked(std::string_view name, const OperatorTarget& target, const std::string& text);
    bool applyKeyLine(std::string_view name, const OperatorTarget& target, const std::string& text);
    bool applyInvert(std::string_view name, const OperatorTarget& target, const std::string& text);
    bool applyStretch(std::string_view name, const OperatorTarget& target, const std::string& text);
    bool applyLength(std::string_view name, const OperatorTarget& target, const std::string& text);
    bool applyFilter(std::string_view name, const OperatorTarget& target, const std::string& text);
    bool applyRetrograde(std::string_view name, const OperatorTarget& target,
                         const std::string& text);
    bool applyTranspose(std::string_view name, const OperatorTarget& target,
                        const std::string& text);
    bool applyLift(std::string_view name, const OperatorTarget& target, const std::string& text);
    bool applyLimit(std::string_view name, const OperatorTarget& target, const std::string& text);
    bool applyRamp(std::string_view name, const OperatorTarget& target, const std::string& text);
    bool askTransform(const TransformSpec& spec, const OperatorTarget& target);
    std::string defaultAnswer(std::string_view answer, const OperatorTarget& target,
                              const Answers& answers) const;
    bool pedalAsked(std::string_view op, const OperatorTarget& target, const std::string& text);
    std::chrono::milliseconds pedalLift_{10};
    std::chrono::milliseconds pedalPress_{60};
    std::string gridOf(const OperatorTarget& target) const;
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
    // The adjoining tracks a track command acts on, as first and count: the selected ones, or the
    // cursor's. None on the conductor or the section strip.
    std::optional<std::pair<std::size_t, std::size_t>> trackBlock() const;
    // Moves them there, in a step with others of the same key, keeping the cursor on its track.
    void moveTrackBlock(std::size_t first, std::size_t count, std::size_t to,
                        const std::string& mergeKey);
    // What a key does while moving tracks. Only the keys for that do anything.
    bool trackMoveKey(const Key& key);

    // :event-insert puts an event of any kind but a note in the cursor's track at its time.
    CommandResult insertEvent(std::string_view text);

    // :section-split NAME starts a new section at the bar line of the cursor's bar, in the view's
    // own bars; :section-rename NAME renames the section at the cursor.
    CommandResult section(std::string_view name);
    CommandResult sectionHere(std::string_view command, int count);
    CommandResult askBarInsert(int count);
    // Bars on every track, at the cursor's bar in the conductor's bars.
    CommandResult barDelete(int count);
    // The conductor's bars from one bar line to another, on every track, closing up.
    CommandResult deleteBars(core::Tick from, core::Tick to);
    CommandResult barInsert(std::string_view arguments);
    // Labels made section starts, the one at or before the cursor, or all of them.
    CommandResult labelToSection();
    // :label-to-section on a selection: every label from one time to another.
    CommandResult labelsToSections(core::Tick from, core::Tick to);
    CommandResult renameSection(std::string_view name);

    // The prompt's line, on a row of its own above the status line, or at the top of the
    // palette, which never covers the status line; and whether it shows on its own row.
    Region promptRegion() const;
    bool promptRowShown() const;
    // What panels above the status line stand on: the status line, or the prompt's row.
    Rect panelBase() const;
    // How many of the palette's matches show, the same however many match.
    std::size_t paletteRows() const;
    Region statusRegion() const;
    std::string operatorAbout() const;
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
    std::unique_ptr<TracksView> tracks_;
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
    // The selection as it was before an object made it, which another object starts from.
    std::optional<Selection> beforeObject_;
    struct SavedSelection
    {
        Selection selection;
        SongCursor cursor;
        std::optional<Selection> beforeObject;
    };
    std::optional<SavedSelection> lastSelection_;
    // A command in Visual mode, or nothing to run it as in Normal mode; and ending it.
    std::optional<bool> runVisual(std::string_view command, int count);
    void selectObject(std::string_view command, int count);
    void widenSelection();
    // The range a selection names, restoring an object's original place before resolving it.
    std::pair<std::string, int> selectionRange(std::string_view command);
    // What the prompt shows before the line.
    std::u32string promptLabel() const;
    // The arguments of the command at the prompt not yet typed, as a hint after the line.
    std::string argumentHint() const;
    // The command palette, open at the prompt: the commands it searches, those matching what's
    // typed, best first, a command's keys (none if it has none), its keys while it's open, and
    // the list above the prompt.
    struct PaletteState
    {
        std::optional<std::size_t> chosen; // among the matches, 0 the best; none at first
    };
    Remembered remembered_;
    const Settings* settings_ = nullptr;
    std::function<void(const Remembered&)> rememberedChanged_;
    std::function<std::int64_t()> clock_ = []
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    };
    void rememberedChanged();
    std::vector<PaletteItem> paletteItems() const;
    std::vector<PaletteItem> paletteShown() const;
    // The palette searching other things, by a first character: @ sections, # tracks, '
    // labels, ? settings; what it lists then, and going to one.
    std::optional<char> palettePrefix() const;
    bool inSongOrder() const;
    std::vector<PaletteItem> prefixItems(char prefix) const;
    bool runPrefixItem(char prefix, const PaletteItem& item);
    std::string keysFor(std::string_view command) const;
    bool runPalette(std::string_view command);
    // A key as typed, and as . and macros replay them; the keys of the last change, for .;
    // and repeating it.
    bool handleKey(const Key& key, std::int64_t now);
    bool takeKey(const Key& key, std::int64_t now);
    bool recordChange(const Key& key, std::int64_t now);
    // Ends the change being typed, for ., if nothing's waiting any more.
    void endChange();
    bool repeatChange(int count);
    // Macros, a to z, kept in the remembered state: the one recording (0 for none, rather than
    // an optional, which GCC's release build takes for uninitialised) and its keys so far,
    // where the keys of the binding being typed began, the last played, how deep in macros
    // the keys being run are, and whether a command among them failed, stopping them.
    static constexpr int kMaxMacroDepth = 20;
    struct MacroState
    {
        char32_t recording = 0;
        std::vector<Key> recorded;
        std::size_t recordedBefore = 0;
        char32_t last = 0;
        int depth = 0;
        bool failed = false;
    };
    MacroState macros_;
    static std::string macroName(char32_t name);
    bool endRecording();
    bool playMacro(char32_t name, int count);
    bool sayMacros();
    // Commands that take a character after them ("a), waiting for it; running one with it.
    struct Awaiting
    {
        std::string command;
        int count = 0;
    };
    std::optional<Awaiting> awaiting_;
    bool takesCharacter(std::string_view command) const;
    bool characterAfter(std::string_view command, const Key& key, int count);
    bool withCharacter(std::string_view command, char32_t character, int count);
    // Marks: setting one, going to one or back (''), the place before the last jump, and the
    // time of the one an operator is taking the range to (-1 for none, rather than an
    // optional, which GCC's release build takes for uninitialised).
    bool setMark(char32_t character);
    bool goToMark(char32_t character, int count);
    std::string markAbout(const core::Mark& mark) const;
    bool sayMarks();
    CommandResult deleteMarks(std::string_view letters);
    CommandResult label(std::string_view name);
    CommandResult deleteLabels();
    std::optional<SongCursor> jumpedFrom_;
    core::Tick jumpTime_ = -1;
    // Finds (fE): the pitch class of the last, its name as typed, and whether f
    // and F repeat it, while it's the last thing done, and whether the key being handled kept
    // that. A find whose name may yet take a sharp or flat (b or #) after its letter: where it
    // started, whether it found nothing, which a macro fails on only once the name is done,
    // and for an operator, which has taken its range to the natural, what to do it again
    // from: the operator, the song and registers before it, and the undo group it's in until
    // the name is done.
    struct Redo
    {
        PendingOperator op;
        core::Song song;
        EventRegister unnamed;
        std::map<char32_t, EventRegister> registers;
        std::optional<char32_t> registerName;
        EventRegister registerBefore;
        bool cut = false;
    };
    struct Naming
    {
        char32_t letter;
        bool forward;
        int count;
        SongCursor from;
        bool failed = false;
        std::optional<Redo> redo = {};
    };
    struct FindState
    {
        std::optional<Naming> naming;
        std::optional<core::EventQuery> query;
        std::string name;
        bool forward = true;
    };
    FindState finding_;
    bool startFind(bool forward, int count);
    // ; and Alt-; (decided November 2026): the last find again, its way or the other.
    bool repeatFind(bool same, int count);
    // Escape after an operator's find (dfC): undone, the song, registers and cursor as before.
    void cancelNaming();
    // The operator over the find's range, to its note named so far, saying so if there's none;
    // whether it found one.
    bool operateToFound(bool forward, int count);
    bool nameFind(std::string_view command, char32_t letter, int count);
    bool finishNaming(std::optional<char32_t> accidental);
    void settleNaming();
    // Going to the find's next or previous match, saying so if there's none; whether it found
    // one.
    bool goToFound(bool forward, int count);
    void sayNotFound(bool forward);
    // The named registers, a to z: the one named for the next command, the unnamed register
    // as it was then, adding to a register, and saying what they hold.
    std::map<char32_t, EventRegister> registers_;
    std::optional<char32_t> registerName_;
    EventRegister registerBefore_;
    static EventRegister appended(EventRegister to, const EventRegister& more);
    static std::string registerAbout(const EventRegister& reg);
    // A listing over the view (:registers, :macros), a name and what it is a line, closed by
    // the next key; and drawing it.
    std::vector<std::pair<std::string, std::string>> listing_;
    Region listingRegion() const;
    bool sayRegisters();
    // The change . repeats: the keys typed since Normal mode was last idle, the last change's,
    // the song's version before them, whether undo, redo or . ran among them, and whether . or
    // a macro is replaying keys, which aren't recorded again.
    struct RepeatState
    {
        std::vector<Key> changeKeys;
        std::vector<Key> lastChange;
        std::uint64_t versionBefore = 0;
        bool notAChange = false;
        bool replaying = false;
    };
    RepeatState repeat_;
    Region paletteRegion() const;
    bool operateOnSelection(std::string_view command, int count);
    // Cutting at the edges (|): an operator run after cutting the notes crossing its range's
    // edges, as cutThen says; the cut on the song; and what y took, cut to the range.
    bool cutThen(std::string_view op, std::string_view motion, int count,
                 const std::function<bool()>& run);
    void cutSong(const CutRange& range);
    void cutRegister(const CutRange& range);
    bool cutNext_ = false;            // | typed, for the next operator
    std::optional<CutRange> cutting_; // the range the operator running cuts, for its target
    bool putOverSelection();
    void endVisual();
    // A question asked: what it runs for y, for n, and for Escape, and the question, shown
    // again at any other key.
    struct Question
    {
        std::function<void()> yes;
        std::function<void()> no;
        std::function<void()> stop;
        std::string text;
    };
    std::optional<Question> question_;
    // A question answered at the prompt, such as ^'s velocity: its label, shown in place of
    // the :, and what runs with the line typed.
    // An answer given nothing takes the default, if there's one, as a transform's does.
    void askFor(std::string label, std::function<void(const std::string&)> answer,
                std::string defaultAnswer = {});
    void askSettings(const std::string& op, const OperatorTarget& target, Answers answers);
    std::string optionsCommand() const;
    // Search (/C1): what the last looks for, as read and as typed, and whether it went
    // forwards (* or /) or back (#), which n keeps to, as in Vim; and gn's run through matches
    // (see operateOnMatch): where its last change was, whether the run goes on, and whether
    // the key being handled kept it going.
    struct SearchState
    {
        std::optional<core::EventQuery> query;
        std::string text;
        bool forward = true;
        std::optional<SongCursor> gnLast;
        bool gnRun = false;
        bool gnKept = false;
    };
    SearchState searching_;
    // Searching for what's typed, / and Enter alone clearing the matches; going to the next
    // match in the search's direction, or the one before; and searching for what's under the
    // cursor (* and #).
    bool search(const std::string& text);
    bool goToMatch(bool next, int count);
    bool searchHere(bool forward, int count);
    // The , s group: selecting the bar, section, track or what's visible, or the matches in
    // them, or narrowing a selection to its matches (, s m).
    bool select(std::string_view command);
    // An operator's target narrowed to the selection's matches, saying so if there are none;
    // whether there are any. And d and y on them.
    bool onlyMatches(OperatorTarget& target);
    bool operateOnMatches(std::string_view op);
    // d or y on events: taken as a register holds them, from a time, with a span (0 for up to
    // the last's end), and deleted for d.
    bool takeTarget(std::string_view op, const OperatorTarget& target, core::Tick from,
                    core::Tick span);
    // gn: an operator on the match at the cursor, or the next, the cursor going to it; and
    // without one, selecting it. While running through matches with . and n, the status line
    // says how (decided November 2026, as it's seldom used): where the last change was, so .
    // moves on from it though it still matches, and whether the run goes on.
    bool operateOnMatch(std::string_view op, int amount);
    bool selectMatch();
    bool toMatchForGn();
    // A target an operator takes rather than asking the view, as gn's.
    std::optional<OperatorTarget> presetTarget_;
    // The transforms: ! and a range keeps the range while the transform menu waits for a key.
    std::optional<OperatorTarget> transformTarget_;
    bool openTransformMenu(OperatorTarget target);
    std::vector<Key> transformKeys() const;
    CommandResult transformLine(std::string_view name, std::string_view arguments);
    bool applyTransform(std::string_view name, const OperatorTarget& target,
                        const std::string& text);
    bool velocityOverTime(std::string_view name, const OperatorTarget& target,
                          const std::string& text);
    bool velocityInChords(std::string_view name, const OperatorTarget& target,
                          const std::string& text);
    std::chrono::milliseconds chordWindow_{30};
    // Keys, asked for each time (decided November 2026), the last used offered again, with a
    // history of their own for Up and Down.
    std::vector<core::Scale> userScales_;
    std::string lastKey_ = "C major";
    std::vector<std::string> keySuggestions_; // for the range asked about, best first
    void askKey(const OperatorTarget& target, std::function<void(const core::Key&)> then);
    void askKeyed(const std::string& op, const OperatorTarget& target, const core::Key& key);
    bool applyKeyed(std::string_view op, const OperatorTarget& target, const core::Key& key,
                    const std::string& text);
    std::string firstPitch(const OperatorTarget& target) const;
    core::VelocityLimits velocityLimits_;
    QuantizePedal quantizePedal_ = QuantizePedal::Follow;
    bool quantizeMarks_ = true;
    bool refused_ = false; // the last key's command, refused or given up
    std::chrono::milliseconds humanizeTime_{10};
    int humanizeVelocity_ = 8;
    std::mt19937_64 random_{std::random_device{}()};
    // :label-to-section on a selection, going through its labels, asking about those off a
    // bar line.
    struct LabelRun
    {
        SongCursor from; // where the cursor goes back to
        std::size_t next = 0;
        std::vector<std::pair<core::Tick, std::string>> starts; // a section's start, its name
        std::vector<core::Tick> labels;                         // the labels turned
        int atStarts = 0;
        int sharing = 0;
        int declined = 0;
        core::Tick first = 0; // the selection's span, outside which labels are passed over
        core::Tick last = 0;
    };
    void runLabels(std::shared_ptr<LabelRun> run);
    void finishLabels(const LabelRun& run);
    EventRegister register_;
    std::string message_;
    MessageKind messageKind_ = MessageKind::Information;
    LineHandler lineHandler_;
    PortFinder portFinder_;

    // Moving tracks through the others, from , t o until Enter or Escape.
    struct TrackMove
    {
        std::size_t first = 0; // where they are now
        std::size_t count = 1;
        std::size_t start = 0; // where they started, for Escape
        std::string mergeKey;  // so that the moves are one undo step, and another's a new one
        int number = 0;        // a count being typed
        bool g = false;        // g typed, waiting for the other
    };
    std::optional<TrackMove> trackMove_;
    std::uint64_t trackMoves_ = 0;
    Mode mode_ = Mode::Normal;
    int width_ = 0;
    int height_ = 0;
    CellSize cell_{1, 1};
    WindowLayout layout_;
    core::Player* player_ = nullptr;
    bool rewind_ = true;
    bool wasPlaying_ = false;     // or stopping, until the player knows where it stopped
    bool following_ = true;       // the views following the playhead, until moved away from it
    core::Tick lastPlayhead_ = 0; // where the playhead was last shown
    std::optional<SongCursor> playedFrom_; // the cursor when playback started
    bool returning_ = false;               // stopping with play-stop-back, back to it
    std::int64_t lastTime_ = 0;            // of the last key or wake-up
    std::optional<std::int64_t> inputDue_; // when to take MIDI input again, after some arrived
    std::size_t inputLost_ = 0;            // input messages lost, as last reported
    bool hintsOn_ = true;
    std::int64_t hintDelay_ = 400;
    std::optional<std::int64_t> waitingSince_; // when keys started longer bindings
    bool hintsShown_ = false;
    void noteWaiting(std::int64_t now);
    std::vector<Hint> hintsAfter(const Keymap& keymap, Mode mode, std::span<const Key> keys) const;
    // After an operator: its own key and the objects, then the motions, and how many are the
    // first.
    std::vector<Hint> operatorHints(std::size_t* objects = nullptr) const;
    // A find waiting for its note name (f), or after an operator for a sharp or flat (dfB),
    // which the hint menu shows.
    bool findWaiting() const;
    static std::vector<Hint> characterHints(std::string_view command);
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
        std::size_t scroll = 0; // the first match shown, when they don't all fit
    };
    // The prompt's state, together (gathered November 2026, after a code review): the : line,
    // and the lines that take its place for a search and a key, each with its own history, as
    // Vim keeps searches apart from commands; the question being answered, if any, its label
    // shown in place of the :, its default for an empty answer, and the transform asking and
    // which of its settings; the palette, while open; and completion, while it shows.
    struct PromptState
    {
        CommandLine line;
        CommandLine searchLine;
        CommandLine keyLine;
        bool searchPrompt = false;
        bool keyPrompt = false;
        std::string answerLabel;
        std::function<void(const std::string&)> answer;
        std::string answerDefault;
        std::string answerCommand;
        std::size_t answerSetting = 0;
        std::optional<PaletteState> palette;
        std::optional<Completing> completing;
    };
    PromptState prompt_;
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
    // The most matches the : prompt's completion shows at once, and how many it shows now.
    static constexpr std::size_t kCompletionRows = 10;
    static std::size_t completionRows(std::size_t matches)
    {
        return std::min(matches, kCompletionRows);
    }
    Region completionRegion(const Completions& shown) const;
    Region keysRegion() const;
    Region hintRegion(const std::vector<Hint>& hints) const;
    Region operatorHintRegion(const std::vector<Hint>& hints, std::size_t objects) const;
    std::string hintHeading() const;
};

} // namespace modal::ui
