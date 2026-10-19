#!/bin/sh
# Launch PokeDaisy on SimpleOS (Weston/Wayland) and give the screens back
# to the DSi home when the game exits. Do not kill Weston. Do not kill the
# SimpleOS loop: its exit path powers the handheld off.
DIR="$(cd "$(dirname "$0")" && pwd)"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/var/run}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
export SDL_VIDEODRIVER=wayland
export SDL_VIDEO_DISPLAY_PRIORITY="${SDL_VIDEO_DISPLAY_PRIORITY:-DSI-1,DSI-2}"
export POKEDAISY_HOME="${POKEDAISY_HOME:-$DIR}"
kill -s STOP $(pidof simpleos) 2>/dev/null
cd "$DIR" || exit 1
"$DIR/pokedaisy" --layout dual --nintendo "$@"
st=$?
kill -s CONT $(pidof simpleos) 2>/dev/null
exit "$st"
