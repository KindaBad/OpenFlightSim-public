#include "gltf.hpp"

#include "json.hpp"
#include "texture.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <stdexcept>

namespace ofs::client {
namespace {

using json::Value;

// glTF accessor "componentType" values.
constexpr int kByte = 5120, kUnsignedByte = 5121, kShort = 5122,
              kUnsignedShort = 5123, kUnsignedInt = 5125, kFloat = 5126;

struct Accessor {
  int bufferView{-1};
  std::size_t byteOffset{};
  int componentType{};
  bool normalized{};
  std::size_t count{};
  // Cached from the accessor "type" name; 0 means unsupported.
  int components{};
  std::size_t elementSize{};
  bool valid{};
  // Sparse replacements resolved during load.
  std::vector<std::size_t> sparseIndices;
  std::vector<float> sparseValues;
};

struct BufferView {
  int buffer{-1};
  std::size_t byteOffset{};
  std::size_t byteLength{};
  std::size_t byteStride{};
  bool valid{};
};

// Column-major 4x4 used for node transform composition. Stored as 16 floats so
// it can be handed to GLM without a conversion.
struct Mat4 {
  float m[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

  static Mat4 identity() { return Mat4{}; }

  static Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c)
      for (int row = 0; row < 4; ++row) {
        float sum = 0;
        for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[c * 4 + k];
        r.m[c * 4 + row] = sum;
      }
    return r;
  }

  void transformPoint(const float in[3], float out[3]) const {
    for (int row = 0; row < 3; ++row)
      out[row] = m[0 * 4 + row] * in[0] + m[1 * 4 + row] * in[1] +
                 m[2 * 4 + row] * in[2] + m[3 * 4 + row];
  }

  // Normals need the inverse transpose of the upper 3x3. Every node transform
  // in practice is rigid or uniformly scaled, so the cofactor matrix is used;
  // it is correct for both and stays stable without a general inverse.
  void transformDirection(const float in[3], float out[3]) const {
    // Cofactor matrix of the upper-left 3x3 equals its inverse transpose.
    const float* a = m;
    float c[9];
    c[0] = a[5] * a[10] - a[6] * a[9];
    c[1] = a[6] * a[8] - a[4] * a[10];
    c[2] = a[4] * a[9] - a[5] * a[8];
    c[3] = a[2] * a[9] - a[1] * a[10];
    c[4] = a[0] * a[10] - a[2] * a[8];
    c[5] = a[1] * a[8] - a[0] * a[9];
    c[6] = a[1] * a[6] - a[2] * a[5];
    c[7] = a[2] * a[4] - a[0] * a[6];
    c[8] = a[0] * a[5] - a[1] * a[4];
    for (int row = 0; row < 3; ++row)
      out[row] = c[row * 3 + 0] * in[0] + c[row * 3 + 1] * in[1] + c[row * 3 + 2] * in[2];
  }
};

struct Node {
  int mesh{-1};
  std::string name;
  Mat4 local;
  std::vector<int> children;
};

std::string directoryOf(const std::string& path) {
  const std::size_t slash = path.find_last_of("/\\");
  return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

std::vector<std::uint8_t> decodeBase64(std::string_view text) {
  std::vector<int> table(256, -1);
  const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for (int i = 0; i < 64; ++i) table[static_cast<unsigned char>(alphabet[i])] = i;
  std::vector<std::uint8_t> out;
  out.reserve(text.size() * 3 / 4 + 3);
  std::uint32_t buffer = 0;
  int bits = 0;
  for (char c : text) {
    if (c == '=' ) break;
    const int v = table[static_cast<unsigned char>(c)];
    if (v < 0) continue;  // Skip whitespace and any stray characters.
    buffer = (buffer << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xff));
    }
  }
  return out;
}

Mat4 nodeLocalMatrix(const Value& node) {
  if (const Value& matrix = node["matrix"]; matrix.isArray() && matrix.count() == 16) {
    Mat4 result;
    matrix.numbers(result.m, 16);
    return result;
  }
  float t[3]{0, 0, 0}, r[4]{0, 0, 0, 1}, s[3]{1, 1, 1};
  node["translation"].numbers(t, 3);
  node["rotation"].numbers(r, 4);
  node["scale"].numbers(s, 3);
  const float x = r[0], y = r[1], z = r[2], w = r[3];
  Mat4 result;
  result.m[0] = (1 - 2 * (y * y + z * z)) * s[0];
  result.m[1] = (2 * (x * y + z * w)) * s[0];
  result.m[2] = (2 * (x * z - y * w)) * s[0];
  result.m[3] = 0;
  result.m[4] = (2 * (x * y - z * w)) * s[1];
  result.m[5] = (1 - 2 * (x * x + z * z)) * s[1];
  result.m[6] = (2 * (y * z + x * w)) * s[1];
  result.m[7] = 0;
  result.m[8] = (2 * (x * z + y * w)) * s[2];
  result.m[9] = (2 * (y * z - x * w)) * s[2];
  result.m[10] = (1 - 2 * (x * x + y * y)) * s[2];
  result.m[11] = 0;
  result.m[12] = t[0];
  result.m[13] = t[1];
  result.m[14] = t[2];
  result.m[15] = 1;
  return result;
}

