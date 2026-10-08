#pragma once
// Land cover derived from the shared terrain surface.
//
// The simulation owns the terrain shape (ofs/terrain.hpp); nothing here changes
// it. This file classifies that surface for presentation: where forest grows,
// where the lowland is farmed, where closed basins hold lakes, and where each
// individual tree stands. The renderer bakes the maps into textures, and the
// same CPU functions place the trees so geometry and ground shading agree.
//
// Axes follow the simulation: north/east in metres, heights positive up.

#include <cstdint>
#include <vector>

namespace ofs::client {

struct TreeInstance {
  float east, up, south;  // render axes: +X east, +Y up, +Z south
  float height;           // metres, trunk base to crown top
  float spread;           // crown radius relative to the species mesh
  float yaw;              // radians
  float tint;             // 0..1 per-tree colour variation
  float conifer;          // 0 broadleaf, 1 conifer
};

class Landscape {
 public:
  // The maps cover a square of this half-size centred on the airfield.
  static constexpr float kExtent = 48000.f;
  static constexpr float kNoLake = -10000.f;
  static constexpr float kTreeChunk = 1000.f;

  // Heights are stored as 16-bit fractions of this range, in metres.
  static constexpr float kHeightBase = -200.f, kHeightRange = 5600.f;

  // Builds the maps. Done once at start-up, on every core.
  explicit Landscape(int landSize = 1024, int heightSize = 2048);

  int landSize() const { return landSize_; }
  int lakeSize() const { return lakeSize_; }
  int heightSize() const { return heightSize_; }
  // The terrain's own shape, laid out like the land cover: the surface shader
  // reads its slopes and long shadows from this.
  const std::vector<std::uint16_t>& heightTexels() const { return height_; }
  // Bilinear elevation from the height map, metres.
  float elevation(double north, double east) const;
  // RGBA8, row 0 is the northern edge and column 0 the western edge, so a
  // texture lookup at (east, south) / (2 * kExtent) + 0.5 addresses it directly.
  // R forest density, G farmland, B moisture, A rock exposure.
  const std::vector<std::uint8_t>& landTexels() const { return land_; }
  // Lake surface elevation per cell, or kNoLake. Constant across each basin.
  const std::vector<float>& lakeTexels() const { return lake_; }

  // Bilinear forest density, 0..1.
  float forestDensity(double north, double east) const;
  float farmland(double north, double east) const;
  // Water surface elevation above the point, or kNoLake when it is dry land.
  float lakeSurface(double north, double east) const;
  bool underWater(double north, double east) const;
  int lakeCount() const { return lakeCount_; }
  double lakeAreaKm2() const { return lakeAreaKm2_; }

  // The pattern of fields the surface shader draws on farmland, which is
  // where hedgerow trees have to stand: how far a point is from the edge of
  // its field in metres, whether that edge carries a hedge or a belt of trees
  // and how far to either side it reaches, whether the field is farmed at all,
  // and whether it is a wood. `hedge` is the nearest point on that edge.
  struct Field {
    float edge{}, reach{};
    bool hedged{}, farmed{}, wooded{};
    double hedgeNorth{}, hedgeEast{};
  };
  Field fieldPattern(double north, double east) const;

  // Appends the trees standing in the chunk whose south-west corner is at
  // (chunkEast, chunkSouth) * kTreeChunk. `density` is candidates per square
  // kilometre; the result is deterministic for a given chunk and density.
  void treesInChunk(int chunkEast, int chunkSouth, int density, std::vector<TreeInstance>& out) const;

 private:
  float land(double north, double east, int channel) const;
  int landSize_, lakeSize_, heightSize_;
  std::vector<std::uint16_t> height_;
  std::vector<std::uint8_t> land_;
  std::vector<float> lake_;
  int lakeCount_{};
  double lakeAreaKm2_{};
};

// True inside the airfield's fence and on the roads and approaches around it,
// where nothing grows.
bool insideAirfieldClearway(double north, double east);

}  // namespace ofs::client
