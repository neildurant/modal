# Building and maintaining the manual

The manual is a Quarto book: the same `.qmd` chapter files produce a website and
a continuous PDF. The initial draft follows the accepted sample chapters'
explanatory style; remaining review and work states belong in
[the schedule](../schedule.md#user-manual).

## Build

Install **Quarto 1.11.9**, using the matching release from
[Quarto's releases](https://github.com/quarto-dev/quarto-cli/releases/tag/v1.11.9),
and CMake 3.25 or later. Quarto includes Pandoc, Typst and the Typst book
template and packages used here. No R, Python, Jupyter, LaTeX installation,
application build or online publishing account is needed. The PDF uses a
small local Typst template partial for its cover and chapter styling.

From the repository root, on Linux or Windows:

```text
cmake -P tools/build-manual.cmake
```

The output is `build/manual/index.html` and `build/manual/modal-user-manual.pdf`.
The `examples` folder beside them contains the example song. The website
includes its fonts, scripts, styles, images and search index; hosting it only
requires serving this directory as static files. There are no external font
or script services. Open `index.html` directly for a quick reading check;
use a local static web server when checking search and hosting behaviour.

To specify the executable when it is not on PATH:

```text
cmake -DQUARTO_EXECUTABLE=/path/to/quarto -P tools/build-manual.cmake
```

Pass CMake `-D` options before `-P`. A path containing spaces needs shell quoting.
Both formats are built together so the website's PDF download is always present.
Each successful build replaces `build/manual` with the new output;
the last successful output is retained if rendering fails.

The build stages sources in `build/manual-source`, copies the application's
existing JetBrains Mono fonts there, and renders that copy. Edit the sources
in `docs/manual`, not the staged copy. Font licences accompany the output.
The source font files remain owned by `assets/fonts`; they are not duplicated
in this directory. Body text uses local browser serif fonts for HTML and
Typst's bundled Libertinus Serif for PDF.

The publisher is pinned to keep template and rendering changes deliberate.
Upgrading it means updating `_quarto.yml`, `tools/build-manual.cmake` and this
file, then reviewing both outputs. Publication after setup should need no
network access: avoid remote includes, downloaded fonts, external images
and Typst packages that are not bundled with the pinned Quarto release.

## Sources and styling

- `_quarto.yml` defines reading order, metadata and output formats.
- `index.qmd` introduces the book and key notation.
- `first-session.qmd` explains the window, navigation, discovery and MIDI routing.
- `editing-language.qmd` teaches selection, operators, objects, ranges and scope.
- `notes.qmd`, `arrangement.qmd` and `tracks-recording.qmd` develop note editing,
  song structure, track setup, transport and recording.
- `performance.qmd` and `controllers.qmd` explain transforms, playback settings,
  lanes and pedalling.
- `efficient-work.qmd`, `customisation.qmd` and `files.qmd` cover search, memory,
  preferences and file exchange.
- The reference chapters collect compact key tables and include generated
  settings and transform definitions from `reference/`.
- `examples/first-phrase.modal` is the common starting point for worked examples.
- `images/` holds actual offscreen renders of that song, committed so publishing
  does not require building Modal.
- `_brand.yml` holds the shared accent palette. `styles/` holds the HTML light
  and dark themes, common CSS and PDF typography.

Add a chapter to `book.chapters` in reading order. Use stable explicit section
IDs (`{#sec-name}`) and Quarto references (`@sec-name`) so links become chapter
and section references in the PDF as well. Give figures an ID, a caption and
`fig-alt` text describing their useful content. Explain the information in the
text too, so understanding never depends on reading small details in an image.

Key sequences use inline code; distinguish simultaneous keys (`Ctrl-P`) from
sequences (`gg`). Write for someone studying the book away from a computer:
explain what a facility does, why it is useful and how its parts fit together.
Use key mnemonics to support understanding (`ga`, go to arrangement; `ib`,
in bar; `ab`, around bar), without presenting a mnemonic as a universal rule.
Examples should state their musical context and describe the result in the
text. Keep procedural walkthroughs for separate tutorials rather than making
the reader follow a chain of edits to understand a chapter.

Introduce a concept before using its terminology, including in tables and
captions. Explain Vim as an influence before referring to it, identify screen
regions visually before directing readers to them, and distinguish Linux and
Windows path examples explicitly. Describe `v` and `V` as vertical and horizontal
selections in the timeline views, alongside their precise scope and exceptions.
Name arrow-key alternatives alongside `h`/`j`/`k`/`l`, including modified
arrows where the default bindings support them. Explain the direction before
the letter in context: Down (`j`) returns from the section strip to the tracks.
Check actual bindings rather than assuming every modified letter has an arrow
equivalent. Explain commands and bindings early, and give a named command's
default leader/key route alongside it when one exists. Keep unbound commands
available through the palette, prompt or a documented personal binding.
Make the independent `a`–`z` names for marks, registers and macros explicit.
Refer to implemented behaviour;
`design.md` also contains proposals, so check the grammar, command registry,
default bindings and Workspace tests before teaching a feature.

The manual explains the application for users. The engineering design,
implementation grammar, configuration specification, decisions, schedule and
handoff retain their existing roles. Any later consolidation should preserve
their distinct purposes.

## Verify examples and refresh illustrations

Two normal `[manual-reference]` tests compare the committed reference includes
against `Settings`, the transform specifications and default bindings. A change
to those definitions fails the tests until the tables are refreshed. With a
built test executable:

```text
cmake -DMODAL_TESTS=/absolute/path/to/modal_tests -P tools/manual-references.cmake
```

Review the generated diff alongside the prose: an accurate new default does
not automatically update the chapter that explains it. The includes are
committed so ordinary publication needs neither a C++ build nor a MIDI setup.

The `[manual]` test reads the actual supplied song and drives Workspace with
the chapter's key sequences, at multiple PPQNs. Run it with a built test
executable, for example:

```text
build/linux-gcc/tests/Debug/modal_tests "[manual]"
```

On Windows use the corresponding `windows-msvc` path and `.exe`. The hidden
`[.manual-images]` case draws the illustrations using SDL's offscreen driver,
including two labelled window guides. The callouts are extra drawing regions
in a gutter, generated alongside the application frame for review.
It never opens or drives the real desktop. To refresh the checked-in PNGs,
install ImageMagick 7 (`magick` on PATH), build the tests, then run:

```text
cmake -DMODAL_TESTS=/absolute/path/to/modal_tests -P tools/manual-images.cmake
```

On Linux, if the desktop's temporary directory is unavailable to the build,
run with `env -u XDG_RUNTIME_DIR TMPDIR=/tmp` as for the application checks.
Review the resulting PNG changes and rebuild both publication formats.
ImageMagick is only needed when refreshing images, not when publishing.

## Review and upkeep

Check the HTML in light and dark themes, at a narrow width and with keyboard
navigation. In the PDF, check the cover, contents, chapter openings, figures,
key sequences, table widths, page breaks and internal links. Check the examples
against an unchanged example song. MIDI routing and listening need a real setup;
offscreen tests do not establish hardware behaviour or timing results.

Update affected explanations, examples and references before a user-visible
feature or logical step is marked Done or published. A related series of
implementation commits may share a documentation follow-up, with the pending
work explicit in `docs/status.md`. Internal-only changes need no token prose
edit. At milestone boundaries, review affected chapters and regenerate figures
whose interface changed. Keep work states in the schedule and validation in
the handoff.

The application CI matrix runs the example and reference checks. The separate
`.github/workflows/manual.yml` installs the pinned Quarto version, builds both
formats and uploads `build/manual` for review. It uses the
[official Quarto setup action](https://github.com/quarto-dev/quarto-actions/tree/main/setup).
It does not deploy hosting or prove that every prose statement is correct.
Editorial review remains necessary for explanations, coverage and musical examples.
