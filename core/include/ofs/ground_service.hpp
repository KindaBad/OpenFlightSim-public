#pragma once
// Turning an aircraft round on the ground.
//
// An aircraft that lands and stands still for a short while is repaired,
// refuelled and rearmed. The rule lives here so the server, the solo game and
// the display that counts the wait down all agree on it.

#include "ofs/aircraft.hpp"

namespace ofs {

// Seconds an aircraft has to stand before the work is done, and how slowly it
// may still be rolling, in m/s, to count as standing.
inline constexpr double serviceSeconds = 10, serviceMaxSpeed = 2;

// Down, in one piece and at rest: within a few metres of the ground and no
// faster than a walk, which nothing that is still flying can be.
bool standingOnGround(const AircraftConfig& config, const State& state);

// Whether the airframe, the engines or the tanks would gain from the stop.
bool needsRepair(const AircraftConfig& config, const State& state);

// Makes every part whole and fills the tanks. Position, motion, the weapons
// load and the controls are the caller's.
void repairAndRefuel(const AircraftConfig& config, State& state);

}  // namespace ofs
