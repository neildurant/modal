# Command-line tools

Milestone 1's tools, for playing songs and measuring timing until the GUI exists. Each takes `--help`. They're built with everything else: on Windows in `build/windows-msvc/apps/Debug` or `Release`, on Linux in `build/linux-gcc/apps/Debug` or `Release` (and likewise for `linux-clang`). Measure timing with a Release build.

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

## Timing tests without MIDI

Two hidden tests measure the machine's timer and the engine's waking with no MIDI involved. They're for diagnosis, so they aren't run by default:

```
build/windows-msvc/tests/Release/modal_tests.exe "[.timing]"
```

## ALSA tests on Linux

On a native Linux install with `snd-seq-dummy` loaded (it provides the "Midi Through" ports), set `MODAL_TEST_ALSA_SEQ=1` before running the tests to check port listing and sending notes through Midi Through. Without it, those checks are skipped, as in CI and WSL.
