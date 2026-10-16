#pragma once
// The round Earth's navigation map (docs/WHOLE_EARTH_PLAN.md, phase 5).
//
// A north-up picture of the ground about a point, painted from the land cover
// with the sea, lakes and relief of the terrain pack, at whatever scale is
// asked for: a few kilometres for the minimap, up to most of a hemisphere for
// the full map. It is a local plate carrée: north is up and a degree of
// longitude is shortened by the cosine of the centre's latitude, so it is
// true near the centre and stretched far from it. Pictures are painted on a
// background thread; this file holds the painting and the projection and has
// no renderer dependency.

#include "earth_scenery.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ofs::client {

inline constexpr double kEarthMapRadius = 6371000.;

struct EarthMapView {
  double latitude{}, longitude{};  // radians, of the centre
  double halfSpan{10000};          // metres from the centre to an edge

  // Metres east and north of the centre, and back.
  void offsets(double lat, double lon, double& east, double& north) const {
    double dlon = std::remainder(lon - longitude, 2 * kPi);
    east = kEarthMapRadius * std::cos(latitude) * dlon;
    north = kEarthMapRadius * (lat - latitude);
  }
  void place(double east, double north, double& lat, double& lon) const {
    lat = latitude + north / kEarthMapRadius;
    lon = longitude + east / (kEarthMapRadius * std::max(std::cos(latitude), 1e-3));
  }
};

struct EarthMapImage {
  EarthMapView view;
  int size{};
  std::vector<std::uint8_t> rgba;  // row 0 north, column 0 west
};

EarthMapImage buildEarthMap(const EarthTerrain& terrain, const EarthCover& cover, const EarthMapView& view, int size);

// The full map's scales, in metres from the centre to an edge.
inline constexpr double kEarthMapScales[] = {12500, 25000, 50000, 100000, 200000, 400000, 800000, 1600000, 3200000, 6400000};
inline constexpr int kEarthMapScaleCount = int(sizeof(kEarthMapScales) / sizeof(kEarthMapScales[0]));

}  // namespace ofs::client
