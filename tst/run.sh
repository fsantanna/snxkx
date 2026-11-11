#!/bin/sh
# Runs the tests: each `tst/X.atm` is compiled, run, and its output
# compared with `tst/X.out`.  `ASAN=1 tst/run.sh` uses the address
# sanitizer.  Needs `tools/setup.sh` first.

cd "$(dirname "$0")/.."
ROOT=$(pwd)
ATMOS="$ROOT/ext/ceu-atmos/atmos"
export ATMOS_HOME="$ROOT/ext/ceu-atmos"
export ATMOS_CFLAGS="-O1 -I$ROOT/ext/box2d/include"
export ATMOS_LDFLAGS="$ROOT/ext/box2d/build/libbox2d.a -lm"
[ -n "$ASAN" ] && export ATMOS_ASAN=1
mkdir -p build/tst

fail=0
for t in tst/*.atm; do
    n=$(basename "$t" .atm)
    if (cd build/tst && "$ATMOS" "$ROOT/$t") > "build/tst/$n.got" 2>&1 \
       && diff -u "tst/$n.out" "build/tst/$n.got" > /dev/null; then
        echo "ok   $n"
    else
        echo "FAIL $n"
        diff -u "tst/$n.out" "build/tst/$n.got" | head -20
        fail=1
    fi
done
exit $fail
