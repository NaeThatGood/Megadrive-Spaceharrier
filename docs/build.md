# Build guide and troubleshooting

This project builds Sega Mega Drive ROMs with [SGDK](https://github.com/Stephane-D/SGDK).
On macOS, **always compile through Docker** — do not use Homebrew `m68k-elf-gcc` for
linking or for rebuilding `libmd.a`.

## Quick reference (macOS)

```sh
colima start                    # once per boot, if using Colima
make build                      # → out/rom.bin (Docker GCC 13.2)
make run                        # build + Ares/BlastEm
make clean                      # remove out/ (local; fine with Homebrew make)
```

Verify the SGDK library before building if audio ever breaks:

```sh
strings tools/sgdk/lib/libmd.a | grep -m1 "GCC: "
# Must print: GCC: (crosstool-NG ...) 13.2.0
# If it prints GCC: (GNU) 16.x — lib is broken; see “PCM crash” below.
```

## How builds work

| Step | Tool | Notes |
| --- | --- | --- |
| Game compile/link | `make build` | Runs inside `ghcr.io/stephane-d/sgdk:latest` (GCC 13.2) |
| Resource compile | ResComp (Java) | Inside Docker during `make build` |
| SGDK library | `tools/sgdk/lib/libmd.a` | Must be GCC **13.2.0 crosstool-NG**, not Homebrew 16.x |
| CI / production | GitHub Actions | Same Docker image (`.github/workflows/build.yml`) |

The project `Makefile` blocks `make sgdk-lib` locally. Use `make restore-sgdk-lib`
or rebuild the library inside Docker (see below).

## One-time setup (macOS)

```sh
brew install openjdk colima docker
brew install --cask ares-emulator
colima start

git clone https://github.com/Stephane-D/SGDK.git tools/sgdk
mkdir -p tools/bin
clang -O2 -o tools/bin/bintos tools/sgdk/tools/bintos/src/bintos.c
make -C tools/sgdk/tools/sjasm/src && cp tools/sgdk/tools/sjasm/src/sjasm tools/bin/

make restore-sgdk-lib
```

## Restore or rebuild `libmd.a`

### Option A — copy from Docker image (fast)

Use when the library was clobbered by a local `make sgdk-lib` and the bundled
SGDK in the image still matches `tools/sgdk`:

```sh
make restore-sgdk-lib
strings tools/sgdk/lib/libmd.a | grep -m1 "GCC: "
```

Image path: `/sgdk/lib/libmd.a` (`SGDK_PATH=/sgdk` in the container).

### Option B — rebuild inside Docker (when `tools/sgdk` is newer than the image)

If linking fails with missing symbols (e.g. `z80VIntCB`), the image lib is older
than your SGDK tree. Rebuild the library with the image's GCC 13.2:

```sh
docker run --rm --platform linux/amd64 -v "$PWD":/src -w /src/tools/sgdk \
  -u "$(id -u):$(id -g)" --entrypoint sh ghcr.io/stephane-d/sgdk:latest \
  -c 'make -f makelib.gen EXTRA_FLAGS="-std=gnu11 -fno-lto" -j1 release'
strings tools/sgdk/lib/libmd.a | grep -m1 "GCC: "
```

Use `-fno-lto` and `-j1` on Apple Silicon Colima to avoid GCC segfaults under
x86_64 emulation.

**Never** run `make sgdk-lib` on the Mac host (Homebrew GCC 16 miscompiles the
XGM2 driver).

## Test ROMs (do not overwrite production)

Stock SGDK sample for PCM sanity check:

```sh
docker run --rm --platform linux/amd64 \
  -v "$PWD":/src -w /src/tools/sgdk/sample/basics/pools \
  -u "$(id -u):$(id -g)" --entrypoint sh ghcr.io/stephane-d/sgdk:latest \
  -c 'make -f /src/tools/sgdk/makefile.gen EXTRA_FLAGS="-std=gnu11" clean release'
mkdir -p out/tests
cp tools/sgdk/sample/basics/pools/out/rom.bin out/tests/TEST4.bin
```

Run in Ares/BlastEm; shoot and collide — PCM must not crash.

## Troubleshooting

### PCM / XGM2 crashes on playback (ROM boots, audio dies on first sample)

**Symptom:** Game loads; "Get ready" or shoot SFX crashes the emulator.

**Cause:** `tools/sgdk/lib/libmd.a` was rebuilt with Homebrew `m68k-elf-gcc` 15+ / 16.x.
The ROM links fine but the XGM2 driver object code is wrong.

**Diagnose:**

```sh
strings tools/sgdk/lib/libmd.a | grep -m1 "GCC: "
# Broken:  GCC: (GNU) 16.1.0
# Good:    GCC: (crosstool-NG 1.26.0) 13.2.0
```

**Fix:** Back up the bad lib, then restore or Docker-rebuild (options A/B above).
Then `make clean && make build`.

Backups in-repo (if present): `libmd.a.broken-gcc16`, `libmd.a.broken-gcc15`.

### `make build` fails: docker not found

Install and start Colima:

```sh
brew install colima docker
colima start
```

Or use CI ROMs from GitHub Releases / PR artifacts (see README).

### LTO version mismatch (local Homebrew link)

**Symptom:** `lto1: fatal error: bytecode stream ... LTO version 13.0 instead of 16.0`

**Cause:** Trying to link Docker-built `libmd.a` (GCC 13 LTO) with Homebrew
`m68k-elf-gcc` 16.

**Fix:** Use `make build` (Docker). Do not compile the game with Homebrew GCC.

### `make build` fails: absolute path in `.d` file after switching build environments

**Symptom:** `No rule to make target '/Users/.../res/...png'`

**Fix:** `make clean` then `make build`.

### `make sgdk-lib` prints ERROR and exits

Intentional guard. Use `make restore-sgdk-lib` or Docker makelib (option B).

## Linux / Windows

- **Linux:** Prefer the same Docker workflow as macOS, or ensure distro
  `m68k-elf-gcc` is 13.2 before running `make sgdk-lib`.
- **Windows:** Official SGDK release bundles a working toolchain.
- **No local toolchain:** Push to GitHub; CI builds with the official Docker image.

## Related files

- `Makefile` — `build` (Docker), `restore-sgdk-lib`, blocked `sgdk-lib`
- `.github/workflows/build.yml` — CI ROM build
- `README.md` — setup summary and download links
