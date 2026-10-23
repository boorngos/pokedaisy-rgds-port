# Building PokeDaisy for the Anbernic RG DS

**Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy**

This is the Linux port of PokeDaisy for the Anbernic RG DS: a Rockchip RK3568 handheld with two 640×480 panels. The GBA game runs on the top panel (mGBA 0.10.5). The companion runs on the bottom panel. One `pokedaisy` binary is enough for any Linux firmware on that hardware, as long as the binary matches **that** firmware.

A PC build (`cmake -S . -B build && cmake --build build`) produces an x86_64 program. It will not run on the RG DS. The steps below produce the aarch64 program.

No ROM or BIOS is included. Use a dump of a cartridge you own.

## What the binary has to match

Three things are fixed by the firmware, not by this repository:

| | What to match | How a mismatch shows up |
|---|---|---|
| CPU | aarch64 | `Exec format error` |
| C library | The firmware's glibc (the version it ships, or older), or musl if the firmware is musl | `GLIBC_2.xx not found`, or a glibc binary that will not start on musl |
| SDL2 | The firmware's `libSDL2-2.0.so.0`, linked dynamically | No picture, `SDL_Init` failed, or a missing `libSDL2` |

SDL2 is the display, audio, touch, and pad layer. Rocknix, ArkOS, muOS, SimpleOS, and Anbernic's own Linux each ship an SDL2 built for their video stack (Wayland, KMS/DRM, or X11). CMake in this tree will, if it cannot find SDL2, download SDL 2.30.11 and link it statically. That static library does not know the firmware's video driver. For a handheld build, point CMake at the firmware's SDL2 so `find_package(SDL2)` succeeds.

The finished program should depend on `libSDL2-2.0.so.0`, `libm.so.6`, and `libc.so.6`. On glibc 2.34 and newer, `libpthread` and `libdl` live inside `libc`.

## 1. Read the firmware

SSH into the handheld (same LAN as your PC) and save this output. You need it for the toolchain and for the launcher.

```sh
uname -m
ldd --version 2>/dev/null || true
ls -l /lib/libc.so* /lib/ld-linux* 2>/dev/null || true
find /usr /lib -name 'libSDL2*.so*' 2>/dev/null
echo "SDL_VIDEODRIVER=${SDL_VIDEODRIVER-} WAYLAND_DISPLAY=${WAYLAND_DISPLAY-} DISPLAY=${DISPLAY-} XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR-}"
ls /dev/dri 2>/dev/null || true
```

`uname -m` must print `aarch64`.

From `ldd --version`, note the glibc version (for example 2.34). A musl firmware prints nothing useful there and has `libc.so` from musl plus a loader named `ld-musl-aarch64.so.1`. Use a musl toolchain in that case (`aarch64-linux-musl`), not `aarch64-linux-gnu`.

From the `find` line, note the real SDL2 file. The name the loader looks up is `libSDL2-2.0.so.0`. It is usually a symlink to `libSDL2-2.0.so.0.xxxx.y`.

From the environment line, note which video stack is already in use. That value goes into the launcher in section 6.

## 2. Copy SDL2 off the device

On the build machine, make a directory that holds only the device library and headers. Headers must be from the **same SDL2 version** as that `.so`. Newer headers can reference symbols the device library does not export.

If the firmware image or its SDK contains `/usr/include/SDL2`, copy that directory. If it does not, read the version out of the library and download that release's headers:

```sh
strings libSDL2-2.0.so.0 | grep -E 'SDL2-[0-9]+\.[0-9]+\.[0-9]+' | head
```

Download `SDL2-<version>.tar.gz` from https://github.com/libsdl-org/SDL/releases and use its `include/` tree. Keep the device `.so` as the library you link. Do not build this tarball.

Layout used by the toolchain file below:

```text
rgds-sdl/
  include/SDL.h
  include/SDL_*.h
  lib/libSDL2-2.0.so.0      # the file copied from the device
```

`libSDL2-2.0.so.0` must be the aarch64 file from the handheld. An x86_64 `libSDL2` from your PC will configure and then fail at link time, or produce a binary the device cannot load.

## 3. Toolchain

