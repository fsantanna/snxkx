#!/bin/sh
# Builds and runs the game (or another program) with the Atmos compiler.
#
#   ./run.sh                  the game
#   ./run.sh -o out main.atm  only builds
#
# Needs `tools/setup.sh` first. The assets come from the checkout of the
# original in `ext/SNKRX` (or SNKRX_DIR). See `eng/eng.h` for the
# variables of the test mode (SNX_FRAMES, SNX_INPUT, SNX_SHOT, ...).

ROOT=$(cd "$(dirname "$0")" && pwd)
export ATMOS_HOME="$ROOT/ext/ceu-atmos"
export ATMOS_CFLAGS="${ATMOS_CFLAGS:--O2} -I$ROOT -I$ROOT/ext/box2d/include"
export ATMOS_LDFLAGS="$ROOT/ext/box2d/build/libbox2d.a -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm"
[ $# -eq 0 ] && set -- main.atm
cd "$ROOT" && exec "$ROOT/ext/ceu-atmos/atmos" "$@"
