# The pilot

Two things happen to the pilot, as distinct from the aircraft: too much load
puts them out, and they can leave by the ejection seat.

## Blacking out

A turn pulled hard enough for long enough drains the blood from the pilot's
head. The edges of the view darken and close in, the sound of the world goes
dull and the pilot's pulse is heard; if the load is held, the view goes
altogether, the pilot loses consciousness and lets go of the stick and the
trigger for three and a half seconds, and sight comes back over the couple of
seconds after that. Easing the turn before then brings it back at once.

| Load held from rested | Until the pilot is out |
|---|---|
| up to 6 g | never |
| 7.5 g | about 15 s |
| 9 g | about 6 s |
| 12 g | under 3 s |

Sight begins to go a third of the way there. Recovery is quicker the lighter
the load: a few seconds at 1 g, slower while still pulling 5. Negative load
beyond -2 g reddens the view instead, and is tolerated for less time (about 3 s
at -4 g). The G figure on the flight panel turns amber above what can be held
and red once sight is going.

The model is `ofs::PilotStrain` (`core/include/ofs/pilot.hpp`). The numbers are
chosen for the game, assuming a fit pilot in a g-suit, and are not a
physiological prediction. Each client applies it to its own pilot: an
unconscious pilot's controls are centred before they are sent, and nothing
about it is replicated. Bots are not subject to it. The throttle stays where it
was.

## Ejecting

Hold **J** for one second. The canopy goes, the seat fires up and clear of the
fin, the air stops it within a second or so, and a parachute opens and lets the
pilot down at about six metres a second. The camera follows the pilot.

- **Solo.** The aircraft flies on with nobody in it, engines at idle, until it
  comes down. Six seconds after leaving, the pilot is given a new aircraft the
  way the flight began: on the runway, or in the air.
- **With bots or online.** The server takes the aircraft as lost at once. It
  counts as a death, and as a kill for whoever hit the aircraft in the last few
  seconds, exactly as if it had flown into the ground. The pilot is back after
  the usual wait. Everyone nearby sees the seat and the parachute. A bot ejects
  when both its engines have been shot out.

Any aircraft can be left, armed or not, in the air or on the ground. Ejecting
low and inverted ends at the ground.

The seat and parachute are presentation: `Ejections` (`client/src/ejection.hpp`)
flies them from the aircraft's position and velocity at the moment of leaving.
Online that moment is the server's `Ejected` combat event (network protocol 17).

## Checking it

`core.pilot` holds the loads in the table and checks the times, the order in
which sight and consciousness go, and complete recovery. `battle.ejection`
flies a seat from 600 m and checks that it clears the aircraft, that the
parachute opens once, the rate of descent and the landing. `m4.countermeasures` checks
that the server announces an ejection, counts the death, credits the kill and
starts a new life, and that the request and the event cross the wire.

```sh
./build/release/client/ofs_client --aircraft typhoon --visual-scenario eject --frames 200 --screenshot /tmp/eject.ppm
./build/release/client/ofs_client --aircraft typhoon --visual-scenario blackout --frames 80 --screenshot /tmp/blackout.ppm
```
