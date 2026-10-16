#include "earth_map.hpp"

#include "ofs/water.hpp"

#include <algorithm>
#include <cmath>

namespace ofs::client {
namespace {

constexpr double kQuarterMeridian = 10001966.;

// Heights and water from one level of the pack, bilinear, without the fine
// relief the simulation adds: enough for a map, and cheap.
struct Sampler {
  const EarthTerrain& terrain;
  int level;
  float sample(const Vec3& up, float& water) const {
    int face;
    double s, t;
    earthFaceOf(up, face, s, t);
    const int tiles = terrain.roots() << level, side = terrain.tileSamples() - 1;
    const double fx = std::clamp(s, 0., 1.) * tiles, fy = std::clamp(t, 0., 1.) * tiles;
    const int tx = std::clamp(int(fx), 0, tiles - 1), ty = std::clamp(int(fy), 0, tiles - 1);
    const auto tile = terrain.tile(face, level, tx, ty);
    const double i = (fx - tx) * side, j = (fy - ty) * side;
    const int i0 = std::min(int(i), side - 1), j0 = std::min(int(j), side - 1);
    const double di = i - i0, dj = j - j0;
    water = -10000.f;
    if (tile->kind == 1) water = 0;
    if (!tile->water.empty()) {
      const float w = tile->water[std::size_t(j0) * (side + 1) + std::size_t(i0)];
      if (w > kNoWater + 1) water = w;
    }
    if (tile->heights.empty()) return -50.f;
    const auto at = [&](int a, int b) { return double(tile->heights[std::size_t(j0 + b) * (side + 1) + std::size_t(i0 + a)]); };
    return float((at(0, 0) * (1 - di) + at(1, 0) * di) * (1 - dj) + (at(0, 1) * (1 - di) + at(1, 1) * di) * dj);
  }
};

}  // namespace

EarthMapImage buildEarthMap(const EarthTerrain& terrain, const EarthCover& cover, const EarthMapView& view, int size) {
  EarthMapImage image;
  image.view = view;
  image.size = size;
  image.rgba.assign(std::size_t(size) * size * 4, 0);
  const double cell = 2 * view.halfSpan / size;
  // The finest level of the pack whose spacing is no finer than about half a pixel.
  int level = 0;
  while (level + 1 < terrain.levels() &&
         kQuarterMeridian / (terrain.roots() << (level + 1)) / (terrain.tileSamples() - 1) > cell * .5)
    ++level;
  const Sampler sampler{terrain, level};
  // Heights for every pixel and a ring round them, for the relief.
  const int ring = size + 2;
  std::vector<float> height(std::size_t(ring) * ring), water(std::size_t(ring) * ring);
  std::vector<std::uint8_t> valid(std::size_t(ring) * ring);
  std::vector<double> latitudes(std::size_t(ring) * ring), longitudes(std::size_t(ring) * ring);
  for (int row = -1; row <= size; ++row)
    for (int column = -1; column <= size; ++column) {
      const std::size_t k = std::size_t(row + 1) * ring + std::size_t(column + 1);
      double lat, lon;
      view.place((column + .5) * cell - view.halfSpan, view.halfSpan - (row + .5) * cell, lat, lon);
      if (std::abs(lat) > kPi / 2) continue;
      valid[k] = 1;
      latitudes[k] = lat;
      longitudes[k] = lon;
      const Vec3 up{std::cos(lat) * std::cos(lon), std::cos(lat) * std::sin(lon), std::sin(lat)};
      height[k] = sampler.sample(up, water[k]);
    }
  for (int row = 0; row < size; ++row)
    for (int column = 0; column < size; ++column) {
      const std::size_t k = std::size_t(row + 1) * ring + std::size_t(column + 1);
      std::uint8_t* texel = &image.rgba[(std::size_t(row) * size + column) * 4];
      if (!valid[k]) {
        texel[3] = 255;
        continue;
      }
      float r, g, b;
      if (water[k] > height[k] + .5f) {
        // Deeper sea darker, as on a printed map.
        const float depth = std::clamp((water[k] - height[k]) / 400.f, 0.f, 1.f);
        r = .30f - .08f * depth, g = .47f - .10f * depth, b = .62f - .08f * depth;
      } else {
        const std::uint32_t c = cover.colour(latitudes[k], longitudes[k]);
        r = float(c & 0xff) / 255.f, g = float(c >> 8 & 0xff) / 255.f, b = float(c >> 16 & 0xff) / 255.f;
        // Natural Earth's tints are pale for a map read at a glance: a little
        // deeper and more saturated.
        const float grey = (r + g + b) / 3;
        r = (grey + (r - grey) * 1.35f) * .86f, g = (grey + (g - grey) * 1.35f) * .86f, b = (grey + (b - grey) * 1.35f) * .86f;
        // High ground pales toward the snow line.
        const float high = std::clamp((height[k] - 1500.f) / 2500.f, 0.f, 1.f) * .45f;
        r += (.92f - r) * high, g += (.93f - g) * high, b += (.94f - b) * high;
        // Relief lit from the north-west.
        const auto at = [&](int dr, int dc) {
          const std::size_t n = std::size_t(row + 1 + dr) * ring + std::size_t(column + 1 + dc);
          return valid[n] ? height[n] : height[k];
        };
        const double slopeNorth = (at(-1, 0) - at(1, 0)) / (2 * cell), slopeEast = (at(0, 1) - at(0, -1)) / (2 * cell);
        const float shade = std::clamp(1.f + float(slopeNorth - slopeEast) * 2.2f * float(std::sqrt(cell / 100)), .62f, 1.3f);
        r *= shade, g *= shade, b *= shade;
      }
      texel[0] = std::uint8_t(std::clamp(r, 0.f, 1.f) * 255.f + .5f);
      texel[1] = std::uint8_t(std::clamp(g, 0.f, 1.f) * 255.f + .5f);
      texel[2] = std::uint8_t(std::clamp(b, 0.f, 1.f) * 255.f + .5f);
      texel[3] = 255;
    }
  return image;
}

}  // namespace ofs::client
