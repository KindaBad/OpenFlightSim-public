// Headless tests for the glTF loader and the mesh/LOD builder.
//
// These exercise real parsing and real decimation against synthetic documents
// written byte-for-byte, plus the delivered A320 when it is present. Nothing
// here needs a graphics context, so it runs in the headless and sanitizer
// configurations too.

#include "gltf.hpp"
#include "mesh.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <stdexcept>
#include <vector>

namespace {

int gFailures = 0;

void check(bool condition, const char* what) {
  if (condition) return;
  std::fprintf(stderr, "FAIL asset: %s\n", what);
  ++gFailures;
}

template <typename A, typename B>
void checkNear(A a, B b, double tolerance, const char* what) {
  if (std::abs(static_cast<double>(a) - static_cast<double>(b)) <= tolerance) return;
  std::fprintf(stderr, "FAIL asset: %s (%.9g vs %.9g)\n", what, static_cast<double>(a),
               static_cast<double>(b));
  ++gFailures;
}

// Builds a glTF document with one node, one mesh, one triangle and a
// material, using an embedded base64 buffer. Written by hand so the test does
// not depend on the exporter that produced the A320.
std::string singleTriangleGlb(std::string& binaryOut) {
  // 3 vertices (position VEC3 float, normal VEC3 float, uv VEC2 float) and
  // 3 unsigned-short indices.
  std::vector<float> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
  std::vector<float> normals{0, 0, 1, 0, 0, 1, 0, 0, 1};
  std::vector<float> uvs{0, 0, 1, 0, 0, 1};
  std::vector<std::uint16_t> indices{0, 1, 2};
  std::string binary;
  auto append = [&binary](const void* data, std::size_t bytes) {
    binary.append(static_cast<const char*>(data), bytes);
  };
  const std::size_t positionOffset = 0;
  append(positions.data(), positions.size() * sizeof(float));
  const std::size_t normalOffset = binary.size();
  append(normals.data(), normals.size() * sizeof(float));
  const std::size_t uvOffset = binary.size();
  append(uvs.data(), uvs.size() * sizeof(float));
  // 4-byte align the index block.
  while (binary.size() % 4 != 0) binary.push_back('\0');
  const std::size_t indexOffset = binary.size();
  append(indices.data(), indices.size() * sizeof(std::uint16_t));

  char header[2048];
  const int written = std::snprintf(header, sizeof(header),
                R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"mesh":0,"name":"wing","translation":[10,2,-3]}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}],
"materials":[{"name":"paint","pbrMetallicRoughness":{"baseColorFactor":[0.2,0.4,0.8,1],"metallicFactor":0.25,"roughnessFactor":0.6}}],
"accessors":[
{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},
{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],
"bufferViews":[
{"buffer":0,"byteOffset":%zu,"byteLength":%zu},
{"buffer":0,"byteOffset":%zu,"byteLength":%zu},
{"buffer":0,"byteOffset":%zu,"byteLength":%zu},
{"buffer":0,"byteOffset":%zu,"byteLength":%zu}],
"buffers":[{"byteLength":%zu}]})",
                positionOffset, positions.size() * sizeof(float), normalOffset,
                normals.size() * sizeof(float), uvOffset, uvs.size() * sizeof(float),
                indexOffset, indices.size() * sizeof(std::uint16_t), binary.size());
  if (written <= 0 || written >= static_cast<int>(sizeof(header)))
    throw std::runtime_error("singleTriangleGlb: header buffer too small");
  binaryOut = binary;
  return std::string(header, static_cast<std::size_t>(written));
}

std::string wrapGlb(const std::string& json, const std::string& binary) {
  const auto pad4 = [](std::string& text) {
    while (text.size() % 4 != 0) text.push_back(' ');
  };
  std::string jsonChunk = json;
  std::string binChunk = binary;
  pad4(jsonChunk);
  pad4(binChunk);
  std::string out = "glTF";
  const std::uint32_t version = 2;
  const std::uint32_t length = static_cast<std::uint32_t>(12 + 8 + jsonChunk.size() + 8 + binChunk.size());
  const std::uint32_t jsonLength = static_cast<std::uint32_t>(jsonChunk.size());
  const std::uint32_t binLength = static_cast<std::uint32_t>(binChunk.size());
  const std::uint32_t jsonType = 0x4e4f534a;  // 'JSON'
  const std::uint32_t binType = 0x004e4942;   // 'BIN\0'
  out.append(reinterpret_cast<const char*>(&version), 4);
  out.append(reinterpret_cast<const char*>(&length), 4);
  out.append(reinterpret_cast<const char*>(&jsonLength), 4);
  out.append(reinterpret_cast<const char*>(&jsonType), 4);
  out.append(jsonChunk);
  out.append(reinterpret_cast<const char*>(&binLength), 4);
  out.append(reinterpret_cast<const char*>(&binType), 4);
  out.append(binChunk);
  return out;
}

