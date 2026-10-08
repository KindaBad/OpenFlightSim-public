#pragma once
// Standing water on the shared terrain: where it lies and how high it stands.
//
// A lake fills a closed hollow in the low country part-way to its rim. The
// levels are worked out once from the terrain function alone, so the server,
// every client's prediction and the picture all put the same water in the same
// place.
#include <vector>

namespace ofs {
inline constexpr double kNoWater = -10000.;
// Lakes are mapped over a square of this half-size centred on the airfield.
inline constexpr double kWaterExtent = 48000.;
inline constexpr int kWaterCells = 512;

// Metres above the airfield of the water surface over a point, or kNoWater
// where the ground is dry.
double waterSurfaceElevation(double north, double east);

struct WaterMap {
  // Row 0 is the northern edge, column 0 the western; kNoWater where dry.
  std::vector<float> level;
  int lakes{};
  double areaKm2{};
};
const WaterMap& waterMap();
}  // namespace ofs
