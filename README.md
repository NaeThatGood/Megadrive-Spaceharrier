# Sky Rush Proto — Mega Drive Sprite-Scaling Technical Prototype

An original Sega Mega Drive / Genesis homebrew prototype in the fast pseudo-3D
style of arcade sprite-scalers. It exists to answer one question:

> For large enemies flying toward the player, is it better to store many
> pre-rendered sprite scale frames in ROM (assuming a 16 Mbit cart), or to
> scale sprites in software on the 68000 at runtime?

Both approaches are implemented in a single ROM, sharing the same gameplay
shell, and can be toggled live with the C button.

The ROM targets **NTSC 60 Hz** and runs the game loop every other VBlank for
a clean 30 fps cadence. On PAL hardware it uses the taller 320x240 (V30)
mode but drops to a 25 Hz cadence. The scrolling checkerboard ground uses the
authentic techniques of Space Harrier II (per-scanline perspective scroll
table, vertical horizon scroll) and Burning Force (palette-animated forward
motion) — see [docs/technical-plan.md](docs/technical-plan.md).

All graphics and audio are original generated placeholders — no copyrighted
material is used.

See the companion documents:

- [docs/technical-plan.md](docs/technical-plan.md) — architecture and next steps
- [docs/comparison-notes.md](docs/comparison-notes.md) — measured comparison of the two approaches
- [docs/asset-budget.md](docs/asset-budget.md) — ROM cost analysis for a 16 Mbit cartridge

## Controls

| Input | Action |
| --- | --- |
| D-pad | Move player |
| A / B | Slow / speed up enemy scaling |
| X | Fire |
| Y | Toggle graduated horizon / sky scanline effect |
| Z | Cycle Harrier movement speed in 25% steps |
| START | Pause / resume |

The debug HUD (top of screen) shows the active mode, live object count,
CPU load percentage (>100% = missed frames) and frames per second.

## Project layout

```
src/main.c                          game loop, player, shots, collision
src/engine/                         ground renderer, world/projection, HUD
src/prototypes/stored_frames/       pre-rendered scale-frame renderer
res/                                rescomp resources (sprites, audio, palettes)
tools/gen_assets.py                 procedural placeholder art generator
tools/gen_scale_frames.py           pre-rendered scale strip generator
tools/gen_shadow.py                 shared ground-shadow strip generator
tools/gen_proj_lut.py               world projection lookup-table generator
tools/gen_voice.sh                  "Get ready!" sample synthesis (macOS)
docs/                               technical documentation
```

## Prerequisites