You need CMake 3.16 or newer, Ninja or Make, a C compiler, and a C++ compiler. mGBA is C++. Git is required the first time, unless `third_party/mgba` is already a checkout of tag `0.10.5`.

The compiler's glibc must be **less than or equal to** the version from section 1. A stock `aarch64-linux-gnu-gcc` from a new Ubuntu often needs glibc 2.38 or newer, and the binary then fails on the handheld with `GLIBC_2.38 not found`.

Two compilers that can target the device's glibc:

- The firmware SDK, or a crosstool-ng toolchain built for that glibc.
- Zig. The target triple includes the glibc version. For a device that reports glibc 2.34:

  ```sh
  zig cc -target aarch64-linux-gnu.2.34
  zig c++ -target aarch64-linux-gnu.2.34
  ```

  Use the version you read from the device, or an older one. `2.17` is the oldest glibc Zig will target and is safe when you do not know the exact version, at the cost of a slightly more conservative binary. For musl, use `aarch64-linux-musl`.

Put this in `build-aarch64/toolchain.cmake` and edit the four paths. Forward slashes work on Linux and Windows.

```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# GCC:
# set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
# set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
# Zig (glibc 2.34 shown; use the version from section 1):
set(CMAKE_C_COMPILER zig cc -target aarch64-linux-gnu.2.34)
set(CMAKE_CXX_COMPILER zig c++ -target aarch64-linux-gnu.2.34)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(RGDS_SDL_ROOT /absolute/path/to/rgds-sdl)
set(CMAKE_PREFIX_PATH ${RGDS_SDL_ROOT})

set(SDL2_INCLUDE_DIRS ${RGDS_SDL_ROOT}/include)
set(SDL2_LIBRARIES ${RGDS_SDL_ROOT}/lib/libSDL2-2.0.so.0)
set(SDL2_FOUND TRUE)

if(NOT TARGET SDL2::SDL2)
  add_library(SDL2::SDL2 SHARED IMPORTED)
  set_target_properties(SDL2::SDL2 PROPERTIES
    IMPORTED_LOCATION ${RGDS_SDL_ROOT}/lib/libSDL2-2.0.so.0
    INTERFACE_INCLUDE_DIRECTORIES ${RGDS_SDL_ROOT}/include)
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
```

`CMAKE_TRY_COMPILE_TARGET_TYPE` lets CMake test the compiler without running an aarch64 binary on your PC. `CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH` lets `find_package(SDL2)` see the SDL2 you just described. The imported `SDL2::SDL2` target is what this project's `CMakeLists.txt` links when `find_package` succeeds.

If `zig` is not on `PATH`, set `CMAKE_C_COMPILER` to the full path of the `zig` binary and pass the arguments with a wrapper script:

```sh
#!/bin/sh
exec /path/to/zig cc -target aarch64-linux-gnu.2.34 "$@"
```

On Windows the same wrapper is a `zig-cc.cmd` that calls `zig.exe cc -target aarch64-linux-gnu.2.34 %*`, and `CMAKE_C_COMPILER` points at that `.cmd`. Use a matching `c++` wrapper for `CMAKE_CXX_COMPILER`.

## 4. Configure and build

From the repository root:

```sh
cmake -S . -B build-aarch64 -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/build-aarch64/toolchain.cmake" \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF
cmake --build build-aarch64 -j$(nproc)
```

On Windows, pass the toolchain path with forward slashes and pick a job count (`-j 8`) instead of `nproc`.

The first configure clones mGBA 0.10.5 into the build tree (or uses `third_party/mgba` if that checkout is already present). It needs network and `git` only in the first case. To vendor it yourself:

```sh
git clone --depth 1 --branch 0.10.5 https://github.com/mgba-emu/mgba.git third_party/mgba
```

Leave `third_party/SDL2` empty for this build. A tree there is only a fallback for when `find_package(SDL2)` fails, and that fallback is the static SDL this guide is avoiding.

The binary is `build-aarch64/pokedaisy`. `pokedaisy-preview` is a PC-side tool and is not needed on the device.

If a previous configure already started downloading SDL, delete `build-aarch64` (keep `toolchain.cmake` and `rgds-sdl` somewhere outside it) and configure again.

