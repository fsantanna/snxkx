#!/bin/sh
# Sets up the environment of the port, in `ext/` (not versioned):
#
#   ext/ceu-atmos   the compiler, built with `make`
#   ext/box2d       Box2D v3, built as `ext/box2d/build/libbox2d.a`
#   ext/SNKRX       the original, the reference and the source of assets
#
# System packages (Ubuntu 24.04), installed once:
#
#   apt-get install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev \
#                   libsdl2-mixer-dev love xvfb xauth
#
# (In a container without man pages, the package `love` fails to
# configure: `touch /usr/share/man/man6/love-11.5.6.gz` and
# `dpkg --configure -a` fix it.)

set -e
cd "$(dirname "$0")/.."
mkdir -p ext

ATMOS_REV=main
BOX2D_REV=v3.1.1
SNKRX_REV=6b93a64

if [ ! -d ext/ceu-atmos ]; then
    git clone -q https://github.com/fsantanna/ceu-atmos ext/ceu-atmos
fi
(cd ext/ceu-atmos && git fetch -q origin && git checkout -q "origin/$ATMOS_REV" && make -s)

if [ ! -d ext/box2d ]; then
    git clone -q --depth 1 -b "$BOX2D_REV" https://github.com/erincatto/box2d ext/box2d
fi
(
    cd ext/box2d
    mkdir -p build
    for f in src/*.c; do
        o="build/$(basename "$f" .c).o"
        [ "$o" -nt "$f" ] || cc -std=gnu17 -O2 -Iinclude -c "$f" -o "$o"
    done
    ar rcs build/libbox2d.a build/*.o
)

if [ ! -d ext/SNKRX ]; then
    git clone -q https://github.com/a327ex/SNKRX ext/SNKRX
fi
(cd ext/SNKRX && git checkout -q "$SNKRX_REV")

echo "ok: ext/ceu-atmos/atmos ext/box2d/build/libbox2d.a ext/SNKRX"