MeshTopology topologyOf(int mode, std::vector<std::string>& report) {
  switch (mode) {
    case 0: return MeshTopology::Points;
    case 1: return MeshTopology::Lines;
    case 2: return MeshTopology::LineLoop;
    case 3: return MeshTopology::LineStrip;
    case 4: return MeshTopology::Triangles;
    case 5: return MeshTopology::TriangleStrip;
    case 6: return MeshTopology::TriangleFan;
    default:
      report.push_back("primitive mode " + std::to_string(mode) + " is not a glTF mode");
      return MeshTopology::Triangles;
  }
}

}  // namespace

int gltfComponentCount(std::string_view type) {
  // glTF accessor "type" is a name, not an enum. Counts match the matrix
  // layouts: MAT2x3 packs 6 floats, MAT3x2 packs 6, and so on.
  if (type == "SCALAR") return 1;
  if (type == "VEC2") return 2;
  if (type == "VEC3") return 3;
  if (type == "VEC4") return 4;
  if (type == "MAT2") return 4;
  if (type == "MAT3") return 9;
  if (type == "MAT4") return 16;
  if (type == "MAT2x3" || type == "MAT3x2") return 6;
  if (type == "MAT2x4" || type == "MAT4x2") return 8;
  if (type == "MAT3x4" || type == "MAT4x3") return 12;
  return 0;
}

int gltfComponentSize(int componentType) {
  switch (componentType) {
    case kByte: case kUnsignedByte: return 1;
    case kShort: case kUnsignedShort: return 2;
    case kUnsignedInt: case kFloat: return 4;
    default: return 0;
  }
}

namespace {

// Reads one accessor element into `out` as floats, honouring normalized
// integer formats. Returns false when the accessor is unusable.
bool readElement(const Accessor& accessor, const std::vector<BufferView>& views,
                 const std::vector<std::vector<std::uint8_t>>& buffers,
                 std::size_t index, float* out) {
  if (!accessor.valid || index >= accessor.count) return false;
  const int components = accessor.components;
  if (components <= 0 || components > 4) return false;
  const int size = gltfComponentSize(accessor.componentType);
  if (size <= 0) return false;
  if (accessor.bufferView < 0) {
    std::fill_n(out, components, 0.f); // sparse-only accessors have a zero base
    return true;
  }
  if (accessor.bufferView >= static_cast<int>(views.size())) return false;
  const BufferView& view = views[static_cast<std::size_t>(accessor.bufferView)];
  if (!view.valid || view.buffer < 0 ||
      view.buffer >= static_cast<int>(buffers.size()))
    return false;
  const std::vector<std::uint8_t>& data = buffers[static_cast<std::size_t>(view.buffer)];
  const std::size_t stride = view.byteStride ? view.byteStride : accessor.elementSize;
  if(stride<accessor.elementSize || accessor.byteOffset>view.byteLength ||
     accessor.elementSize>view.byteLength-accessor.byteOffset ||
     index>(view.byteLength-accessor.byteOffset-accessor.elementSize)/stride) return false;
  if(view.byteOffset>data.size() || view.byteLength>data.size()-view.byteOffset) return false;
  const std::size_t offset = view.byteOffset + accessor.byteOffset + index * stride;
  const std::uint8_t* p = data.data() + offset;
  for (int c = 0; c < components; ++c) {
    const std::uint8_t* q = p + static_cast<std::size_t>(c) * static_cast<std::size_t>(size);
    float value = 0;
    switch (accessor.componentType) {
      case kFloat: {
        float v;
        std::memcpy(&v, q, 4);
        value = v;
        break;
      }
      case kUnsignedByte:
        value = accessor.normalized ? static_cast<float>(*q) / 255.0f
                                    : static_cast<float>(*q);
        break;
      case kByte: {
        const auto v = static_cast<std::int8_t>(*q);
        value = accessor.normalized ? std::max(static_cast<float>(v) / 127.0f, -1.0f)
                                    : static_cast<float>(v);
        break;
      }
      case kUnsignedShort: {
        std::uint16_t v;
        std::memcpy(&v, q, 2);
        value = accessor.normalized ? static_cast<float>(v) / 65535.0f
                                    : static_cast<float>(v);
        break;
      }
      case kShort: {
        std::int16_t v;
        std::memcpy(&v, q, 2);
        value = accessor.normalized ? std::max(static_cast<float>(v) / 32767.0f, -1.0f)
                                    : static_cast<float>(v);
        break;
      }
      case kUnsignedInt: {
        std::uint32_t v;
        std::memcpy(&v, q, 4);
        value = static_cast<float>(v);
        break;
      }
      default: return false;
    }
    out[c] = value;
  }
  return true;
}

struct Loader {
  const Value* root{nullptr};
  const std::vector<std::uint8_t>* binary{};
  std::string basePath;
  Mesh mesh;
  int defaultMaterial{-1};

