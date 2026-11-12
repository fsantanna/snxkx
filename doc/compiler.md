# The compiler, as seen from the port

Findings about `fsantanna/ceu-atmos` while porting SNKRX, with what the
port does about each one. Each entry says the commit of the compiler
where it was seen, and its status.

| | Finding | Status |
|-|---------|--------|
| 1 | Time is global | by design; a clock per pool is a question for the author |
| 2 | A table cannot keep the tasks of a pool | a flat table pinned in the block of the pool can; a nested one cannot (a question for the author) |
| 3 | `math` has no `atan2`, `exp`, `log` | fixed in `9ec67ec` |
| 4 | `pub` accepted as a name | fixed in `9ec67ec`: a compile error |
| 5 | A hang when the program ends inside an emit | fixed in `9ec67ec` |
| 6 | `await :all` on an empty pool | as the reference Atmos; a question for the author |

The port follows `main` of the compiler; it was at `995c3b2` when the
three fixes arrived.

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

Status: time is global by design. Whether a pool or a task may have a
clock of its own is a question of the language, raised to its author.

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

Status: a table pinned in the block of the pool does keep its tasks
(`pin m = []`, then `set m@0 = t`). In the example, only `w` is pinned,
and the table inside it (`w.ts`) is a plain one, which could be read
out and kept elsewhere. Whether the tables built inside a `pin` should
be pinned with it is a question raised to the author.

## 3. `math` has no `atan2`, `exp`, or `log`

Seen at `2f45211`.

The original uses `math.atan2` for every angle to a target (steering,
rotations, projectiles) and `math.exp` in an easing. The `math` of this
compiler has neither, nor `log` or `tan`.

What the port did: `cmath` in `eng/eng.atm`, four functions over C.

Status: fixed in `9ec67ec`: `math` has `tan`, `asin`, `acos`, `atan`,
`atan2`, `exp`, and `log` (and `atan(y, x)` takes its second argument
since `995c3b2`). The port uses them, and `cmath` is gone.

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

Status: fixed in `9ec67ec`: `invalid declaration : unexpected 'pub'`.

## 5. The program hangs when it ends inside an emit from an awaited task

Seen at `2f45211`.

```
task Item () { await(false) }
task Inner (ui) {
    spawn @ui Item()
    await 1s
    emit @:global :done
    await(false)
}
task Outer () {
    pin ui = tasks()
    await Inner(ui)
}
spawn Outer()
await :done
print("main ends")
```

The main chunk awakes on `:done` and ends, which aborts `Outer` while
`Inner`, awaited by `Outer`, is still inside its `emit`. The abort never
returns: the program spins in `a_blk_xfer` (`rt/task.c:1958`), with no
output, and does not react to SIGTERM. Without the `spawn @ui` in
`Inner`, or with `spawn Inner(ui)` instead of `await Inner(ui)`, or with
an `await 1s` after the `print`, it ends normally.

The port met it in a test whose main chunk ended on the event that ends
a level (`tst/game/level.atm`). The game itself does not end that way.

What the port did: the test awaited a moment before it ended. Later, a
test of a level hung again in this way, and was left spinning by the run
of the tests (which now kills a test after 600 s).

Status: fixed in `9ec67ec`. A block that aborted its tasks waited for
the ones it pinned, but not for the ones confined to it that nobody
pinned: `Inner`, inside its emit, could only unwind when the control
returned to it. The block now waits for those too. The example is the
test `tst/cases/late-10.atm` of the compiler, and the tests of the port
no longer wait at their end.

## 6. `await :all` on an empty pool never awakes

Seen at `2f45211`.

```
pin ts = tasks()
await :all ts           ;; never awakes
```

`await :all ts` awaits the pool to drain, including tasks spawned into
it while it awaits, but on a pool that is already empty it awaits the
next one forever. The port guards it: `if #ts > 0 { await :all ts }`.

Status: the reference Atmos does the same (it awakes on the end of a
task of the pool). Whether an empty pool satisfies it at once is a
question raised to the author. The port keeps its guard.
