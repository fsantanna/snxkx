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
| `Seeker.headbutt_charging` | `enemies.lua:186-191` | no second headbutt while one charges; the seeker aims | gone as a guard: the loop of the attack; the aim is the count `e.aiming` |
| `Seeker.headbutting` | `enemies.lua:193, 198, 410-432` | the dash: no chasing, and a hit ends it | kept: read by the movement and by the contacts |
| `Seeker.last_headbutt_time` | `enemies.lua:182, 184, 194` | 10 s between headbutts | gone: `await WaitReal(10)` beside the dash |
| `Seeker.shooting` | `enemies.lua:222, 308` | the shooter aims instead of chasing | the count `e.aiming` |
| `Seeker.speed_boosting` (a timestamp, with a tagged timer) | `enemies.lua:269-277, 719-720` | the factor of a speed boost, and its end | gone as a timestamp: a task (`SpeedBoosts`) with the time in a local; the factor `e.boost` remains |
| `ShopCard.selected` | `buy_screen.lua:1617, 1638, 1651, 1659-1662` | the look of a card, its pulse, and whether a click buys it | the phase *over* of the card, with a local read by its drawing |
| `ShopCard.owned`, `ShopCard.owned_n` (set by `refresh`) | `buy_screen.lua:1597-1611` | the marks of the copies in the party, refreshed by every writer | gone: computed when the card draws (`owned_marks`) |
| `CharacterPart.just_created` | `buy_screen.lua:1162-1163, 1211` | no sale in the first 0.1 s of a square | a local, cleared by a task after 0.1 s |
| `selected` of the buttons of the shop (reroll, lock, level, GO) | `buy_screen.lua:706, 717, 733, 741, 769, 789, 797, 822-872, 996-1040` | the look, and whether a click counts | gone: the phases of `ShopButton` |
| `GoButton.transitioning` | `buy_screen.lua:690, 709` | GO starts the level once | gone: after GO, the shop awaits forever, until the transition aborts it |
| `locked_state` (a global) | `buy_screen.lua:61, 136, 173, 703, 763-765, 1625` | the cards kept for the next shop, rewritten by four writers | the fields `run.locked` and `run.cards`, written by the shop only |
| `Seeker.jester_cursed`, `bane_cursed`, `infested`, `silenced` (tagged timers) | `enemies.lua:730-760` | the curses, read when the enemy dies or attacks | kept as fields holding a token (`Cursed`): a new curse replaces the token |
| `Seeker.barbarian_stunned` | `player.lua:2486-2489`, `enemies.lua:477` | twice the damage and no attacks | kept as a field (`stunned`) set by a task for its time |
| `Seeker.slowed` (a tagged timer) | `enemies.lua:724-727` | the speed | kept, set by one task that restarts on a new slow (`SeekerSlows`) |
| `Seeker.taunted` (a tagged timer) | `player.lua:2842`, `enemies.lua:306` | the target of the chase | kept as a token field |
| `Seeker.juggernaut_push`, `launcher_push` | `enemies.lua:299, 375-382` | the damage on a wall while thrown | kept, cleared at the end of the push phase |
| `Seeker.pyrod` | `enemies.lua:441-442, 593-599` | an area on death after the hit of a pyromancer | gone: the hit carries it (`[pyrod=a]`), and the death reads the last hit |
| `Seeker.doom`, `usurer_count` | `enemies.lua:758, 771` | counts toward a big hit | kept as counters |
| `Player.chain_infused` (a tagged timer) | `player.lua:1727-1730` | the sparks of a hit | kept as a token field |
| `Player.divined`, `Player.undead` | `player.lua:813-818, 1600-1611` | a second life, ten more seconds | kept as fields (the hit and the drawing read them) |
| `Player.fairyd` | `player.lua:693-727, 1454` | the halo of the fairy | gone: the halo reads the speed it gives |
| `Player.hasted` (a timestamp, with a tagged timer) | `player.lua:3853, 1329-1332` | the speed of haste, by the time since the orb | kept as a token holding the time |
| `sorcerer_count`, `dg_counter`, `barrager_counter` | `player.lua:58-73, 376, 644` | the repeats of the sorcerers, the gun kata, the volley | kept as counters (the barrager's as a local) |
| `Player.stored_heal` | `player.lua:1568-1570` | the healing of the psykeeper | kept as a field |
| `Critter.invulnerable` | `player.lua:3887-3888, 3933` | no hit in the first 0.5s | a local cleared by a task |
| `HealingOrb.cant_be_picked_up`, `Gold.cant_be_picked_up` | `player.lua:3670-3671, 3777-3778` | no pickup in the first 0.5s | gone: `await 500ms` before awaiting a unit |
| `Arena.choosing_passives` | `arena.lua:719, 294, 361, 1127` | the arena waits for the choice | gone: the choice is a step of `Clear` |
| `PassiveCard.selected`, `ItemCard.selected` | `buy_screen.lua:1392, 1443, 1542` | the look, and whether a click counts | the phases of the card (a local for the drawing of the item) |
| `info_text` of an object under the mouse (`Button`, `LevelButton`, `CharacterPart`, `CharacterIcon`, `ClassIcon`, `ItemCard`, `PassiveCard`) | `buy_screen.lua:119-128, 1261-1323, 1395-1416, 1515-1562, 1759-1788, 2013-2060` | whether a text is open, to close it on exit, on death, and before making it again | gone: the text is a branch of the phase *over* of its owner (`src/info.atm`) |
| `info_text`, `info_text_2` of a message (`BuyScreen`, `GoButton`, `RerollButton`, `LevelButton`, `ItemCard`), with a tagged timer | `buy_screen.lua:275-322, 692-700, 824-864, 998-1027, 1445-1452` | one message at a time for its owner, closed 2 s after the last request | gone: a task per owner (`Messages`), whose 2 s start again in a `par :any` |
| `CharacterPart.highlighted` | `buy_screen.lua:1335-1345, 2004-2050` | the look of a square while the icon of one of its classes is under the mouse | a local of the square, set on `:Shop.highlight` from the icon |

Count so far: 66 variables in the original (counting the nine flags of
the steering as one, the `selected` of the four buttons of the shop as
one, and the `info_text` of the objects under the mouse, and of the
messages, as one each). In the port, 33 of them are gone. The other 33
became 33 variables, none of which guards a phase of the task that sets
it:

- 17 fields that other tasks read (from 18 of the original): the curses
  (4), the stun, the slow, the taunt, the throws (2), the chain
  infusion, the second life, undead, the haste, the stored healing, and
  the three of the elites (`aiming`, a count shared by two attacks, for
  `headbutt_charging` and `shooting`; `headbutting`; `boost`);
- 6 counts: of the tasks of a flash and of a bar of hp (2), of the
  curses toward a big hit (2), of the repeats of a sorcerer and of the
  gun kata (2);
- 6 locals of a task: `selected` of a card and of an item, `created` and
  `lit` of a square, `invulnerable` of a critter, the count of the
  barrager;
- 4 fields of the run or of the HUD (from 3): `locked` and `cards`,
  `start_time`, `wave`.

The two counters are state, but not a flag guarding a phase: each
counts tasks that are running, and a task restarts the effect by simply
running. The original restarts it by cancelling a timer by its tag.
