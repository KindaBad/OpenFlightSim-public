#include "landscape.hpp"

#include "ofs/bases.hpp"

#include "airfield.hpp"

#include "ofs/terrain.hpp"
#include "ofs/water.hpp"
#include "procedural.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace ofs::client {
namespace {

inline float saturate(float v) { return std::clamp(v, 0.f, 1.f); }
inline float smooth(float a, float b, float x) {
  const float t = saturate((x - a) / (b - a));
  return t * t * (3 - 2 * t);
}
inline std::uint32_t mix32(std::uint32_t h) {
  h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
  return h;
}
inline float unit(std::uint32_t h) { return float(h & 0xffffffu) / float(0x1000000); }

// Non-repeating fractal noise over the map: the lattice period is far larger
// than the map, so the tiling of the primitive never shows.
float field(double north, double east, double wavelength, int octaves, std::uint32_t seed) {
  constexpr int kPeriod = 4096;
  return procedural::perlinFbm(float(east / wavelength) + 1000.f, float(-north / wavelength) + 1000.f, .5f,
                               kPeriod, octaves, seed);
}

}  // namespace

bool insideAirfieldClearway(double north, double east) {
  // Nothing grows inside the fence, on the roads that lead to it, or under
  // either approach, where the trees would stand in the way of the lights.
  if (airfieldUse(north, east) != AirfieldUse::Outside) return true;
  for (const auto& site : kAirfieldSites)
    if (std::abs(east - site.east) < 130 && std::abs(north - site.north) < 1900) return true;
  // Nor on the ground the depot beside a team airfield stands on.
  return airfieldOwner(north, east) != Team::None && std::abs(east - teamAirfield(airfieldOwner(north, east)).east - 900) < 420 &&
         std::abs(north - teamAirfield(airfieldOwner(north, east)).north) < 480;
}

Landscape::Landscape(int landSize, int heightSize)
    : landSize_(landSize), lakeSize_(kWaterCells), heightSize_(heightSize) {
  // ---- The shape of the ground ---------------------------------------------
  height_.resize(std::size_t(heightSize_) * heightSize_);
  procedural::parallelRows(heightSize_, [&](int row) {
    const double step = 2.0 * kExtent / heightSize_, north = kExtent - (row + .5) * step;
    for (int column = 0; column < heightSize_; ++column) {
      const double metres = terrainElevation(north, -kExtent + (column + .5) * step);
      height_[std::size_t(row) * heightSize_ + column] =
          std::uint16_t(std::clamp((metres - kHeightBase) / kHeightRange, 0., 1.) * 65535. + .5);
    }
  });
  // ---- Lakes: the simulation's own, so the picture is where the water is ----
  const WaterMap& water = waterMap();
  lake_ = water.level;
  lakeCount_ = water.lakes;
  lakeAreaKm2_ = water.areaKm2;

  // ---- Land cover ----------------------------------------------------------
  const int m = landSize_;
  const double landCell = 2.0 * kExtent / m;
  land_.resize(std::size_t(m) * m * 4);
  procedural::parallelRows(m, [&](int row) {
    const double north = kExtent - (row + .5) * landCell;
    for (int column = 0; column < m; ++column) {
      const double east = -kExtent + (column + .5) * landCell;
      const float altitude = elevation(north, east);
      constexpr double d = 60;
      const float slope = std::hypot(elevation(north + d, east) - elevation(north - d, east),
                                     elevation(north, east + d) - elevation(north, east - d)) / float(2 * d);
      const float radius = float(airfieldDistance(north, east));
      const float moisture = field(north, east, 2600, 4, 501) * .5f + .5f;
      // Farmland: the flat lowland around the field, away from the runway.
      const float farm = smooth(1900.f, 3400.f, radius) * (1 - smooth(70.f, 210.f, altitude)) *
                         (1 - smooth(.035f, .11f, slope)) * (1 - smooth(.58f, .78f, moisture));
      // Woodland: groves on a ~1 km scale, denser on the hills, thinning out at
      // the treeline and on cliffs.
      const float groves = field(north, east, 950, 4, 502) * .5f + .5f + smooth(120.f, 520.f, altitude) * .10f +
                           (moisture - .5f) * .16f;
      float forest = smooth(.47f, .60f, groves);
      forest *= 1 - smooth(1380.f, 1720.f, altitude + field(north, east, 700, 2, 503) * 110.f);
      forest *= 1 - smooth(.62f, .95f, slope);
      forest *= 1 - farm * .92f;
      forest *= smooth(450.f, 1000.f, radius);
      const float level = lakeSurface(north, east);
      if (level > kNoLake + 1 && altitude < level + 1.5f) forest = 0;
      const float rock = field(north, east, 420, 4, 504) * .5f + .5f;
      std::uint8_t* texel = &land_[(std::size_t(row) * m + column) * 4];
      texel[0] = std::uint8_t(saturate(forest) * 255.f + .5f);
      texel[1] = std::uint8_t(saturate(farm) * 255.f + .5f);
      texel[2] = std::uint8_t(saturate(moisture) * 255.f + .5f);
      texel[3] = std::uint8_t(saturate(rock) * 255.f + .5f);
    }
  });
}

