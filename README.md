<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="resources/img/equinox_logo_dark.png">
    <img alt="Equinox" src="resources/img/equinox_logo_light.png" width="420">
  </picture>
</p>

Equinox is a game streaming client for the Nintendo Switch. It is built to pair with
[Solstice](https://github.com/luizfbalves/Solstice), and it also works with
[Sunshine](https://github.com/LizardByte/Sunshine).

> [!NOTE]
> Equinox is an unofficial fork of [Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch)
> by XITRIX, itself a port of the [Moonlight Game Streaming Project](https://github.com/moonlight-stream).
> Most of the streaming client comes from those projects. Equinox is not affiliated with or
> endorsed by their authors. See [Credits](#credits).

## What Equinox adds

- **Connection test.** Measures latency and download speed to the host, then streams at
  increasing bitrates to find the highest one your network and console sustain. The stream
  is shown in a preview while the test runs.
- **Recommended settings in one click.** After the test, Equinox suggests a bitrate,
  resolution and frame pacing based on the results and applies them to the profile you
  tested (docked or handheld).
- **Home screen of your PCs.** Each paired host shows as a card with its status (online,
  asleep, out of reach) and the game running on it, so you can resume straight away.
- **Solstice support.** Uses Solstice's speed test endpoint when the host offers it, and
  falls back cleanly on Sunshine.
- **In-app updates.** Checks for new releases and installs them when Equinox runs as an NRO
  from the SD card.
- **New look.** Dusk blue theme, sun amber accents and the Onest typeface.
- **Super Nintendo games.** Play your own SNES ROMs from the SD card, on the same home
  screen as your PCs. See [Super Nintendo](#super-nintendo).

# Installing

1. Download the latest Equinox [release](https://github.com/luizfbalves/Equinox/releases).
2. Copy `Equinox.nro` to `sdmc:/switch/Equinox/`.
3. Open the Homebrew Menu through *title redirection* (hold R while launching a game), so
   Equinox gets full RAM access.
4. Launch Equinox.

Settings are stored in `sdmc:/switch/Moonlight-Switch/settings.json`, the same place
Moonlight-Switch uses, so existing pairings and favorites carry over.

> [!TIP]
> High bitrates, especially at 1080p, may need the console's CPU/GPU overclocked. See
> [sys-clk](https://github.com/retronx-team/sys-clk). Overclocking is at your own risk.

## Controls

### Touch and mouse
- Touch the screen to move the cursor, tap to left click, scroll with two fingers.
- While touching the screen, ZR and ZL act as left and right mouse buttons, and the sticks
  act as a scroll wheel.
- USB mice work too.

### Keyboard
Tap the screen with three fingers to open the on-screen keyboard. USB keyboards work too.

### Gamepad
By default the Switch controller is mapped as an Xbox 360 controller (A/B and X/Y swapped).
The mapping can be changed in Settings.

Up to 5 controllers are supported, including handheld mode and single Joy-Cons.

### Motion controls
Configure the host to see the controller as a DualShock 4 to use the gyroscope and
accelerometer. Only player 1 is supported.

### In-game overlay
Press − and + together, or hold Esc on a keyboard. The combination and hold time can be
changed in Settings.

## Super Nintendo

Equinox can also run Super Nintendo games on the console itself, with
[snes9x](https://github.com/luizfbalves/equinox-snes9x).

1. On the home screen, open the **Add** card and pick **Super Nintendo**. Equinox
   downloads the emulator to `sdmc:/equinox/emulators/snes/`.
2. Copy your own game files (`.sfc`, `.smc` or `.zip`) to `sdmc:/equinox/roms/snes/`.
3. Select the Super Nintendo card to see your games. Covers are fetched on their own.

Saves and save states live in `sdmc:/equinox/saves/snes/` and `sdmc:/equinox/states/snes/`.
Games can be added to Favorites like PC games. Removing the console keeps your games, saves
and covers on the SD card. Equinox does not provide games or BIOS files.

## Forwarders

Equinox can install a forwarder on the HOME Menu that starts a stream straight away. Add the
game to Favorites, select it and press X (*Make forwarder*).

A forwarder passes these arguments to Equinox:
- `--ip`: IP address of the host
- `--appid`: ID of the game, which must be in Favorites (listed in `settings.json`)
- `--appname`: the game's name, without spaces

Example: `--ip=192.168.1.101 --appid=1233211234 --appname=Steam`

## Languages

Equinox follows the console's system language. English and Brazilian Portuguese are
complete. German, Spanish, French, Italian, Japanese, Korean, Russian and Chinese
(Simplified and Traditional) come from Moonlight-Switch and miss the strings Equinox added,
which show in English.

Translations live in `resources/i18n/<language>/main.json`. Pull requests are welcome.

## Build

```bash
git clone https://github.com/luizfbalves/Equinox.git --recursive
cd Equinox
```

### Switch

With Docker (no local devkitPro needed):

```bash
docker compose run --rm switch
```

The `.nro` is written to `build/docker/Equinox.nro`.

Or with a local [devkitPro](https://devkitpro.org/wiki/Getting_Started) setup:

```bash
cmake -B build/switch -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON
make -C build/switch Moonlight.nro -j$(nproc)
```

### Windows (development only)

The Windows build exists to test the interface without a console; it is not a release
target. It uses MSYS2 under `C:\msys64` and DirectX 11.

Install the packages from a UCRT64 shell:

```bash
pacman -S --needed --noconfirm \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-ffmpeg \
  mingw-w64-ucrt-x86_64-curl \
  mingw-w64-ucrt-x86_64-mbedtls \
  mingw-w64-ucrt-x86_64-jansson \
  mingw-w64-ucrt-x86_64-libpng \
  mingw-w64-ucrt-x86_64-opus \
  mingw-w64-ucrt-x86_64-expat \
  mingw-w64-ucrt-x86_64-zstd
```

Then build (add `--run` to start the app):

```bash
scripts/windows-dev.sh
```

The executable, its DLLs and the `resources` folder end up in `build/windows-ucrt64`.

### Tests

Unit tests use doctest and build on the host (UCRT64 shell on Windows):

```bash
cmake -S tests -B build/tests -G Ninja
cmake --build build/tests
```

### Logo

The logo source is `art/logo/equinox.svg`. `python art/logo/render_logo.py` (needs Pillow)
regenerates the console icon and the PNGs in `resources/img`.

## Credits

Equinox exists thanks to:

- [XITRIX](https://github.com/XITRIX) and [Moonlight-Switch](https://github.com/XITRIX/Moonlight-Switch),
  the app Equinox is forked from.
- The [Moonlight](https://github.com/moonlight-stream) team and
  [moonlight-common-c](https://github.com/moonlight-stream/moonlight-common-c).
- [Rock88](https://github.com/rock88) and [Moonlight-NX](https://github.com/rock88/moonlight-nx),
  where much of the original streaming code came from.
- [Xfangfang](https://github.com/xfangfang) for maintaining the
  [Borealis](https://github.com/xfangfang/borealis) UI library, created by Natinusala.
- [Averne](https://github.com/averne) for the NVDEC implementation in
  [FFmpeg](https://github.com/averne/FFmpeg).
- [Cooler3D](https://github.com/Cooler3Ds) for help with the deko3d renderer and performance.
- The forwarder installer is adapted from [sphaira](https://github.com/ITotalJustice/sphaira).
- Console icons are from the RetroSystem theme of
  [libretro/retroarch-assets](https://github.com/libretro/retroarch-assets), licensed under
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

The Switch deko3d upscaling path includes AMD FidelityFX Super Resolution 1.0 EASU and RCAS
code translated from the MIT-licensed
[GPUOpen FidelityFX-FSR](https://github.com/GPUOpen-Effects/FidelityFX-FSR) reference
implementation. Copyright (c) Advanced Micro Devices, Inc. All rights reserved.

## License

See [LICENSE](LICENSE).
