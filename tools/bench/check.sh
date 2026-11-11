#!/bin/sh
# Runs each scripted run of the original, `tst/original/X.sh`, and
# compares its trace with the stored `tst/original/X.txt`.
#   tools/bench/check.sh          compares
#   tools/bench/check.sh -save    stores the traces again

cd "$(dirname "$0")/../.."
fail=0
for s in tst/original/*.sh; do
    n=$(basename "$s" .sh)
    (
        set -a
        . "./$s"
        SNKRX_TRACE="build/original-$n.txt"
        set +a
        tools/bench/run.sh
    ) || { echo "FAIL $n (run)"; fail=1; continue; }
    if [ "$1" = "-save" ]; then
        cp "build/original-$n.txt" "tst/original/$n.txt"
        echo "saved $n"
    elif cmp -s "build/original-$n.txt" "tst/original/$n.txt"; then
        echo "ok   $n"
    else
        echo "FAIL $n"
        diff "tst/original/$n.txt" "build/original-$n.txt" | head -10
        fail=1
    fi
done
exit $fail
