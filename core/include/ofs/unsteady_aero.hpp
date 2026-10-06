#pragma once
#include "ofs/aircraft.hpp"
namespace ofs {
struct UnsteadyDerivative {
  double alpha[2]{}, separation[2]{}, vortex[2]{};
};
// Pure functions: evaluation/trim do not mutate aerodynamic memory.
UnsteadyDerivative unsteadyDerivative(const AircraftConfig&, const State&, Vec3 air_velocity);
void initializeUnsteady(const AircraftConfig&, State&, Vec3 air_velocity);
}
