#include "ofs/water.hpp"

#include "ofs/terrain.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <queue>
#include <utility>

namespace ofs {
namespace {

inline std::uint32_t mix32(std::uint32_t h) {
  h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15; h *= 0x846ca68bu; h ^= h >> 16;
  return h;
}

WaterMap build() {
  const int n = kWaterCells;
  const double cell = 2.0 * kWaterExtent / n;
  const auto northOf = [&](int row) { return kWaterExtent - (row + .5) * cell; };
  const auto eastOf = [&](int column) { return -kWaterExtent + (column + .5) * cell; };
  std::vector<float> height(std::size_t(n) * n);
  for (int row = 0; row < n; ++row)
    for (int column = 0; column < n; ++column)
      height[std::size_t(row) * n + column] = float(terrainElevation(northOf(row), eastOf(column)));
  // Priority flood (Barnes et al. 2014): raise every cell to the lowest level
  // from which water can still drain to the map edge. The airfields drain too,
  // otherwise the basin each sits in would be one large lake.
  std::vector<float> spill(height.size(), 0.f);
  std::vector<std::uint8_t> visited(height.size(), 0);
  using Entry = std::pair<float, int>;
  std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
  for (int row = 0; row < n; ++row) for (int column = 0; column < n; ++column) {
    const int index = row * n + column;
    const bool edge = row == 0 || column == 0 || row == n - 1 || column == n - 1;
    if (edge || airfieldDistance(northOf(row), eastOf(column)) < 3800.) {
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
  WaterMap map;
  map.level.assign(height.size(), float(kNoWater));
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
    // only reasonably deep basins hold one at all. Water stands in the low
    // country; a hollow high in the mountains is on ground too steep and too
    // coarse to hold a level surface.
    const float depth = rim - floor;
    if (depth < 30.f || floor > 520.f) continue;
    const float level = floor + std::clamp(depth * .5f, 12.f, 45.f);
    int wet = 0;
    for (const int index : members) wet += height[index] < level;
    const double area = wet * cell * cell * 1e-6;
    // Leave some basins dry so valleys are not uniformly flooded.
    if (area < .25 || float(mix32(std::uint32_t(start) * 2654435761u) & 0xffffffu) / float(0x1000000) > .78f) continue;
    for (const int index : members) map.level[index] = level;
    ++map.lakes;
    map.areaKm2 += area;
  }
  return map;
}

}  // namespace

const WaterMap& waterMap() {
  static const WaterMap map = build();
  return map;
}

double waterSurfaceElevation(double north, double east) {
  if (!(std::abs(north) < kWaterExtent) || !(std::abs(east) < kWaterExtent)) return kNoWater;
  const int column = std::clamp(int((east + kWaterExtent) / (2.0 * kWaterExtent) * kWaterCells), 0, kWaterCells - 1);
  const int row = std::clamp(int((kWaterExtent - north) / (2.0 * kWaterExtent) * kWaterCells), 0, kWaterCells - 1);
  return waterMap().level[std::size_t(row) * kWaterCells + column];
}

}  // namespace ofs
