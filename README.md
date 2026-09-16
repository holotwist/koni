[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=for-the-badge)](./LICENSE)

Koni is a lightweight, high-performance music player written in C. It includes a terminal interface (`koni`) and an optional graphical Raylib-based frontend (`koni-sparkles`).

## Koni - A Music Player

Koni features real-time audio visualizers, synchronized lyrics retrieval, parametric and graphic equalization, studio DSP processing (Krystal engine), desktop integration via MPRIS, ReplayGain, and gapless playback.

## Features

- Multi-format audio playback (MP3, FLAC, WAV, OGG Vorbis, Opus, AAC/M4A, PxTone, and DANA).
- Modular, self-registering codec architecture (missing dependencies disable individual codecs).
- Built-in PxTone synthesis engine and real-time tracker tab (CLI-only for now).
- 10-band Graphic Equalizer and 10-band Parametric EQ (PEQ) with AutoEQ preset support.
- DSP (Krystal) acoustic engine (3D spatial binaural audio, exciter, sub-bass synthesis, analog tape/tube saturation, and EBU R128 loudness compensation).
- SQLite-backed music library with background recursive scanning and multi-folder management.
- Multi-criteria library sorting (Title, Artist / Album, Album, Duration, File Path).
- Browser tabs (Queue, Music Library, Playlists, and Files) with `/` search filtering.
- Gapless playback transitions.
- High-density Unicode visualizers (CLI):
  - Spectrum analyzer.
  - Waveform oscilloscope.
  - Stereo field ellipse.
  - Lissajous vector scope.
- Desktop media control via MPRIS v2 D-Bus interface (supported in both CLI and Sparkles).
- Synchronized lyrics engine:
  - Embedded tags and local `.lrc` support.
  - Online fetching via LRCLIB and NetEase Cloud Music with offline caching.
- Optional graphical interface: **Koni Sparkles** (`koni-sparkles`):
  - 16 2D/3D visualizers.

## Supported Formats

- MP3 (`.mp3`)
- FLAC (`.flac`)
- WAV (`.wav`)
- Ogg Vorbis (`.ogg`, `.oga`)
- Opus (`.opus`) *(requires `libopusfile`)*
- AAC / M4A (`.m4a`, `.aac`, `.mp4`) *(requires `libfaad2`)*
- PxTone (`.ptcop`, `.pttune`)
- DANA (`.dana`, `.dahl/.dahc`) *(if holotwist/dana repo is cloned inside src)*

## Dependencies

- C11 compatible compiler (GCC or Clang)
- CMake 3.16+
- `ncursesw`
- `libcurl`
- `sqlite3`
- `dbus-1` (optional, for MPRIS support)
- `libopusfile` (optional, for Opus playback)
- `libfaad2` (optional, for AAC / M4A playback)
- `raylib` (optional, required only for `koni-sparkles` GUI)

### Package Installation

Debian / Ubuntu / Linux Mint:
```bash
sudo apt update
sudo apt install build-essential cmake libncursesw5-dev libcurl4-openssl-dev libsqlite3-dev libdbus-1-dev libopusfile-dev libfaad-dev libraylib-dev
```

Arch Linux / Manjaro:
```bash
sudo pacman -S base-devel cmake ncurses curl sqlite dbus opusfile faad2 raylib
```

Fedora / RHEL:
```bash
sudo dnf install gcc cmake ncurses-devel libcurl-devel sqlite-devel dbus-devel opusfile-devel faad2-devel raylib-devel
```

## Compilation

Koni defaults to Release mode (`-O3`, stripped symbols, native CPU). Any missing optional codec library simply disables that specific codec and continues building the binary.

### Using `build.sh` (Recommended)

```bash
# Compile CLI player (koni)
./build.sh

# Compile both CLI player and Koni Sparkles GUI (requires raylib)
./build.sh -s

# Clean and rebuild targeting generic modern x86_64 (x86-64-v3)
./build.sh -c -s -m
```

### Using CMake Directly

```bash
# Build CLI player
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Build CLI and Sparkles GUI
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_SPARKLES=ON
cmake --build build --parallel
```

## Usage

### Terminal Interface (`koni`)

```bash
# Launch in the current directory
./build/koni

# Open a specific music folder
./build/koni ~/Music

# Force 256-color palette
./build/koni --force-colors ~/Music
```

### Graphical Interface (`koni-sparkles`)

```bash
# Launch Koni Sparkles
./build/koni-sparkles
```

## Keybindings (Terminal UI)

- Navigation, Search & Library:
  - `Tab`: Cycle browser tabs (Queue -> Music Library -> Playlists -> Files).
  - `/`: Search / filter current tab (`Esc` to cancel, `Enter` to select).
  - `Up` / `Down` or `k` / `j`: Move selection.
  - `Enter`: Play selected item / Open directory or playlist.
  - `a`: Add selected track to queue.
  - `A`: Add all tracks in view to queue.
  - `d` / `Delete` / `Backspace`: Remove track from queue.
  - `w` / `W`: Clear queue.
  - `u` / `U`: Rescan library folders.
  - `o` / `O`: Change library sort criteria.
  - `s` / `S` (on a folder in Files): Add/remove folder from library.
  - `i` / `I`: Open track details & actions modal (play next, add to playlist, etc.).
  - `F` / `*`: Toggle favourite status.
  - `L`: Locate and scroll to current playing track.

- Playback & Audio Controls:
  - `Space` / `p`: Toggle play / pause.
  - `n` / `>`: Next track.
  - `b` / `<`: Previous track.
  - `Left` / `Right`: Seek 5 seconds backward / forward.
  - `+` / `=`: Volume up.
  - `-` / `_`: Volume down.
  - `m` / `M`: Mute / unmute.
  - `s` / `S`: Toggle shuffle.
  - `r` / `R`: Cycle repeat (Off, All, One).
  - `g` / `G`: Cycle ReplayGain mode (Off, Meta, Calc).

- View & Display:
  - `1`: Visualizer view.
  - `2`: Lyrics view.
  - `3`: PxTone tracker view (active for `.ptcop` / `.pttune`).
  - `c` / `C`: Cycle visualizer mode.
  - `E`: Open Equalizer (Graphic & Parametric PEQ).
  - `K`: Open Krystal Audio DSP panel.
  - `v` / `V`: Toggle visualizer panel visibility.
  - `y` / `Y`: Toggle lyrics overlay bar.
  - `l` / `L`: Toggle horizontal / vertical panel layout.
  - `f`: Toggle fullscreen visualizer.
  - `h` / `H`: Toggle help bar.
  - `q` / `Q`: Quit.

## Configuration

Settings and databases are kept in `~/.config/koni/`:

- `config.ini`: Configuration, music directories, and custom paths.
- `library.db`: SQLite database for cached track metadata and library index.
- `profile.db`: Local listening history and weighted shuffle metrics.
- `state`: Saved player state, volume, visualizer modes, and DSP settings.
- `queue.m3u`: Saved playback queue.
- `playlists/`: Custom user playlists (`.m3u`).
- `lyrics/`: Offline cached lyrics.
- `peq/`: Custom parametric EQ (`.txt` / AutoEQ) presets.

## License

Licensed under GPLv3. See [LICENSE](./LICENSE.txt) for details.