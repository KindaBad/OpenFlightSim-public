#include "mesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace ofs::client {
namespace {

// Distance from a primitive's centre to its furthest corner, used for per-batch
// culling bounds.
float primitiveRadius(const Primitive& primitive) {
  float sum = 0;
  for (int c = 0; c < 3; ++c) {
    const float half = 0.5f * (primitive.boundsMax[c] - primitive.boundsMin[c]);
    sum += half * half;
  }
  return std::sqrt(sum);
}

// Expands a non-triangle topology into an explicit triangle list. glTF strips
// and fans are legal and must not be dropped.
std::vector<std::uint32_t> expandTopology(const Primitive& primitive) {
  const std::vector<std::uint32_t>& source = primitive.indices;
  switch (primitive.topology) {
    case MeshTopology::Triangles:
      return source;
    case MeshTopology::TriangleStrip: {
      std::vector<std::uint32_t> result;
      if (source.size() < 3) return result;
      result.reserve((source.size() - 2) * 3);
      for (std::size_t i = 2; i < source.size(); ++i) {
        // Odd triangles flip winding so a strip keeps a consistent front face.
        if (i & 1) {
          result.push_back(source[i - 1]);
          result.push_back(source[i - 2]);
          result.push_back(source[i]);
        } else {
          result.push_back(source[i - 2]);
          result.push_back(source[i - 1]);
          result.push_back(source[i]);
        }
      }
      return result;
    }
    case MeshTopology::TriangleFan: {
      std::vector<std::uint32_t> result;
      if (source.size() < 3) return result;
      result.reserve((source.size() - 2) * 3);
      for (std::size_t i = 2; i < source.size(); ++i) {
        result.push_back(source[0]);
        result.push_back(source[i - 1]);
        result.push_back(source[i]);
      }
      return result;
    }
    default:
      // Line and point topologies carry no surface. Treating them as triangles
      // would render garbage, so they are dropped; the loader reports them.
      return {};
  }
}

struct WeldKey {
  std::uint64_t hash{};
  bool operator==(const WeldKey& other) const noexcept { return hash == other.hash; }
};

struct WeldKeyHash {
  std::size_t operator()(const WeldKey& key) const noexcept {
    return static_cast<std::size_t>(key.hash);
  }
};

std::uint64_t hashFloats(const float* values, std::size_t count) {
  std::uint64_t hash = 1469598103934665603ull;
  for (std::size_t i = 0; i < count; ++i) {
    std::uint32_t bits;
    std::memcpy(&bits, values + i, 4);
    hash = (hash ^ bits) * 1099511628211ull;
  }
  return hash;
}

// Order-independent key for a triangle, so clustering duplicates collapse.
std::uint64_t triangleKey(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
  std::array<std::uint32_t, 3> sorted{a, b, c};
  std::sort(sorted.begin(), sorted.end());
  return static_cast<std::uint64_t>(sorted[0]) |
         (static_cast<std::uint64_t>(sorted[1]) << 21) |
         (static_cast<std::uint64_t>(sorted[2]) << 42);
}

}  // namespace

std::size_t lodForDistance(float featureRadius, double distance) {
  // Apparent angular size is the honest metric; radius/distance is monotonic in
  // it and costs a single divide. Thresholds are tuned so a 37.6 m A320 uses
  // full detail inside ~400 m, the mid tier out to ~3.3 km, and the coarse tier
  // beyond that, which keeps a 16-aircraft scene within a sane triangle budget.
  const double d = distance < 1.0 ? 1.0 : distance;
  const double ratio = static_cast<double>(featureRadius) / d;
  if (ratio > 0.050) return 0;
  if (ratio > 0.006) return 1;
  return 2;
}

std::size_t gpuBytes(const GpuMesh& mesh) {
  std::size_t total = 0;
  for (const LodLevel& level : mesh.levels)
    total += level.vertices.size() * sizeof(float) + level.indices.size() * sizeof(std::uint32_t);
  return total;
}

