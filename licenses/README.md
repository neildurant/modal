# Third-party licences

Modal itself is licensed under the GNU General Public License, version 3 or later (see
[`LICENSE`](../LICENSE) at the top of the repository).

The programs Modal builds include the libraries and font below, each under its own licence, all
compatible with the GPL. The files here are those licences, as vcpkg installs them alongside each
library; they go with any copy of Modal's programs that's passed on.

| Component | Used for | Licence | File |
|-----------|----------|---------|------|
| [SDL3](https://libsdl.org) | The window, drawing and keyboard | zlib (with some bundled parts under their own permissive licences, listed in the file) | [`sdl3.txt`](sdl3.txt) |
| [SDL_ttf](https://github.com/libsdl-org/SDL_ttf) | Drawing text | zlib | [`sdl3-ttf.txt`](sdl3-ttf.txt) |
| [FreeType](https://freetype.org) | Reading the font, through SDL_ttf | The FreeType License (FTL), one of its two licences | [`freetype.txt`](freetype.txt) |
| [zlib](https://zlib.net) | Used by FreeType and libpng | zlib | [`zlib.txt`](zlib.txt) |
| [libpng](http://www.libpng.org) | Used by FreeType | libpng | [`libpng.txt`](libpng.txt) |
| [bzip2](https://sourceware.org/bzip2/) | Used by FreeType | BSD-style | [`bzip2.txt`](bzip2.txt) |
| [Brotli](https://github.com/google/brotli) | Used by FreeType | MIT | [`brotli.txt`](brotli.txt) |
| [libremidi](https://github.com/celtera/libremidi) | MIDI input and output | BSD-2-Clause, and the MIT-style licences of the code it derives from | [`libremidi.txt`](libremidi.txt) |
| [JetBrains Mono](https://www.jetbrains.com/lp/mono/) | The built-in font | SIL Open Font License 1.1 | [`jetbrains-mono.txt`](jetbrains-mono.txt) |

FreeType asks for this credit, under the FreeType License:

> Portions of this software are copyright © The FreeType Project (https://freetype.org). All
> rights reserved.

The tests also use [Catch2](https://github.com/catchorg/Catch2) (Boost Software License 1.0),
which isn't part of the programs. On Linux, ALSA, X11 and Wayland are the system's own
libraries, used where they're installed rather than built in.
