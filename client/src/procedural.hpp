#pragma once
// Procedural texture generation.
//
// The repository ships source only, so every environment texture the renderer
// samples is synthesised here at start-up: the cloud density volumes, the
// weather map, the tiling terrain material layers, a water ripple map and a
// general-purpose noise tile. Everything is deterministic and tiles exactly, and
// nothing depends on a GPU, so the headless suite can verify it.

#include <cstdint>
#include <functional>
#include <vector>

namespace ofs::client::procedural {

// Runs fn(row) for every row in [0, rows) across the available cores.
void parallelRows(int rows, const std::function<void(int)>& fn);

// --- Tileable noise primitives ---------------------------------------------
// Coordinates are in lattice cells; `period` is the repeat length in cells.
// Gradient noise in roughly [-1, 1].
float perlin(float x, float y, float z, int period, std::uint32_t seed);
// Fractal sum of `octaves` gradient-noise bands, normalised to about [-1, 1].
float perlinFbm(float x, float y, float z, int period, int octaves, std::uint32_t seed);
// Cellular noise: distance to the nearest and second-nearest feature point, in
// cells, plus a stable identifier for the nearest cell.
struct Cell { float f1, f2; std::uint32_t id; };
Cell worley(float x, float y, float z, int period, std::uint32_t seed);

// --- Cloud volumes ----------------------------------------------------------
// Single-channel 8-bit cubes, x fastest. The shape volume is a Perlin-Worley
// blend that gives cumulus their billowed outline; the detail volume is a
// higher-frequency Worley sum used to erode the edges.
std::vector<std::uint8_t> cloudShapeVolume(int size);
std::vector<std::uint8_t> cloudDetailVolume(int size);

// RGBA8 weather map: R cumulus coverage, G cloud-type/height variation,
// B cirrus streak density, A large-scale density variation.
std::vector<std::uint8_t> weatherMap(int size);
// RGBA8: four independent tiling fractal channels for shader-side variation.
std::vector<std::uint8_t> noiseTile(int size);
// RGBA8: RG tangent-space ripple normal, B ripple height.
std::vector<std::uint8_t> waterNormalTile(int size);

// --- Terrain material layers ------------------------------------------------
enum TerrainLayer : int {
  kLayerGrass = 0,
  kLayerSoil,
  kLayerRock,
  kLayerForest,
  kLayerSnow,
  kLayerAsphalt,
  kTerrainLayerCount
};
struct TerrainLayers {
  int size{};
  // Per layer: sRGB albedo in RGB with height in A.
  std::vector<std::vector<std::uint8_t>> albedoHeight;
  // Per layer: tangent-space normal in RG, roughness in B, cavity occlusion in A.
  std::vector<std::vector<std::uint8_t>> normalRoughness;
};
TerrainLayers terrainLayers(int size);

}  // namespace ofs::client::procedural