### Check the link before you copy it

On the device, after copying the binary:

```sh
file pokedaisy
ldd pokedaisy
```

`file` should say `ELF 64-bit LSB executable, ARM aarch64` and name the firmware's interpreter (`/lib/ld-linux-aarch64.so.1` for glibc). Every line of `ldd` should resolve. `libSDL2-2.0.so.0 => not found` means the firmware has SDL2 under a different SONAME or not at all; link the file you found in section 1, or copy that `.so` next to the binary only if its own dependencies also exist on the device.

On the build machine, the same check without a device is:

```sh
llvm-readelf -d build-aarch64/pokedaisy | grep NEEDED
llvm-objdump -T build-aarch64/pokedaisy | grep -o 'GLIBC_2\.[0-9]*' | sort -u
```

`NEEDED` includes `libSDL2-2.0.so.0`. The highest `GLIBC_2.xx` is less than or equal to the version from section 1. A binary with no `libSDL2` in `NEEDED` was linked to the vendored static SDL; fix the toolchain file and reconfigure from a clean build directory.

## 5. Build on the handheld instead

Some images have `gcc`, `g++`, `cmake`, and `git`. Then you do not need a cross compiler. Install the firmware's SDL2 development package so the same system library is found, then in the source tree on the device:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Confirm with `ldd build/pokedaisy` that `libSDL2-2.0.so.0` is in the list. If CMake could not find SDL2 it will compile its own, and `ldd` will not mention `libSDL2`. Install the dev package and reconfigure in a new `build` directory.

Images without a compiler (most stock and port firmwares) use sections 2–4.

## 6. Install and start

Create one directory the firmware can execute. Typical locations:

| Firmware | Port directory |
|---|---|
| Rocknix, JELOS, and other `/roms` layouts | `/roms/ports/pokedaisy` |
| Images that mount the card at `/mnt/mmc` (Anbernic Linux, SimpleOS) | `/mnt/mmc/roms/Ports/pokedaisy` |

The menu name is whatever that firmware uses to scan Ports. Keep the launcher and the binary in the **same** directory.

```text
pokedaisy/
  pokedaisy          # the aarch64 binary
  PokeDaisy.sh       # launcher below
  roms/              # your .gba and .zip files
```

`chmod 755 pokedaisy PokeDaisy.sh`. If you edited the script on Windows, convert it to Unix line endings before copying (`dos2unix`, or save it as LF). A `^M` in the shebang makes the kernel report `bad interpreter`.

`PokeDaisy.sh`:

```sh
#!/bin/sh
DIR="$(cd "$(dirname "$0")" && pwd)"
export POKEDAISY_HOME="${POKEDAISY_HOME:-$DIR}"
mkdir -p "$POKEDAISY_HOME/roms" "$POKEDAISY_HOME/saves" "$POKEDAISY_HOME/states" "$POKEDAISY_HOME/backups"
cd "$DIR" || exit 1

# Set ONE of these to match section 1. Leave the others unset.
# Wayland / Weston:
#   export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run}"
#   export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
#   export SDL_VIDEODRIVER=wayland
# KMS/DRM, no compositor (a /dev/dri node and no WAYLAND_DISPLAY):
#   export SDL_VIDEODRIVER=kmsdrm
# X11:
#   export DISPLAY="${DISPLAY:-:0}"
#   export SDL_VIDEODRIVER=x11

exec "$DIR/pokedaisy" --layout auto --nintendo "$@"
```

Uncomment the block that matches the environment you printed in section 1. `--layout auto` opens one fullscreen window per SDL display when SDL reports two, and uses a single 640×960 window when the firmware exposes one tall framebuffer. `--nintendo` maps the RG DS face buttons (east = A, south = B).

Data lives under `POKEDAISY_HOME`:

| Path | Contents |
|---|---|
| `roms/` | `.gba` and `.zip` shown in the list |
| `saves/` | Cartridge `.sav` (mGBA / RetroArch) |
| `states/` | Save states |
| `backups/` | A copy of the `.sav` taken before each session |
| `settings.ini` | HUD, theme, video mode |

Saves stay with the port only when `POKEDAISY_HOME` is that directory. Without it, the default is `~/.local/share/pokedaisy`.

