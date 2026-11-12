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

## 2. A table cannot keep the tasks of a pool

Seen at `2f45211`.

The physics reports contacts between bodies, and the port wants to send
each contact to the two tasks of the bodies, as the original calls
`on_collision_enter` on the two objects. For that, the world needs a
table from body to task. But a task spawned in a pool cannot be stored
in any table outside of itself, even one pinned in the block of the
pool, before or after it:

```
task T () { await(false) }
do {
    pin ts = tasks()
    pin w = [ ts=[] ]
    val t = spawn @ts T()
    set w.ts@0 = t          ;; ==> invalid escape : pinned value
}
```

What the port does: the task that steps the world emits each contact
to itself and its subtree, with the body that it is for, and the task
of each body awaits only its own (`until it.b == b`). A contact costs
an emit through the whole arena, matched by every task that awaits a
contact, instead of two emits.

What would help: a table pinned in the block of a pool, or a weak
reference to a task, that may keep the tasks of the pool and loses them
when they end.

## 3. `math` has no `atan2`, `exp`, or `log`

Seen at `2f45211`.

The original uses `math.atan2` for every angle to a target (steering,
rotations, projectiles) and `math.exp` in an easing. The `math` of this
compiler has neither, nor `log` or `tan`.

What the port does: `cmath` in `eng/eng.atm`, four functions over C.

## 4. A parameter named `pub` is the `pub` of the task, silently

Seen at `2f45211`.

```
func body (w, pub, x, y) {
    set pub.b = 1       ;; ==> invalid index : expected table : got nil
}
body(w, [], 0, 0)
```

`pub` is a keyword, but it is accepted as the name of a parameter (and
of a `val`), which then refers to the `pub` of the running task, not to
the argument. The error comes later, far from the cause.

What the port does: no variable is named `pub`.

What would help: a compile error for `pub` as a name.