- [SGDK](https://github.com/Stephane-D/SGDK) (cloned into `tools/sgdk`, see below)
- Java runtime (for SGDK's resource compiler)
- m68k-elf GCC cross-toolchain
- GNU make
- An emulator: BlastEm (x86 machines), Ares, or RetroArch + Genesis Plus GX
- Optional, only to regenerate placeholder assets: Python 3 with Pillow, ffmpeg

## Setup

### macOS

```sh
brew install openjdk m68k-elf-gcc m68k-elf-binutils
brew install --cask ares-emulator         # emulator (Apple Silicon friendly)
# BlastEm only works on Intel Macs: brew install blastem

# SGDK + its native tools
git clone https://github.com/Stephane-D/SGDK.git tools/sgdk
mkdir -p tools/bin
clang -O2 -o tools/bin/bintos tools/sgdk/tools/bintos/src/bintos.c
make -C tools/sgdk/tools/sjasm/src && cp tools/sgdk/tools/sjasm/src/sjasm tools/bin/

# build the SGDK library once
make sgdk-lib
```

Note: Homebrew's OpenJDK is keg-only; the project `Makefile` adds
`/opt/homebrew/opt/openjdk/bin` to PATH automatically.

### Linux

```sh
# Debian/Ubuntu: toolchain from your distro or build via marsdev
sudo apt install openjdk-17-jre gcc-m68k-linux-gnu make blastem   # if packaged
# otherwise use https://github.com/andwn/marsdev for the m68k-elf toolchain

git clone https://github.com/Stephane-D/SGDK.git tools/sgdk
mkdir -p tools/bin
cc -O2 -o tools/bin/bintos tools/sgdk/tools/bintos/src/bintos.c
make -C tools/sgdk/tools/sjasm/src && cp tools/sgdk/tools/sjasm/src/sjasm tools/bin/
make sgdk-lib
```

The toolchain prefix defaults to `m68k-elf-`; override with
`make PREFIX=m68k-linux-gnu-` style variables if yours differs.

### Windows

Use the official SGDK Windows release, which bundles the whole toolchain:

1. Download SGDK from <https://github.com/Stephane-D/SGDK/releases> and unpack
   it (e.g. `C:\sgdk`), set the `GDK` environment variable to that path.
2. From the project root run `%GDK%\bin\make -f %GDK%\makefile.gen` to build
   `out\rom.bin`.
3. Run the ROM in BlastEm or Ares for Windows.

### No local toolchain at all?

Every push to GitHub builds the ROM with the official SGDK Docker image
(`.github/workflows/build.yml`). If you have Docker locally you can do the
same:

```sh
docker run --rm -v "$PWD":/src -u "$(id -u):$(id -g)" ghcr.io/stephane-d/sgdk:latest
```

## Download ROMs

GitHub is the source of truth for playable builds.

| Build | File | Where to get it |
| --- | --- | --- |
| **Production** (current `main`) | `sky-rush-proto.bin` | [Latest release](https://github.com/NaeThatGood/Megadrive-Spaceharrier/releases/tag/latest) |
| **Pull request** #N | `sky-rush-proto-pr-N.bin` | Open the PR → **Checks** → **Build ROM** → **Artifacts** |

Production (CLI):

```sh
gh release download latest --repo NaeThatGood/Megadrive-Spaceharrier --pattern 'sky-rush-proto.bin' -D .
```

Each merged PR updates the production release; PR artifacts stay available on
that PR's workflow runs (GitHub retains them for 90 days).

## Build and run

```sh
make build        # build out/rom.bin
make run          # build + launch in blastem (if found) or ares
make run-ares     # build + launch in ares explicitly
make clean        # remove build output
make rebuild-run  # clean + build + run
```

The same four targets are available as Cursor/VS Code tasks
(`.vscode/tasks.json`): "MD: Build ROM", "MD: Clean", "MD: Run in emulator",
"MD: Rebuild + Run".

The ROM is a plain `out/rom.bin` (128 KB, padded, checksummed) and also runs
on real hardware via an EverDrive-style flash cart.

## Playtesting

1. `make run` — the ROM boots in stored-frame mode ("Get ready!" plays).
2. Watch enemies spawn at the horizon and fly toward you. They step through
   pre-rendered sizes from 8 to 64 px while the HUD tracks live object count,
   CPU load, and FPS.
3. Fly into an enemy to see the collision flash; shoot enemies with X.

## Regenerating placeholder assets

```sh
python3 -m venv tools/venv && tools/venv/bin/pip install pillow
tools/venv/bin/python tools/gen_assets.py        # ground, player, enemy, shot
tools/venv/bin/python tools/gen_scale_frames.py  # pre-rendered scale strip
tools/venv/bin/python tools/gen_shadow.py        # shared ground-shadow strip
tools/venv/bin/python tools/gen_proj_lut.py      # world projection lookup table
tools/gen_voice.sh                               # "Get ready!" (macOS 'say')
```

To use your own voice sample, replace `res/audio/getready.wav` with any mono
16-bit PCM WAV and rebuild.

## Notes

- `src/rom_header.c` is auto-generated by SGDK on first build (untracked);
  edit it locally to change the ROM title shown by emulators. Set its last
  field (region) to `"U               "` so auto-region emulators run the
  ROM in NTSC mode for the 30 fps target.
- SGDK is compiled with `-std=gnu11` because its `bool` typedef clashes with
  the C23 default of GCC >= 15 (handled by the Makefile).
