# Command-line tools

The command-line tools, for playing and recording songs and measuring timing until the GUI exists. Each takes `--help`. They're built with everything else: on Windows in `build/windows-msvc/apps/Debug` or `Release`, on Linux in `build/linux-gcc/apps/Debug` or `Release` (and likewise for `linux-clang`). Measure timing with a Release build.

Port names can be given in full, or as any part that matches only one port, ignoring case. Quote names with spaces: `--port "My Synth Out"`.

## modal-ports

Lists the MIDI input and output ports Modal can see, and which MIDI API it's using. No options.

## modal-play

Plays a Standard MIDI File, every track to one output port.

```
modal-play --port NAME [--start BAR[:BEAT]] [--all-notes-off] [--reset-sustain] [--spin MS] FILE.mid
modal-play --port NAME --panic
```

- `--start` starts from a bar, or a bar and beat (`17:3`), chasing notes already sounding there.
- `--all-notes-off` and `--reset-sustain` send All Notes Off and sustain off on each channel used when stopping.
- `--spin` sets how long before each event the engine busy-waits instead of sleeping (default 1.5 ms; 0 turns it off).
- `--panic` sends All Notes Off and Reset All Controllers on every channel of the port, then exits.

While playing: space stops, or resumes from where it stopped; `!` panics; `q`, Esc or Ctrl-C stops and exits. It exits by itself at the end of the song.

It prints a summary of the song first, and its send lateness at the end: how long after its due time each message was handed to the MIDI driver, split into waking (the engine's timer) and sending (the driver call).

## modal-record

Records MIDI input to a Standard MIDI File: onto a new track, or with `--overdub` into a track of the file it plays along with.

```
modal-record --in NAME --out NAME [options] OUT.mid
```

- `--in` is an input to record from; repeat it to record from several at once.
- `--out` is the output for playback, MIDI thru and the metronome; `--click` sends the metronome and count-in to another port instead.
- `--play FILE.mid` plays along with a file, and the take is added to it. `--overdub N` merges the take into track N of that file; otherwise it goes on a new track called "Recorded", on `--channel` (default 1), after the file's tracks. Without `--play`, `--tempo` (default 120) and `--meter` (default 4/4) set the bars.
- `--start BAR[:BEAT]` is where recording starts (default bar 1). Before it comes a 1-bar count-in, or `--count-in BARS`, `--count-in-beats N`, `--pre-roll BARS` (play the song from that many bars before) or `--no-count-in`.
- `--no-click` turns the metronome off while recording (a count-in still clicks); `--no-thru` stops the input being sent on to `--out`.
- `--type0` writes a type 0 file. An existing OUT.mid isn't overwritten without `--force`.
- `--spin`: as for `modal-play`.

While recording: space stops, saves and exits; Esc or Ctrl-C stops without saving, after asking; `!` panics and exits without saving. Stopping before the record point (during the count-in) saves nothing.

At the end it reports what was recorded, how many input messages arrived (and any dropped, which should be none), the thru delay (from a message's arrival to just after it was sent on) and the send lateness.

## modal-loopback

Measures timing through a MIDI cable from an output back to an input: it sends a steady stream of notes and reports how long each message took to come back, from the time it was due.

```
modal-loopback --port NAME [options]
modal-loopback --out NAME --in NAME [options]
```

- `--count N`: notes to send (default 2000).
- `--interval MS`: time between note-ons (default 10); each note lasts half of it.
- `--chord N`: notes sent at once, 1 to 16 (default 1). Later notes of a chord arrive later, since a 3-byte message takes about 1 ms on the cable.
- `--spin MS`: as for `modal-play`.

It reports:

- **Latency**: from each message's due time to its arrival back, as minimum, median, p99 and maximum.
- **Jitter**: p99 minus median, and the standard deviation of the latency.
- **After the send call**: from the end of the driver call to arrival, which is the interface and cable alone.
- **Send lateness**: as for `modal-play`.
- **Messages lost, out of order and unexpected**: these should all be 0.

### Checking recording: `--record`

With `--record`, the stream is also recorded, as `modal-record` would record it, and each recorded note is compared with the one sent. The stream becomes a song at 120 BPM, with each note at the tick at or before its time in the plain stream. MIDI thru is off, since it would send every note round the cable again. There's no metronome or count-in. Notes are matched by pitch and velocity.

- `--save FILE.mid`: also save the song as a type 1 file, with what was sent on a track "Sent" and what was recorded on a track "Recorded". It won't overwrite a file unless `--force` is given.

As well as the usual report, it gives:

- **Recorded, missing and extra**: how many of the notes sent were recorded. Missing and extra should be 0.
- **Late**: each recorded note's position less the one sent, in milliseconds and in ticks. This should be close to the latency, since a note is recorded where it arrived.
- **Longer**: each recorded note's length less the one sent. This should be close to 0, since a note-on and its note-off take the same time to come back.
- **Off arrival time**: notes recorded at a tick other than the one at which the note-on arrived, worked out from the arrival time and the time the song started. This checks the path from timestamp to tick, and should be 0.

The exit status is 0 only if nothing was lost, missing, extra or recorded off its arrival time.

## Timing tests without MIDI

Two hidden tests measure the machine's timer and the engine's waking with no MIDI involved. They're for diagnosis, so they aren't run by default:

```
build/windows-msvc/tests/Release/modal_tests.exe "[.timing]"
```

## ALSA tests on Linux

On a native Linux install with `snd-seq-dummy` loaded (it provides the "Midi Through" ports), set `MODAL_TEST_ALSA_SEQ=1` before running the tests to check port listing and sending notes through Midi Through. Without it, those checks are skipped, as in CI and WSL.
