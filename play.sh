#!/usr/bin/env bash
# Game Boy emulator launcher.
#
# Usage:
#   ./play.sh                 # runs tetris.gb
#   ./play.sh mygame.gb       # runs a ROM in this folder (or a full path)
#   ./play.sh mygame.gb --headless   # extra emulator flags pass through
#
# Controls: arrows = D-Pad, Z = A, X = B, Enter = Start, RShift = Select.

set -e
cd "$(dirname "$0")"

# Make SDL2.dll visible to Windows.
export PATH="/c/msys64/ucrt64/bin:$PATH"

ROM="${1:-tetris.gb}"
shift || true   # drop the ROM argument; keep any remaining flags

if [ ! -f "$ROM" ]; then
  echo "error: ROM not found: $ROM" >&2
  echo "Available ROMs in this folder:" >&2
  ls -1 *.gb 2>/dev/null || true
  exit 1
fi

exec ./build/gameboy_emu.exe "$ROM" "$@"
