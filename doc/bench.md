# The benchmark

The port against the original, in the same container, one after the
other, with the same scripted input and the same seed
(`tools/bench/perf.py`).

## Setup

- Machine: 4 cores of an Intel Xeon at 2.10 GHz, 16 GB, Linux 6.18.
- Both on a virtual display (Xvfb, 960x540, the scale 2 of both), with
  OpenGL over Mesa 25.2.8 (llvmpipe, which renders on the CPU, in
  threads), every frame drawn, no cap on the frames.
- The sound is mixed but not played: the port on the "disk" driver of
  SDL 2.30.0, to `/dev/null`; the original on the "null" driver of
  OpenAL.
- The original: LÖVE 11.5 with LuaJIT 2.1, in the bench mode of
  `tools/bench/bench.lua` (every loop is one tick of 1/60 s).
- The port: at `b2c2089` (the whole game, S9), built with `-O2`,
  compiler `ceu-atmos` at `995c3b2`.
- Each run is a process, measured by `wait4`: the time on the clock, the
  CPU time (user and system, of all of its threads, which includes the
  threads of llvmpipe), and the peak of its resident memory. The display
  server is not measured. Three runs of each; the table has the median.

The scenarios share the input of `tst/original/run1.sh`: "arena run" at
tick 62, the first card of the shop at tick 452, GO at tick 522, and
level 1 from tick 573, with no steering.

- menu: 600 ticks of the menu.
- shop: 720 ticks, the shop from tick 113.
- level 1: 1800 ticks, the arena from tick 573.
- stress: 1500 ticks, as level 1 with 200 more seekers in a grid when
  the arena starts, and units that take no damage, so that the crowd
  lives to the end (`SNX_STRESS=200`, `SNKRX_STRESS=200`).

## Results

| scenario | program | ticks | clock (s) | CPU (s) | ticks/s | peak (MB) |
|----------|---------|------:|----------:|--------:|--------:|----------:|
| menu     | port     |  600 |  3.72 | 10.24 | 161 | 130.6 |
| menu     | original |  600 | 11.02 | 18.13 |  54 | 946.2 |
| shop     | port     |  720 |  4.10 | 11.17 | 176 | 131.7 |
| shop     | original |  720 | 11.18 | 18.72 |  64 | 946.1 |
| level 1  | port     | 1800 | 10.46 | 29.13 | 172 | 131.6 |
| level 1  | original | 1800 | 19.85 | 40.86 |  91 | 946.2 |
| stress   | port     | 1500 | 13.26 | 29.58 | 113 | 140.8 |
| stress   | original | 1500 | 23.85 | 42.37 |  63 | 946.9 |

- Ticks per second: the port runs 3.0 times as many as the original in
  the menu, 2.8 in the shop, 1.9 in level 1, and 1.8 in the stress.
- CPU: the port spends 56% to 71% of the CPU time of the original.
- Memory: the peak of the port is 131 to 141 MB, of the original 946 to
  947 MB, in every scenario. Why the original needs that much was not
  investigated.

### An earlier run, with a port that did not attack

The first runs of this benchmark were at the end of S7 (port at
`f151195`, compiler at `2f45211` and `995c3b2`): 155 to 177 ticks per
second for the port in the first three scenarios (as above), but 74 to
79 in the stress, against 59 to 62 for the original. At that time, the
units of the port never attacked (a task spawned in a loop or an `if`
ended at once, `compiler.md`, 7): the snake took no damage and killed no
seeker, so the crowd of 200 lived to the end, while the snake of the
original killed it. With attacks, 197 seekers die by tick 1500 of the
stress in the port, and the logic has less to do.

## Where the time of the port goes

`SNX_PROFILE=1` splits the time of a run of the port into its logic (the
reactions of the tasks), its drawing (the lists of shapes), and its
rendering (SDL and OpenGL). The scenarios level 1 and stress, 1500
ticks each, median of three runs:

| | logic (s) | draw (s) | render (s) |
|-|----------:|---------:|-----------:|
| level 1 | 0.23 | 0.35 | 8.10 |
| stress  | 3.90 | 0.56 | 8.57 |

With few objects, the port spends almost all of its time in the
rendering of llvmpipe: the logic of a tick takes about 0.15 ms. With the
crowd, the logic takes about 4 ms per tick in the 927 ticks of the arena,
less as the crowd dies.

The rest of this section was measured on the port of S7, whose units
did not attack, so the 200 seekers lived through the whole stress: it
shows what a crowd that stays costs (9.73 s of logic then).

A profile of the stress with `callgrind` (700 ticks, the last 127 of them
with the crowd) puts 52% of the instructions under `separate`
(`eng/steer.atm`), which visits every other enemy for each seeker, as
the original does (`steering.lua:164-177`, over all the objects of the
classes of the enemies), and 23% under `phys_step` (`eng/phys.atm`),
which sends each contact as an event to all the tasks that await one
(`doc/compiler.md`, 2). The instructions themselves are mostly the
dispatch of events to the tasks (`emit_trail`, `trail_resume`), the
access to tables (`a_index`, `a_tab_get`), and the arithmetic on the
dynamic values of Atmos.

The same with one part switched off (logic of the stress, two runs
each, with no rendering):

| | logic (s) |
|-|----------:|
| as is | 9.35, 8.43 |
| no separation | 6.16, 6.77 |
| no events of the contacts | 10.39, 9.35 |
| neither | 2.46, 2.35 |

The two parts depend on each other, since each changes how the crowd
moves: without the separation, the seekers pile up and touch more
often; without the events, the units do not push the seekers that touch
them, and the seekers do not bounce off the walls. Together, they are
about 70% of the logic of the stress. The original runs the same loop of the
separation in LuaJIT.

## Size of the code

Lines of code (no blanks, no comments) and tokens (`tools/count.py`):

| | lines | tokens |
|-|------:|-------:|
| original, the game (`main`, `arena`, `buy_screen`, `enemies`, `player`, `objects`, `shared`, `mainmenu`, `media`) | 10374 | 134786 |
| original, its engine (`engine/`, without `engine/external`) | 4433 | 40853 |
| port, the game (`src/`, `main.atm`) | 4755 | 43606 |
| port, its engine (`eng/`, Atmos and C) | 2080 | 18476 |

The port covers a part of the game (S0 to S7 of the plan: ten of the
characters, four elites and a boss, the levels, the shop, no passives
or items), so the totals do not compare part for part.
