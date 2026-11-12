# snxkx

A port of [SNKRX](https://github.com/a327ex/SNKRX) to
[Atmos](https://github.com/fsantanna/ceu-atmos), over SDL2.

SNKRX is an arcade roguelite by a327ex: a snake of heroes that attack
by themselves, in an arena with waves of enemies, with a shop between
levels. The original is written in Lua over LÖVE.

The port is written from scratch. Its goal is a study: every timer
callback of the original (`after`, `every`, `tween`, `cooldown`)
becomes an `await` in a task, and the flags and timestamps that guard
the phases of the game disappear.

## Running

On Ubuntu 24.04, with the packages listed in `tools/setup.sh`:

```
tools/setup.sh      # the compiler, Box2D, and the original, in ext/
./run.sh            # the game
tst/run.sh          # the tests, with no screen
```

## Status

Work in progress: the menu, the shop, the levels with their waves,
elites and bosses, all characters and classes, and the passives with
their items; not yet the other enemies, the loops after level 25, the
options, or the save.

- `doc/study.md`: the original and the port, side by side, behavior by
  behavior.
- `doc/state.md`: the flags and timestamps of the original, and what
  became of each one.
- `doc/bench.md`: the port against the original (`tools/bench/perf.py`).
- `doc/compiler.md`: findings about the compiler.

## Sources

| What | Where |
|------|-------|
| Original | `a327ex/SNKRX@6b93a64`, Lua over LÖVE 11 |
| Student port, read as a starting point | `kboltiz/SNKRX@18c593b`, folder `atmos/` |
| Compiler | `fsantanna/ceu-atmos`, branch `main` |
| Physics | Box2D v3 (C) |

## License

The code of this port is under the MIT license (`LICENSE`). See
`CREDITS.md` for the original works, and `doc/assets.md` for the
licenses of the assets of the original.