  std::vector<Accessor> accessors;
  std::vector<BufferView> views;
  std::vector<std::vector<std::uint8_t>> buffers;
  std::vector<Node> nodes;
  const Value* meshes{nullptr};

  void note(std::string message) {
    if (mesh.report.size() < 64) mesh.report.push_back(std::move(message));
  }

  void loadBuffers() {
    const Value& list = (*root)["buffers"];
    buffers.resize(list.count());
    for (std::size_t i = 0; i < list.count(); ++i) {
      const Value& entry = list[i];
      const std::string_view uri = entry["uri"].text();
      if (uri.empty()) {
        // No URI means the GLB binary chunk; the caller guarantees it exists.
        if (!binary) {
          note("buffer " + std::to_string(i) + " has no URI and no binary chunk");
          continue;
        }
        buffers[i] = *binary;
        continue;
      }
      if (uri.rfind("data:", 0) == 0) {
        const std::size_t comma = uri.find(',');
        if (comma == std::string_view::npos) {
          note("buffer " + std::to_string(i) + " has a malformed data URI");
          continue;
        }
        const std::string_view meta = uri.substr(5, comma - 5);
        const std::string_view payload = uri.substr(comma + 1);
        if (meta.find(";base64") == std::string_view::npos) {
          note("buffer " + std::to_string(i) + " data URI is not base64");
          continue;
        }
        // decodeBase64 skips whitespace, so plain and wrapped payloads both work.
        buffers[i] = decodeBase64(payload);
        continue;
      }
      const std::string path = basePath + std::string(uri);
      std::ifstream file(path, std::ios::binary);
      if (!file) {
        note("buffer file not found: " + path);
        continue;
      }
      buffers[i] = std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                             std::istreambuf_iterator<char>());
    }
  }

  void loadViews() {
    const Value& list = (*root)["bufferViews"];
    views.resize(list.count());
    for (std::size_t i = 0; i < list.count(); ++i) {
      const Value& entry = list[i];
      BufferView& view = views[i];
      view.buffer = entry["buffer"].integer();
      view.byteOffset = static_cast<std::size_t>(entry["byteOffset"].number());
      view.byteLength = static_cast<std::size_t>(entry["byteLength"].number());
      view.byteStride = static_cast<std::size_t>(entry["byteStride"].number());
      view.valid = view.buffer >= 0 && !buffers.empty();
    }
  }

  void loadAccessors() {
    const Value& list = (*root)["accessors"];
    accessors.resize(list.count());
    for (std::size_t i = 0; i < list.count(); ++i) {
      const Value& entry = list[i];
      Accessor& accessor = accessors[i];
      accessor.bufferView = entry.has("bufferView") ? entry["bufferView"].integer() : -1;
      accessor.byteOffset = static_cast<std::size_t>(entry["byteOffset"].number());
      accessor.componentType = entry["componentType"].integer();
      accessor.normalized = entry["normalized"].boolean();
      accessor.count = static_cast<std::size_t>(entry["count"].number());
      accessor.components = gltfComponentCount(entry["type"].text());
      const int size = gltfComponentSize(accessor.componentType);
      if (accessor.components <= 0 || size <= 0) {
        note("accessor " + std::to_string(i) + " has an unsupported type");
        continue;
      }
      accessor.elementSize = static_cast<std::size_t>(accessor.components * size);
      accessor.valid = true;
      // A view with no bufferView is legal for sparse-only accessors, which
      // read as zero wherever no sparse replacement is supplied.
      if (accessor.bufferView >= 0 && accessor.bufferView < static_cast<int>(views.size()) &&
          !views[static_cast<std::size_t>(accessor.bufferView)].valid) {
        note("accessor " + std::to_string(i) + " references an invalid bufferView");
        accessor.valid = false;
        continue;
      }
      if (const Value& sparse = entry["sparse"]; !sparse.isNull()) {
        loadSparse(i, sparse);
      }
    }
  }

  void loadSparse(std::size_t accessorIndex, const Value& sparse) {
    Accessor& accessor = accessors[accessorIndex];
    const std::size_t n = static_cast<std::size_t>(sparse["count"].number());
    const Value& indices = sparse["indices"];
    const Value& values = sparse["values"];
    Accessor indexAccessor;
    indexAccessor.bufferView = indices["bufferView"].integer();
    indexAccessor.byteOffset = static_cast<std::size_t>(indices["byteOffset"].number());
    indexAccessor.componentType = indices["componentType"].integer();
    indexAccessor.count = n;
    indexAccessor.components = 1;
    indexAccessor.elementSize = static_cast<std::size_t>(gltfComponentSize(indexAccessor.componentType));
    indexAccessor.valid = indexAccessor.elementSize > 0;

    Accessor valueAccessor;
    valueAccessor.bufferView = values["bufferView"].integer();
    valueAccessor.byteOffset = static_cast<std::size_t>(values["byteOffset"].number());
    valueAccessor.componentType = accessor.componentType;
    valueAccessor.count = n;
    valueAccessor.components = accessor.components;
    valueAccessor.elementSize = accessor.elementSize;
    valueAccessor.valid = true;

    if (!indexAccessor.valid || valueAccessor.bufferView < 0) {
      note("accessor " + std::to_string(accessorIndex) + " has an unreadable sparse block");
      return;
    }
    const int components = accessor.components;
    accessor.sparseValues.assign(n * static_cast<std::size_t>(components), 0.0f);
    accessor.sparseIndices.assign(n, 0);
    float scratch[4]{};
    for (std::size_t i = 0; i < n; ++i) {
      if (!readElement(indexAccessor, views, buffers, i, scratch)) {
        note("accessor " + std::to_string(accessorIndex) + " sparse index read failed");
        return;
      }
      accessor.sparseIndices[i] = static_cast<std::size_t>(scratch[0]);
      if (!readElement(valueAccessor, views, buffers, i, scratch)) {
        note("accessor " + std::to_string(accessorIndex) + " sparse value read failed");
        return;
      }
      for (int c = 0; c < components && c < 4; ++c)
        accessor.sparseValues[i * static_cast<std::size_t>(components) +
                              static_cast<std::size_t>(c)] = scratch[c];
    }
  }

