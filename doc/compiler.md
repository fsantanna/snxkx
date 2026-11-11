# The compiler, as seen from the port

Findings about `fsantanna/ceu-atmos` while porting SNKRX, with what the
port does about each one. Each entry says the commit of the compiler
where it was seen.

## 1. Time is global: no clock for a part of the program

Seen at `2f45211`.

The original slows the game down with a factor per group of objects:
the arena updates its main group with `dt*slow_amount*main_slow_amount`
and its other groups with `dt*slow_amount`, while the global timer and
the buttons of the pause menu run in real time (`arena.lua:409-417`).
When the snake dies, the main group slows to a stop in 0.7 s while the
interface keeps moving.

In Atmos, time is an emit of a number, and it must be global:

```
pin ts = tasks()
spawn @ts T()
emit @ts (50000)    ;; ==> invalid emit : expected global
```

So a pool cannot have its own clock, and `await 100ms` in a task of
the pool always follows the time of the whole program.

What the port does: the engine scales the time it emits by a global
factor (`eng.slow`), and emits the real time of each tick as an event
`:tick [us]`, for what must keep moving. Tasks of the interface that
must ignore the slow motion count real ticks instead of awaiting a
duration.

What would help: time emitted to a task or a pool, which advances the
timers of that subtree only, e.g. `emit @ts (us)` for a pool that is
toggled off for the global time.
