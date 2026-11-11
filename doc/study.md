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
