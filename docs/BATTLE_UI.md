# Flight interface

The flight display, the in-flight windows and the launcher share one look: deep
navy panels with a thin blue edge, pale text, green flight symbology, and amber
and red kept for warnings. No game assets or branding are imported.

## Flight display (`client/src/hud.cpp`)

- **Flight panel**, upper left: aircraft and camera, indicated airspeed in km/h
  and altitude in metres in large figures, Mach, load factor and vertical
  speed, a thrust gauge that turns amber and reads REHEAT with the afterburner
  lit, and a fuel gauge that turns red below 15 %.
- **Weapons**, below it when armed in a game: gun rounds, one pip per missile
  still on its pylon, and a plain status line.
- **Heading tape**, top centre. **Connection**, top right: pilots and ping, or
  the number of bandits in a local dogfight, with the radar scope under it.
- **Gunsight**: a ring with a pip where the gun points, in every flying view.
  It closes up and turns amber while firing. Own rounds striking draw a white
  cross on it; a kill draws a red one and ENEMY DESTROYED.
- **Airframe diagram**, beside the minimap: the aircraft from above with each
  part coloured by its damage, hit points, and a line for every part in
  trouble (RIGHT ENGINE OUT, LEFT WING 34%). The screen edges flash red when
  hit. See [the damage model](DAMAGE_MODEL.md).
- **Status strip**, bottom centre: lit switches for gear, flaps, airbrake and
  maneuver mode, and PAUSED or PARKING BRAKE.
- **Chat**, lower left: recent lines fade after nine seconds; server notices
  (arrivals, departures, kills) are amber. `/` or Enter opens the box.
- **Scoreboard**: hold K for every pilot's name, aircraft, kills and losses.
- **Labels** on other aircraft show the pilot's name, range and a small bar of
  what is left of them once they are damaged.
- A banner says when a game is being joined or could not be, with the reason.

## Windows (`client/src/debug_ui.cpp`)

Esc opens the menu: resume, fly again, back to the runway and pause in solo
flight, fight bots, mouse aim and maneuver mode, settings, a controls
reference, and quit. A solo flight waits while the menu is up; a shared one
carries on. Esc backs out one step at a time (chat box, open window, menu).
F1 still shows the developer diagnostics with the same settings window.

Scripted runs (smoke tests, captures, `--frames`, `--seconds`) have no menu:
Esc ends them as before. `--visual-scenario menu` and `controls` render the
menu for inspection.

## Validation

Reviewed 1280x800 captures of a local dogfight, a two-client game, the menu and
the damage scenarios on Linux/OpenGL; the graphical smoke, dogfight smoke and
gun smoke runs pass. `battle.chat` covers chat text and visibility. Generated
captures remain local under the repository's source-only policy.
