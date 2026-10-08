# The airfield

The airfield is built in code (`client/src/airfield.cpp`) on the level ground
the simulation keeps within 3.3 km of the origin. It is presentation only: the
ground under it is flat and nothing on it has a collision shape, so an
aircraft still passes through a building. One table of rectangles lays out the
paving, and the same table answers what the ground at a point is used for
(`airfieldUse`), which keeps trees off the field, colours the map and decides
whether a wheel throws up tyre smoke or dust.

Renderer axes: east is +X, south is +Z. The runway runs north-south through
the origin, where a flight starts.

- **Runway 18/36**, 2,600 m by 45 m with concrete shoulders and a 100 m stopway
  with chevrons at each end. Threshold bar and piano keys, the numbers, aiming
  points at 400 m, touchdown zone bars, centreline and edge stripes. Edge lights
  every 50 m, amber over the last 600 m; green threshold and red end lights; an
  approach light centreline to 390 m with a crossbar at 300 m; a four-unit PAPI
  on the left of each landing direction.
- **Taxiway**, parallel and 105 m west of the centreline, with five exits,
  yellow centre and edge lines, holding-position bars 60 m from the runway and
  blue edge lights.
- **Apron**, 283 m by 850 m of concrete with a taxilane, lead-in lines to four
  arched hangars whose doors stand open, and four numbered stands in front of
  a glazed terminal with two walkways. Floodlight masts stand along the
  buildings' side. Fuel bowsers, tugs and baggage carts are parked on it.
- **Control tower** with a leaning glazed cab, a fire station with two tenders
  standing ready, a fuel farm of four tanks in a bund, two warehouses, a radar
  under a radome, and a car park.
- **Dispersal**: a loop of taxiway south-west of the apron with six hardened
  shelters, each on its own hardstand.
- **Perimeter**: a fence with gates on the west and east, a security road
  inside it, the road in from the west and a spur to the country road east of
  the field. Two windsocks stand beside the runway.

Structures are drawn in eighteen batches, one per material, about 47,000
vertices in all, and skipped beyond 30 km. Paving and paint are layers of the
terrain pass; each kind has its own depth layer because they overlap where
they meet.

`environment.airfield` checks that every structure stands on level ground
inside the fence and below 60 m, that no building overlaps another or comes
within 60 m of the runway or 8 m of a taxiway, and that the places an aircraft
is parked are paved. Visual scenarios `airfield`, `apron`, `shelters` and
`threshold` render it.
