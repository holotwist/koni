[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=for-the-badge)](./LICENSE)

Koni is a lightweight terminal music player written in C.

## Koni - A Music Player

Koni is an ncurses-based audio player featuring audio visualizers, synchronized lyrics retrieval, desktop integration via MPRIS, ReplayGain, and a little more.

## Features

- Multi-format audio playback (MP3, FLAC, WAV, OGG, PxTone, and DANA streams).
- Built-in PxTone synthesis engine and real-time tracker tab.
- SQLite-backed music library with background recursive scanning and multi-folder management.
- Multi-criteria library sorting (Title, Artist / Album, Album, Duration, File Path).
- Three-panel tabbed browser (Queue, Music Library, and File Browser) with real-time `/` search filtering.
- Extensible plugin/extension architecture for codec interfaces, custom tabs, and hotkeys.
- Gapless playback transitions.
- High-density Unicode Braille visualizers:
  - Spectrum analyzer.
  - Waveform oscilloscope.
  - Stereo field ellipse.
  - Lissajous vector scope.
- Stereo VU meter.
- Synchronized lyrics engine:
  - Embedded metadata and local `.lrc` / `.ttml` plugin support.
  - Online fetching via LRCLIB and NetEase Cloud Music with local disk caching.
  - Dedicated lyrics view tab and floating overlay bar.
- ReplayGain normalization:
  - Metadata mode (ID3v2 and Vorbis comments).
  - Dynamic sliding-window RMS AGC mode (-20 dBFS target).
- Desktop media control via MPRIS v2 D-Bus interface.
- Responsive interface:
  - Adaptive horizontal and vertical layouts.
  - Fullscreen visualizer mode.
  - Marquee scrolling for overflowing text.
- Automatic session, settings, and queue persistence.

## Supported Formats

- MP3
- FLAC
- WAV
- Ogg Vorbis (`.ogg`, `.oga`)
- PxTone (`.ptcop`, `.pttune`)
- DANA (`.dana`, `.dahl/.dahc`)

## Dependencies

- C11 compatible compiler (GCC or Clang)
- CMake 3.16+
- `ncursesw`
- `libcurl`
- `sqlite3`
- `dbus-1` (optional, for MPRIS support)
- `pthread`, `m (libm)`

### Package Installation

Debian / Ubuntu / Linux Mint:
```bash
sudo apt update
sudo apt install build-essential cmake libncursesw5-dev libcurl4-openssl-dev libsqlite3-dev libdbus-1-dev
```

Arch Linux / Manjaro:
```bash
sudo pacman -S base-devel cmake ncurses curl sqlite dbus
```

Fedora / RHEL:
```bash
sudo dnf install gcc cmake ncurses-devel libcurl-devel sqlite-devel dbus-devel
```

## Installation

```bash
git clone https://github.com/holotwist/koni.git
cd koni
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
sudo make install
```

## Usage

```bash
# Launch in the current directory
koni

# Open a specific music directory
koni ~/Music

# Enforce 256-color palette
koni --force-colors ~/Music
```

## Keybindings

- Navigation, Search & Library:
  - `Tab`: Cycle active browser tab (Queue -> Music Library -> Files).
  - `/`: Open search bar to filter current browser tab (Esc to cancel, Enter to select).
  - `Up` / `Down`: Navigate list entries.
  - `Enter`: Play selected track / Enter directory.
  - `a`: Add selected track to queue.
  - `A`: Add all tracks in view (or current directory) to queue.
  - `d` / `Delete` / `Backspace`: Remove track from queue.
  - `w` / `W`: Clear queue.
  - `u` / `U`: Trigger background music library rescan.
  - `o` / `O`: Cycle library sort order (Music tab).
  - `s` / `S` (on a folder in Files tab): Add/remove directory from music library folders.

- Playback & Volume:
  - `Space` / `p`: Toggle play / pause.
  - `n` / `>`: Next track.
  - `b` / `<`: Previous track.
  - `Left` / `Right`: Seek backward / forward 5 seconds.
  - `+` / `=`: Increase volume.
  - `-` / `_`: Decrease volume.
  - `m` / `M`: Mute / Unmute.
  - `s` / `S` (when not on folder): Toggle shuffle.
  - `r` / `R`: Cycle repeat (Off, All, One).
  - `g` / `G`: Cycle ReplayGain (Off, Meta, Calc).

- View & Display:
  - `1` / `2`: Switch view (1: Visualizer, 2: Lyrics).
  - `3`: Switch to tracker tab (active when playing PxTone files).
  - `[` / `]` or `,` / `.`: Horizontal channel scroll in tracker view.
  - `c` / `C`: Cycle visualizer mode (Spectrum, Oscilloscope, Ellipse, Lissajous).
  - `y` / `Y`: Toggle lyrics overlay bar.
  - `v` / `V`: Toggle visualizer panel visibility.
  - `l` / `L`: Toggle horizontal / vertical layout.
  - `f` / `F`: Toggle fullscreen visualizer.
  - `h` / `H`: Toggle bottom help bar.
  - `q` / `Q`: Quit.

## Configuration

Configuration files and state are stored in `~/.config/koni/`:

- `config.ini`: User settings, music folders, and custom paths.
- `library.db`: SQLite database caching tracks, mtimes, and metadata.
- `state`: Saved playback state and UI settings.
- `queue.m3u`: Saved playlist queue.
- `lyrics/`: Offline cached lyrics.

Example `config.ini`:
```ini
[Paths]
music_directories = "~/Music, ~/Downloads/OST"
lyrics.custom_path = "~/Music/lyrics"

[Lyrics]
lyrics.online = true
lyrics.download_online = true
```

## License

Licensed under GPLv3. See [LICENSE](./LICENSE.txt) for details.