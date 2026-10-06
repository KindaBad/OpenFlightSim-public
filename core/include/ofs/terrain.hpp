#pragma once
// One deterministic triangle surface for rendering, server and prediction.
// Heights and contact normals use NED; the origin airfield remains level.
#include "math.hpp"
#include <algorithm>
#include <array>
#include <utility>

namespace ofs {
inline constexpr int kTerrainRings = 96, kTerrainSegments = 256;
inline constexpr double kTerrainRadius = 32000.;

inline double terrainElevation(double north, double east) {
  const double radius = std::hypot(north, east);
  const double t = std::clamp((radius - 3500.) / 5000., 0., 1.);
  const double ramp = t * t * (3. - 2. * t);
  const double south = -north;
  const double mountainT=std::clamp((radius-11000.)/9000.,0.,1.);
  const double mountainRamp=mountainT*mountainT*(3.-2.*mountainT);
  const double ridge=1.-std::abs(std::sin(east*.00031+std::sin(south*.00022)*1.4));
  const double peaks=mountainRamp*(500.+1450.*ridge*ridge);
  return peaks + ramp * (380. + 240. * std::sin(east * .0008) * std::cos(south * .00065)
      + 140. * std::sin(south * .0013 + east * .0004)
      + 65. * std::sin(east * .0027 + south * .0011) * std::cos(south * .0023));
}

inline const Vec3& terrainVertex(int ring, int segment) {
  static const auto vertices = [] {
    std::array<Vec3, (kTerrainRings + 1) * kTerrainSegments> data{};
    for (int r = 0; r <= kTerrainRings; ++r) {
      const double radius = kTerrainRadius * std::pow(double(r) / kTerrainRings, 2);
      for (int s = 0; s < kTerrainSegments; ++s) {
        const double angle = 2 * kPi * s / kTerrainSegments;
        const double north = -radius * std::sin(angle), east = radius * std::cos(angle);
        data[r * kTerrainSegments + s] = {north, east, -terrainElevation(north, east)};
      }
    }
    return data;
  }();
  return vertices[std::clamp(ring, 0, kTerrainRings) * kTerrainSegments +
                  (segment % kTerrainSegments + kTerrainSegments) % kTerrainSegments];
}

struct TerrainSample {
  double heightNed{};
  Vec3 normalNed{0, 0, -1}; // outward, unit length
};

inline TerrainSample sampleTerrain(double north, double east) {
  if (!std::isfinite(north) || !std::isfinite(east)) return {};
  const double radius = std::hypot(north, east);
  if (radius < 3300.) return {}; // wholly inside the flat airfield triangles
  double angle = std::atan2(-north, east);
  if (angle < 0) angle += 2 * kPi;
  const double sector = angle * kTerrainSegments / (2 * kPi);
  const int segment = std::min(kTerrainSegments - 1, int(sector));
  // Ring boundaries are polygon chords, not circles. Account for the chord
  // before selecting the cell, including at the azimuth seam.
  const double half = kPi / kTerrainSegments;
  const double polygonRadius = radius * std::cos((sector - segment - .5) * 2 * half) / std::cos(half);
  if (polygonRadius >= kTerrainRadius) {
    const double h = terrainElevation(north, east);
    return {-h, Vec3{(terrainElevation(north + 1, east) - terrainElevation(north - 1, east)) * .5,
                     (terrainElevation(north, east + 1) - terrainElevation(north, east - 1)) * .5, -1}.normalized()};
  }
  const int ring = std::clamp(int(std::sqrt(polygonRadius / kTerrainRadius) * kTerrainRings), 0, kTerrainRings - 1);
  const auto& a = terrainVertex(ring, segment);
  const auto& b = terrainVertex(ring, segment + 1);
  const auto& c = terrainVertex(ring + 1, segment + 1);
  const auto& d = terrainVertex(ring + 1, segment);
  const auto triangle = [&](const Vec3& v0, const Vec3& v1, const Vec3& v2) {
    const Vec3 edge1 = v1 - v0, edge2 = v2 - v0;
    const double det = edge1.x * edge2.y - edge1.y * edge2.x;
    const double x = north - v0.x, y = east - v0.y;
    const double u = (x * edge2.y - y * edge2.x) / det;
    const double v = (edge1.x * y - edge1.y * x) / det;
    Vec3 n = edge1.cross(edge2).normalized();
    if (n.z > 0) n = -n;
    return std::pair{TerrainSample{v0.z + u * edge1.z + v * edge2.z, n}, u >= -1e-9 && v >= -1e-9 && u + v <= 1 + 1e-9};
  };
  const auto first = triangle(a, b, c);
  return first.second ? first.first : triangle(a, c, d).first;
}

inline double groundHeightNed(double north, double east) {
  return sampleTerrain(north, east).heightNed;
}
} // namespace ofs
