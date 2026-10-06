#pragma once
// ISA atmosphere to ~20 km (troposphere + lower stratosphere).
// Inputs/outputs SI. Altitude input is geometric MSL in metres (up-positive).

#include "ofs/math.hpp"

namespace ofs {

struct AirData {
  double rho{1.225};       // kg/m^3
  double pressure{101325};  // Pa
  double temp{288.15};      // K
  double sound{340.294};    // m/s
};

AirData isaAtAltitude(double alt_msl_m, double temp_offset_c = 0.0);

}  // namespace ofs
