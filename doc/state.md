# State variables

The flags and timestamps of the original that guard a phase or compute
a time, in the parts that the port covers, with what became of each one.
"Gone" means the port has no variable for it.

| Original | Where | What it guards | In the port |
|----------|-------|----------------|-------------|
| `colliding_with_mouse` | `engine/game/gameobject.lua:57` | the change of the mouse over an object, to call `on_mouse_enter` and `on_mouse_exit` | gone: two awaits in a loop (`src/button.atm`) |
| `Button.selected` | `buy_screen.lua:653` | the look of a button, and whether a click counts | gone: the phase *over* draws the light look and accepts clicks |
| `Button.text` (set on enter and exit) | `buy_screen.lua:654, 662` | the color of the text, by phase | gone: two texts, each drawn by its phase |
| `MainMenu.transitioning` | `mainmenu.lua:88, 180` | the menu stops updating while the next screen starts | gone: the menu is aborted when the transition covers it |
| `TransitionEffect.dead` | `shared.lua:469, 471` | the end of the effect, for its group to remove it | gone: the end of the task |
| `TransitionEffect.text` (set to `nil`) | `shared.lua:469, 471` | whether the text is drawn | gone: the drawing ends with the task |

Count so far: 6 variables in the original, 0 in the port.
