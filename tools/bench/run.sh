#!/bin/sh
# Runs the original SNKRX in bench mode (see `bench.lua`), with no
# screen, on a virtual display of 960x540 (scale 2).  The checkout in
# `ext/SNKRX` gets `bench.lua` and `original.patch` the first time.
#
#   SNKRX_FRAMES=600 SNKRX_TRACE=out.txt tools/bench/run.sh

set -e
# Paths in the variables are relative to the root of the repository.
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
if ! grep -q SNKRX_BENCH ext/SNKRX/main.lua; then
    (cd ext/SNKRX && git apply "$ROOT/tools/bench/original.patch")
fi
cp tools/bench/bench.lua tools/bench/dump.lua ext/SNKRX/
mkdir -p build

export SNKRX_BENCH=1
export ALSOFT_DRIVERS=null
xvfb-run -a -s "-screen 0 960x540x24" love ext/SNKRX 2> "$ROOT/build/love.err" || {
    tail -20 "$ROOT/build/love.err" | grep -v "^ALSA\|pw.conf\|ALSOFT" >&2
    exit 1
}