  // Reads accessor element `index`, applying sparse replacements when present.
  bool read(int accessorIndex, std::size_t index, float* out) {
    if (accessorIndex < 0 || accessorIndex >= static_cast<int>(accessors.size())) return false;
    const Accessor& accessor = accessors[static_cast<std::size_t>(accessorIndex)];
    const int components = accessor.components;
    if (!accessor.valid || index>=accessor.count || components <= 0 || components > 4) return false;
    if (!accessor.sparseIndices.empty()) {
      const auto it = std::lower_bound(accessor.sparseIndices.begin(),
                                       accessor.sparseIndices.end(), index);
      if (it != accessor.sparseIndices.end() && *it == index) {
        const std::size_t slot = static_cast<std::size_t>(it - accessor.sparseIndices.begin());
        for (int c = 0; c < components; ++c)
          out[c] = accessor.sparseValues[slot * static_cast<std::size_t>(components) +
                                         static_cast<std::size_t>(c)];
        return true;
      }
    }
    return readElement(accessor, views, buffers, index, out);
  }

  static const Value& pbrTexture(const Value& entry,const char* name) { return entry["pbrMetallicRoughness"][name]; }
  void loadMaterials() {
    const Value& list = (*root)["materials"];
    mesh.materials.resize(list.count());
    for (std::size_t i = 0; i < list.count(); ++i) {
      const Value& entry = list[i];
      Material& material = mesh.materials[i];
      material.name = std::string(entry["name"].text());
      if (const Value& pbr = entry["pbrMetallicRoughness"]; pbr.isObject()) {
        pbr["baseColorFactor"].numbers(material.baseColor, 4);
        material.metallic = pbr["metallicFactor"].numberf();
        material.roughness = pbr["roughnessFactor"].numberf();
        if (pbr["baseColorTexture"].isObject())
          material.baseColorTexture = pbr["baseColorTexture"]["index"].integer();
        if (pbr["metallicRoughnessTexture"].isObject())
          material.metallicRoughnessTexture = pbr["metallicRoughnessTexture"]["index"].integer();
        {
          // glTF default baseColorFactor is [1,1,1,1] and metallic/roughness 1.
          if (!pbr.has("baseColorFactor")) {
            material.baseColor[0] = material.baseColor[1] = material.baseColor[2] = 1;
            material.baseColor[3] = 1;
          }
          if (!pbr.has("metallicFactor")) material.metallic = 1;
          if (!pbr.has("roughnessFactor")) material.roughness = 1;
        }
      } else {
        // The spec's default material is fully rough white.
        material.baseColor[0] = material.baseColor[1] = material.baseColor[2] = 1;
        material.baseColor[3] = 1;
        material.metallic = 1;
        material.roughness = 1;
      }
      // Only TEXCOORD_0 is implemented. Required UV sets fail explicitly.
      for(const auto& info : {pbrTexture(entry,"baseColorTexture"), pbrTexture(entry,"metallicRoughnessTexture"),
                             entry["normalTexture"],entry["occlusionTexture"],entry["emissiveTexture"]})
        if(info.isObject() && info.has("texCoord") && info["texCoord"].integer()!=0)
          throw std::runtime_error("glTF: material '"+material.name+"' requires unsupported UV set");
      entry["emissiveFactor"].numbers(material.emissive, 3);
      if (entry["emissiveTexture"].isObject())
        material.emissiveTexture = entry["emissiveTexture"]["index"].integer();
      if (entry["normalTexture"].isObject()) {
        material.normalTexture = entry["normalTexture"]["index"].integer();
        material.normalScale = entry["normalTexture"].has("scale") ? entry["normalTexture"]["scale"].numberf() : 1.f;
        if (entry["normalTexture"]["texCoord"].integer() != 0)
          note("material '" + material.name + "': normal map requires unsupported UV set");
      }
      material.doubleSided = entry["doubleSided"].boolean();
      if(entry["occlusionTexture"].isObject()) {
        material.occlusionTexture=entry["occlusionTexture"]["index"].integer();
        material.occlusionStrength=entry["occlusionTexture"].has("strength")?
            entry["occlusionTexture"]["strength"].numberf():1.f;
        if(entry["occlusionTexture"]["texCoord"].integer()!=0)
          note("material '"+material.name+"': occlusion map requires unsupported UV set");
      }
      material.environmentReflection=std::clamp(entry["extras"]["ofs_environment_reflection"].numberf(),0.f,1.f);
      const std::string_view alpha = entry["alphaMode"].text();
      if (alpha == "MASK") material.alpha = Material::Alpha::Mask;
      else if (alpha == "BLEND") material.alpha = Material::Alpha::Blend;
      material.alphaCutoff = entry.has("alphaCutoff") ? entry["alphaCutoff"].numberf() : .5f;
      if (const Value& extensions = entry["extensions"]; extensions.isObject()) {
        if (extensions["KHR_materials_clearcoat"].isObject())
          note("material '" + material.name +
               "': KHR_materials_clearcoat ignored");
        if (extensions["KHR_materials_emissive_strength"].isObject())
          note("material '" + material.name + "': KHR_materials_emissive_strength ignored");
      }
    }
  }

