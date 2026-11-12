# SNKRX in Atmos: a study

This document compares behaviors of SNKRX as written in the original
(Lua over LÖVE, `a327ex/SNKRX@6b93a64`) and in this port (Atmos over
SDL2). Each section shows the Lua code, the Atmos code, and an analysis.

It starts from the comparison written by Kazabite Boltiz for a partial
port to the reference Atmos over `pico-sdl` (`kboltiz/SNKRX@18c593b`,
`atmos/readme.md`). Its sections and their order are kept; the code and
the analysis are redone for this port, which runs on another compiler
(`fsantanna/ceu-atmos`), in pixels, with the drawing of the original.

References to the original are `file:line` at `6b93a64`; references to
the port are `file:line` in this repository.

---

## 1. Positioning and anchoring

The main menu shows three buttons on the left: *arena run*, *options*,
and *quit*.

### Lua + LÖVE

Each button is created with its center (`mainmenu.lua:85-119`):

```lua
self.arena_run_button = Button{group = self.main_ui, x = 55, y = gh/2 - 10, ..., button_text = 'arena run', ...}
self.options_button   = Button{group = self.main_ui, x = 47, y = gh/2 + 12, ..., button_text = 'options', ...}
self.quit_button      = Button{group = self.main_ui, x = 37, y = gh/2 + 34, ..., button_text = 'quit', ...}
```

The centers 55, 47, and 37 decrease with the length of the text, so
that the three buttons line up on the left: each number is the left
edge plus half of the width of the button, computed by hand.

### Atmos

The port keeps the same numbers (`src/menu.atm:47-49`):

```
spawn @bs Button(task, :arena_run, 55, H/2 - 10, "arena run")
spawn @bs Button(task, :options,   47, H/2 + 12, "options")
spawn @bs Button(task, :quit,      37, H/2 + 34, "quit")
```

### Analysis

The student's port replaced the numbers by an anchor of `pico-sdl`
(`anchor='W'`), so that a button is placed by its left edge. That is a
feature of the drawing library, not of the language: a function
`Button` in Lua with an anchor would do the same.

This port draws as the original, in pixels, to compare screenshots with
it, so it keeps the original's centers. Nothing in this section is
about control flow, and the two versions are equivalent.

---

## 2. Button: encapsulation and sizing

A button sizes itself to its text, and draws a rounded rectangle with
the text in it.

### Lua + LÖVE

The size is computed in `Button:init` (`buy_screen.lua:592-597`):

```lua
function Button:init(args)
  self:init_game_object(args)
  self.shape = Rectangle(self.x, self.y, args.w or (pixul_font:get_text_width(self.button_text) + 8), pixul_font.h + 4)
  self.interact_with_mouse = true
  self.text = Text({{text = '[' .. self.fg_color .. ']' .. self.button_text, font = pixul_font, alignment = 'center'}}, global_text_tags)
end
```

and the drawing is in `Button:draw` (`buy_screen.lua:635-646`):

```lua
function Button:draw()
  graphics.push(self.x, self.y, 0, self.spring.x, self.spring.y)
    ...
    graphics.rectangle(self.x, self.y, self.shape.w, self.shape.h, 4, 4, self.selected and fg[0] or _G[self.bg_color][0])
    self.text:draw(self.x, self.y + 1, 0, 1, 1)
  graphics.pop()
end
```

What the button does is in five methods of the class (`init`,
`update`, `draw`, `on_mouse_enter`, `on_mouse_exit`), plus the update of
the engine (`GameObject:update_game_object`, `engine/game/gameobject.lua`),
which updates its spring and calls `on_mouse_enter` and `on_mouse_exit`.

### Atmos

The button is one task (`src/button.atm:9-59`). Its size and its two
looks are values at the top of the task:

```
task Button (owner, id, x, y, label, opts) {
    ...
    val w = opts && opts.w || (text_width(pixul_font, label) + 8)
    val h = pixul_font.h + 4
    val s = spring(1)
    ...
    val t_out  = text([ [text="[" ++ fg_color ++ "]" ++ label, font=pixul_font, alignment=:center] ])
    val t_over = text([ [text="[fgm5]" ++ label, font=pixul_font, alignment=:center] ])
    val draw = func (color, t) {
        eng.push(x, y, 0, s.x)
        eng.rect(x, y, w, h, 4, 4, color)
        text_draw(t, x, y + 1, 0, 1, 1)
        eng.pop()
    }
    ...
}
```

