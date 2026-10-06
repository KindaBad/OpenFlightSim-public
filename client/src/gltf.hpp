#pragma once
// glTF 2.0 / GLB reader for the native client.
//
// Scope is deliberately narrow: static triangle/line geometry, node transforms,
// hierarchy, rig metadata, PNG/JPEG images and PBR materials. Free of bgfx/SDL,
// unit tested headlessly; the renderer turns the result into GPU buffers.
//
// Unsupported glTF features are reported through `report()` rather than
// silently ignored, so an asset that needs more than this loader can support
// fails loudly instead of rendering wrong.

#include <cstdint>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace ofs::client {

// Topology of a loaded primitive, matching glTF primitive modes.
enum class MeshTopology : std::uint8_t { Triangles, TriangleStrip, TriangleFan, Lines, LineStrip, LineLoop, Points };

struct Material {
  std::string name;
  // Linear base colour; glTF factors are already linear.
  float baseColor[4]{1, 1, 1, 1};
  float metallic{1};
  float roughness{1};
  float emissive[3]{0, 0, 0};
  // Alpha mask is evaluated in the shader; blend uses alpha compositing.
  enum class Alpha : std::uint8_t { Opaque, Mask, Blend } alpha{Alpha::Opaque};
  float alphaCutoff{0.5};
  bool doubleSided{};
  // Number of referenced supported texture slots.
  std::uint32_t textureSlots{0};
  int baseColorTexture{-1}, metallicRoughnessTexture{-1}, emissiveTexture{-1};
  int normalTexture{-1};
  int occlusionTexture{-1};
  float occlusionStrength{1};
  float normalScale{1};
  float environmentReflection{}; // Optional owned-material extras; legacy default 0.
};

struct Image {
  std::string name;
  std::string source; // URI or embedded image identifier, retained for memory diagnostics
  unsigned width{}, height{};
  std::vector<std::uint8_t> rgba;
};
struct Texture {
  int image{-1};
  int wrapS{10497}, wrapT{10497}, minFilter{9987}, magFilter{9729};
};

struct GltfNode {
  std::string name;
  int parent{-1}, mesh{-1};
  std::vector<int> children;
  std::array<float, 16> local{}, world{};
  // Optional owned aircraft rig metadata exported as glTF extras.
  std::string channel;
  std::array<float, 3> axis{0, 0, 1}, slide{};
  float gain{1};
  bool active{};
};

// One loaded primitive with its node transform already applied to positions
// and normals, expressed in the asset's own right-handed Y-up space.
struct Primitive {
  std::string name;  // Node name, used to bind animated parts.
  int node{-1}, transformNode{-1};
  std::uint32_t material{};
  MeshTopology topology{MeshTopology::Triangles};
  // Interleaved position(3) normal(3) uv(2) tangent(4); see kMeshVertexSize.
  std::vector<float> vertices;
  std::vector<std::uint32_t> indices;
  float boundsMin[3]{};
  float boundsMax[3]{};
  bool hasNormals{};
  bool hasTangents{};
  bool hasUv{};
  // Indices consumed by a triangle/line topology when converting strips/fans.
  std::uint32_t sourcePrimitiveCount{};
};

// Interleaved attribute layout of a loaded primitive: position(3) normal(3)
// uv(2) tangent(4). Authored tangent frames survive the render mesh and GPU upload.
inline constexpr std::size_t kGltfVertexFloats = 12;
inline constexpr std::size_t kGltfVertexSize = kGltfVertexFloats * sizeof(float);

struct Mesh {
  std::string name;
  std::vector<Primitive> primitives;
  std::vector<Material> materials;
  std::vector<GltfNode> nodes;
  std::vector<Image> images;
  std::vector<Texture> textures;
  float boundsMin[3]{};
  float boundsMax[3]{};
  std::uint64_t triangleCount{};
  std::uint64_t vertexCount{};
  // Named diagnostics for everything the loader skipped.
  std::vector<std::string> report;
  // True when at least one primitive loaded successfully.
  bool valid() const { return !primitives.empty(); }
};

// Reads a .glb or .gltf file. `basePath` is the directory containing the file,
// used to resolve external buffers and images. Throws std::runtime_error when
// the file is missing or structurally invalid; recoverable omissions land in
// Mesh::report instead.
Mesh loadGltf(const std::string& path);

// Exposed for tests: decodes an in-memory GLB or glTF document.
Mesh parseGltf(std::string_view json, const std::vector<std::uint8_t>& binary);

// Number of float components in a glTF accessor `type` string ("SCALAR",
// "VEC3", "MAT4", ...). Returns 0 for an unknown name.
int gltfComponentCount(std::string_view type);
// Size in bytes of one glTF componentType (5120..5126).
int gltfComponentSize(int componentType);

}  // namespace ofs::client