float Landscape::land(double north, double east, int channel) const {
  const double u = std::clamp((east + kExtent) / (2.0 * kExtent), 0.0, 1.0) * landSize_ - .5;
  const double v = std::clamp((kExtent - north) / (2.0 * kExtent), 0.0, 1.0) * landSize_ - .5;
  const int x0 = std::clamp(int(std::floor(u)), 0, landSize_ - 2), y0 = std::clamp(int(std::floor(v)), 0, landSize_ - 2);
  const float fx = float(std::clamp(u - x0, 0.0, 1.0)), fy = float(std::clamp(v - y0, 0.0, 1.0));
  const auto at = [&](int x, int y) { return float(land_[(std::size_t(y) * landSize_ + x) * 4 + channel]) / 255.f; };
  return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
}

float Landscape::elevation(double north, double east) const {
  const int size = heightSize_;
  const double u = std::clamp((east + kExtent) / (2.0 * kExtent), 0.0, 1.0) * size - .5;
  const double v = std::clamp((kExtent - north) / (2.0 * kExtent), 0.0, 1.0) * size - .5;
  const int x0 = std::clamp(int(std::floor(u)), 0, size - 2), y0 = std::clamp(int(std::floor(v)), 0, size - 2);
  const float fx = float(std::clamp(u - x0, 0.0, 1.0)), fy = float(std::clamp(v - y0, 0.0, 1.0));
  const auto at = [&](int x, int y) { return float(height_[std::size_t(y) * size + x]); };
  const float texel = (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) + (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
  return kHeightBase + texel * (kHeightRange / 65535.f);
}

float Landscape::forestDensity(double north, double east) const { return land(north, east, 0); }
float Landscape::farmland(double north, double east) const { return land(north, east, 1); }

float Landscape::lakeSurface(double north, double east) const {
  if (std::abs(north) >= kExtent || std::abs(east) >= kExtent) return kNoLake;
  const int column = std::clamp(int((east + kExtent) / (2.0 * kExtent) * lakeSize_), 0, lakeSize_ - 1);
  const int row = std::clamp(int((kExtent - north) / (2.0 * kExtent) * lakeSize_), 0, lakeSize_ - 1);
  return lake_[std::size_t(row) * lakeSize_ + column];
}

bool Landscape::underWater(double north, double east) const {
  const float level = lakeSurface(north, east);
  return level > kNoLake + 1 && terrainElevation(north, east) < level;
}

namespace {
// These mirror terrain_fs.glsl (cellHash, fieldEdge, fieldWander) in single
// precision, operation for operation.
constexpr float kFieldSize = 430.f, kFieldWander = 55.f;
struct Vec2f { float x, y; };
inline float fractf(float v) { return v - std::floor(v); }
float cellHash(Vec2f p) {
  float x = fractf(p.x * .1031f), y = fractf(p.y * .1031f), z = fractf(p.x * .1031f);
  const float d = x * (y + 33.33f) + y * (z + 33.33f) + z * (x + 33.33f);
  x += d; y += d; z += d;
  return fractf((x + y) * z);
}
Vec2f fieldWander(Vec2f p) {
  return {p.x + kFieldWander * (std::sin(p.y / 340.f) + .5f * std::sin(p.x / 190.f + 1.7f) + .3f * std::sin(p.y / 95.f + 4.1f)),
          p.y + kFieldWander * (std::sin(p.x / 410.f + .6f) + .5f * std::sin(p.y / 230.f + 2.1f) + .3f * std::sin(p.x / 83.f + .9f))};
}
}  // namespace

Landscape::Field Landscape::fieldPattern(double north, double east) const {
  // The shader works in (east, south).
  const Vec2f wandered = fieldWander({float(east), float(-north)});
  const Vec2f q{wandered.x / kFieldSize, wandered.y / kFieldSize};
  const Vec2f home{std::floor(q.x), std::floor(q.y)};
  float nearest = 1e9f, second = 1e9f;
  Vec2f own = home, neighbour = home, ownSite = home, otherSite{0, 0};
  for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i) {
      const Vec2f cell{home.x + float(i), home.y + float(j)};
      const Vec2f site{cell.x + .15f + .7f * cellHash({cell.x + 5.3f, cell.y + 5.3f}),
                       cell.y + .15f + .7f * cellHash({cell.x + 91.7f, cell.y + 91.7f})};
      const float away = std::hypot(q.x - site.x, q.y - site.y);
      if (away < nearest) {
        second = nearest; neighbour = own; otherSite = ownSite;
        nearest = away; own = cell; ownSite = site;
      } else if (away < second) {
        second = away; neighbour = cell; otherSite = site;
      }
    }
  Vec2f between{otherSite.x - ownSite.x, otherSite.y - ownSite.y};
  const float length = std::max(std::hypot(between.x, between.y), 1e-4f);
  between = {between.x / length, between.y / length};
  const float gap = ((ownSite.x + otherSite.x) * .5f - q.x) * between.x + ((ownSite.y + otherSite.y) * .5f - q.y) * between.y;
  Field field;
  field.edge = gap * kFieldSize;
  const float boundary = cellHash({own.x + neighbour.x + 40.f, own.y + neighbour.y + 40.f});
  field.hedged = boundary >= .30f;
  field.reach = boundary < .75f ? 4.f : 11.f;
  field.wooded = cellHash({own.x + 61.f, own.y + 61.f}) <= .085f;
  field.farmed = farmland(double(-ownSite.y * kFieldSize), double(ownSite.x * kFieldSize)) >= .42f;
  // The nearest point of the edge, taken back out of the wandering: a few
  // rounds of fixed-point settle it to well under a metre.
  const Vec2f target{(q.x + between.x * gap) * kFieldSize, (q.y + between.y * gap) * kFieldSize};
  Vec2f point = target;
  for (int round = 0; round < 10; ++round) {
    const Vec2f moved = fieldWander(point);
    point = {point.x + target.x - moved.x, point.y + target.y - moved.y};
  }
  field.hedgeEast = point.x;
  field.hedgeNorth = -point.y;
  return field;
}

