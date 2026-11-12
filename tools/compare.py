#!/usr/bin/env python3
"""Compares the timings of a trace of the port with one of the original.

    tools/compare.py original.txt port.txt [screen]

Both traces are aligned at the start of `screen` (default: arena), and
for each kind of event that both have (`go`, and `add` of SpawnMarker
and Seeker), the ticks of their first occurrences after it are listed
side by side, relative to that start, with the difference.
"""
import sys

KINDS = [('add', 'SpawnMarker'), ('add', 'Seeker')]

def load(path, screen):
    evs = []
    start = None
    for line in open(path):
        p = line.rstrip('\n').split('\t')
        if len(p) < 3:
            continue
        t, ev, det = int(p[0]), p[1], p[2]
        if start is None:
            if ev == 'go' and det == screen:
                start = t
            continue
        if ev == 'go':
            break
        evs.append((t - start, ev, det.split(' ')[0]))
    return evs

def main():
    orig, port = sys.argv[1], sys.argv[2]
    screen = sys.argv[3] if len(sys.argv) > 3 else 'arena'
    a = load(orig, screen)
    b = load(port, screen)
    for ev, what in KINDS:
        ta = [t for (t, e, d) in a if e == ev and d == what]
        tb = [t for (t, e, d) in b if e == ev and d == what]
        n = min(len(ta), len(tb), 12)
        print(f'{ev} {what}: {len(ta)} in the original, {len(tb)} in the port')
        for i in range(n):
            print(f'  {i:2d}  {ta[i]:5d}  {tb[i]:5d}  {tb[i]-ta[i]:+d}')

main()