std::size_t stableAircraftLod(double radius, double distance, std::size_t previous, std::size_t levels) {
  const double near=radius*5, far=radius*55;
  levels=std::clamp(levels,std::size_t{1},kMaxLodCount);
  auto lod=std::min(previous,levels-1);
  if (levels==1) return 0;
  if (lod==0 && distance>near*1.15) lod=1;
  if (lod==1 && distance<near*.85) lod=0;
  if (lod==1 && levels>2 && distance>far*1.15) lod=2;
  if (lod==2 && distance<far*.85) lod=1;
  if (levels==4 && lod==2 && distance>radius*180*1.15) lod=3;
  if (lod==3 && distance<radius*180*.85) lod=2;
  return lod;
}

GpuMesh buildGpuMesh(const Mesh& mesh, const float clusterCell[kLodCount - 1], std::size_t levelCount) {
  if (!levelCount || levelCount>kMaxLodCount || (levelCount>1 && !clusterCell))
    throw std::invalid_argument("invalid mesh LOD request");
  GpuMesh result;
  result.levels.resize(levelCount);
  result.boundsMin[0] = result.boundsMin[1] = result.boundsMin[2] =
      std::numeric_limits<float>::max();
  result.boundsMax[0] = result.boundsMax[1] = result.boundsMax[2] =
      -std::numeric_limits<float>::max();

  const std::size_t materialCount = mesh.materials.size();
  if (materialCount == 0) throw std::runtime_error("mesh: document has no materials");

  // Primitives grouped by material, each carrying its own render-layout
  // vertices (position, normal, uv) converted from the loader's wider layout.
  struct Work {
    std::vector<float> vertices;
    std::vector<std::uint32_t> indices;
    float radius{};
  };
  std::map<std::pair<int, std::uint32_t>, std::vector<Work>> groups;
  for (const Primitive& primitive : mesh.primitives) {
    std::vector<std::uint32_t> expanded = expandTopology(primitive);
    if (expanded.size() < 3) continue;
    Work work;
    work.radius = primitiveRadius(primitive);
    const std::size_t sourceCount = primitive.vertices.size() / kGltfVertexFloats;
    work.vertices.resize(sourceCount * kMeshVertexFloats);
    for (std::size_t v = 0; v < sourceCount; ++v) {
      const float* source = primitive.vertices.data() + v * kGltfVertexFloats;
      float* out = work.vertices.data() + v * kMeshVertexFloats;
      out[0] = source[0]; out[1] = source[1]; out[2] = source[2];
      out[3] = source[3]; out[4] = source[4]; out[5] = source[5];
      out[6] = source[6]; out[7] = source[7];
      std::copy_n(source+8,4,out+8);
    }
    work.indices = std::move(expanded);
    groups[{primitive.transformNode, primitive.material}].push_back(std::move(work));
  }

  // ---- Level 0: weld and merge, one contiguous index range per material ----
  LodLevel& level0 = result.levels[0];
  level0.boundsMin[0] = level0.boundsMin[1] = level0.boundsMin[2] =
      std::numeric_limits<float>::max();
  level0.boundsMax[0] = level0.boundsMax[1] = level0.boundsMax[2] =
      -std::numeric_limits<float>::max();

  std::unordered_map<WeldKey, std::uint32_t, WeldKeyHash> weld;
  weld.reserve(1u << 21);
  std::vector<std::uint32_t> remap;

  for (auto& [key, works] : groups) {
    const auto [transformNode, material] = key;
    weld.clear(); // Do not weld vertices across independently moving groups.
    if (works.empty()) continue;
    // Largest features first, so a batch's bounds stay representative.
    std::sort(works.begin(), works.end(),
              [](const Work& a, const Work& b) { return a.radius > b.radius; });

    std::vector<std::vector<std::uint32_t>> mapped(works.size());
    for (std::size_t w = 0; w < works.size(); ++w) {
      const std::vector<float>& source = works[w].vertices;
      const std::size_t count = source.size() / kMeshVertexFloats;
      remap.resize(count);
      for (std::size_t v = 0; v < count; ++v) {
        const float* vertex = source.data() + v * kMeshVertexFloats;
        const WeldKey key{hashFloats(vertex, kMeshVertexFloats)};
        auto it = weld.find(key);
        if (it != weld.end()) {
          // Hash collisions are resolved by comparing the actual data, so two
          // genuinely different vertices can never be merged.
          const float* existing =
              level0.vertices.data() + static_cast<std::size_t>(it->second) * kMeshVertexFloats;
          if (std::memcmp(existing, vertex, kMeshVertexFloats * sizeof(float)) == 0) {
            remap[v] = it->second;
            continue;
          }
        }
        const auto index = static_cast<std::uint32_t>(level0.vertexTotal());
        level0.vertices.insert(level0.vertices.end(), vertex, vertex + kMeshVertexFloats);
        weld.emplace(key, index);
        remap[v] = index;
      }
      mapped[w].resize(works[w].indices.size());
      for (std::size_t i = 0; i < works[w].indices.size(); ++i)
        mapped[w][i] = remap[works[w].indices[i]];
    }

    Batch batch;
    batch.material = static_cast<std::uint32_t>(material);
    batch.transformNode = transformNode;
    batch.firstIndex = static_cast<std::uint32_t>(level0.indices.size());
    batch.boundsMin[0] = batch.boundsMin[1] = batch.boundsMin[2] =
        std::numeric_limits<float>::max();
    batch.boundsMax[0] = batch.boundsMax[1] = batch.boundsMax[2] =
        -std::numeric_limits<float>::max();
    for (std::size_t w = 0; w < works.size(); ++w) {
      level0.indices.insert(level0.indices.end(), mapped[w].begin(), mapped[w].end());
      for (std::size_t i = 0; i < mapped[w].size(); ++i) {
        const float* vertex =
            level0.vertices.data() + static_cast<std::size_t>(mapped[w][i]) * kMeshVertexFloats;
        for (int c = 0; c < 3; ++c) {
          batch.boundsMin[c] = std::min(batch.boundsMin[c], vertex[c]);
          batch.boundsMax[c] = std::max(batch.boundsMax[c], vertex[c]);
        }
      }
    }
    batch.indexCount = static_cast<std::uint32_t>(level0.indices.size()) - batch.firstIndex;
    if (batch.indexCount == 0) continue;
    level0.batches.push_back(batch);
    for (int c = 0; c < 3; ++c) {
      level0.boundsMin[c] = std::min(level0.boundsMin[c], batch.boundsMin[c]);
      level0.boundsMax[c] = std::max(level0.boundsMax[c], batch.boundsMax[c]);
      result.boundsMin[c] = std::min(result.boundsMin[c], batch.boundsMin[c]);
      result.boundsMax[c] = std::max(result.boundsMax[c], batch.boundsMax[c]);
    }
  }
  if (level0.batches.empty())
    throw std::runtime_error("mesh: document contains no drawable triangles");
  level0.triangleCount = level0.indices.size() / 3;
  level0.vertexCount = level0.vertexTotal();
  weld.clear();
  weld.rehash(0);

  // ---- Reduced levels: vertex-cluster decimation of level 0 ----
  for (std::size_t levelIndex = 1; levelIndex < levelCount; ++levelIndex) {
    const float cell = clusterCell[levelIndex - 1];
    LodLevel& level = result.levels[levelIndex];
    level.cellSize = cell;
    if (!(cell > 0)) {
      // Degenerate request: reuse the full-detail level unchanged.
      level = level0;
      level.cellSize = 0;
      continue;
    }
    level.boundsMin[0] = level.boundsMin[1] = level.boundsMin[2] =
        std::numeric_limits<float>::max();
    level.boundsMax[0] = level.boundsMax[1] = level.boundsMax[2] =
        -std::numeric_limits<float>::max();

    // Cluster across all batches, so a cell straddling two materials still
    // produces one shared vertex.
    std::unordered_map<std::uint64_t, std::uint32_t> cells;
    cells.reserve(1u << 20);
    const std::size_t sourceVertexCount = level0.vertexTotal();
    std::vector<std::uint32_t> cluster(sourceVertexCount);
    // Running means, so a cluster vertex is the average of its contributors
    // and the decimated silhouette stays smooth instead of snapping to a cell
    // edge. `averages` already holds the mean; do not divide again below.
    std::vector<float> averages(sourceVertexCount * kMeshVertexFloats);
    std::vector<std::uint32_t> weights(sourceVertexCount, 0);
    std::vector<bool> visited(sourceVertexCount, false);
    std::uint32_t clusterCount = 0;
    for (const auto& sourceBatch : level0.batches) {
    cells.clear(); // Preserve material/UV seams and articulated group boundaries.
    for (std::uint32_t iv = 0; iv < sourceBatch.indexCount; ++iv) {
      const std::size_t v = level0.indices[sourceBatch.firstIndex + iv];
      if (visited[v]) continue;
      visited[v] = true;
      const float* vertex = level0.vertices.data() + v * kMeshVertexFloats;
      const auto gx = static_cast<std::int64_t>(std::floor(vertex[0] / cell));
      const auto gy = static_cast<std::int64_t>(std::floor(vertex[1] / cell));
      const auto gz = static_cast<std::int64_t>(std::floor(vertex[2] / cell));
      // Cell indices are small (a 40 m model at 0.3 m), so 21 bits per axis is
      // collision-free for any plausible airframe.
      const auto key = static_cast<std::uint64_t>(gx + (1 << 20)) |
                       (static_cast<std::uint64_t>(gy + (1 << 20)) << 21) |
                       (static_cast<std::uint64_t>(gz + (1 << 20)) << 42);
      auto it = cells.find(key);
      if (it == cells.end()) {
        const auto index = clusterCount++;
        cells.emplace(key, index);
        cluster[v] = index;
        std::memcpy(averages.data() + static_cast<std::size_t>(index) * kMeshVertexFloats,
                    vertex, kMeshVertexFloats * sizeof(float));
        weights[index] = 1;
      } else {
        const std::uint32_t index = it->second;
        cluster[v] = index;
        float* accumulator = averages.data() + static_cast<std::size_t>(index) * kMeshVertexFloats;
        const float count = static_cast<float>(weights[index]) + 1.0f;
        for (std::size_t f = 0; f < kMeshVertexFloats; ++f)
          accumulator[f] += (vertex[f] - accumulator[f]) / count;
        weights[index] = static_cast<std::uint32_t>(count);
      }
    }
    }

    level.vertices.resize(static_cast<std::size_t>(clusterCount) * kMeshVertexFloats);
    for (std::size_t i = 0; i < clusterCount; ++i) {
      float* out = level.vertices.data() + i * kMeshVertexFloats;
      std::memcpy(out, averages.data() + i * kMeshVertexFloats,
                  kMeshVertexFloats * sizeof(float));
      // Averaging shortens normals, so renormalise.
      const float length = std::sqrt(out[3] * out[3] + out[4] * out[4] + out[5] * out[5]);
      if (length > 1e-6f) {
        const float inv = 1.0f / length;
        out[3] *= inv; out[4] *= inv; out[5] *= inv;
      } else {
        out[3] = 0; out[4] = 1; out[5] = 0;
      }
      for (int c = 0; c < 3; ++c) {
        level.boundsMin[c] = std::min(level.boundsMin[c], out[c]);
        level.boundsMax[c] = std::max(level.boundsMax[c], out[c]);
      }
    }
    level.vertexCount = clusterCount;

    // Clustering maps many source triangles onto the same vertex triple.
    // Deduplicating is what actually reduces the triangle count; without it a
    // 0.3 m cell still leaves 72% duplicates.
    std::unordered_set<std::uint64_t> seen;
    seen.reserve(1u << 20);
    level.batches.clear();
    level.batches.reserve(level0.batches.size());
    for (const Batch& source : level0.batches) {
      Batch batch = source;
      batch.firstIndex = static_cast<std::uint32_t>(level.indices.size());
      seen.clear();
      for (std::uint32_t i = 0; i + 2 < source.indexCount; i += 3) {
        const std::uint32_t a = cluster[level0.indices[source.firstIndex + i]];
        const std::uint32_t b = cluster[level0.indices[source.firstIndex + i + 1]];
        const std::uint32_t c = cluster[level0.indices[source.firstIndex + i + 2]];
        if (a == b || b == c || a == c) continue;  // collapsed by clustering
        if (!seen.insert(triangleKey(a, b, c)).second) continue;  // duplicate
        level.indices.push_back(a);
        level.indices.push_back(b);
        level.indices.push_back(c);
      }
      batch.indexCount = static_cast<std::uint32_t>(level.indices.size()) - batch.firstIndex;
      if (batch.indexCount) level.batches.push_back(batch);
    }
    level.triangleCount = level.indices.size() / 3;
  }
  return result;
}

}  // namespace ofs::client
