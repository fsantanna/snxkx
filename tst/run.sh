#!/bin/sh
# Runs the tests, with no screen. Needs `tools/setup.sh` first.
#
#   tst/run.sh            runs and compares with the stored results
#   tst/run.sh update     stores the new results as the expected ones
#   ASAN=1 tst/run.sh     the same, with the address sanitizer
#
# Each test is a program `tst/[dir/]X.atm`, run from the root:
#   - its output is compared with `X.out`;
#   - `X.sh`, if any, sets its environment (e.g. SNX_FRAMES, SNX_INPUT);
#     a screenshot is saved as `$OUT/name.png` and compared with
#     `tst/shots/name.png`.

cd "$(dirname "$0")/.."
ROOT=$(pwd)
ATMOS="$ROOT/ext/ceu-atmos/atmos"
export ATMOS_HOME="$ROOT/ext/ceu-atmos"
export ATMOS_CFLAGS="-O1 -I$ROOT -I$ROOT/ext/box2d/include"
export ATMOS_LDFLAGS="$ROOT/ext/box2d/build/libbox2d.a -lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer -lm"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy SNX_MUTE=1
[ -n "$ASAN" ] && export ATMOS_ASAN=1
OUT="$ROOT/build/tst"
mkdir -p "$OUT" tst/shots
[ "$1" = "update" ] && UPDATE=1

fail=0
for t in tst/*.atm tst/*/*.atm; do
    n=$(echo "${t#tst/}" | sed 's/\.atm$//; s|/|-|g')
    base="${t%.atm}"
    rm -f "$OUT/$n".*.png "$OUT/$n.png"
    export OUT
    (
        if [ -f "$base.sh" ]; then
            set -a
            . "./$base.sh"
            set +a
        fi
        [ -z "$SNX_MUTE" ] && unset SNX_MUTE
        "$ATMOS" "$t"
    ) > "$OUT/$n.got" 2>&1 < /dev/null
    if [ "$UPDATE" = 1 ]; then
        cp "$OUT/$n.got" "$base.out"
        for p in "$OUT/$n".png "$OUT/$n".*.png; do
            [ -f "$p" ] && cp "$p" tst/shots/
        done
        echo "saved $n"
        continue
    fi
    ok=1
    diff -u "$base.out" "$OUT/$n.got" > "$OUT/$n.diff" 2>&1 || ok=0
    for p in "$OUT/$n".png "$OUT/$n".*.png; do
        [ -f "$p" ] || continue
        if ! cmp -s "$p" "tst/shots/$(basename "$p")"; then
            ok=0
            echo "     shot differs: $p" >> "$OUT/$n.diff"
        fi
    done
    if [ $ok = 1 ]; then
        echo "ok   $n"
    else
        echo "FAIL $n"
        head -20 "$OUT/$n.diff"
        fail=1
    fi
done
exit $fail