### Analysis

The arithmetic is the same: the port computes the size and the offset
of the text (`y + 1`) as the original does. What changes is where the
behavior lives: the original spreads it over the methods of a class
and the update loop of the engine; the port has it in one task, read
from top to bottom: the values, the drawing, and then the sequence of
the button (section 3).

The original also keeps the text of the button as state, and replaces
it on every enter and exit (`self.text:set_text{...}`). The port builds
both texts once and draws the one of the current phase.

---

## 3. Hover detection

A button lights up while the mouse is over it.

### Lua + LÖVE

The engine checks the mouse against the shape of every object in every
tick, keeps the result in a flag, and calls a method on each change
(`engine/game/gameobject.lua:42`):

```lua
if self.interact_with_mouse then
  local colliding_with_mouse = self.shape:is_colliding_with_point(self.group:get_mouse_position())
  if colliding_with_mouse and not self.colliding_with_mouse then
    self.colliding_with_mouse = true
    if self.on_mouse_enter then self:on_mouse_enter() end
  elseif not colliding_with_mouse and self.colliding_with_mouse then
    self.colliding_with_mouse = false
    if self.on_mouse_exit then self:on_mouse_exit() end
  end
  ...
end
```

The button keeps a second flag, `selected`, set and cleared by those
methods (`buy_screen.lua:649-665`), and read by `draw` (for the color)
and by `update` (for the click):

```lua
function Button:on_mouse_enter()
  ...
  self.selected = true
  self.text:set_text{{text = '[fgm5]' .. self.button_text, font = pixul_font, alignment = 'center'}}
  self.spring:pull(0.2, 200, 10)
end

function Button:on_mouse_exit()
  self.text:set_text{{text = '[' .. self.fg_color .. ']' .. self.button_text, font = pixul_font, alignment = 'center'}}
  self.selected = false
end

function Button:update(dt)
  ...
    if self.selected and input.m1.pressed then
      if self.action then self:action() end
    end
  ...
end
```

### Atmos

The two states are two steps of a loop (`src/button.atm:32-58`):

```
loop {
    ;; out
    if !inside() {
        par :any {
            await :mouse.mv until inside()
        } with {
            loop on :draw {
                draw(bg_color@0, t_out)
            }
        }
    }
    ;; over
    play(ui_hover1, [pitch=rfloat(1.3, 1.5), volume=0.5])
    play(pop2, [pitch=rfloat(0.95, 1.05), volume=0.5])
    spring_pull(s, 0.2, 200, 10)
    par :any {
        await :mouse.mv until !inside()
    } with {
        loop on :mouse.dn until it.but == 1 {
            emit @owner :Button.click [ id=id, spring=s ]
        }
    } with {
        loop on :draw {
            draw(fg@0, t_over)
        }
    }
}
```

### Analysis

The original needs two flags for one fact, whether the mouse is over
the button: `colliding_with_mouse` in the engine, to detect the change,
and `selected` in the button, for the methods that run later (`draw`
and `update`). Both are written in one place and read in others.

In the port, the fact is the position of the task in its loop. The
step *out* draws the button dark and waits for the mouse to enter; the
step *over* plays the sounds, pulls the spring, draws the button light,
and accepts clicks, until the mouse leaves. A click is only possible in
the step *over*, so no flag guards it; the look is drawn by the step
that is active, so no flag chooses it. Both flags are gone.

The check `if !inside()` covers a mouse that is already over the button
when it appears, which the original detects in its first tick.

---

## 4. Spring-based scaling

A button bounces when the mouse enters it: its scale is a spring,
pulled by 0.2, which oscillates back to 1.

### Lua + LÖVE

Every game object has a spring, created by the engine
(`engine/game/gameobject.lua:37`), and updated
by the engine in every tick, for the whole life of the object:

```lua
function GameObject:update_game_object(dt)
  ...
  self.spring:update(dt)
  ...
end
```

The update reaches the button through the group of the menu, which
updates its objects until the menu destroys it (`mainmenu.lua:148`,
`self.main_ui:destroy()`).

