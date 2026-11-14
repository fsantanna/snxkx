#!/bin/sh
# runs one test: build/one.sh tst/game/shop
ROOT=$(pwd)
export ATMOS_HOME="$ROOT/ext/atmos"
export ATMOS_CFLAGS="-O0 -I$ROOT -I$ROOT/ext/box2d/include"
export ATMOS_LDFLAGS="$ROOT/ext/box2d/build/libbox2d.a -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNX_MUTE=1 OUT="$ROOT/build/tst"
mkdir -p "$OUT"
set -a; [ -f "$1.sh" ] && . "./$1.sh"; set +a
"$ROOT/ext/atmos/atmos" "$1.atm"
