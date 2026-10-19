#!/usr/bin/env bash
# Copy PokeDaisy to an Anbernic RG DS over SSH and start the demo on its screens.
# Run this from a computer on the SAME Wi-Fi as the handheld. A cloud VM cannot
# reach a 192.168.x address on your LAN.
#
#   SSHPASS='your-ssh-password' ./scripts/run-on-rgds.sh
#   RGDS_HOST=192.168.1.88 RGDS_USER=root SSHPASS=... ./scripts/run-on-rgds.sh
set -euo pipefail

HOST="${RGDS_HOST:-192.168.1.88}"
PORT="${RGDS_PORT:-22}"
USER="${RGDS_USER:-root}"
REMOTE_DIR="${RGDS_DIR:-/root/pokedaisy}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [[ -z "${SSHPASS:-}" ]]; then
  echo "Set SSHPASS to the device SSH password, then re-run." >&2
  echo "Example: SSHPASS='...' $0" >&2
  exit 2
fi

need() { command -v "$1" >/dev/null || { echo "install $1 first" >&2; exit 1; }; }
need ssh
need sshpass
need tar

SSH=(sshpass -e ssh -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 -p "$PORT" "$USER@$HOST")
SCP=(sshpass -e scp -o StrictHostKeyChecking=accept-new -o ConnectTimeout=8 -P "$PORT")

echo "==> $USER@$HOST:$PORT  probing..."
if ! "${SSH[@]}" 'uname -a; echo ---; command -v gcc; command -v cmake; echo DISPLAY=$DISPLAY; ls /dev/dri 2>/dev/null || true'; then
  echo "Cannot reach $HOST. Use a PC on the same network as the RG DS." >&2
  exit 1
fi

echo "==> packing sources..."
STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/pokedaisy"
tar -C "$ROOT" -cf - \
  --exclude './build' --exclude './build-*' --exclude './.git' \
  --exclude './third_party/mgba' --exclude './third_party/SDL2' \
  --exclude './agent-tools' --exclude './web/shots' \
  . | tar -C "$STAGE/pokedaisy" -xf -

echo "==> copying to $REMOTE_DIR ..."
"${SSH[@]}" "mkdir -p '$REMOTE_DIR'"
tar -C "$STAGE" -czf - pokedaisy | "${SSH[@]}" "tar -C '$(dirname "$REMOTE_DIR")' -xzf -"

echo "==> building on device (needs gcc, g++, cmake, git, network for mGBA/SDL2)..."
"${SSH[@]}" "set -e
cd '$REMOTE_DIR'
if ! command -v cmake >/dev/null || ! command -v gcc >/dev/null; then
  echo 'Missing build tools on the RG DS.'
  echo 'Install gcc, g++, cmake, git, plus SDL2 or X11/KMS headers, then re-run.'
  echo 'Rocknix/Anbernic images often have no compiler: compile on a PC for aarch64 instead.'
  exit 3
fi
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j\$(nproc 2>/dev/null || echo 4)
"

echo "==> launching on the handheld screens..."
"${SSH[@]}" "set -e
cd '$REMOTE_DIR'
export POKEDAISY_HOME='$REMOTE_DIR'
export DISPLAY='${RGDS_DISPLAY:-:0}'
export SDL_VIDEODRIVER='${SDL_VIDEODRIVER:-}'
# Prefer the local graphical session if one is running.
if [ -z \"\$SDL_VIDEODRIVER\" ]; then
  if [ -d /dev/dri ]; then export SDL_VIDEODRIVER=kmsdrm; fi
fi
pkill -f './build/pokedaisy' 2>/dev/null || true
nohup ./build/pokedaisy --layout auto --nintendo --demo firered >/tmp/pokedaisy.log 2>&1 &
echo started pid \$!
sleep 1
tail -n 20 /tmp/pokedaisy.log || true
"
echo "If the screens stay black, SSH in and try: SDL_VIDEODRIVER=x11 DISPLAY=:0 $REMOTE_DIR/build/pokedaisy --layout dual --demo firered"