  void loadTextures() {
    const auto& list = (*root)["images"];
    mesh.images.resize(list.count());
    for (std::size_t i = 0; i < list.count(); ++i) {
      const auto& entry = list[i];
      std::vector<std::uint8_t> encoded;
      const auto uri = entry["uri"].text();
      if (uri.starts_with("data:")) {
        const auto comma = uri.find(',');
        if (comma != std::string_view::npos) encoded = decodeBase64(uri.substr(comma + 1));
      } else if (!uri.empty()) {
        std::ifstream file(basePath + std::string(uri), std::ios::binary);
        if (file) encoded.assign(std::istreambuf_iterator<char>(file), {});
      } else if (entry.has("bufferView")) {
        const int index = entry["bufferView"].integer();
        if (index >= 0 && index < static_cast<int>(views.size())) {
          const auto& view = views[index];
          if (view.buffer >= 0 && view.buffer < static_cast<int>(buffers.size())) {
            const auto& bytes = buffers[view.buffer];
            if (view.byteOffset <= bytes.size() && view.byteLength <= bytes.size() - view.byteOffset)
              encoded.assign(bytes.begin() + view.byteOffset, bytes.begin() + view.byteOffset + view.byteLength);
          }
        }
      }
      try {
        mesh.images[i] = decodeImage(encoded);
        mesh.images[i].name = std::string(entry["name"].text());
        mesh.images[i].source = !uri.empty() && !uri.starts_with("data:") ? basePath+std::string(uri)
          : "embedded["+std::to_string(i)+"]:"+mesh.images[i].name;
      } catch (const std::exception& error) {
        note("image " + std::to_string(i) + ": " + error.what());
      }
    }
    const auto& textures = (*root)["textures"];
    for (std::size_t i = 0; i < textures.count(); ++i) {
      Texture texture;
      texture.image = textures[i]["source"].integer();
      const auto samplerIndex = textures[i]["sampler"].integer();
      const auto& sampler = (*root)["samplers"][static_cast<std::size_t>(std::max(0, samplerIndex))];
      if (samplerIndex >= 0) {
        if (sampler.has("wrapS")) texture.wrapS = sampler["wrapS"].integer();
        if (sampler.has("wrapT")) texture.wrapT = sampler["wrapT"].integer();
        if (sampler.has("minFilter")) texture.minFilter = sampler["minFilter"].integer();
        if (sampler.has("magFilter")) texture.magFilter = sampler["magFilter"].integer();
      }
      mesh.textures.push_back(texture);
    }
  }

  void loadNodes() {
    const Value& list = (*root)["nodes"];
    nodes.resize(list.count());
    mesh.nodes.resize(list.count());
    for (std::size_t i = 0; i < list.count(); ++i) {
      const Value& entry = list[i];
      Node& node = nodes[i];
      node.name = std::string(entry["name"].text());
      node.local = nodeLocalMatrix(entry);
      auto& preserved = mesh.nodes[i];
      preserved.name = node.name;
      std::copy_n(node.local.m, 16, preserved.local.begin());
      preserved.channel = std::string(entry["extras"]["ofs_channel"].text());
      entry["extras"]["ofs_axis"].numbers(preserved.axis.data(), 3);
      entry["extras"]["ofs_slide"].numbers(preserved.slide.data(), 3);
      if (entry["extras"].has("ofs_gain")) preserved.gain = entry["extras"]["ofs_gain"].numberf();
      if (entry.has("mesh")) node.mesh = entry["mesh"].integer();
      preserved.mesh = node.mesh;
      if (entry.has("skin")) note("node '" + node.name + "' uses a skin; skinned meshes are unsupported");
      if (entry.has("weights")) note("node '" + node.name + "' has morph weights; morph targets are unsupported");
      const Value& children = entry["children"];
      for (std::size_t c = 0; c < children.count(); ++c) {
        const int child = children[c].integer();
        if (child >= 0 && child < static_cast<int>(nodes.size()))
          node.children.push_back(child);
      }
      preserved.children = node.children;
    }
    for (std::size_t i = 0; i < nodes.size(); ++i)
      for (int child : nodes[i].children) {
        if (mesh.nodes[child].parent >= 0) throw std::runtime_error("glTF: node has multiple parents");
        mesh.nodes[child].parent = static_cast<int>(i);
      }
    meshes = &(*root)["meshes"];
    if (!(*root)["animations"].isNull() || (*root)["animations"].count())
      note("the document contains animations; this loader does not evaluate them");
    if ((*root)["skins"].count()) note("the document contains skins; they are unsupported");
  }

