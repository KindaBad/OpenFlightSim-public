#pragma once
// Turns a loaded glTF Mesh into GPU-ready batches and LOD levels.
//
// The articulated A320 arrives as 1155 primitives and 1.12M triangles.
// Drawing every primitive separately is not viable, so this stage
//
//   * expands strip/fan topologies into explicit triangle lists,
//   * welds vertices that are bit-identical in every attribute,
//   * merges by material and articulated ancestor (1155 draws -> 91),
//   * builds lower LOD levels by vertex-cluster decimation.
//
// Each level owns one static vertex buffer and one index buffer, uploaded once
// and reused by every aircraft instance.

#include "gltf.hpp"

#include <cstdint>
#include <vector>

namespace ofs::client {

// Position(3) + normal(3) + UV0(2) + authored tangent/handedness(4).
inline constexpr std::size_t kMeshVertexFloats = 12;
inline constexpr std::size_t kMeshVertexSize = kMeshVertexFloats * sizeof(float);
inline constexpr std::size_t kLodCount = 3;
inline constexpr std::size_t kMaxLodCount = 4;

struct Batch {
  std::uint32_t material{};
  std::uint32_t firstIndex{};
  std::uint32_t indexCount{};
  float boundsMin[3]{};
  float boundsMax[3]{};
  int transformNode{-1}; // -1 static, otherwise animated node rest-world delta.
};

struct LodLevel {
  std::vector<float> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<Batch> batches;
  std::uint64_t triangleCount{};
  std::uint64_t vertexCount{};
  float boundsMin[3]{};
  float boundsMax[3]{};
  // Decimation cell size in metres; 0 for the full-detail level.
  float cellSize{};
  std::size_t vertexTotal() const { return vertices.size() / kMeshVertexFloats; }
};

// `clusterCell[kLodCount - 1]` is the vertex-cluster cell size in metres for
// each reduced level, in metres. Throws std::runtime_error when the mesh has no
// drawable triangle geometry.
struct GpuMesh {
  std::vector<LodLevel> levels;  // levels[0] is full detail
  float boundsMin[3]{};
  float boundsMax[3]{};

  std::size_t batchCount() const { return levels.empty() ? 0 : levels.front().batches.size(); }
  std::uint64_t triangles(std::size_t level) const {
    return level < levels.size() ? levels[level].triangleCount : 0;
  }
};

GpuMesh buildGpuMesh(const Mesh& mesh, const float clusterCell[kLodCount - 1], std::size_t levelCount = kLodCount);

// Distance-based LOD selection for a feature of `featureRadius` metres seen
// from `distance` metres. Clamped to [0, kLodCount).
std::size_t lodForDistance(float featureRadius, double distance);
std::size_t stableAircraftLod(double radius, double distance, std::size_t previous, std::size_t levels = kLodCount);

// Total GPU bytes the mesh will occupy, for the developer overlay.
std::size_t gpuBytes(const GpuMesh& mesh);

}  // namespace ofs::client