### Atmos

The spring is a record, and a task updates it (`eng/anim.atm:97-105`),
spawned by the button (`src/button.atm:16`):

```
task UISprings (...) {
    val ss = [...]
    loop e on :tick {
        val dt = e.us / 1000000
        loop _, s in ss {
            spring_update(s, dt)
        }
    }
}
```

```
val s = spring(1)
set pub = [ spring=s ]
spawn UISprings(s)
```

### Analysis

As the original, the port updates the spring in every tick while the
button lives, even at rest: the student's port stopped its spring when
it converged, and woke it on the next pull. This port keeps the
original's numbers instead (a spring pulled by 0.2 matches the formula
of the original to 4 decimals over 12 ticks, `tst/eng/anim.atm`).

What changes is how the update stops. In the original, the spring
stops when nothing calls its update any more: the menu destroys the
group, the group stops calling `update` on the button, and the button
stops calling `update` on the spring. In the port, the task that
updates the spring belongs to the button, and the button belongs to
the pool of the menu: when the menu ends, the language aborts the pool,
the button, and the task, in that order. No code of the port stops it.

The updates use the real time (`:tick`), not the game time, as the
original's buttons do with `force_update` (they keep moving in slow
motion).

The menu nudges the soundtrack button every 2 seconds, from outside of
the button (`mainmenu.lua:124`):

```lua
self.t:every(2, function() self.soundtrack_button.spring:pull(0.025, 200, 10) end)
```

```
spawn {
    loop {
        await 2s
        spring_pull(soundtrack.pub.spring, 0.025, 200, 10)
    }
}
```

The `every` becomes a loop with an await. The spring is public in the
button (`pub.spring`) only for this.

---

## 5. Button click: the action flow

A click on *arena run* starts a transition, and the next screen comes
when the transition covers the menu.

### Lua + LÖVE

Each button receives an `action`, a function that the button calls on
a click (`buy_screen.lua:621-625`). The action of *arena run* creates
the transition and gives it another function, `transition_action`,
which loads the run and goes to the shop (`mainmenu.lua:85-111`):

```lua
self.arena_run_button = Button{..., button_text = 'arena run', ..., action = function(b)
  ui_transition2:play{pitch = random:float(0.95, 1.05), volume = 0.5}
  ...
  TransitionEffect{group = main.transitions, x = gw/2, y = gh/2, color = ..., transition_action = function()
    self.transitioning = true
    ...
    main:add(BuyScreen'buy_screen')
    main:go_to('buy_screen', run.level or 1, ...)
  end, text = Text({{text = '[wavy, ...]starting...', ...}}, global_text_tags)}
end}
```

What the menu does after a click is in a function inside a function,
both given away: the first to the button, the second to the transition.
The menu also sets a flag, `self.transitioning`, which its `update`
reads to stop the menu while the next screen starts
(`mainmenu.lua:180`).

### Atmos

The button only reports the click to its owner (section 3), and the
menu waits for clicks in a loop, after it creates its buttons
(`src/menu.atm:68-91`):

```
loop {
    val e = await :Button.click
    match e.id {
        :arena_run => {
            play(ui_transition2, [pitch=rfloat(0.95, 1.05), volume=0.5])
            ...
            emit @:global :goto [ screen=:buy_screen, x=W/2, y=H/2, color=fg@0,
                                  text="[wavy, bg]starting..." ]
            await(false)
        }
        :quit => {
            return :quit
        }
        ...
    }
}
```

### Analysis

In the original, the continuation of a click (the transition, then the
shop) is carried by functions given to other objects, and runs later,
inside them. In the port, it is the code after `await :Button.click`,
in the menu: the menu decides what follows a click, and the button does
not know.

The menu does not start the next screen itself either: it asks for it
(`:goto`), and lives on, as the original's menu does while the circle
grows over it. After the request, the menu awaits nothing
(`await(false)`): it no longer accepts clicks, with no flag for that.
In the original, a second click on *arena run* during the transition
would create a second transition.

---

## 6. Transition effect: control flow

The transition is a circle that grows from a point until it covers the
screen, with a text in the middle. Then the screen below changes, and
the circle shrinks.

### Lua + LÖVE

`TransitionEffect` (`shared.lua:455-476`) is five timers, nested in
their callbacks:

