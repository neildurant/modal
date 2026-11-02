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
#include <modal/ui/palette.hpp>
#include <modal/ui/piano_roll_view.hpp>
#include <modal/ui/remembered.hpp>
#include <modal/ui/view.hpp>

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
#include <vector>

namespace modal::ui
{

class Settings;

// Whether a command is an operator, waiting for a range or object: d, y, > and the others.
bool isOperator(std::string_view command);

// A transform's options, NAME=VALUE after its amount: how each is written, for the hint under
// its question, and the values Tab offers, note lengths too where it takes one.
struct TransformOption
{
    std::string_view name;
    std::string_view form;
    std::vector<std::string_view> values = {};
    bool lengths = false;
};

std::vector<TransformOption> transformOptions(std::string_view command);

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
    // cursor stays where it was; otherwise the play-from marker moves to where playback stops, and
    // the cursor follows the playhead there.
    void setRewind(bool rewind);

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
    // How far apart notes can start and still be a chord, for the chord tools.
    void setChordWindow(std::chrono::milliseconds window) { chordWindow_ = window; }
    // How velocity changes keep within their limits: the velclip and velfloor settings.
    void setVelocityLimits(core::VelocityLimits limits) { velocityLimits_ = limits; }

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
    // What a listing (:registers) shows, a name and what it is a line, until the next key.
    const std::vector<std::pair<std::string, std::string>>& listing() const { return listing_; }

    // What the unnamed register holds: an area's events, yanked or deleted; and those of its
    // first track alone.
    const EventRegister& registerArea() const { return register_; }
    const std::vector<core::Event>& registerEvents() const;

    // Shows a message in the status line until the next key.
    std::optional<core::EventQuery> findQuery() const override { return findQuery_; }
    std::optional<core::EventQuery> searchQuery() const override { return search_; }
    void searchWrapped(bool forward) override;
    std::optional<core::Tick> jumpTime() const override
    {
        return jumpTime_ >= 0 ? std::optional{jumpTime_} : std::nullopt;
    }

    void setMessage(std::string message, MessageKind kind) override
    {
        message_ = std::move(message);
        messageKind_ = kind;
        // A warning in a macro is a command failing, which stops it.
        macroFailed_ = macroFailed_ || (kind == MessageKind::Warning && macroDepth_ > 0);
    }
    const std::string& message() const { return message_; }

private:
    void startTyping() override { mode_ = Mode::CommandLine; }
    void openPrompt(std::string line) override;
    void ask(std::string question, std::function<void()> yes, std::function<void()> no = {},
             std::function<void()> stop = {}) override;
    void playFrom(core::Tick tick) override;
    std::optional<Selection> selection() const override { return selection_; }
    void chosenMeter(core::TimeSignature meter) override;
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
    // Labels made section starts, the one at or before the cursor, or all of them.
    CommandResult labelToSection();
    // :label-to-section on a selection: every label from one time to another.
    CommandResult labelsToSections(core::Tick from, core::Tick to);
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
    // The arguments of the command at the prompt not yet typed, as a hint after the line.
    std::string argumentHint() const;
    // The command palette, open at the prompt: the commands it searches, those matching what's
    // typed, best first, a command's keys (none if it has none), its keys while it's open, and
    // the list above the prompt.
    struct PaletteState
    {
        std::optional<std::size_t> chosen; // among the matches, 0 the best; none at first
    };
    static constexpr std::ptrdiff_t kPaletteRows = 12;
    std::optional<PaletteState> palette_;
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
    char32_t recording_ = 0;
    std::vector<Key> recorded_;
    std::size_t recordedBefore_ = 0;
    char32_t lastMacro_ = 0;
    int macroDepth_ = 0;
    bool macroFailed_ = false;
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
    std::optional<Naming> naming_;
    std::optional<core::EventQuery> findQuery_;
    std::string findName_;
    bool findForward_ = true;
    bool findRepeats_ = false;
    bool findKept_ = false;
    bool startFind(bool forward, int count);
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
    std::vector<Key> changeKeys_; // typed since Normal mode was last idle
    std::vector<Key> lastChange_;
    std::uint64_t versionBefore_ = 0;
    bool notAChange_ = false; // undo, redo or . ran among them
    bool replaying_ = false;
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
    // An answer given nothing takes the default, if there's one, as a transform's does.
    void askFor(std::string label, std::function<void(const std::string&)> answer,
                std::string defaultAnswer = {});
    std::string answerDefault_;
    std::string answerCommand_; // the transform asking, for its options
    std::string optionsCommand() const;
    std::string answerLabel_;
    std::function<void(const std::string&)> answer_;
    // Search (/C1): what the last looks for, as typed and as read, whether it went forwards (*
    // or /) or back (#), which n keeps to, as in Vim, and whether the prompt is for one.
    std::optional<core::EventQuery> search_;
    std::string searchText_;
    bool searchForward_ = true;
    bool searchPrompt_ = false;
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
    std::optional<SongCursor> gnLast_;
    bool gnRun_ = false;
    bool gnKept_ = false;
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
    CommandLine keyLine_;
    bool keyPrompt_ = false;
    std::vector<std::string> keySuggestions_; // for the range asked about, best first
    void askKey(const OperatorTarget& target, std::function<void(const core::Key&)> then);
    void askKeyed(const std::string& op, const OperatorTarget& target, const core::Key& key);
    bool applyKeyed(std::string_view op, const OperatorTarget& target, const core::Key& key,
                    const std::string& text);
    std::string firstPitch(const OperatorTarget& target) const;
    core::VelocityLimits velocityLimits_;
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
    Mode mode_ = Mode::Normal;
    CommandLine line_;
    // The / prompt's line, with its own history, as Vim keeps searches apart from commands; it
    // takes line_'s place while searching.
    CommandLine searchLine_;
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
    bool hintsOn_ = true;
    std::int64_t hintDelay_ = 400;
    std::optional<std::int64_t> waitingSince_; // when keys started longer bindings
    bool hintsShown_ = false;
    void noteWaiting(std::int64_t now);
    std::vector<Hint> hintsAfter(const Keymap& keymap, Mode mode, std::span<const Key> keys) const;
    std::vector<Hint> operatorHints() const;
    // A find waiting for its note name (f), or after an operator for a sharp or flat (dfB),
    // which the hint menu shows.
    bool findWaiting() const;
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