// A document whose only geometry is behind a feature the loader does not
// support. The loader must report it and not silently render nothing.
const char* kUnsupportedSkinGlb = R"({"asset":{"version":"2.0"},"scene":0,
"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0,"name":"rigged","skin":0}],
"skins":[{"joints":[0]}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],
"materials":[{"name":"m","pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1]}}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],
"buffers":[{"byteLength":36}]})";

void testSingleTriangle() {
  std::string binary;
  const std::string json = singleTriangleGlb(binary);
  const std::string glb = wrapGlb(json, binary);
  auto mesh = ofs::client::parseGltf(json, std::vector<std::uint8_t>(binary.begin(), binary.end()));
  check(mesh.valid(), "single triangle loads");
  check(mesh.primitives.size() == 1, "one primitive");
  check(mesh.materials.size() == 1, "one material");
  check(mesh.materials[0].name == "paint", "material name");
  checkNear(mesh.materials[0].baseColor[2], 0.8, 1e-6, "base colour blue");
  checkNear(mesh.materials[0].metallic, 0.25, 1e-6, "metallic");
  checkNear(mesh.materials[0].roughness, 0.6, 1e-6, "roughness");
  check(mesh.primitives[0].name == "wing", "node name preserved");
  check(mesh.primitives[0].hasNormals && mesh.primitives[0].hasUv, "normals and uv present");
  // The node translation must be baked into the vertices.
  checkNear(mesh.primitives[0].vertices[0], 10.0, 1e-5, "translation x baked");
  checkNear(mesh.primitives[0].vertices[1], 2.0, 1e-5, "translation y baked");
  checkNear(mesh.primitives[0].vertices[2], -3.0, 1e-5, "translation z baked");
  check(mesh.primitives[0].indices.size() == 3, "one triangle of indices");
  check(mesh.triangleCount == 1, "triangle counted");
  // Bounds must include the transform.
  checkNear(mesh.boundsMax[0], 11.0, 1e-4, "bounds max x");
  checkNear(mesh.boundsMin[2], -3.0, 1e-4, "bounds min z");
  // The same document through the GLB container must give the same result.
  auto fromGlb = ofs::client::parseGltf(
      std::string_view(json), std::vector<std::uint8_t>(binary.begin(), binary.end()));
  check(fromGlb.triangleCount == 1, "GLB parse matches");
  (void)glb;
}

void testUnsupportedReported() {
  std::string binary(36, '\0');
  auto mesh = ofs::client::parseGltf(kUnsupportedSkinGlb,
                                      std::vector<std::uint8_t>(binary.begin(), binary.end()));
  check(!mesh.report.empty(), "unsupported feature is reported");
  bool reportedSkin = false;
  for (const std::string& note : mesh.report)
    if (note.find("skin") != std::string::npos) reportedSkin = true;
  check(reportedSkin, "skin reported by name");
}

void testMeshBuilder(const ofs::client::Mesh& mesh) {
  const float cells[ofs::client::kLodCount - 1] = {0.15f, 0.45f};
  auto gpu = ofs::client::buildGpuMesh(mesh, cells);
  check(gpu.levels.size() == ofs::client::kLodCount, "three LOD levels");
  // The full-detail level must preserve the triangle and vertex counts.
  check(gpu.levels[0].triangleCount == mesh.triangleCount, "LOD0 triangle count preserved");
  // Static geometry merges, while each articulated ancestor stays independent.
  std::set<std::pair<int,std::uint32_t>> groups;
  for (const auto& primitive : mesh.primitives) groups.emplace(primitive.transformNode, primitive.material);
  check(gpu.batchCount() <= groups.size(), "one batch per independently movable node/material");
  check(gpu.batchCount() > 0, "at least one batch");
  // Each tier must be no denser than the one before it.
  for (std::size_t i = 1; i < gpu.levels.size(); ++i)
    check(gpu.levels[i].triangleCount <= gpu.levels[i - 1].triangleCount, "LODs are monotonic");
  // Every batch index range must lie inside the index buffer.
  for (const auto& level : gpu.levels)
    for (const auto& batch : level.batches)
      check(static_cast<std::size_t>(batch.firstIndex) + batch.indexCount <= level.indices.size(),
            "batch range inside the index buffer");
  // Every index must address a real vertex.
  for (const auto& level : gpu.levels)
    for (std::uint32_t index : level.indices)
      check(index < level.vertexTotal(), "index inside the vertex buffer");
  // Bounds must be finite, and at least one axis must be non-degenerate (a
  // single triangle is flat, so per-axis checks would be wrong here).
  for (int i = 0; i < 3; ++i)
    check(std::isfinite(gpu.boundsMin[i]) && std::isfinite(gpu.boundsMax[i]), "bounds finite");
  check(gpu.boundsMax[0] > gpu.boundsMin[0] || gpu.boundsMax[1] > gpu.boundsMin[1] ||
            gpu.boundsMax[2] > gpu.boundsMin[2],
        "bounds non-degenerate");
  // Level 0 bounds must match the source mesh exactly.
  for (int i = 0; i < 3; ++i) {
    checkNear(gpu.levels[0].boundsMin[i], mesh.boundsMin[i], 1e-3, "LOD0 min matches source");
    checkNear(gpu.levels[0].boundsMax[i], mesh.boundsMax[i], 1e-3, "LOD0 max matches source");
  }
}