### Wayland example

SimpleOS on this handheld runs Weston. Its launcher exports `SDL_VIDEODRIVER=wayland`, `WAYLAND_DISPLAY=wayland-0`, and `XDG_RUNTIME_DIR=/var/run`, then starts `pokedaisy --layout dual`. Do not kill Weston. On that firmware the shell that owns the UI also powers the unit off when it exits, so a port stops and continues that process around the game; it does not replace it. Other Wayland firmwares need the same three variables and do not need that stop/continue pair.

When `SDL_VIDEODRIVER=wayland` or `WAYLAND_DISPLAY` is set, touch coordinates are corrected for the RG DS touchscreen, which reports a position on one half of a 1280-wide desktop. Leave those variables unset on a KMS or X11 firmware that reports touch in the window's own coordinates.

### Check that it started

```sh
pidof pokedaisy
```

A running pid with a black panel is a video-driver mismatch. Stop `pokedaisy` and try the other `SDL_VIDEODRIVER` value from section 1. `SDL_Init: No available video device` is the same problem, printed on stderr. Launch from a terminal once so you can read it:

```sh
POKEDAISY_HOME=/roms/ports/pokedaisy \
  SDL_VIDEODRIVER=kmsdrm \
  /roms/ports/pokedaisy/pokedaisy --layout dual
```

Replace the path and the driver with yours.

Two panels stacked on the top screen means SDL sees one display. `--layout stacked` is the mode for a single 640×960 framebuffer. `--layout dual` is the mode when `SDL_GetNumVideoDisplays` is 2.

## 7. When something fails

| Symptom | Cause | Fix |
|---|---|---|
| `GLIBC_2.xx not found` | Compiler glibc is newer than the firmware | Rebuild with Zig `-target aarch64-linux-gnu.X.Y` (or a musl triple) for the version from section 1 |
| `Exec format error` | Not an aarch64 ELF, or a glibc binary on musl | `file pokedaisy` on the device; rebuild with the matching triple |
| `libSDL2-2.0.so.0 => not found` | Linked to a library the image does not ship | Section 2, then a clean configure |
| `ldd` has no `libSDL2` line | CMake built the vendored static SDL | Toolchain file must define `SDL2::SDL2` before configure; delete the build dir |
| `bad interpreter: No such file or directory` on `PokeDaisy.sh` | Windows CRLF, or the shebang names a shell the image lacks | Save the script as LF; `#!/bin/sh` is the portable shebang |
| `SDL_Init` / `No available video device` | Wrong `SDL_VIDEODRIVER`, or the compositor was killed | Use the driver printed in section 1; leave the firmware's display server running |
| Picture on one panel only | Firmware merged both panels into one SDL output | `--layout stacked` |
| Touch hits the wrong half of the bottom screen | Wayland coordinate fix did not run, or it ran on a non-Wayland stack | Set `SDL_VIDEODRIVER=wayland` only when the session is Wayland |
| CMake error about the C compiler test | Cross compiler cannot execute aarch64 binaries | `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY` in the toolchain file |
| Configure tries to download SDL 2.30.11 | `find_package(SDL2)` failed | The `SDL2::SDL2` block in the toolchain file, then a clean build directory |
| Undefined C++ symbols at link | mGBA's static library was linked without a C++ driver | `CMAKE_CXX_COMPILER` must be the matching `g++` or `zig c++` |

## Reference

Configure options the handheld build relies on live in `CMakeLists.txt`:

- mGBA is pinned to **0.10.5**, GBA only, no frontends and no extra deps (zlib, png, ffmpeg, lua).
- SDL2 is `find_package` first. The download (SDL **2.30.11**, static) runs only when that find fails.
- The executable is `pokedaisy`. It links mGBA, SDL2, `libm`, and pthreads.

Runtime flags (`pokedaisy --help`):

```text
--layout auto|stacked|dual|window
--home DIR          data directory (default: $POKEDAISY_HOME, else ~/.local/share/pokedaisy)
--roms DIR          .gba and .zip directory (default: <home>/roms)
--nintendo          east = A, south = B (RG DS, the default)
--xbox              south = A, east = B
```
