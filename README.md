[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg?style=for-the-badge)](./LICENSE)

Koni is a lightweight terminal music player written in C.

## Koni - A Music Player

Koni is an ncurses-based audio player featuring audio visualizers, synchronized lyrics retrieval, desktop integration via MPRIS, ReplayGain, and a little more.

## Features

- Multi-format audio playback (MP3, FLAC, WAV, and DANA streams).
- Gapless playback transitions.
- High-density Unicode Braille visualizers:
  - Spectrum analyzer.
  - Waveform oscilloscope.
  - Stereo field ellipse.
  - Lissajous vector scope.
- Stereo VU meter.
- Synchronized lyrics engine:
  - Embedded metadata and local `.lrc` file support.
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
- DANA (`.dana`, `.dahl/.dahc`)

## Dependencies

- C11 compatible compiler (GCC or Clang)
- CMake 3.16+
- `ncursesw`
- `libcurl`
- `dbus-1` (optional, for MPRIS support)
- `pthread`, `m (libm)`

### Package Installation

Debian / Ubuntu / Linux Mint:
```bash
sudo apt update
sudo apt install build-essential cmake libncursesw5-dev libcurl4-openssl-dev libdbus-1-dev
```

Arch Linux / Manjaro:
```bash
sudo pacman -S base-devel cmake ncurses curl dbus
```

Fedora / RHEL:
```bash
sudo dnf install gcc cmake ncurses-devel libcurl-devel dbus-devel
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

- Navigation & Queue:
  - `Tab`: Switch focus between files and playlist panels.
  - `Up` / `Down`: Navigate list entries.
  - `Enter`: Play selected file / Open directory.
  - `a`: Add selected file to playlist.
  - `A`: Add entire current directory to playlist.
  - `d` / `Delete` / `Backspace`: Remove track from playlist.
  - `W`: Clear playlist queue.

- Playback & Volume:
  - `Space` / `p`: Toggle play / pause.
  - `n` / `>`: Next track.
  - `b` / `<`: Previous track.
  - `Left` / `Right`: Seek backward / forward 5 seconds.
  - `+` / `=`: Increase volume.
  - `-` / `_`: Decrease volume.
  - `m` / `M`: Mute / Unmute.
  - `s` / `S`: Toggle shuffle.
  - `r` / `R`: Cycle repeat (Off, All, One).
  - `g` / `G`: Cycle ReplayGain (Off, Meta, Calc).

- View & Display:
  - `1` / `2`: Switch view (1: Visualizer, 2: Lyrics).
  - `c` / `C`: Cycle visualizer mode.
  - `y` / `Y`: Toggle lyrics overlay.
  - `v` / `V`: Toggle visualizer panel visibility.
  - `l` / `L`: Toggle horizontal / vertical layout.
  - `f` / `F`: Toggle fullscreen visualizer.
  - `h` / `H`: Toggle bottom help bar.
  - `q` / `Q`: Quit.

## Configuration

Configuration files and state are stored in `~/.config/koni/`:

- `config.ini`: User settings and custom paths.
- `state`: Saved playback state and UI settings.
- `queue.m3u`: Saved playlist queue.
- `lyrics/`: Offline cached lyrics.

Example `config.ini`:
```ini
[Paths]
lyrics.custom_path = "~/Music/lyrics"

[Lyrics]
lyrics.online = true
lyrics.download_online = true
```

## License

Licensed under GPLv3. See [LICENSE](./LICENSE) for details.