void Landscape::treesInChunk(int chunkEast, int chunkSouth, int density, std::vector<TreeInstance>& out) const {
  const std::uint32_t base = mix32(std::uint32_t(chunkEast) * 0x9e3779b1u ^ std::uint32_t(chunkSouth) * 0x85ebca77u ^ 0x5bd1e995u);
  for (int i = 0; i < density; ++i) {
    const std::uint32_t h = mix32(base + std::uint32_t(i) * 0x27d4eb2fu);
    const std::uint32_t h2 = mix32(h ^ 0x165667b1u), h3 = mix32(h2 ^ 0xc2b2ae3du);
    double east = (chunkEast + unit(h)) * double(kTreeChunk);
    double south = (chunkSouth + unit(h2)) * double(kTreeChunk);
    double north = -south;
    const float forest = forestDensity(north, east);
    // Dense inside woodland, with the occasional solitary tree in open country.
    const float farm = farmland(north, east);
    float chance = forest * forest + .012f * (1 - farm);
    bool hedgerow = false;
    Field field;
    if (farm >= .3f) {
      // A field that was never cleared is a wood, a few metres in from its edge.
      field = fieldPattern(north, east);
      if (field.farmed && field.wooded && field.edge > 3) chance = std::max(chance, .8f);
    }
    if (unit(h3) > chance) {
      // On farmland the trees that are not in a wood stand in its hedgerows
      // and belts: the candidate is moved onto the nearest, if its field has one.
      if (farm < .3f || unit(mix32(h3 ^ 0x68bc21ebu)) > (field.reach > 5 ? 1.f : .8f)) continue;
      if (!field.farmed || !field.hedged || field.edge > 160) continue;
      const double offset = (unit(mix32(h3 ^ 0x02e5be93u)) - .5) * (field.reach > 5 ? 17 : 5);
      const double length = std::max(1e-6, std::hypot(field.hedgeNorth - north, field.hedgeEast - east));
      const double towardNorth = (field.hedgeNorth - north) / length, towardEast = (field.hedgeEast - east) / length;
      north = field.hedgeNorth + towardNorth * offset;
      east = field.hedgeEast + towardEast * offset;
      south = -north;
      // Moved out of the chunk, or onto a corner where three fields meet and
      // the line is another hedge's or none.
      if (east < chunkEast * double(kTreeChunk) || east >= (chunkEast + 1) * double(kTreeChunk) ||
          south < chunkSouth * double(kTreeChunk) || south >= (chunkSouth + 1) * double(kTreeChunk))
        continue;
      const Field there = fieldPattern(north, east);
      if (!there.hedged || std::abs(there.edge) > there.reach + .5f) continue;
      hedgerow = true;
    }
    if (insideAirfieldClearway(north, east)) continue;
    const TerrainSample ground = sampleTerrain(north, east);
    const float altitude = float(-ground.heightNed);
    if (altitude > 1750.f || -ground.normalNed.z < .72) continue;
    // Water is drawn wherever the analytic bed lies below the lake level, and
    // the mesh the tree stands on can differ from that by a metre or two.
    const float level = lakeSurface(north, east);
    if (level > kNoLake + 1 && std::min(altitude, float(terrainElevation(north, east))) < level + 1.5f) continue;
    const std::uint32_t h4 = mix32(h3 ^ 0x2545f491u), h5 = mix32(h4 ^ 0x9e3779b9u);
    TreeInstance tree;
    tree.east = float(east);
    tree.up = altitude;
    tree.south = float(south);
    // Conifers take over with altitude; stunted growth near the treeline.
    tree.conifer = !hedgerow && unit(h4) < .12f + .78f * smooth(260.f, 1050.f, altitude) ? 1.f : 0.f;
    const float vigour = 1 - .45f * smooth(1100.f, 1700.f, altitude);
    tree.height = (tree.conifer > .5f ? 13.f + 13.f * unit(h5) : 10.f + 11.f * unit(h5)) * vigour;
    tree.spread = .82f + .36f * unit(mix32(h5 ^ 0x1b873593u));
    tree.yaw = unit(mix32(h5 ^ 0xcc9e2d51u)) * 6.2831853f;
    tree.tint = unit(mix32(h4 ^ 0xe6546b64u));
    out.push_back(tree);
  }
}

}  // namespace ofs::client