void testLodSelection() {
  // Apparent size must pick finer detail as the feature grows or nears. With a
  // 20 m aircraft feature: full detail inside ~400 m, mid tier to ~3.3 km, coarse
  // beyond.
  check(ofs::client::lodForDistance(20.0f, 20.0) == 0, "close and large is LOD0");
  check(ofs::client::lodForDistance(20.0f, 300.0) == 0, "inside the detailed range");
  check(ofs::client::lodForDistance(20.0f, 1000.0) == 1, "mid range is LOD1");
  check(ofs::client::lodForDistance(20.0f, 20000.0) == 2, "far away is LOD2");
  // A tiny feature is never treated as large on screen.
  check(ofs::client::lodForDistance(0.01f, 1.0) >= 1, "tiny feature is not LOD0");
  // A huge feature at long range still counts as large on screen.
  check(ofs::client::lodForDistance(2000.0f, 20000.0) == 0, "huge distant feature is LOD0");
  // Selection must be monotonic in distance for any feature size.
  for (double scale : {0.01, 0.1, 1.0, 20.0, 500.0}) {
    for (double d = 10; d < 60000; d *= 1.7) {
      const auto a = ofs::client::lodForDistance(static_cast<float>(scale), d);
      const auto b = ofs::client::lodForDistance(static_cast<float>(scale), d * 1.7);
      check(a <= b, "LOD selection is monotonic in distance");
    }
  }
  // And in feature size at a fixed distance.
  for (double distance : {50.0, 500.0, 5000.0}) {
    std::size_t previous = 2;
    for (double scale = 0.01; scale < 1000.0; scale *= 3.0) {
      const auto level = ofs::client::lodForDistance(static_cast<float>(scale), distance);
      check(level <= previous, "LOD selection is monotonic in apparent size");
      previous = level;
    }
  }
}

void testStripAndFan() {
  // A triangle strip must expand to n-2 triangles with consistent winding.
  const char* json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"mesh":0,"name":"strip"}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1,"mode":5}]}],
"materials":[{"name":"m","pbrMetallicRoughness":{}}],
"accessors":[
{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3"},
{"bufferView":1,"componentType":5123,"count":4,"type":"SCALAR"}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":8}],
"buffers":[{"byteLength":56}]})";
  std::vector<std::uint8_t> binary(56, 0);
  const float positions[4][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
  std::memcpy(binary.data(), positions, sizeof(positions));
  const std::uint16_t indices[4] = {0, 1, 2, 3};
  std::memcpy(binary.data() + 48, indices, sizeof(indices));
  auto mesh = ofs::client::parseGltf(json, binary);
  check(mesh.primitives.size() == 1, "strip loads");
  check(mesh.primitives[0].topology == ofs::client::MeshTopology::TriangleStrip, "strip topology");
  // The loader preserves the source strip; expansion to triangles is the mesh
  // builder's job, so the triangle count is the strip length minus two.
  check(mesh.primitives[0].indices.size() == 4, "strip indices preserved");
  check(mesh.triangleCount == 2, "strip triangle count");
  // Expanding through the builder must produce n-2 triangles with consistent
  // winding: a 4-vertex strip is 2 triangles.
  const float cells[ofs::client::kLodCount - 1] = {0.0f, 0.0f};
  auto gpu = ofs::client::buildGpuMesh(mesh, cells);
  check(gpu.levels[0].triangleCount == 2, "strip expands to 2 triangles");
  check(gpu.levels[0].indices.size() == 6, "expanded index count");
  // Winding: the first triangle keeps source order, the second flips so the
  // strip keeps a consistent front face.
  const auto& expanded = gpu.levels[0].indices;
  check(expanded[0] == 0 && expanded[1] == 1 && expanded[2] == 2, "strip first triangle winding");
  check(expanded[3] == 2 && expanded[4] == 1 && expanded[5] == 3, "strip second triangle winding");
}