```lua
function TransitionEffect:init(args)
  self:init_game_object(args)
  self.rs = 0
  self.text_sx, self.text_sy = 0, 0
  self.t:after(0.25, function()
    self.t:after(0.1, function()
      self.t:tween(0.1, self, {text_sx = 1, text_sy = 1}, math.cubic_in_out)
    end)
    self.t:tween(0.6, self, {rs = 1.2*gw}, math.linear, function()
      if self.transition_action then self:transition_action(unpack(self.transition_action_args or {})) end
      self.t:after(0.3, function()
        self.x, self.y = gw/2, gh/2
        self.t:after(0.6, function() self.t:tween(0.05, self, {text_sx = 0, text_sy = 0}, math.cubic_in_out) end)
        if not args.dont_tween_out then
          self.t:tween(0.6, self, {rs = 0}, math.linear, function() self.text = nil; self.dead = true end)
        else
          self.t:after(0.6, function() self.text = nil; self.dead = true end)
        end
      end)
    end)
  end)
end
```

### Atmos

The transition is a task (`src/shared.atm:103-128`):

```
task Transition (x, y, color, txt) {
    val st = [ x=x, y=y, rs=0, ts=0 ]
    val t = text([ [text=txt, font=pixul_font, alignment=:center] ])
    spawn {
        loop on :draw {
            eng.circle(st.x, st.y, st.rs, color)
            text_draw(t, W/2, H/2, 0, st.ts, st.ts)
        }
    }
    await 250ms
    spawn {
        await 100ms
        await Tween(0.1, st, [ts=1], cubic_in_out)
    }
    await Tween(0.6, st, [rs=1.2*W], linear)
    emit @:global :covered
    await 300ms
    set st.x = W/2
    set st.y = H/2
    spawn {
        await 600ms
        await Tween(0.05, st, [ts=0], cubic_in_out)
    }
    await Tween(0.6, st, [rs=0], linear)
}
```

and the screens wait for it (`main.atm:23-48`):

```
par {
    var screen = [ Menu, nil, "mainmenu" ]
    loop {
        val next = do {
            trace("go", screen@2)
            pin s = spawn (screen@0)(screen@1)
            val e = await <:goto || s>
            if e == :quit {
                break
            }
            await :covered
            e
        }
        set screen = match next.screen { ... }
    }
} with {
    loop e on :goto {
        await Transition(e.x, e.y, e.color, e.text)
    }
}
```

### Analysis

The five timers become a sequence: each `after` is an `await` of a
duration, each `tween` with a callback is an `await Tween(...)`
followed by the code of the callback, and the two timers that run
alongside the main line (the text pops in, and later out) are two
`spawn` blocks. The nesting of five levels becomes the indentation of
one. The end of the transition, `self.dead = true`, is the end of the
task.

The callback in the middle, `transition_action`, becomes an event,
`:covered`, which the loop of screens awaits: when it comes, the block
of the old screen ends, and the language aborts the screen with all its
tasks. The original has no such end: its `transition_action` calls
`main:go_to`, and each screen cleans up in its `on_exit`, by hand
(`mainmenu.lua:142-163`, twenty lines that destroy groups and set
fields to `nil`).

The transitions run in the second branch of a `par`, so they are drawn
after the screens, and a transition outlives the screen it covers.

Measured with the trace of both programs (`tst/game/menu.out` and
`tst/original/run1.txt`): with a click at tick 62, the shop starts at
tick 113 in both.

---

## 7. Snake units: state management

The snake is a leader and its followers. When a unit dies, the units
behind it move up, and if it was the leader, the next unit leads.

### Lua + LÖVE

A unit carries its role and its links in fields: `leader`, `followers`,
`parent`, `follower_index`, and `dead`. Its update branches on the role
in every tick (`player.lua:1351-1426`), and a dead unit repairs the
links of the others (`player.lua:1754-1781`):

```lua
function Player:recalculate_followers()
  if self.dead then
    local new_leader = table.remove(self.followers, 1)
    new_leader.leader = true
    new_leader.previous_positions = {}
    new_leader.followers = self.followers
    new_leader.t:every(0.01, function()
      table.insert(new_leader.previous_positions, 1, {x = new_leader.x, y = new_leader.y, r = new_leader.r})
      ...
    end)
    main.current.player = new_leader
    for i, follower in ipairs(self.followers) do
      follower.parent = new_leader
      follower.follower_index = i
    end
  else
    ...
  end
end
```

