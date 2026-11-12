# State variables

The flags and timestamps of the original that guard a phase or compute
a time, in the parts that the port covers, with what became of each one.
"Gone" means the port has no variable for it.

| Original | Where | What it guards | In the port |
|----------|-------|----------------|-------------|
| `colliding_with_mouse` | `engine/game/gameobject.lua:60-66` | the change of the mouse over an object, to call `on_mouse_enter` and `on_mouse_exit` | gone: two awaits in a loop (`src/button.atm`) |
| `Button.selected` | `buy_screen.lua:653` | the look of a button, and whether a click counts | gone: the phase *over* draws the light look and accepts clicks |
| `Button.text` (set on enter and exit) | `buy_screen.lua:654, 662` | the color of the text, by phase | gone: two texts, each drawn by its phase |
| `MainMenu.transitioning` | `mainmenu.lua:90, 180` | the menu stops updating while the next screen starts | gone: the menu is aborted when the transition covers it |
| `TransitionEffect.dead` | `shared.lua:469, 471` | the end of the effect, for its group to remove it | gone: the end of the task |
| `TransitionEffect.text` (set to `nil`) | `shared.lua:469, 471` | whether the text is drawn | gone: the drawing ends with the task |
| `Player.dead` | `player.lua:1618, 1623` | a dead unit stops: every method checks it | gone: the task of the unit ends |
| `Player.leader` | `player.lua:1757` | which movement a unit runs, set on the follower that becomes the leader | gone: a follower phase that ends on `:Snake.lead`, then the leader phase (`src/snake.atm:177-206`) |
| `Player.following` | `player.lua:1420` | the effects of a follower that reaches its place, once | gone: a first step before the loop of the follower |
| `Player.followers`, `Player.parent` | `player.lua:1754-1788` | the links of the snake, moved on every death | one vector of records, `A.units`, in order |
| `Seeker.dead` | `enemies.lua:535` | as `Player.dead` | gone: the task of the seeker ends |
| `Seeker.being_pushed` | `enemies.lua:297, 709` | the tumble after a push, until it slows below 25 | gone: a phase of the loop of the seeker (`src/seeker.atm:83-112`) |
| `Physics.steering_enabled` | `enemies.lua:298, 710` | no steering while pushed | gone: the phase of the push does not steer |
| `seeking`, `wandering`, `separating` (and six others) | `engine/game/steering.lua` | which forces of the tick count | gone: the forces add up in one vector, consumed by `steer_update` |
| `Flashes.f` (with a tagged timer) | `engine/game/flashes.lua` | a white flash after a hit | a counter of `HfxUse` tasks running (`src/fx.atm`) |
| `HPBar.hidden` (with a tagged timer) | `objects.lua:198-203` | the bar of hp, shown for 2 s after a hit | a counter of `ShowHp` tasks running (`src/snake.atm`) |
| `Arena.spawning_enemies` (with tagged timers) | `arena.lua:230-231, 298, 320, 1034-1078` | the next wave and the end wait while enemies spawn | gone: the pool `spawns` of tasks that spawn |
| `Arena.quitting` | `arena.lua:425` | the end of a level happens once | gone: the end is after the loop of waves, in a `par :any` with death |
| `Arena.died` | `arena.lua:820` | no spawns and no end after death | gone: death aborts the waves (`par :any`) |
| `Arena.arena_clear_text` (as a flag) | `arena.lua:672` | the clear happens once | gone: as `quitting` |
| `Arena.start_time` | `arena.lua:206-210` | the countdown, and the HUD reads it | kept as a field for the HUD; the countdown is a loop of three |
| `Arena.wave` | `arena.lua:205, 217` | the number of the wave, read by the condition of the end and by the HUD | kept as a field for the HUD; the end follows the loop |

Count so far: 24 variables in the original (counting the nine flags of
the steering as one), 2 counters and 2 fields for the HUD in the port.

The two counters are state, but not a flag guarding a phase: each
counts tasks that are running, and a task restarts the effect by simply
running. The original restarts it by cancelling a timer by its tag.
