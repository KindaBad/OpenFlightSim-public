#include "landscape.hpp"

#include "airfield.hpp"

#include "ofs/terrain.hpp"
#include "procedural.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
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

float slopeAt(double north, double east) {
  constexpr double d = 25;
  const double dn = (terrainElevation(north + d, east) - terrainElevation(north - d, east)) / (2 * d);
  const double de = (terrainElevation(north, east + d) - terrainElevation(north, east - d)) / (2 * d);
  return float(std::hypot(dn, de));
}

}  // namespace

bool insideAirfieldClearway(double north, double east) {
  // Nothing grows inside the fence, on the roads that lead to it, or under
  // either approach, where the trees would stand in the way of the lights.
  return airfieldUse(north, east) != AirfieldUse::Outside ||
         (std::abs(east) < 130 && std::abs(north) < 1900);
}

Landscape::Landscape(int landSize, int lakeSize) : landSize_(landSize), lakeSize_(lakeSize) {
  // ---- Lakes: fill closed basins part-way to their spill point -------------
  const int n = lakeSize_;
  const double cell = 2.0 * kExtent / n;
  const auto northOf = [&](int row) { return kExtent - (row + .5) * cell; };
  const auto eastOf = [&](int column) { return -kExtent + (column + .5) * cell; };
  std::vector<float> height(std::size_t(n) * n);
  procedural::parallelRows(n, [&](int row) {
    for (int column = 0; column < n; ++column)
      height[std::size_t(row) * n + column] = float(terrainElevation(northOf(row), eastOf(column)));
  });
  // Priority flood (Barnes et al. 2014): raise every cell to the lowest level
  // from which water can still drain to the map edge. The airfield drains too,
  // otherwise the basin it sits in would be one large lake.
  std::vector<float> spill(height.size(), 0.f);
  std::vector<std::uint8_t> visited(height.size(), 0);
  using Entry = std::pair<float, int>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
  for (int row = 0; row < n; ++row) for (int column = 0; column < n; ++column) {
    const int index = row * n + column;
    const bool edge = row == 0 || column == 0 || row == n - 1 || column == n - 1;
    if (edge || std::hypot(northOf(row), eastOf(column)) < 3800.) {
      spill[index] = height[index];
      visited[index] = 1;
      open.emplace(height[index], index);
    }
  }
  constexpr int kNeighbours[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  while (!open.empty()) {
    const auto [level, index] = open.top();
    open.pop();
    const int row = index / n, column = index % n;
    for (const auto& offset : kNeighbours) {
      const int r = row + offset[0], c = column + offset[1];
      if (r < 0 || c < 0 || r >= n || c >= n || visited[r * n + c]) continue;
      visited[r * n + c] = 1;
      spill[r * n + c] = std::max(height[r * n + c], level);
      open.emplace(spill[r * n + c], r * n + c);
    }
  }
  lake_.assign(height.size(), kNoLake);
  std::vector<int> component(height.size(), -1), stack, members;
  for (int start = 0; start < n * n; ++start) {
    if (component[start] >= 0 || spill[start] - height[start] < 1.f) continue;
    members.clear();
    stack.assign(1, start);
    component[start] = start;
    float floor = height[start], rim = spill[start];
    while (!stack.empty()) {
      const int index = stack.back();
      stack.pop_back();
      members.push_back(index);
      floor = std::min(floor, height[index]);
      rim = std::max(rim, spill[index]);
      const int row = index / n, column = index % n;
      for (const auto& offset : kNeighbours) {
        const int r = row + offset[0], c = column + offset[1];
        if (r < 0 || c < 0 || r >= n || c >= n) continue;
        const int next = r * n + c;
        if (component[next] >= 0 || spill[next] - height[next] < 1.f) continue;
        component[next] = start;
        stack.push_back(next);
      }
    }
    // Real basins leak and evaporate: the lake stands well below the rim, and
    // only reasonably deep basins hold one at all.
    const float depth = rim - floor;
    if (depth < 45.f) continue;
    const float level = floor + std::clamp(depth * .30f, 10.f, 42.f);
    int wet = 0;
    for (const int index : members) wet += height[index] < level;
    const double area = wet * cell * cell * 1e-6;
    // Leave some basins dry so valleys are not uniformly flooded.
    if (area < .25 || unit(mix32(std::uint32_t(start) * 2654435761u)) > .78f) continue;
    for (const int index : members) lake_[index] = level;
    ++lakeCount_;
    lakeAreaKm2_ += area;
  }

  // ---- Land cover ----------------------------------------------------------
  const int m = landSize_;
  const double landCell = 2.0 * kExtent / m;
  land_.resize(std::size_t(m) * m * 4);
  procedural::parallelRows(m, [&](int row) {
    const double north = kExtent - (row + .5) * landCell;
    for (int column = 0; column < m; ++column) {
      const double east = -kExtent + (column + .5) * landCell;
      const float altitude = float(terrainElevation(north, east));
      const float slope = slopeAt(north, east);
      const float radius = float(std::hypot(north, east));
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

void Landscape::treesInChunk(int chunkEast, int chunkSouth, int density, std::vector<TreeInstance>& out) const {
  const std::uint32_t base = mix32(std::uint32_t(chunkEast) * 0x9e3779b1u ^ std::uint32_t(chunkSouth) * 0x85ebca77u ^ 0x5bd1e995u);
  for (int i = 0; i < density; ++i) {
    const std::uint32_t h = mix32(base + std::uint32_t(i) * 0x27d4eb2fu);
    const std::uint32_t h2 = mix32(h ^ 0x165667b1u), h3 = mix32(h2 ^ 0xc2b2ae3du);
    const double east = (chunkEast + unit(h)) * double(kTreeChunk);
    const double south = (chunkSouth + unit(h2)) * double(kTreeChunk);
    const double north = -south;
    const float forest = forestDensity(north, east);
    // Dense inside woodland, with the occasional solitary tree in open country.
    const float chance = forest * forest + .012f * (1 - farmland(north, east));
    if (unit(h3) > chance) continue;
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
    tree.conifer = unit(h4) < .12f + .78f * smooth(260.f, 1050.f, altitude) ? 1.f : 0.f;
    const float vigour = 1 - .45f * smooth(1100.f, 1700.f, altitude);
    tree.height = (tree.conifer > .5f ? 13.f + 13.f * unit(h5) : 10.f + 11.f * unit(h5)) * vigour;
    tree.spread = .82f + .36f * unit(mix32(h5 ^ 0x1b873593u));
    tree.yaw = unit(mix32(h5 ^ 0xcc9e2d51u)) * 6.2831853f;
    tree.tint = unit(mix32(h4 ^ 0xe6546b64u));
    out.push_back(tree);
  }
}

}  // namespace ofs::client
