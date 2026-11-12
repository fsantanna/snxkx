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
- The port: at `f151195`, built with `-O2`, compiler `ceu-atmos` at
  `995c3b2`.
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
| menu     | port     |  600 |  3.87 | 10.56 | 155 | 128.1 |
| menu     | original |  600 | 11.13 | 18.52 |  54 | 946.5 |
| shop     | port     |  720 |  4.16 | 11.15 | 173 | 128.8 |
| shop     | original |  720 | 11.41 | 19.54 |  63 | 946.2 |
| level 1  | port     | 1800 | 10.60 | 29.42 | 170 | 128.7 |
| level 1  | original | 1800 | 20.10 | 40.81 |  90 | 946.6 |
| stress   | port     | 1500 | 19.06 | 36.21 |  79 | 132.9 |
| stress   | original | 1500 | 24.27 | 42.28 |  62 | 946.0 |

- Ticks per second: the port runs 2.9 times as many as the original in
  the menu, 2.7 in the shop, 1.9 in level 1, and 1.3 in the stress.
- CPU: the port spends 57% to 72% of the CPU time of the original in
  the first three scenarios, and 86% in the stress.
- Memory: the peak of the port is 128 to 133 MB, of the original 946 to
  947 MB, in every scenario. Why the original needs that much was not
  investigated.
- A first run, with the compiler at `2f45211`, gave the same picture
  (167, 173, 177, and 74 ticks per second for the port; 53, 64, 87, and
  59 for the original).

## Where the time of the port goes

`SNX_PROFILE=1` splits the time of a run of the port into its logic (the
reactions of the tasks), its drawing (the lists of shapes), and its
rendering (SDL and OpenGL). The scenarios level 1 and stress, 1500
ticks each, median of three runs:

| | logic (s) | draw (s) | render (s) |
|-|----------:|---------:|-----------:|
| level 1 | 0.21 | 0.32 | 7.78 |
| stress  | 9.73 | 0.73 | 9.27 |

With few objects, the port spends almost all of its time in the
rendering of llvmpipe: the logic of a tick takes about 0.14 ms. With the
crowd, the logic costs as much as the rendering: about 10 ms per tick in
the 927 ticks of the arena with more than 200 seekers.

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
