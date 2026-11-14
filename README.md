# sdl-snkrx

A port of [SNKRX](https://github.com/a327ex/SNKRX) to
[Atmos](https://github.com/ia-atmos/lang), over SDL2.

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

The game as played: the menu, the shop, the 25 levels with their
waves, elites, and bosses, all 54 characters and 16 classes, the 82
passives, the win and the loops, new game plus, the options, the music,
and the save, and the texts under the mouse. Left out: the tutorial, the
credits, and Steam (see `doc/case-study.md`).

- `doc/case-study.md`: what became of the timers and the flags, in short.

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
| Compiler | `ia-atmos/lang`, branch `main` |
| Physics | Box2D v3 (C) |

## License

The code of this port is under the MIT license (`LICENSE`). See
`CREDITS.md` for the original works, and `doc/assets.md` for the
licenses of the assets of the original.
