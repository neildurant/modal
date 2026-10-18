# Configuration

Modal is configured with a plain-text command file, `modalrc`, in the style of a Vim `vimrc`. It holds the same commands as the `:` command line will, one a line.

## Where the files go

| System  | Config directory |
|---------|------------------|
| Linux   | `$XDG_CONFIG_HOME/modal`, or `~/.config/modal` |
| Windows | `%APPDATA%\Modal` |

At startup Modal runs its built-in command file (the default key bindings), then `modalrc` in the config directory, if there is one. Your own themes go in `themes/` in the same directory.

To start a `modalrc`, have Modal write one with everything in it, each line commented out at its default:

```
modal --write-config modalrc
```

It won't overwrite a file unless you add `--force`. Move the file into the config directory, and uncomment and change only the lines you want; the rest keep following Modal's defaults as they improve.

If a command fails, the first error shows in the status line until you press a key, as `modalrc:12: unknown setting "fontsiz"`, and the rest of the file still runs. On Linux all errors are also printed to stderr.

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

- `set NAME=VALUE ...` sets settings. Several can go on one line.
- `nmap KEYS COMMAND ["HINT"]` binds keys in Normal mode to a command, with an optional hint. `vmap`, `imap`, `omap` and `cmap` do the same for Visual, Insert, operator-pending and command-line mode (for later milestones). `nunmap KEYS` (and `vunmap` and so on) removes a binding.
- `highlight ROLE #rrggbb` sets the colour of a theme role.
- `source FILE` runs another command file, found relative to the file running it.

Arguments are separated by spaces. As in a shell, double quotes keep spaces in an argument, with `\"` for a quote and `\\` for a backslash. A map command's keys are taken as they are, since `"` is itself a key.

### Keys

Keys are written as in Vim. A typed character stands for itself (`j`, `G`, `$`); `<` is written `<lt>` and space `<Space>`. Other keys go in angle brackets: `<Esc>`, `<Tab>`, `<CR>`, `<BS>`, `<Del>`, `<Insert>`, `<Up>`, `<Down>`, `<Left>`, `<Right>`, `<Home>`, `<End>`, `<PageUp>`, `<PageDown>`, `<F1>` to `<F12>`. Modifiers come first: `C-` for Ctrl, `A-` (or `M-`) for Alt, `S-` for Shift, as in `<C-d>`, `<S-Tab>` and `<C-S-d>`. With Ctrl or Alt, a letter is the same in either case, so Ctrl-Shift-d is written `<C-S-d>`. A sequence of keys is written one after another: `gg`, `<C-w>j`.

When a sequence is bound as well as a longer one starting with it (such as `z` and `zz`), Modal waits up to `timeoutlen` milliseconds for the next key before running the shorter.

## Settings

| Setting      | Default | Meaning |
|--------------|---------|---------|
| `theme`      | `dark`  | The colour theme: `themes/NAME` in the config directory, or a built-in theme, `dark` or `light` |
| `font`       | (empty) | A TrueType or OpenType font file, or empty for the built-in JetBrains Mono |
| `fontsize`   | `12`    | The font size in points (4 to 72), before the display's own scaling |
| `minsize`    | `80x24` | The smallest the window can be made, in cells |
| `timeoutlen` | `500`   | How long to wait, in milliseconds, for the rest of a key sequence (0 to 10000) |

## Themes

A theme is a command file of `highlight` lines, one for each role. `set theme=NAME` starts again from the dark theme, then runs `themes/NAME` from the config directory, or else the built-in theme of that name. So a theme of your own only needs the colours it changes, and `highlight` lines after `set theme` change single colours on top of a theme.

The roles are:

| Role | For |
|------|-----|
| `background`, `text`, `dim`, `accent`, `warning` | the window and its text |
| `cursor`, `cursor-text` | the cursor and the text on it |
| `selection` | the background of what's selected |
| `bar`, `bar-text` | the status line and other bars |
| `mode`, `mode-text` | the mode indicator, such as NORMAL |
| `grid-line`, `beat-line`, `bar-line` | lines in timelines |
| `note`, `marker` | notes, and markers |
| `density-1` to `density-4` | how busy a stretch of a track is in the arrangement, from sparse to dense |

`modal --write-config` lists the dark theme's colours for every role. The test view's samples page (Tab to it) shows each role in the current theme.
