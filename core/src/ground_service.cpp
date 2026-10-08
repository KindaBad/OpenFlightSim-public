#include "ofs/ground_service.hpp"

#include "ofs/terrain.hpp"

#include <algorithm>
#include <cmath>

namespace ofs {

bool standingOnGround(const AircraftConfig& config, const State& state) {
  // Speed first: it rules out everything that is flying without a look at the ground.
  if (aircraftCrashed(state) || !std::isfinite(state.pos_ned.norm2()) || !(state.vel_ned.norm2() <= serviceMaxSpeed * serviceMaxSpeed))
    return false;
  const double height = groundHeightNed(state.pos_ned.x, state.pos_ned.y) - state.pos_ned.z;
  return height < std::abs(config.gear_nose.z) + 3;
}

bool needsRepair(const AircraftConfig& config, const State& state) {
  const auto worn = [](double health) { return health < 1; };
  return std::any_of(state.surface_health.begin(), state.surface_health.end(), worn) ||
         std::any_of(state.surface_drag.begin(), state.surface_drag.end(), [](double drag) { return drag > 1; }) ||
         worn(state.engine_health[0]) || worn(state.engine_health[1]) ||
         (state.fuel_mass >= 0 && state.fuel_mass < config.initial_fuel * .98);
}

void repairAndRefuel(const AircraftConfig& config, State& state) {
  state.surface_health.fill(1);
  state.surface_drag.fill(1);
  state.engine_health[0] = state.engine_health[1] = 1;
  state.fuel_mass = config.initial_fuel;
}

}  // namespace ofs
