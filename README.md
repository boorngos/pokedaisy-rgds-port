# PokeDaisy Linux (Anbernic RG DS)

Port Linux di [PokeDaisy](https://github.com/lidor30/pokedaisy) per l’**Anbernic RG DS**: due schermi IPS 4" **640×480**, Rockchip RK3568, Android 14 *o* Linux 64-bit.

Il gioco GBA gira sul pannello **superiore** (libmgba 0.10.5). Il **companion** live sta su quello **inferiore**: party, borsa, lotta, mappa, Pokédex, guida e scheda allenatore, nello stile menu di FireRed. Stesso core C portabile della versione 3DS di PokeDaisy.

**Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy**

Fan project, GPL-3. Non affiliato a Nintendo, Game Freak, The Pokémon Company o Anbernic. **Nessuna ROM o BIOS è inclusa.** Usa dump legali dei tuoi cartucce.

## Cosa gira oggi

| | |
|---|---|
| Piattaforma | Linux 64-bit (x86_64 per il PC, aarch64 sull’RG DS) |
| Schermi | Auto: due display = fullscreen su ciascuno; altrimenti finestra 640×960 (gioco sopra, companion sotto) |
| Giochi | Qualsiasi `.gba`, anche dentro uno `.zip`. Il companion legge **FireRed / LeafGreen (rev 0 e 1)**, **Emerald** inglese e **Lazarus v2.0**. |
| Elenco | Sullo schermo basso. La rotella **SETTINGS** (o Y) apre aspetto, HUD, tasti lotta, shader e tema. |

Non è un recompile dell’app Android (Kotlin / Compose). È un host SDL2 intorno al core C già scritto per schermi doppi.

## Build

Dipendenze: CMake ≥ 3.16, un compilatore C, pthread. SDL2 e mGBA 0.10.5 vengono scaricati al configure se non sono già in `third_party/`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

### Con le tue ROM

```sh
mkdir -p ~/.local/share/pokedaisy/roms
cp "Pokemon - FireRed Version (USA, Europe) (Rev 1).gba" ~/.local/share/pokedaisy/roms/
./build/pokedaisy --layout window
```

Oppure passa il file: `./build/pokedaisy /percorso/al/gioco.gba`.

## Avvio via SSH (stessa Wi‑Fi)

Da un PC **nella stessa rete locale** dell’RG DS (un cloud agent non raggiunge `192.168.x.x`):

```sh
sudo apt install sshpass   # se manca
SSHPASS='la-tua-password-ssh' ./scripts/run-on-rgds.sh
```

Default: `root@192.168.1.88:22`. Altrimenti `RGDS_HOST`, `RGDS_PORT`, `RGDS_USER`. Lo script copia i sorgenti, compila **sul dispositivo** (servono gcc/cmake) e lancia la demo sui due schermi.

## Installazione sull’RG DS (Linux)

L’RG DS può avviare Linux da microSD senza cancellare Android (documentazione Anbernic). I due pannelli DSI 640×480 restano due output.

1. Compila **aarch64** contro la `libSDL2` e la libc di quel firmware (glibc della versione giusta, o musl). La procedura è in [docs/building-rgds.md](docs/building-rgds.md). Il binario è `build-aarch64/pokedaisy`.

2. Copia nella cartella Ports (Rocknix / Anbernic Linux), per esempio `/roms/ports/pokedaisy/`:

   - `pokedaisy` (il binario)
   - `PokeDaisy.sh` (launcher in [docs/building-rgds.md](docs/building-rgds.md))
   - i tuoi `.gba` o `.zip` in `roms/`

3. Avvio: `PokeDaisy.sh`. Con due display usa `--layout dual`; una sola framebuffer alta 960 px usa `--layout stacked`. `auto` sceglie da solo.

Saves: `~/.local/share/pokedaisy/saves/` (o `$POKEDAISY_HOME/saves`), formato `.sav` come mGBA / RetroArch. Backup automatico prima di ogni partita.

## Comandi

| RG DS / pad | Tastiera | Azione |
|---|---|---|
| D-pad, stick L | Frecce | D-pad GBA |
| A (est) / B (sud) | Z / X | A / B GBA (layout Nintendo, default) |
| Start / Select | Invio / Backspace | Start / Select |
| L / R | Q / E | Spalle GBA |
| X | Tab | Scheda successiva del companion |
| Y | Esc | Indietro nel companion. Nell'elenco apre SETTINGS |
| L3 | F1 | Menu rapido (PC, avanti veloce, salva, zoom, schede) |
| R3 | F2 | Cicla ORIGINAL / FIT / STRETCH |
| R2 | Space | Fast-forward tenuto |
| Touch schermo basso | Mouse | Tocchi del companion |
| X+Y tenuti 1 s | | Esce dal gioco (con salvataggio) |
| | F11 | Fullscreen |

`--xbox` inverte A/B se usi un pad Xbox sul PC.

## Opzioni

```
pokedaisy [--layout auto|stacked|dual|window]
          [--home DIR] [--roms DIR]
          [--demo firered|emerald|battle]   # solo fixture, non in elenco
          # i .zip in --roms vengono aperti come i .gba
          [--shot file.png]
          [--nintendo | --xbox]
          [rom.gba]
```

`--shot` disegna un fotogramma dual-screen e esce (utile senza display).

## Licenza

GNU GPL v3, con il termine aggiuntivo di PokeDaisy: chi distribuisce questo lavoro deve mantenere il credito

**Based on PokeDaisy by Lidor Itzhari - https://github.com/lidor30/pokedaisy**

in NOTICE, README e nella schermata crediti. Dettagli in [NOTICE](NOTICE) e [LICENSE](LICENSE).
