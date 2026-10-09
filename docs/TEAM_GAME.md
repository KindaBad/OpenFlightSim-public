# Team battle

A hosted game is a free-for-all unless the server is started with `--teams`,
which makes it Red against Blue. `--score-limit N` (50 to 5,000, default 300)
sets the score that wins a round. The launcher's Multiplayer page has both
under "Game type" and "Score to win", and "Team" for the side to ask for.

## The map

The world has three airfields, laid out alike with north-south runways:

| | North | East | Belongs to |
|---|---|---|---|
| Origin | 0 | 0 | nobody |
| North field | 19,000 m | −2,750 m | Red |
| South field | −17,000 m | 5,750 m | Blue |

They are part of the terrain in every game type (`core/include/ofs/terrain.hpp`).
Each side also has a depot 800 m east of its runway and three outposts in the
country between the fields. `core/src/bases.cpp` lists every structure:

| Structure | Where | Points |
|---|---|---|
| Command post | depot | 40 |
| Fuel store × 2 | depot | 20 each |
| Ammunition store × 2 | depot | 20 each |
| Radar | depot | 20 |
| Missile site × 2 | north and south of the field | 15 each |
| Flak gun × 4 | around the field | 10 each |
| Supply depot × 2, radar, flak gun | each outpost | 15, 15, 15, 10 |

## Rules

- A pilot joins the side asked for, or the smaller one if none was. `/red` and
  `/blue` in chat change sides, in a fresh aircraft.
- Each side starts at its own airfield: in the air over it, or on its runway
  if the server was started with `--ground`.
- Only bombs damage structures. A destroyed structure scores its points for
  the side that bombed it and is rebuilt after 240 seconds.
- An aircraft shot down scores 10 for the side that did it.
- Weapons do no harm to their own side, with one exception: a bomb's blast
  still reaches the aircraft that dropped it.
- Radar and heat seekers show only the other side.
- Repair, fuel and weapons come only from standing still for ten seconds on
  the side's own airfield.
- The first side to the limit wins. Fifteen seconds later the score is cleared,
  everything is rebuilt and everyone starts again.

## Defences

Flak guns engage aircraft of the other side within 2,600 m, in one-second
bursts with a wandering aim. Missile sites launch a radar missile at aircraft
within 11 km that are more than 120 m above the ground and in line of sight,
one every 22 seconds; chaff and crossing flight defeat it as they do any
other. A destroyed gun or site is silent until rebuilt. The figures are in
`core/include/ofs/bases.hpp` and `network/src/world.cpp`.

## What is not there

Guns and missiles do not damage structures, so only a bomber scores on the
ground. Bots join the smaller side and fight the other side's aircraft; they
do not bomb. Nothing collides with a structure: an aircraft passes through it.
