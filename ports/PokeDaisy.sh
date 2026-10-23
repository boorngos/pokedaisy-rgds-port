#!/bin/bash
# Anbernic RG DS / Rocknix / PortMaster-style launcher.
# Copy this folder to /roms/ports/pokedaisy/ (or Ports) on the device.
DIR="$(cd "$(dirname "$0")" && pwd)"
export POKEDAISY_HOME="${POKEDAISY_HOME:-$DIR}"
export SDL_GAMECONTROLLERCONFIG
mkdir -p "$POKEDAISY_HOME/roms" "$POKEDAISY_HOME/saves" "$POKEDAISY_HOME/states"
cd "$DIR" || exit 1
# Two DRM panels: dual fullscreen. One tall framebuffer: stacked.
exec "$DIR/pokedaisy" --layout auto --nintendo "$@"