A follower also has `following`, false until it first reaches its place
on the path, when it plays a sound and some particles
(`player.lua:1415-1421`).

### Atmos

A unit is a task (`src/snake.atm:117`). A follower runs the code of a
follower until it is promoted, and then the code of the leader, below
it (`src/snake.atm:175-206`):

```
;; a follower, until the units ahead of it die
if A.units@0 != u {
    par :any {
        ;; first, until there is a place for it on the path
        loop on :clock {
            ...
            until snake_place(A, u)
        }
        play(spawn1, [pitch=rfloat(0.8, 1.2), volume=0.15])
        ...
        ;; then along the path of the leader
        loop on :clock {
            ...
        }
    } with {
        await :Snake.lead until it.u == u
    }
}
;; the leader
par {
    loop us on :clock {
        ...
    }
} with {
    ;; the walls: it bounces, and the followers feel it in turn
    loop e on :collision.enter until (it.b == u.b) && (it.other.kind == :wall) {
        ...
    }
}
```

The units are records in one vector, in order (`A.units`). A dying unit
leaves it, and if it was the first, emits `:Snake.lead` to the next one
(`src/snake.atm`, `snake_remove`).

### Analysis

`leader` was a flag read in every tick to choose between two blocks of
code, and written once, by another unit. In the port, the two blocks are
two phases of the task, in sequence: a unit that reaches the code of the
leader stays there, and nothing tests its role again. The bounce on the
walls, which only the leader does, lives in the phase of the leader too,
so it needs no test either.

`following` was a flag for something that happens once; in the port it
is the step before the loop. `dead` is the end of the task, which also
ends its drawing, its contacts, and its effects.

What remains is the order of the snake, a vector of records, because the
followers need the leader's path and their place in it. The original
keeps the same in three fields per unit (`followers`, `parent`,
`follower_index`), rewritten on every death.

The student's port made each unit adjust its own index when a unit of
its pool ends. Here the vector is rewritten by the dying unit, as in
the original, since the vector is also what the leader averages for the
speed of the snake.

---

## 8. Snake units: status signalling

A hit flashes a unit white for a moment, and shows its bar of hp for
two seconds; a new hit restarts both.

### Lua + LÖVE

Both are a field and a timer with a tag, so that a new timer replaces
the running one (`engine/game/flashes.lua` and `objects.lua:198-203`):

```lua
self[name] = {f = false, default_duration = default_duration or 0.15, flash = function(_, duration)
  self[name].f = true
  self.trigger:after(duration or self[name].default_duration, function() self[name].f = false end, name)
end}
```

```lua
function Unit:show_hp(n)
  self.hp_bar.hidden = false
  self.hp_bar.color = red[0]
  self.t:after(n or 2, function() self.hp_bar.hidden = true end, 'hp_bar')
end
```

The drawing reads the fields (`self.hfx.hit.f and fg[0] or self.color`,
`if self.hidden then return end`).

### Atmos

Both are tasks that count themselves while they run (`src/fx.atm` and
`src/snake.atm`):

```
task HfxUse (h, x, k, d, dur) {
    spring_pull(h.s, x, k, d)
    set h.f += 1
    defer {
        set h.f -= 1
    }
    await((dur || 0.15) * 1s)
}
```

```
task ShowHp (u, n) {
    set u.hp_shown += 1
    defer {
        set u.hp_shown -= 1
    }
    await((n || 2) * 1s)
}
```

and a hit spawns them: `spawn HfxUse(u.hit, 0.25, 200, 10)` and
`spawn ShowHp(u)`.

### Analysis

The effect is a duration, which a task expresses directly: it starts,
waits, and ends, and the `defer` undoes it even if the unit dies first.
To restart it, the original cancels a timer by its name; the port runs
another task, and the effect lasts while any of them runs, which ends at
the same moment as the restarted timer (the last one started is the last
to end).

The counter is still a variable that the drawing reads. It is not a
flag set in one place and cleared in another, though: each task
increments and decrements it around its own wait.