  void addPrimitive(int nodeIndex, const Mat4& world, int meshIndex, int transformNode) {
    if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes->count())) return;
    const Value& definition = (*meshes)[static_cast<std::size_t>(meshIndex)];
    for (std::size_t p = 0; p < definition["primitives"].count(); ++p) {
      const Value& source = definition["primitives"][p];
      const int mode = source.has("mode") ? source["mode"].integer() : 4;
      Primitive primitive;
      primitive.name = nodes[static_cast<std::size_t>(nodeIndex)].name;
      primitive.node = nodeIndex;
      primitive.transformNode = transformNode;
      primitive.topology = topologyOf(mode, mesh.report);
      if(source.has("material")) primitive.material = source["material"].u32();
      else {
        if(defaultMaterial<0) { Material material; material.name="__gltf_default";defaultMaterial=mesh.materials.size();mesh.materials.push_back(material); }
        primitive.material=defaultMaterial;
      }
      if (primitive.material >= mesh.materials.size()) {
        note("primitive of node '" + primitive.name + "' references an unknown material");
        primitive.material = 0;
      }

      const Value& attributes = source["attributes"];
      if (!attributes["POSITION"].isNumber()) {
        note("primitive of node '" + primitive.name + "' has no POSITION attribute");
        continue;
      }
      // Count comes from POSITION, per the spec.
      const int positionAccessor = attributes["POSITION"].integer();
      if (positionAccessor < 0 || positionAccessor >= static_cast<int>(accessors.size()) ||
          !accessors[static_cast<std::size_t>(positionAccessor)].valid) {
        note("primitive of node '" + primitive.name + "' has an unreadable POSITION accessor");
        continue;
      }
      const std::size_t vertexCount = accessors[static_cast<std::size_t>(positionAccessor)].count;
      if (vertexCount == 0) continue;

      const int normalAccessor = attributes["NORMAL"].integer();
      const int tangentAccessor = attributes["TANGENT"].integer();
      const int uvAccessor = attributes["TEXCOORD_0"].integer();
      primitive.hasNormals = normalAccessor >= 0;
      primitive.hasTangents = tangentAccessor >= 0;
      primitive.hasUv = uvAccessor >= 0;

      primitive.vertices.resize(vertexCount * kGltfVertexFloats);
      bool complete = true;
      for (std::size_t v = 0; v < vertexCount; ++v) {
        float* out = primitive.vertices.data() + v * kGltfVertexFloats;
        if (!read(positionAccessor, v, out)) { complete = false; break; }
        out[3] = out[4] = out[5] = 0.0f;
        if (primitive.hasNormals) {
          float n[4]{};
          if (!read(normalAccessor, v, n)) { complete = false; break; }
          const double length = std::sqrt(double(n[0]) * n[0] + double(n[1]) * n[1] + double(n[2]) * n[2]);
          if (length > 1e-8) {
            const float inv = static_cast<float>(1.0 / length);
            n[0] *= inv; n[1] *= inv; n[2] *= inv;
          } else {
            n[0] = 0; n[1] = 1; n[2] = 0;
          }
          out[3] = n[0]; out[4] = n[1]; out[5] = n[2];
        }
        if (primitive.hasUv) {
          float t[4]{};
          read(uvAccessor, v, t);
          out[6] = t[0]; out[7] = t[1];
        }
        // Tangents are transformed as directions (w handedness preserved).
        out[8] = out[9] = out[10] = 0.0f;
        out[11] = 1.0f;
        if (primitive.hasTangents) {
          float t[4]{};
          if (read(tangentAccessor, v, t)) {
            const double length = std::sqrt(double(t[0]) * t[0] + double(t[1]) * t[1] + double(t[2]) * t[2]);
            if (length > 1e-8) {
              const float inv = static_cast<float>(1.0 / length);
              out[8] = t[0] * inv; out[9] = t[1] * inv; out[10] = t[2] * inv;
            }
            out[11] = t[3];
          }
        }
      }
      if (!complete) {
        note("primitive of node '" + primitive.name + "' had a truncated attribute stream");
        continue;
      }

      if (const Value& indexValue = source["indices"]; indexValue.isNumber()) {
        const int indexAccessor = indexValue.integer();
        if (indexAccessor >= 0 && indexAccessor < static_cast<int>(accessors.size()) &&
            accessors[static_cast<std::size_t>(indexAccessor)].valid) {
          const std::size_t indexCount = accessors[static_cast<std::size_t>(indexAccessor)].count;
          primitive.indices.reserve(indexCount);
          for (std::size_t i = 0; i < indexCount; ++i) {
            float v[4]{};
            if (!read(indexAccessor, i, v)) { complete = false; break; }
            const auto index = static_cast<std::uint32_t>(v[0]);
            if (index >= vertexCount) { complete = false; break; }
            primitive.indices.push_back(index);
          }
        }
      }
      if (!complete) {
        note("primitive of node '" + primitive.name + "' had an out-of-range index");
        continue;
      }
      if (primitive.indices.empty()) {
        // Non-indexed geometry is legal; synthesise a sequential index list.
        primitive.indices.resize(vertexCount);
        for (std::size_t i = 0; i < vertexCount; ++i)
          primitive.indices[i] = static_cast<std::uint32_t>(i);
      }
      primitive.sourcePrimitiveCount = static_cast<std::uint32_t>(primitive.indices.size());

      // Bake the node transform and measure the transformed bounds.
      for (std::size_t v = 0; v < vertexCount; ++v) {
        float* out = primitive.vertices.data() + v * kGltfVertexFloats;
        float p[3]{out[0], out[1], out[2]};
        float transformed[3]{};
        world.transformPoint(p, transformed);
        out[0] = transformed[0]; out[1] = transformed[1]; out[2] = transformed[2];
        if (primitive.hasNormals) {
          float n[3]{out[3], out[4], out[5]};
          float rotated[3]{};
          world.transformDirection(n, rotated);
          const double length = std::sqrt(double(rotated[0]) * rotated[0] +
                                          double(rotated[1]) * rotated[1] +
                                          double(rotated[2]) * rotated[2]);
          if (length > 1e-8) {
            const float inv = static_cast<float>(1.0 / length);
            out[3] = rotated[0] * inv; out[4] = rotated[1] * inv; out[5] = rotated[2] * inv;
          }
        }
        if (primitive.hasTangents && (out[8] != 0.0f || out[9] != 0.0f || out[10] != 0.0f)) {
          float t[3]{out[8], out[9], out[10]};
          float rotated[3]{};
          // Tangents use the linear transform, unlike inverse-transpose normals.
          for(int axis=0;axis<3;++axis) rotated[axis]=world.m[axis]*t[0]+world.m[4+axis]*t[1]+world.m[8+axis]*t[2];
          const double det=world.m[0]*(world.m[5]*world.m[10]-world.m[6]*world.m[9])-
            world.m[4]*(world.m[1]*world.m[10]-world.m[2]*world.m[9])+world.m[8]*(world.m[1]*world.m[6]-world.m[2]*world.m[5]);
          if(det<0)out[11]=-out[11];
          const double length = std::sqrt(double(rotated[0]) * rotated[0] +
                                          double(rotated[1]) * rotated[1] +
                                          double(rotated[2]) * rotated[2]);
          if (length > 1e-8) {
            const float inv = static_cast<float>(1.0 / length);
            out[8] = rotated[0] * inv; out[9] = rotated[1] * inv; out[10] = rotated[2] * inv;
          }
        }
        for (int c = 0; c < 3; ++c) {
          if (v == 0 || transformed[c] < primitive.boundsMin[c]) primitive.boundsMin[c] = transformed[c];
          if (v == 0 || transformed[c] > primitive.boundsMax[c]) primitive.boundsMax[c] = transformed[c];
        }
      }
      if (primitive.hasNormals || primitive.hasTangents) mesh.vertexCount += vertexCount;
      switch (primitive.topology) {
        case MeshTopology::Triangles: mesh.triangleCount += primitive.indices.size() / 3; break;
        case MeshTopology::TriangleStrip:
        case MeshTopology::TriangleFan:
          mesh.triangleCount += primitive.indices.size() >= 3 ? primitive.indices.size() - 2 : 0;
          break;
        default: break;
      }
      mesh.primitives.push_back(std::move(primitive));
    }
  }

  void traverse(int nodeIndex, const Mat4& parent, int transformNode = -1, unsigned depth = 0) {
    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size())) return;
    const Node& node = nodes[static_cast<std::size_t>(nodeIndex)];
    auto& preserved = mesh.nodes[nodeIndex];
    if (preserved.active || depth > nodes.size()) throw std::runtime_error("glTF: cyclic or duplicate scene node");
    preserved.active = true;
    if (!preserved.channel.empty()) transformNode = nodeIndex;
    const Mat4 world = Mat4::multiply(parent, node.local);
    std::copy_n(world.m, 16, preserved.world.begin());
    if (node.mesh >= 0) addPrimitive(nodeIndex, world, node.mesh, transformNode);
    for (int child : node.children) traverse(child, world, transformNode, depth + 1);
  }

  void run() {
    loadBuffers();
    loadViews();
    loadAccessors();
    loadMaterials();
    loadTextures();
    loadNodes();
    mesh.boundsMin[0] = mesh.boundsMin[1] = mesh.boundsMin[2] = std::numeric_limits<float>::max();
    mesh.boundsMax[0] = mesh.boundsMax[1] = mesh.boundsMax[2] = -std::numeric_limits<float>::max();
    const Value& scenes = (*root)["scenes"];
    const int sceneIndex = (*root)["scene"].integer();
    const Value& roots = (sceneIndex >= 0 && sceneIndex < static_cast<int>(scenes.count()))
                             ? scenes[static_cast<std::size_t>(sceneIndex)]["nodes"]
                             : (scenes.count() ? scenes[0]["nodes"] : Value{});
    if (roots.isArray() && roots.count()) {
      for (std::size_t i = 0; i < roots.count(); ++i)
        traverse(roots[i].integer(), Mat4::identity());
    } else {
      // No usable scene: fall back to the whole node list so nothing is lost.
      note("no scene graph; traversed every node");
      for (std::size_t i = 0; i < nodes.size(); ++i)
        if (mesh.nodes[i].parent < 0) traverse(static_cast<int>(i), Mat4::identity());
    }
    if (mesh.primitives.empty())
      throw std::runtime_error("glTF: the document contains no usable geometry");
    for (const Primitive& primitive : mesh.primitives)
      for (int c = 0; c < 3; ++c) {
        mesh.boundsMin[c] = std::min(mesh.boundsMin[c], primitive.boundsMin[c]);
        mesh.boundsMax[c] = std::max(mesh.boundsMax[c], primitive.boundsMax[c]);
      }
  }
};

}  // namespace

