#pragma once
#include "ofs/aircraft_definition.hpp"
#include "ofs/simulator.hpp"
#include <vector>
namespace ofs {
struct GeometryDebugLine { Vec3 start{},end{}; std::uint32_t color{}; };
// World NED diagnostic geometry. Does not require assets and never mutates state.
std::vector<GeometryDebugLine> geometryDebugLines(const Simulator&,AircraftType);
}
