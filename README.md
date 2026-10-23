# PokeDaisy Linux (Anbernic RG DS)

## ❗Disclaimer❗
This project has been built using **AI assistance**. If you deprecate the use of AI and, legitimately, do not want to forage in any way AI companies, don't support this project. 

Linux port of [PokeDaisy](https://github.com/lidor30/pokedaisy) for the **Anbernic RG DS**: two 4" IPS screens at **640×480**, Rockchip RK3568, Android 14 *or* 64-bit Linux.

The GBA game runs on the **top** panel (libmgba 0.10.5). The live **companion** sits on the **bottom** one: party, bag, battle, map, Pokédex, guide, and trainer card, in the FireRed menu style. Same portable C core as the 3DS version of PokeDaisy.

**Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy**

Fan project, GPL-3. Not affiliated with Nintendo, Game Freak, The Pokémon Company, or Anbernic. **No ROM or BIOS is included.** Use legal dumps of your own cartridges.

## What runs today

| | |
|---|---|
| Platform | 64-bit Linux (x86_64 on a PC, aarch64 on the RG DS) |
| Screens | Auto: two displays = fullscreen on each; otherwise a 640×960 window (game on top, companion below) |
| Games | Any `.gba`, including one packed in a `.zip`. The companion reads English **FireRed / LeafGreen (rev 0 and 1)**, **Emerald**, and **Lazarus v2.0**. |
| List | On the bottom screen. The **SETTINGS** gear (or Y) opens appearance, HUD, battle buttons, shader, and theme. |

This is not a recompile of the Android app (Kotlin / Compose). It is an SDL2 host around the C core already written for two screens.

## Build

Dependencies: CMake ≥ 3.16, a C compiler, pthread. SDL2 and mGBA 0.10.5 are downloaded at configure time if they are not already in `third_party/`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### With your ROMs

```sh
mkdir -p ~/.local/share/pokedaisy/roms
cp "Pokemon - FireRed Version (USA, Europe) (Rev 1).gba" ~/.local/share/pokedaisy/roms/
./build/pokedaisy --layout window
```

Or pass the file: `./build/pokedaisy /path/to/the/game.gba`.

## Launch over SSH (same Wi-Fi)

From a PC **on the same LAN** as the RG DS (a cloud agent cannot reach `192.168.x.x`):

```sh
sudo apt install sshpass   # if it is missing
SSHPASS='your-ssh-password' ./scripts/run-on-rgds.sh
```

Default: `root@192.168.1.88:22`. Otherwise set `RGDS_HOST`, `RGDS_PORT`, `RGDS_USER`. The script copies the sources, builds **on the device** (gcc and cmake are required), and starts the demo on both screens.

## Install on the RG DS (Linux)

The RG DS can boot Linux from a microSD without erasing Android (see Anbernic's documentation). The two 640×480 DSI panels stay two outputs.

1. Build **aarch64** against that firmware's `libSDL2` and libc (the matching glibc version, or musl). The procedure is in [docs/building-rgds.md](docs/building-rgds.md). The binary is `build-aarch64/pokedaisy`.

2. Copy it into the Ports folder (Rocknix / Anbernic Linux), for example `/roms/ports/pokedaisy/`:

   - `pokedaisy` (the binary)
   - `PokeDaisy.sh` (launcher in [docs/building-rgds.md](docs/building-rgds.md))
   - your `.gba` or `.zip` files in `roms/`

3. Start it with `PokeDaisy.sh`. Two displays use `--layout dual`; a single framebuffer 960 px tall uses `--layout stacked`. `auto` picks on its own.

Saves: `~/.local/share/pokedaisy/saves/` (or `$POKEDAISY_HOME/saves`), `.sav` files in the same format as mGBA / RetroArch. An automatic backup is taken before each session.

## Controls

| RG DS / pad | Keyboard | Action |
|---|---|---|
| D-pad, left stick | Arrows | GBA D-pad |
| A (east) / B (south) | Z / X | GBA A / B (Nintendo layout, the default) |
| Start / Select | Enter / Backspace | Start / Select |
| L / R | Q / E | GBA shoulders |
| X | Tab | Next companion tab |
| Y | Esc | Back in the companion. On the list, opens SETTINGS |
| L3 | F1 | Quick menu (PC, fast-forward, save, zoom, tabs) |
| R3 | F2 | Cycle ORIGINAL / FIT / STRETCH |
| R2 | Space | Fast-forward while held |
| Bottom-screen touch | Mouse | Companion touches |
| X+Y held for 1 s | | Leave the game (and save) |
| | F11 | Fullscreen |

`--xbox` swaps A/B if you use an Xbox pad on a PC.

## Options

```
pokedaisy [--layout auto|stacked|dual|window]
          [--home DIR] [--roms DIR]
          [--demo firered|emerald|battle]   # fixtures only, not in the list
          # .zip files in --roms are opened the same way as .gba
          [--shot file.png]
          [--nintendo | --xbox]
          [rom.gba]
```

`--shot` draws one dual-screen frame and exits (useful without a display).

## License

GNU GPL v3, with PokeDaisy's additional term: anyone who distributes this work must keep the credit

**Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy**

in NOTICE, the README, and the credits screen. Details are in [NOTICE](NOTICE) and [LICENSE](LICENSE).
