# Configuration

Modal is configured with a plain-text command file, `modalrc`, in the style of a Vim `vimrc`. It holds the same commands as the `:` command line will, one a line.

## Where the files go

| System  | Config directory |
|---------|------------------|
| Linux   | `$XDG_CONFIG_HOME/modal`, or `~/.config/modal` |
| Windows | `%APPDATA%\Modal` |

At startup Modal runs its built-in command file (the default key bindings), then `modalrc` in the config directory, if there is one. Your own themes go in `themes/` in the same directory.

If Modal ever stops on an error it didn't expect, it adds a line saying what it was to `crash.log` in the same directory, with the time and Modal's version. Including that line in a bug report helps find the cause.

To start a `modalrc`, have Modal write one with everything in it: at the `:` prompt, `:write-config modalrc` writes it straight into the config directory (`:write-config!` to overwrite one that's there), or from a terminal:

```
modal --write-config modalrc
```

which won't overwrite a file unless you add `--force`. Whatever you've changed from the defaults (in your `modalrc`, or since starting) is written as commands; everything else is there commented out at its default, with what it does. Uncomment and change only the lines you want, and the rest keep following Modal's defaults as they improve.

If a command fails, the first error shows in the status line until you press a key, as `modalrc:12: unknown setting "fontsiz"`, and the rest of the file still runs. On Linux all errors are also printed to stderr.

## The : prompt

`:` opens a prompt in the bottom bar, as in Vim, where any of the commands below can be typed, and Enter runs them. They take effect at once: a new theme, font size or key binding applies straight away. Escape closes the prompt, Up and Down recall the lines run before (for the session), and Backspace, Delete, the arrow keys, Home, End and `Ctrl-U` edit the line.

As well as the commands below, the prompt takes:

- `set NAME?` shows a setting's value, such as `fontsize=12`. Several can be asked for at once.
- `source` with no file runs your `modalrc` again, after you've edited it. It runs over what's set already, so a line you've deleted keeps its effect until Modal restarts.
- `write-config FILE` writes the config, as above. A relative FILE is in the config directory. `write-config!` overwrites.
- `section NAME` starts a new section at the bar line of the cursor's bar, splitting the section it's in; with no name it's called Section and a number. If the bar line is in the middle of a bar on the conductor or a track with its own meter, that bar ends short, and the message says so.
- `marker-to-section` starts a section at the marker at or before the cursor, at the bar line of its bar, named after it, and the marker goes; it asks first unless the cursor is on a marker on a bar line. `marker-to-section-all` does it for every marker, asking about each marker that isn't on a bar line (Escape stops, with nothing done), and makes the sections as one undo step.
- `bar-delete` deletes the cursor's bar on every track, in the conductor's bars, with the tempo and meter changes and markers in it, and what's after it in the song moves up; `bar-delete 3` deletes three bars from it. The section it was in stays one section, and the bars go in the register as a section named after the one they came from ("From Verse"), so `p` on the section strip puts them back as one. `bar-insert` puts an empty bar in before the cursor's bar on every track, in the section there; `bar-insert 2 3/4` puts in two bars of 3/4, the meter after them going on as before. Both are refused, saying which track, where a track with a meter of its own has bars that wouldn't line up.
- `port NAME` sets the current track's output port, by its full name or any part of it that matches only one port, ignoring case; `port` alone says which port the track plays to and lists the output ports.
- The file commands, as in Vim: `e FILE` (or `edit`) opens a song or imports a MIDI file, `w` (or `write`) saves, `w FILE` saves as FILE, `export FILE` writes a Standard MIDI File, `new` starts a new song, `q` (or `quit`) quits and `wq` saves and quits. With no FILE, `e`, `w` (for a song never saved) and `export` show the system's file dialog. `e`, `new` and `q` refuse while there are changes not saved, and `w FILE` and `export FILE` refuse to write over a file; add `!` to go ahead anyway (`q!`, `w! FILE`). A FILE with no extension gets `.modal` (or `.mid` for `export`).

Any command keys can be bound to can be run at the prompt too, by name, such as `:track-next`; one that takes a count takes it after its name, so `:cursor-down 3` moves down three rows. `write-config` and `quit` can't go in `modalrc`, and the movement and window commands can't either, since there's nothing to move when it runs; the file says so if they're there.

## Commands

A line starting with `"` is a comment.

```
" Bigger text, and a shorter wait for key sequences
set fontsize=14 timeoutlen=400
set font="C:/My Fonts/Iosevka.ttf"

" Q to go to the top, with a hint for the leader menu
nmap Q go-first "Top"
nunmap <PageUp>

set theme=light
highlight cursor #ff9e64

source more-keys
```

- `set NAME=VALUE ...` sets settings. Several can go on one line. A setting that's on or off can also be set as in Vim: `set rewind` turns it on, `set norewind` off, and `set rewind!` the other way.
- `nmap KEYS COMMAND ["HINT"]` binds keys in Normal mode to a command, with an optional hint. `vmap`, `imap`, `omap` and `cmap` do the same for Visual, Insert, operator-pending and command-line mode (for later milestones). `nunmap KEYS` (and `vunmap` and so on) removes a binding.
- `ngroup KEYS "TITLE"` gives keys that start longer bindings a title for the hint menu, such as `ngroup <leader>t "Toggles"`; `vgroup` and the rest are for the other modes.
- `highlight ROLE #rrggbb` sets the colour of a theme role.
- `source FILE` runs another command file, found relative to the file running it.

Arguments are separated by spaces. As in a shell, double quotes keep spaces in an argument, with `\"` for a quote and `\\` for a backslash. A map command's keys are taken as they are, since `"` is itself a key.

### Keys

Keys are written as in Vim. A typed character stands for itself (`j`, `G`, `$`); `<` is written `<lt>` and space `<Space>`. Other keys go in angle brackets: `<Esc>`, `<Tab>`, `<CR>`, `<BS>`, `<Del>`, `<Insert>`, `<Up>`, `<Down>`, `<Left>`, `<Right>`, `<Home>`, `<End>`, `<PageUp>`, `<PageDown>`, `<F1>` to `<F12>`, and the keypad's `<kPlus>`, `<kMinus>` and `<k0>` to `<k9>`, which arrive only with Ctrl or Alt (otherwise the keypad types its characters). Modifiers come first: `C-` for Ctrl, `A-` (or `M-`) for Alt, `S-` for Shift, as in `<C-d>`, `<S-Tab>` and `<C-S-d>`. With Ctrl or Alt, a letter is the same in either case, so Ctrl-Shift-d is written `<C-S-d>`. A sequence of keys is written one after another: `gg`, `<C-w>j`. `<leader>` is the leader key, comma unless the `leader` setting names another: `nmap <leader>n note-default`. It's matched to the setting as you type, so changing `leader` moves every leader binding, the built-in ones too.

When a sequence is bound as well as a longer one starting with it (such as `z` and `zz`), Modal waits up to `timeoutlen` milliseconds for the next key before running the shorter.

After keys that start longer bindings, such as `g`, `z`, `]` or the leader, and after an operator such as `d`, a pause of `hintdelay` milliseconds shows the hint menu above the status line: each key that can come next and what it does, keys that start more marked `+` with their group's title. After an operator it lists the ranges and objects with what each would take in the view you're in. Keep typing to go on, or Escape to give the keys up. `set nohints` turns it off.

## Text size

`Ctrl-+` and `Ctrl--` make the text a point bigger or smaller, on the main keyboard or the keypad, and `Ctrl-0` goes back to the size your config sets. The window keeps its size and shows more or fewer cells. The new size shows in the status line until the next key. To start at another size, set `fontsize`.

## Settings

| Setting      | Default | Meaning |
|--------------|---------|---------|
| `theme`      | `dark`  | The colour theme: `themes/NAME` in the config directory, or a built-in theme, `dark` or `light` |
| `font`       | (empty) | A TrueType or OpenType font file, or empty for the built-in JetBrains Mono |
| `fontsize`   | `12`    | The font size in points (4 to 72), before the display's own scaling |
| `minsize`    | `120x30` | The smallest the window can be made, in cells |
| `timeoutlen` | `500`   | How long to wait, in milliseconds, for the rest of a key sequence (0 to 10000) |
| `leader` | `,` | The leader key, which `<leader>` stands for in key bindings: one key, such as `,` or `<Space>` |
| `hints` | `on` | Whether the hint menu shows, after a pause, the keys that can follow those typed |
| `hintdelay` | `400` | How long to wait, in milliseconds, before the hint menu shows (0 to 10000; 0 shows it at once) |
| `chordwindow` | `30`  | How far apart, in milliseconds, events can start and still be one chord, for `(` and `)` (0 to 1000) |
| `gridadvance` | `off` | Whether `O` in the piano roll moves the cursor on by the grid step (on) or by the note's length (off); `, t a` turns it on and off |
| `nudge` | `5` | How far, in milliseconds, `Alt-H` / `Alt-L` nudge a note in the piano roll and `+` / `_` lengthen or shorten it (1 to 1000) |
| `undolevels` | `1000`  | How many changes can be undone (0 to 100000) |
| `ownports`   | `1`     | How many MIDI output ports of its own Modal makes, for other programs such as a plugin host to connect to (0 to 16; Linux only, since Windows can't) |
| `ownportname` | `Modal` | The name of Modal's own ports: one is `Modal`, several are `Modal 1`, `Modal 2` and so on |
| `port`       | (empty) | The output port for tracks without one: its name, or any part of it that matches only one port, or empty for the first port found |
| `rewind`     | `on`    | Whether the play-from marker, which Space plays from, stays put when playback stops (`on`), or moves to where it stopped (`off`) |

## Themes

A theme is a command file of `highlight` lines, one for each role. `set theme=NAME` starts again from the dark theme, then runs `themes/NAME` from the config directory, or else the built-in theme of that name. So a theme of your own only needs the colours it changes, and `highlight` lines after `set theme` change single colours on top of a theme.

The roles are:

| Role | For |
|------|-----|
| `background`, `text`, `dim`, `accent`, `warning` | the window and its text |
| `message` | a message in the status line, such as the new text size |
| `cursor`, `cursor-text` | the cursor and the text on it |
| `selection` | the background of what's selected |
| `playhead` | the background of what's playing, such as the event list's row |
| `bar`, `bar-text` | the status line and other bars |
| `panel` | the background of the inspector |
| `chrome` | the window around the view (the event list, piano roll or arrangement) and the gaps between bars; the view itself is on `background` |
| `mode`, `mode-text` | the mode indicator, such as NORMAL |
| `grid-line`, `beat-line`, `bar-line` | lines in timelines |
| `note`, `marker` | notes, and markers |
| `density-1` to `density-4` | how busy a stretch of a track is in the arrangement, from sparse to dense |
| `hint-key`, `hint-group` | a key in the hint menu, and a group of keys that leads to more |

`:write-config` lists the colours of every role in the theme you've chosen.
