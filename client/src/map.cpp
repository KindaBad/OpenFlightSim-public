#include "map.hpp"

#include "ofs/terrain.hpp"

#include <array>
#include <cmath>

namespace ofs::client {
namespace {

struct Colour { float r, g, b; };
Colour mix(const Colour& a, const Colour& b, float t) {
  t = std::clamp(t, 0.f, 1.f);
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

// Elevation tints, metres above the airfield.
Colour elevationTint(float height) {
  struct Stop { float height; Colour colour; };
  static constexpr std::array<Stop, 6> stops{{{0, {.42f, .50f, .33f}},
                                              {350, {.50f, .55f, .36f}},
                                              {800, {.62f, .59f, .42f}},
                                              {1300, {.60f, .52f, .41f}},
                                              {1800, {.66f, .64f, .61f}},
                                              {2300, {.92f, .93f, .94f}}}};
  for (std::size_t i = 1; i < stops.size(); ++i)
    if (height < stops[i].height)
      return mix(stops[i - 1].colour, stops[i].colour,
                 (height - stops[i - 1].height) / (stops[i].height - stops[i - 1].height));
  return stops.back().colour;
}

}  // namespace

MapImage buildMapImage(const Landscape& landscape, int size) {
  MapImage image;
  image.size = size;
  image.rgba.resize(std::size_t(size) * size * 4);
  const double extent = Landscape::kExtent, cell = 2 * extent / size;
  for (int row = 0; row < size; ++row) {
    for (int column = 0; column < size; ++column) {
      const double north = extent - (row + .5) * cell, east = (column + .5) * cell - extent;
      const float height = float(terrainElevation(north, east));
      Colour colour = elevationTint(height);
      colour = mix(colour, {.25f, .38f, .23f}, .75f * landscape.forestDensity(north, east));
      colour = mix(colour, {.62f, .64f, .40f}, .55f * landscape.farmland(north, east));
      // Relief: lit from the north-west, as printed maps are.
      const double slopeNorth = (terrainElevation(north + cell, east) - terrainElevation(north - cell, east)) / (2 * cell);
      const double slopeEast = (terrainElevation(north, east + cell) - terrainElevation(north, east - cell)) / (2 * cell);
      const float shade = std::clamp(1.f + .45f * float(slopeNorth - slopeEast), .74f, 1.2f);
      colour = {colour.r * shade, colour.g * shade, colour.b * shade};
      if (landscape.underWater(north, east)) colour = {.24f, .42f, .58f};
      if (insideAirfieldClearway(north, east)) colour = mix(colour, {.50f, .50f, .50f}, .6f);
      std::uint8_t* texel = &image.rgba[(std::size_t(row) * size + column) * 4];
      texel[0] = std::uint8_t(std::clamp(colour.r, 0.f, 1.f) * 255.f + .5f);
      texel[1] = std::uint8_t(std::clamp(colour.g, 0.f, 1.f) * 255.f + .5f);
      texel[2] = std::uint8_t(std::clamp(colour.b, 0.f, 1.f) * 255.f + .5f);
      texel[3] = 255;
    }
  }
  return image;
}

}  // namespace ofs::client
