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
# The GBA clock follows the console. SimpleOS stores the zone it displays.
if [ -z "${TZ:-}" ]; then
	for f in /simpleos/userdata/clock /mnt/mmc/simpleos/userdata/clock; do
		[ -f "$f" ] || continue
		tz=$(sed -n 's/^timezone=//p' "$f" | tail -n 1 | tr -d '\r')
		[ -n "$tz" ] || continue
		export TZ="$tz"
		break
	done
fi
kill -s STOP $(pidof simpleos) 2>/dev/null
cd "$DIR" || exit 1
"$DIR/pokedaisy" --layout dual --nintendo "$@"
st=$?
kill -s CONT $(pidof simpleos) 2>/dev/null
exit "$st"
