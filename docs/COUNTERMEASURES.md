# Countermeasures and the missile warning

Armed aircraft carry 16 flares and 16 bundles of chaff. R drops a flare and Z
chaff, one per press and one every third of a second while held; the dispenser
needs a quarter of a second between releases of either kind. Stocks are
refilled by a new life or by [standing on the ground](DAMAGE_MODEL.md#turn-round).
These are gameplay values with a plausible shape, not a model of any real
dispenser, seeker or decoy.

## What a decoy is

A decoy is a point in the world that the server flies (`ofs::weapons::Decoy`,
`core/src/weapons.cpp`). It leaves from between the engines, thrown down and
out to alternate sides at about 26 m/s relative to the aircraft.

| | Flare | Chaff |
|---|---|---|
| Fools | heat seekers | radar seekers |
| Strength | 1.5, on the scale where an engine at military power seen from astern is 1.0 and full reheat 4.0 | 45 m2 of radar cross-section |
| Full strength | from 0.12 s to 1.4 s | from 0.35 s to 2.5 s |
| Gone | 3.6 s | 6 s |
| Motion | loses speed to the air at 0.9 /s and falls under gravity | stops in the air within a second and sinks slowly |

At most 256 decoys are in the air at once; the oldest goes when another is
released.

## What a seeker does with one

Each tick every missile's seeker follows the strongest source in its field of
view (`seekerDecoy`). Sources are compared by strength, not by how near they
happen to be. Four rules follow from that, and they are what makes
countermeasures a skill rather than a button:

1. **A decoy only wins if it outshines the aircraft.** The source being
   followed is kept unless another is a quarter stronger. A flare (1.5) takes a
   heat seeker off an engine at military power seen from astern (1.0), and
   easily off an aircraft seen from the front or the side, but never off one
   in reheat (up to 4.0). Lighting reheat beside a flare takes the seeker back.
2. **Timing.** A flare dropped while the missile is far away has faded before
   it arrives, and the seeker goes back to the aircraft if that is still in
   view. In the test scenario two flares released at launch, 2.5 km out, do
   nothing; the same two at 1.3 km work.
3. **Break away.** A seeker that loses its flare looks straight ahead again.
   An aircraft still in front of it is retaken; one that has turned out of the
   way is not.
4. **Chaff needs the beam.** A radar seeker tells returns apart by closing
   speed and only compares those within 80 m/s of the one it follows. Chaff
   stops in the air, so it competes with an aircraft only while that aircraft
   flies across the line of sight; at 250 m/s that is within about 19 degrees
   of square to the missile. Turn the missile onto a wingtip, then release.
   Chaff's 45 m2 then beats a fighter seen side-on (3 to 12 m2).

A radar missile still flying on its launch aircraft's guidance has its own
seeker switched off and cannot be decoyed until it goes active, within 12 km of
its target. A seeker with nothing to follow never goes looking for decoys. The
proximity fuse ignores decoys: a missile that follows a flare past an aircraft
closely enough still goes off.

## Missile warning

A missile's target is told that it is the target (protocol 16), so the display
can warn the pilot: where the missile is by the clock, its range and time to
run, whether it is a heat seeker or a radar missile, and the answer to each.
A missile that has gone after a decoy is shown in amber until it is gone. See
[the flight interface](BATTLE_UI.md).

## Bots

A bot answers a missile that comes within 1.8 km with the matching decoy,
again about every second while the missile is still on it. It is inattentive
to every other missile, and it does not change its throttle or its flying to
help the decoy, so it is saved only when its engines happen to be cool enough
or it happens to be crossing.

## Tests

`m4.countermeasures` covers decoy strength and flight, each seeker rule above,
release by request (stock, dispenser cycle, announcement, bounds, wire forms),
bots, and four whole engagements with a heat seeker fired from 2.5 km astern:
unanswered it hits; with sixteen flares and a break turn but the engines in
reheat it hits; with two flares dropped at launch it hits; with two flares
dropped at 1.3 km, the throttle back and a break turn it misses by about 80 m.
`battle.weapons` covers what the decoys look like.