static void validateDocument(Loader& loader) {
  const auto& root=*loader.root;
  if (!root.isObject()) throw std::runtime_error("glTF: root is not an object");
  if(const auto& required=root["extensionsRequired"];required.isArray() && required.count())
    throw std::runtime_error("glTF: unsupported required extension '"+std::string(required[0].text())+"'");
  if (const Value& extensions = root["extensionsUsed"]; extensions.isArray()) {
    for (std::size_t i = 0; i < extensions.count(); ++i) {
      const std::string_view name = extensions[i].text();
      // clearcoat is a factor-only extension that this loader can safely skip.
      if (name != "KHR_materials_clearcoat")
        loader.note("extension '" + std::string(name) + "' is not supported");
    }
  }
  if (const Value& asset = root["asset"]; asset.isObject()) {
    const std::string_view version = asset["version"].text();
    if (!version.empty() && version.rfind("2.", 0) != 0)
      throw std::runtime_error("glTF: unsupported asset version " + std::string(version));
  }
}

Mesh parseGltf(std::string_view document, const std::vector<std::uint8_t>& binary) {
  json::Parser parser(document);
  Loader loader;
  loader.root = &parser.root();
  loader.binary = &binary;
  validateDocument(loader);
  loader.run();
  return std::move(loader.mesh);
}