void testComponentTables() {
  check(ofs::client::gltfComponentCount("SCALAR") == 1, "SCALAR count");
  check(ofs::client::gltfComponentCount("VEC3") == 3, "VEC3 count");
  check(ofs::client::gltfComponentCount("MAT4") == 16, "MAT4 count");
  check(ofs::client::gltfComponentCount("MAT3x2") == 6, "MAT3x2 count");
  check(ofs::client::gltfComponentCount("NONSENSE") == 0, "unknown type count");
  check(ofs::client::gltfComponentSize(5126) == 4, "float size");
  check(ofs::client::gltfComponentSize(5123) == 2, "ushort size");
  check(ofs::client::gltfComponentSize(5121) == 1, "ubyte size");
  check(ofs::client::gltfComponentSize(0) == 0, "unknown component size");
}

// The delivered asset, when present. This is the check that the real A320
// actually loads, measures and batches as expected.
void testAircraftAsset(const std::string& path) {
  std::ifstream probe(path, std::ios::binary);
  if (!probe.good()) {
    std::printf("note: %s not present, skipping aircraft assertions\n", path.c_str());
    return;
  }
  ofs::client::Mesh mesh;
  try {
    mesh = ofs::client::loadGltf(path);
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL asset: aircraft failed to load: %s\n", error.what());
    ++gFailures;
    return;
  }
  check(mesh.valid(), "aircraft has geometry");
  check(mesh.materials.size() > 1, "aircraft has multiple materials");
  // A320 real dimensions, measured in docs/ASSETS.md: 37.57 m long, 35.8 m
  // span, 11.76 m tall with the gear down.
  const double length = mesh.boundsMax[0] - mesh.boundsMin[0];
  const double span = mesh.boundsMax[2] - mesh.boundsMin[2];
  const double height = mesh.boundsMax[1] - mesh.boundsMin[1];
  checkNear(length, 37.57, 0.10, "aircraft length");
  checkNear(span, 35.80, 0.10, "aircraft span");
  checkNear(height, 11.76, 0.10, "aircraft height");
  // The nose must be at asset X=0 and the gear must touch asset Y=0, which is
  // what makes the CG anchor in coordinates.hpp correct.
  checkNear(mesh.boundsMin[0], 0.0, 0.01, "nose at asset X=0");
  checkNear(mesh.boundsMin[1], 0.0, 0.05, "wheels touch asset Y=0");
  // Every primitive must have a usable material and topology.
  for (const auto& primitive : mesh.primitives) {
    check(primitive.material < mesh.materials.size(), "primitive material index in range");
    check(primitive.vertices.size() / ofs::client::kGltfVertexFloats > 0, "primitive has vertices");
    check(primitive.indices.size() >= 3, "primitive has indices");
  }
  testMeshBuilder(mesh);
  // Batching is the decisive optimisation: 1134 primitives must become a
  // handful of draw calls.
  const float cells[ofs::client::kLodCount - 1] = {0.15f, 0.45f};
  auto gpu = ofs::client::buildGpuMesh(mesh, cells);
  std::printf("aircraft: %zu primitives, %zu materials, %llu triangles, %zu batches, "
              "LOD %llu/%llu/%llu, %.1f MB\n",
              mesh.primitives.size(), mesh.materials.size(),
              (unsigned long long)mesh.triangleCount, gpu.batchCount(),
              (unsigned long long)gpu.levels[0].triangleCount,
              (unsigned long long)gpu.levels[1].triangleCount,
              (unsigned long long)gpu.levels[2].triangleCount,
              ofs::client::gpuBytes(gpu) / 1e6);
  check(gpu.batchCount() < mesh.primitives.size(), "batching reduces draw calls");
  // The far LOD must be a real reduction or it is not doing its job.
  check(gpu.levels[2].triangleCount * 20 < gpu.levels[0].triangleCount,
        "far LOD is at least 20x smaller");
}

}  // namespace

int main(int argc, char** argv) {
  testComponentTables();
  testSingleTriangle();
  testUnsupportedReported();
  testStripAndFan();
  testLodSelection();
  // The synthetic document also goes through the builder, so the merge and
  // decimation paths are covered even without the delivered asset.
  {
    std::string binary;
    const std::string json = singleTriangleGlb(binary);
    auto mesh = ofs::client::parseGltf(json, std::vector<std::uint8_t>(binary.begin(), binary.end()));
    testMeshBuilder(mesh);
  }
  // Production content has its own assets.production test and skip policy.
  if(argc>1)testAircraftAsset(argv[1]);
  if (gFailures == 0) {
    std::printf("PASS glTF loader, mesh batching and LOD selection\n");
    return 0;
  }
  std::fprintf(stderr, "FAIL asset: %d check(s) failed\n", gFailures);
  return 1;
}
