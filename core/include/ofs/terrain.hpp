#pragma once
// One deterministic triangle surface for rendering, server and prediction.
// Heights and contact normals use NED; the origin airfield remains level.
#include "math.hpp"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace ofs {
inline constexpr int kTerrainRings = 448, kTerrainSegments = 768;
inline constexpr double kTerrainRadius = 64000.;

// The shape of the land is fractal noise, built from nothing but integer
// hashing and arithmetic so that every machine in a game computes the same
// ground.
namespace terrain_detail {
struct Noise {
  double value, dx, dy;  // value 0..1 and its gradient
};
inline double lattice(std::int32_t x, std::int32_t y) {
  std::uint32_t h = std::uint32_t(x) * 0x8da6b343u ^ std::uint32_t(y) * 0xd8163841u;
  h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
  return double(h) * (1. / 4294967296.);
}
// Smooth noise with its analytic gradient.
inline Noise noise(double x, double y) {
  const double floorX = std::floor(x), floorY = std::floor(y);
  const auto ix = std::int32_t(floorX), iy = std::int32_t(floorY);
  const double fx = x - floorX, fy = y - floorY;
  const double ux = fx * fx * fx * (fx * (fx * 6 - 15) + 10), uy = fy * fy * fy * (fy * (fy * 6 - 15) + 10);
  const double dux = 30 * fx * fx * (fx * (fx - 2) + 1), duy = 30 * fy * fy * (fy * (fy - 2) + 1);
  const double a = lattice(ix, iy), b = lattice(ix + 1, iy), c = lattice(ix, iy + 1), d = lattice(ix + 1, iy + 1);
  const double k1 = b - a, k2 = c - a, k4 = a - b - c + d;
  return {a + k1 * ux + k2 * uy + k4 * ux * uy, dux * (k1 + k4 * uy), duy * (k2 + k4 * ux)};
}
// Each octave is turned and doubled in frequency from the one before.
inline void nextOctave(double& x, double& y) {
  const double turned = 1.6 * x - 1.2 * y;
  y = 1.2 * x + 1.6 * y;
  x = turned;
}
// Rolling ground: plain fractal noise, about -1..1.
inline double rolling(double x, double y, int octaves) {
  double sum = 0, weight = .5;
  for (int i = 0; i < octaves; ++i) {
    sum += weight * (2 * noise(x, y).value - 1);
    weight *= .5;
    nextOctave(x, y);
  }
  return sum;
}
// Mountains: each octave counts for less where the ground is already steep,
// which leaves broad valley floors, sharp crests and spurs running down from
// them, as water would have cut them. About 0..1.
inline double eroded(double x, double y, int octaves) {
  double sum = 0, weight = .5, slopeX = 0, slopeY = 0;
  for (int i = 0; i < octaves; ++i) {
    const Noise n = noise(x, y);
    slopeX += n.dx; slopeY += n.dy;
    sum += weight * n.value / (1 + slopeX * slopeX + slopeY * slopeY);
    weight *= .5;
    nextOctave(x, y);
  }
  return sum;
}
// Crests: noise folded about its middle, so its ridges are sharp lines. Each
// octave is strongest along the crests of the one before. About 0..1.
inline double ridged(double x, double y, int octaves) {
  double sum = 0, weight = .5, carry = 1;
  for (int i = 0; i < octaves; ++i) {
    const double fold = 1 - std::abs(2 * noise(x, y).value - 1);
    const double crest = fold * fold * carry;
    sum += weight * crest;
    carry = std::min(1., crest * 2.2);
    weight *= .5;
    nextOctave(x, y);
  }
  return sum;
}
inline double smooth(double low, double high, double value) {
  const double t = std::clamp((value - low) / (high - low), 0., 1.);
  return t * t * (3 - 2 * t);
}
}  // namespace terrain_detail

// Metres above the airfield. The field stands on a level plain in a broad
// valley; ranges of mountains rise to either side of it and beyond.
inline double terrainElevation(double north, double east) {
  using namespace terrain_detail;
  const double radius = std::hypot(north, east);
  const double ramp = smooth(3500., 8500., radius);
  if (ramp <= 0) return 0;
  // Where the mountains stand: massifs twenty-odd kilometres across, kept
  // back from the airfield and from the valley that winds through it.
  const double valley = east - 1500. - 5200. * (2 * noise(north / 31000. + 40.5, 7.25).value - 1) -
                        1700. * (2 * noise(north / 9000. + 3.5, 21.75).value - 1);
  const double massif = noise(north / 23000. + 11.3, east / 23000. + 5.7).value +
                        .22 * smooth(9000., 30000., radius) - .5 * (1 - smooth(1500., 12000., std::abs(valley)));
  const double range = smooth(.42, .82, massif) * smooth(6000., 16000., radius);
  const double mountains = range * range * (3 - 2 * range);
  // Low country: rolling ground with hollows that hold lakes, and foothills
  // where a range begins.
  const double lowland = 50. + 85. * rolling(north / 5200. + 3.1, east / 5200. + 8.4, 5) -
                         90. * smooth(.60, .82, noise(north / 6100. + 71.3, east / 6100. + 13.9).value) +
                         210. * smooth(.3, .6, massif) * (.35 + noise(north / 2900. + 1.7, east / 2900. + 9.2).value);
  // The ranges are bent out of line with the noise they are made of, and
  // their crests are sharpened, so no summit is a plateau.
  const double mx = (.82 * north + .57 * east) / 8200., my = (.82 * east - .57 * north) / 8200.;
  const double bendX = noise(mx * .45 + 31.7, my * .45 + 2.9).value - .5, bendY = noise(mx * .45 + 8.1, my * .45 + 55.3).value - .5;
  const double mass = eroded(mx + 17.9 + .9 * bendX, my + 4.3 + .9 * bendY, 9);
  const double body = mass * std::sqrt(mass);
  const double crests = ridged(mx * 1.55 + 3.3 + bendY, my * 1.55 + 61.2 - bendX, 5);
  const double peaks = 120. + 4300. * body * (.45 + 1.15 * crests);
  return ramp * (lowland + mountains * peaks);
}

inline const Vec3& terrainVertex(int ring, int segment) {
  static const auto vertices = [] {
    std::vector<Vec3> data(std::size_t(kTerrainRings + 1) * kTerrainSegments);
    for (int r = 0; r <= kTerrainRings; ++r) {
      const double radius = kTerrainRadius * std::pow(double(r) / kTerrainRings, 2);
      for (int s = 0; s < kTerrainSegments; ++s) {
        const double angle = 2 * kPi * s / kTerrainSegments;
        const double north = -radius * std::sin(angle), east = radius * std::cos(angle);
        data[std::size_t(r) * kTerrainSegments + s] = {north, east, -terrainElevation(north, east)};
      }
    }
    return data;
  }();
  return vertices[std::size_t(std::clamp(ring, 0, kTerrainRings)) * kTerrainSegments +
                  std::size_t((segment % kTerrainSegments + kTerrainSegments) % kTerrainSegments)];
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