Mesh loadGltf(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("glTF: cannot open " + path);
  std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
  if (data.size() < 12) throw std::runtime_error("glTF: file is too small: " + path);

  std::string_view json;
  std::vector<std::uint8_t> binary;
  bool isGlb = data.size() >= 4 && data[0] == 'g' && data[1] == 'l' && data[2] == 'T' && data[3] == 'F';
  if (isGlb) {
    std::uint32_t version = 0, length = 0;
    std::memcpy(&version, data.data() + 4, 4);
    std::memcpy(&length, data.data() + 8, 4);
    if (version != 2) throw std::runtime_error("glTF: unsupported GLB version " + std::to_string(version));
    if (length > data.size()) length = static_cast<std::uint32_t>(data.size());
    std::size_t offset = 12;
    bool haveJson = false;
    while (offset + 8 <= length) {
      std::uint32_t chunkLength = 0, chunkType = 0;
      std::memcpy(&chunkLength, data.data() + offset, 4);
      std::memcpy(&chunkType, data.data() + offset + 4, 4);
      offset += 8;
      if (offset + chunkLength > length) chunkLength = static_cast<std::uint32_t>(length - offset);
      if (chunkType == 0x4e4f534a) {  // 'JSON'
        json = std::string_view(reinterpret_cast<const char*>(data.data() + offset), chunkLength);
        haveJson = true;
      } else if (chunkType == 0x004e4942) {  // 'BIN\0'
        binary.assign(data.begin() + static_cast<std::ptrdiff_t>(offset),
                      data.begin() + static_cast<std::ptrdiff_t>(offset + chunkLength));
      }
      // Chunks are 4-byte aligned.
      offset += chunkLength;
      offset += (4 - (chunkLength & 3)) & 3;
    }
    if (!haveJson) throw std::runtime_error("glTF: GLB has no JSON chunk: " + path);
  } else {
    json = std::string_view(reinterpret_cast<const char*>(data.data()), data.size());
  }
  json::Parser parser(json);
  Loader loader;
  loader.root = &parser.root();
  loader.binary = &binary;
  loader.basePath = directoryOf(path);
  validateDocument(loader);
  loader.run();
  return std::move(loader.mesh);
}

}  // namespace ofs::client
