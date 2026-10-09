// Renderer: terrain, lakes, the airfield and the forest. See renderer.hpp for
// the frame graph.

#include "renderer.hpp"
#include "renderer_internal.hpp"

#include "airfield.hpp"
#include "coordinates.hpp"
#include "log.hpp"
#include "ofs/terrain.hpp"
#include "procedural.hpp"
#include "scenery.hpp"
#include "texture_mips.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

namespace ofs::client {
namespace {

// Terrain is drawn to this radius; beyond it the sky pass continues the plane.
constexpr double kTerrainDrawRadius = 262000.;

struct TerrainVertex {
  float x, y, z, nx, ny, nz;
};

// Emits a triangle list, orienting both triangles toward the supplied normal.
void addQuad(std::vector<SurfaceVertex>& out, const glm::vec3& a, const glm::vec3& b,
             const glm::vec3& c, const glm::vec3& d, const glm::vec3& normal) {
  const glm::vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  const glm::vec3* corners[4] = {&a, &b, &c, &d};
  const bool reverse = glm::dot(glm::cross(b - a, c - a), normal) < 0;
  const int forward[] = {0, 1, 2, 0, 2, 3};
  const int backward[] = {0, 2, 1, 0, 3, 2};
  for (const int i : (reverse ? backward : forward))
    out.push_back({corners[i]->x, corners[i]->y, corners[i]->z, normal.x, normal.y, normal.z,
                   uv[i].x, uv[i].y});
}

// How each kind of airfield structure is drawn. Paint and cladding are plain
// colours weathered in the surface shader; lamps are small emissive boxes.
struct StructureLook {
  glm::vec4 color;
  float metallic, roughness, detail;
  glm::vec3 emissive;
  float glass;
};
StructureLook lookOf(AirfieldMaterial material) {
  switch (material) {
    case AirfieldMaterial::Concrete: return {{.47f, .47f, .45f, 1}, 0, .86f, 2, {}, 0};
    case AirfieldMaterial::Cladding: return {{.33f, .37f, .41f, 1}, .25f, .55f, 2, {}, 0};
    case AirfieldMaterial::Roof: return {{.14f, .15f, .16f, 1}, 0, .8f, 1, {}, 0};
    case AirfieldMaterial::Shelter: return {{.30f, .32f, .27f, 1}, 0, .92f, 2, {}, 0};
    case AirfieldMaterial::Glass: return {{.018f, .028f, .036f, 1}, 0, .06f, 0, {}, 1};
    case AirfieldMaterial::Dark: return {{.025f, .026f, .03f, 1}, 0, .9f, 0, {}, 0};
    case AirfieldMaterial::White: return {{.80f, .80f, .77f, 1}, 0, .6f, 1, {}, 0};
    case AirfieldMaterial::Red: return {{.62f, .07f, .045f, 1}, 0, .6f, 1, {}, 0};
    case AirfieldMaterial::Steel: return {{.38f, .40f, .42f, 1}, .6f, .5f, 1, {}, 0};
    case AirfieldMaterial::Olive: return {{.17f, .20f, .11f, 1}, 0, .75f, 1, {}, 0};
    case AirfieldMaterial::Yellow: return {{.74f, .52f, .06f, 1}, 0, .6f, 1, {}, 0};
    case AirfieldMaterial::CarLight: return {{.66f, .68f, .70f, 1}, .3f, .4f, 0, {}, 0};
    case AirfieldMaterial::CarDark: return {{.07f, .08f, .10f, 1}, .3f, .35f, 0, {}, 0};
    case AirfieldMaterial::LightWhite: return {{.9f, .9f, .85f, 1}, 0, .5f, 0, {1.9f, 1.8f, 1.5f}, 0};
    case AirfieldMaterial::LightAmber: return {{.9f, .6f, .2f, 1}, 0, .5f, 0, {1.9f, 1.05f, .12f}, 0};
    case AirfieldMaterial::LightRed: return {{.9f, .15f, .1f, 1}, 0, .5f, 0, {1.9f, .10f, .05f}, 0};
    case AirfieldMaterial::LightGreen: return {{.2f, .85f, .35f, 1}, 0, .5f, 0, {.08f, 1.7f, .30f}, 0};
    case AirfieldMaterial::LightBlue: return {{.2f, .4f, .95f, 1}, 0, .5f, 0, {.06f, .30f, 2.1f}, 0};
    case AirfieldMaterial::Count: break;
  }
  return {{.5f, .5f, .5f, 1}, 0, .8f, 0, {}, 0};
}

}  // namespace

// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

void Renderer::createEnvironmentTextures(Synthesis& data) {
  // Material layers: two arrays so one sampler pair serves every material.
  const auto& layers = data.layers;
  const int layerSize = layers.size;
  const std::uint64_t filtered = settings_.anisotropic ? BGFX_SAMPLER_MIN_ANISOTROPIC : 0;
  terrainAlbedo_ = bgfx::createTexture2D(static_cast<std::uint16_t>(layerSize), static_cast<std::uint16_t>(layerSize), true,
                                         procedural::kTerrainLayerCount, bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_SRGB | filtered);
  terrainNormal_ = bgfx::createTexture2D(static_cast<std::uint16_t>(layerSize), static_cast<std::uint16_t>(layerSize), true,
                                         procedural::kTerrainLayerCount, bgfx::TextureFormat::RGBA8, filtered);
  if (!bgfx::isValid(terrainAlbedo_) || !bgfx::isValid(terrainNormal_))
    throw std::runtime_error("Terrain material array creation failed");
  const auto uploadLayers = [&](bgfx::TextureHandle texture, const std::vector<std::vector<std::uint8_t>>& data, TextureRole role) {
    for (std::uint16_t layer = 0; layer < data.size(); ++layer) {
      const auto chain = textureMipChain(data[layer], layerSize, layerSize, role);
      std::size_t offset = 0;
      int size = layerSize;
      for (std::uint8_t mip = 0; size >= 1; ++mip, size /= 2) {
        const std::size_t bytes = std::size_t(size) * size * 4;
        bgfx::updateTexture2D(texture, layer, mip, 0, 0, static_cast<std::uint16_t>(size), static_cast<std::uint16_t>(size),
                              bgfx::copy(chain.data() + offset, static_cast<std::uint32_t>(bytes)));
        offset += bytes;
        if (size == 1) break;
      }
      auxiliaryTextureBytes_ += chain.size();
    }
  };
  uploadLayers(terrainAlbedo_, layers.albedoHeight, TextureRole::Srgb);
  uploadLayers(terrainNormal_, layers.normalRoughness, TextureRole::Linear);

  // Land cover: mirrored past its edge so far terrain keeps a plausible pattern.
  const int landSize = landscape_->landSize();
  const auto landChain = textureMipChain(landscape_->landTexels(), landSize, landSize, TextureRole::Linear);
  landMap_ = bgfx::createTexture2D(static_cast<std::uint16_t>(landSize), static_cast<std::uint16_t>(landSize), true, 1,
                                   bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_MIRROR | BGFX_SAMPLER_V_MIRROR,
                                   bgfx::copy(landChain.data(), static_cast<std::uint32_t>(landChain.size())));
  // Lake level per basin. Point sampled: the level is constant across a lake
  // and the shoreline comes from comparing it with the terrain height per pixel.
  const int lakeSize = landscape_->lakeSize();
  const auto& lake = landscape_->lakeTexels();
  lakeMap_ = bgfx::createTexture2D(static_cast<std::uint16_t>(lakeSize), static_cast<std::uint16_t>(lakeSize), false, 1,
                                   bgfx::TextureFormat::R32F,
                                   BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
                                   bgfx::copy(lake.data(), static_cast<std::uint32_t>(lake.size() * sizeof(float))));
  // The shape of the ground, for slopes and long shadows. Each level of the
  // chain is the mean of the one below, so distant relief is lit as smoothly
  // as it is seen.
  {
    int size = landscape_->heightSize();
    std::vector<std::uint16_t> level = landscape_->heightTexels();
    heightMapLevels_ = 1;
    for (int s = size; s > 4; s /= 2) ++heightMapLevels_;
    heightMap_ = bgfx::createTexture2D(static_cast<std::uint16_t>(size), static_cast<std::uint16_t>(size), true, 1,
                                       bgfx::TextureFormat::R16, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    if (!bgfx::isValid(heightMap_)) throw std::runtime_error("Terrain height map creation failed");
    for (std::uint8_t mip = 0;; ++mip) {
      bgfx::updateTexture2D(heightMap_, 0, mip, 0, 0, static_cast<std::uint16_t>(size), static_cast<std::uint16_t>(size),
                            bgfx::copy(level.data(), static_cast<std::uint32_t>(level.size() * sizeof(std::uint16_t))));
      auxiliaryTextureBytes_ += level.size() * sizeof(std::uint16_t);
      if (size == 1) break;
      const int half = size / 2;
      std::vector<std::uint16_t> next(std::size_t(half) * half);
      for (int y = 0; y < half; ++y) for (int x = 0; x < half; ++x) {
        const auto at = [&](int dx, int dy) { return std::uint32_t(level[std::size_t(2 * y + dy) * size + 2 * x + dx]); };
        next[std::size_t(y) * half + x] = std::uint16_t((at(0, 0) + at(1, 0) + at(0, 1) + at(1, 1) + 2) / 4);
      }
      level = std::move(next);
      size = half;
    }
  }
  const auto waterChain = textureMipChain(data.waterNormal, kWaterTileSize, kWaterTileSize, TextureRole::Linear);
  waterNormal_ = bgfx::createTexture2D(kWaterTileSize, kWaterTileSize, true, 1, bgfx::TextureFormat::RGBA8, filtered,
                                       bgfx::copy(waterChain.data(), static_cast<std::uint32_t>(waterChain.size())));
  // The navigation map the HUD draws. Clamped: the HUD only shows the part of
  // its frame that the picture covers.
  mapTexture_ = bgfx::createTexture2D(static_cast<std::uint16_t>(data.map.size), static_cast<std::uint16_t>(data.map.size),
                                      false, 1, bgfx::TextureFormat::RGBA8, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                                      bgfx::copy(data.map.rgba.data(), static_cast<std::uint32_t>(data.map.rgba.size())));
  if (!bgfx::isValid(landMap_) || !bgfx::isValid(lakeMap_) || !bgfx::isValid(waterNormal_) || !bgfx::isValid(mapTexture_))
    throw std::runtime_error("Land cover texture creation failed");
  auxiliaryTextureBytes_ += landChain.size() + lake.size() * sizeof(float) + waterChain.size() + data.map.rgba.size();
  stats_.lakes = landscape_->lakeCount();
  log("RENDER", "Land cover: " + std::to_string(landscape_->lakeCount()) + " lakes, " +
                    std::to_string(static_cast<int>(landscape_->lakeAreaKm2())) + " km2 of water; terrain layers " +
                    std::to_string(layerSize) + " px");
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

void Renderer::buildEnvironment(Synthesis& data) {
  landscape_ = std::move(data.landscape);
  createEnvironmentTextures(data);

  // ---- Terrain -------------------------------------------------------------
  // The inner rings are the simulation's collision surface, vertex for vertex.
  // Outside it the simulation uses the analytic height directly, so the outer
  // rings sample the same function and simply continue it to the horizon.
  {
    std::vector<double> radii;
    for (int ring = 0; ring <= kTerrainRings; ++ring)
      radii.push_back(kTerrainRadius * std::pow(double(ring) / kTerrainRings, 2));
    for (double spacing = radii.back() - radii[radii.size() - 2]; radii.back() < kTerrainDrawRadius; spacing *= 1.07)
      radii.push_back(radii.back() + spacing);
    const int rings = static_cast<int>(radii.size());
    std::vector<TerrainVertex> vertices(std::size_t(rings) * kTerrainSegments);
    procedural::parallelRows(rings, [&](int ring) {
      for (int segment = 0; segment < kTerrainSegments; ++segment) {
        Vec3 ned;
        if (ring <= kTerrainRings) {
          ned = terrainVertex(ring, segment);
        } else {
          const double angle = 2 * kPi * segment / kTerrainSegments;
          const double north = -radii[ring] * std::sin(angle), east = radii[ring] * std::cos(angle);
          ned = {north, east, -terrainElevation(north, east)};
        }
        const glm::vec3 position = renderDirection(ned);
        vertices[std::size_t(ring) * kTerrainSegments + segment] = {position.x, position.y, position.z, 0, 1, 0};
      }
    });
    // Smooth shading normals from the triangles as drawn, so the light on far
    // ground follows the surface that is actually there. Near ground takes its
    // slope from the height map instead.
    procedural::parallelRows(rings, [&](int ring) {
      for (int segment = 0; segment < kTerrainSegments; ++segment) {
        const auto at = [&](int r, int s) {
          const auto& v = vertices[std::size_t(std::clamp(r, 0, rings - 1)) * kTerrainSegments +
                                   std::size_t((s + kTerrainSegments) % kTerrainSegments)];
          return glm::vec3(v.x, v.y, v.z);
        };
        glm::vec3 normal = glm::cross(at(ring, segment + 1) - at(ring, segment - 1), at(ring + 1, segment) - at(ring - 1, segment));
        if (normal.y < 0) normal = -normal;
        normal = glm::length(normal) > 1e-6f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
        auto& v = vertices[std::size_t(ring) * kTerrainSegments + segment];
        v.nx = normal.x; v.ny = normal.y; v.nz = normal.z;
      }
    });
    std::vector<std::uint32_t> indices;
    indices.reserve(std::size_t(rings - 1) * kTerrainSegments * 6);
    const auto at = [&](int ring, int segment) {
      return static_cast<std::uint32_t>(ring * kTerrainSegments + segment % kTerrainSegments);
    };
    const auto position = [&](std::uint32_t index) {
      return glm::vec3(vertices[index].x, vertices[index].y, vertices[index].z);
    };
    // The airfield's plain is level, and the collision mesh spends a third of
    // its rings on it. Drawn as they are, those would be a hundred thousand
    // slivers under the runway; the same plane is drawn with a few hundred
    // triangles instead, halving in number toward the middle.
    int level = 0;
    while (level + 1 < kTerrainRings) {
      bool flat = true;
      for (int segment = 0; segment < kTerrainSegments && flat; ++segment) flat = terrainVertex(level + 1, segment).z == 0;
      if (!flat) break;
      ++level;
    }
    if (level > 0) {
      const auto triangle = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        const bool up = glm::cross(position(b) - position(a), position(c) - position(a)).y >= 0;
        indices.push_back(a); indices.push_back(up ? b : c); indices.push_back(up ? c : b);
      };
      std::vector<std::uint32_t> outer(kTerrainSegments);
      for (int segment = 0; segment < kTerrainSegments; ++segment) outer[segment] = at(level, segment);
      double radius = radii[level];
      while (outer.size() > 12 && outer.size() % 2 == 0) {
        radius *= .5;
        std::vector<std::uint32_t> inner(outer.size() / 2);
        for (std::size_t i = 0; i < inner.size(); ++i) {
          // Each inner vertex lies on the line from the middle to every other outer one.
          const auto& rim = vertices[outer[i * 2]];
          const double scale = radius / std::max(1e-6, double(std::hypot(rim.x, rim.z)));
          inner[i] = static_cast<std::uint32_t>(vertices.size());
          vertices.push_back({float(rim.x * scale), 0, float(rim.z * scale), 0, 1, 0});
        }
        for (std::size_t i = 0; i < inner.size(); ++i) {
          const std::uint32_t c0 = inner[i], c1 = inner[(i + 1) % inner.size()];
          const std::uint32_t f0 = outer[i * 2], f1 = outer[i * 2 + 1], f2 = outer[(i * 2 + 2) % outer.size()];
          triangle(c0, f0, f1); triangle(c0, f1, c1); triangle(c1, f1, f2);
        }
        outer = std::move(inner);
      }
      const auto middle = static_cast<std::uint32_t>(vertices.size());
      vertices.push_back({0, 0, 0, 0, 1, 0});
      for (std::size_t i = 0; i < outer.size(); ++i) triangle(middle, outer[i], outer[(i + 1) % outer.size()]);
    }
    for (int ring = level; ring + 1 < rings; ++ring) for (int segment = 0; segment < kTerrainSegments; ++segment) {
      // The same diagonal as ofs::sampleTerrain, so the drawn triangles are the
      // ones the aircraft's wheels touch.
      const std::uint32_t a = at(ring, segment), b = at(ring, segment + 1), c = at(ring + 1, segment + 1), d = at(ring + 1, segment);
      for (const auto& triangle : {std::array{a, b, c}, std::array{a, c, d}}) {
        const bool up = glm::cross(position(triangle[1]) - position(triangle[0]), position(triangle[2]) - position(triangle[0])).y >= 0;
        indices.push_back(triangle[0]);
        indices.push_back(triangle[up ? 1 : 2]);
        indices.push_back(triangle[up ? 2 : 1]);
      }
    }
    terrainVertices_ = bgfx::createVertexBuffer(
        bgfx::copy(vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(TerrainVertex))), terrainLayout_);
    terrainIndices_ = bgfx::createIndexBuffer(
        bgfx::copy(indices.data(), static_cast<std::uint32_t>(indices.size() * sizeof(std::uint32_t))), BGFX_BUFFER_INDEX32);
    terrainIndexCount_ = static_cast<std::uint32_t>(indices.size());
  }

  // ---- Airfield ------------------------------------------------------------
  const auto upload = [&](const std::vector<SurfaceVertex>& data) {
    return bgfx::createVertexBuffer(bgfx::copy(data.data(),
        static_cast<std::uint32_t>(data.size() * sizeof(SurfaceVertex))), surfaceLayout_);
  };
  {
    // Paving and paint are coplanar with the ground. Their draw order is fixed
    // by a per-layer depth offset in terrain_vs: paving over the land, paint
    // over the paving.
    const Airfield airfield = buildAirfield();
    const auto ground = [&](const std::vector<SurfaceVertex>& data, float kind, float layer, glm::vec3 tint) {
      if (data.empty()) return;
      airfieldGround_.push_back({upload(data), kind, layer, tint});
      if (!bgfx::isValid(airfieldGround_.back().buffer)) throw std::runtime_error("Airfield paving upload failed");
    };
    // Where two kinds of paving meet they overlap, so each has a layer of its own.
    // The same airfield is laid at all three sites, which stand at one height.
    const auto moved = [](std::vector<SurfaceVertex> data, const AirfieldSite& site) {
      for (auto& vertex : data) { vertex.x += float(site.east); vertex.z -= float(site.north); }
      return data;
    };
    const auto everywhere = [&](const std::vector<SurfaceVertex>& data) {
      std::vector<SurfaceVertex> all;
      all.reserve(data.size() * 3);
      for (const auto& site : kAirfieldSites) {
        const auto copy = moved(data, site);
        all.insert(all.end(), copy.begin(), copy.end());
      }
      return all;
    };
    ground(everywhere(airfield.ground.roads), 1, 1, {.066f, .065f, .062f});
    ground(everywhere(airfield.ground.concrete), 3, 1.25f, {.215f, .212f, .200f});
    ground(everywhere(airfield.ground.taxiways), 1, 1.5f, {.092f, .092f, .094f});
    ground(everywhere(airfield.ground.runway), 1, 1.75f, {.074f, .075f, .078f});
    ground(everywhere(airfield.ground.whitePaint), 2, 2.5f, {.80f, .81f, .78f});
    ground(everywhere(airfield.ground.yellowPaint), 2, 2.5f, {.72f, .47f, .035f});
    for (const auto& site : kAirfieldSites)
      for (const auto& part : airfield.parts) {
        if (part.vertices.empty()) continue;
        airfieldParts_.push_back({upload(moved(part.vertices, site)), part.material, static_cast<std::uint32_t>(part.vertices.size()),
                                  {-200 + float(site.east), 10, -float(site.north)}, 2100});
        if (!bgfx::isValid(airfieldParts_.back().buffer)) throw std::runtime_error("Airfield structure upload failed");
      }
  }

  // ---- Villages ------------------------------------------------------------
  // Where the low country is farmed there are villages: a street of houses
  // with pitched roofs, and in the larger ones a church. They are scenery,
  // like the trees, and are not collided with.
  std::vector<SurfaceVertex> walls, roofs;
  {
    const auto ground = [](float x, float z) { return float(-groundHeightNed(-z, x)); };
    // A building `width` by `depth` on the ground at (x, z), turned by `yaw`,
    // with walls `height` high and a roof rising `pitch` above them: a ridge
    // along its depth, or with `spire` a point.
    const auto building = [&](float x, float z, float width, float depth, float height, float pitch, float yaw, bool spire) {
      const glm::vec2 along{std::sin(yaw), std::cos(yaw)}, across{std::cos(yaw), -std::sin(yaw)};
      const auto corner = [&](float u, float v) { return glm::vec2{x, z} + across * (u * width * .5f) + along * (v * depth * .5f); };
      const glm::vec2 c[4] = {corner(-1, -1), corner(1, -1), corner(1, 1), corner(-1, 1)};
      // Walls go down to the lowest corner, so nothing stands on air on a slope.
      float low = 1e9f, high = -1e9f;
      for (const auto& p : c) { low = std::min(low, ground(p.x, p.y)); high = std::max(high, ground(p.x, p.y)); }
      const float base = low - .4f, eaves = high + height, ridge = eaves + pitch;
      const auto at = [](const glm::vec2& p, float y) { return glm::vec3{p.x, y, p.y}; };
      for (int i = 0; i < 4; ++i) {
        const glm::vec2 p = c[i], q = c[(i + 1) % 4], out = glm::normalize(glm::vec2{q.y - p.y, p.x - q.x});
        const glm::vec2 mid = (p + q) * .5f, centre{x, z};
        const glm::vec2 normal = glm::dot(out, mid - centre) < 0 ? -out : out;
        addQuad(walls, at(p, base), at(q, base), at(q, eaves), at(p, eaves), {normal.x, 0, normal.y});
      }
      if (spire) {
        const glm::vec3 top{x, ridge, z};
        for (int i = 0; i < 4; ++i) {
          const glm::vec3 p = at(c[i], eaves), q = at(c[(i + 1) % 4], eaves);
          glm::vec3 normal = glm::normalize(glm::cross(q - p, top - p));
          if (normal.y < 0) normal = -normal;
          addQuad(roofs, p, q, top, top, normal);
        }
        return;
      }
      // Eaves overhang the walls a little.
      const auto eave = [&](float u, float v) { return at(corner(u * 1.12f, v * 1.06f), eaves - .15f); };
      const glm::vec3 front = at(corner(0, -1.06f), ridge), back = at(corner(0, 1.06f), ridge);
      for (const float side : {-1.f, 1.f}) {
        const glm::vec3 p = eave(side, -1), q = eave(side, 1);
        glm::vec3 normal = glm::normalize(glm::cross(q - p, front - p));
        if (normal.y < 0) normal = -normal;
        addQuad(roofs, p, q, back, front, normal);
      }
      for (const float end : {-1.f, 1.f}) {
        const glm::vec3 top = end < 0 ? front : back;
        const glm::vec2 normal = along * end;
        addQuad(walls, at(corner(-1, end), eaves), at(corner(1, end), eaves), top, top, {normal.x, 0, normal.y});
      }
    };
    const auto buildable = [&](float x, float z) {
      const double north = -z, east = x;
      return !insideAirfieldClearway(north, east) && airfieldDistance(north, east) > 2700 && !landscape_->underWater(north, east) &&
             -sampleTerrain(north, east).normalNed.z > .985;
    };
    unsigned seed = 0;
    int villages = 0;
    for (int iz = -11; iz <= 11; ++iz) for (int ix = -11; ix <= 11; ++ix) {
      seed += 7;
      const float cx = (ix + sceneryRandom(seed * 53) - .5f) * 2100.f, cz = (iz + sceneryRandom(seed * 71) - .5f) * 2100.f;
      if (landscape_->farmland(-cz, cx) < .5f || sceneryRandom(seed * 97) > .62f || !buildable(cx, cz)) continue;
      ++villages;
      const float heading = sceneryRandom(seed * 13) * 3.1415927f;
      const glm::vec2 street{std::sin(heading), std::cos(heading)}, side{std::cos(heading), -std::sin(heading)};
      const int houses = 8 + int(sceneryRandom(seed * 29) * 20);
      for (int house = 0; house < houses; ++house) {
        const unsigned h = seed * 131 + unsigned(house) * 17;
        // Houses face each other across the street, a second row behind in the larger places.
        const float row = house % 2 ? 1.f : -1.f, depthRow = house >= 18 ? 2.6f : 1.f;
        const float position = (float(house / 2) - float(std::min(houses, 18)) * .25f) * 27.f + (sceneryRandom(h) - .5f) * 9.f;
        const glm::vec2 p = glm::vec2{cx, cz} + street * (house >= 18 ? position - 9 * 27.f + 60 : position) +
                            side * row * (15.f + 5.f * sceneryRandom(h + 1)) * depthRow;
        if (!buildable(p.x, p.y)) continue;
        const float width = 7.5f + 3.5f * sceneryRandom(h + 2), depth = 10.f + 7.f * sceneryRandom(h + 3);
        building(p.x, p.y, width, depth, 4.6f + 2.4f * sceneryRandom(h + 4), 2.6f + 1.6f * sceneryRandom(h + 5),
                 heading + 1.5707963f + (sceneryRandom(h + 6) - .5f) * .25f, false);
      }
      if (houses >= 16) {
        // The church stands back from the middle of the street.
        const glm::vec2 nave = glm::vec2{cx, cz} + side * 58.f + street * 12.f, tower = nave - street * 17.f;
        if (buildable(nave.x, nave.y) && buildable(tower.x, tower.y)) {
          building(nave.x, nave.y, 11, 26, 9, 5.5f, heading, false);
          building(tower.x, tower.y, 6.5f, 6.5f, 21, 11, heading, true);
        }
      }
    }
    log("RENDER", "Villages: " + std::to_string(villages) + ", " + std::to_string((walls.size() + roofs.size()) / 3) + " triangles");
  }
  if (!walls.empty()) houses_ = upload(walls);
  if (!roofs.empty()) roofs_ = upload(roofs);
  houseVertices_ = static_cast<std::uint32_t>(walls.size());
  roofVertices_ = static_cast<std::uint32_t>(roofs.size());

  // ---- Trees ---------------------------------------------------------------
  for (int species = 0; species < 2; ++species) for (int lod = 0; lod < 2; ++lod) {
    const auto mesh = unitTree(species == 1, lod == 1);
    treeMeshes_[species][lod].vertices = upload(mesh);
    treeMeshes_[species][lod].count = static_cast<std::uint32_t>(mesh.size());
    if (!bgfx::isValid(treeMeshes_[species][lod].vertices)) throw std::runtime_error("Tree mesh upload failed");
  }

  // Optional developer grid, retained from M0 as a toggle.
  std::vector<UnlitVertex> grid;
  for (int i = -2000; i <= 2000; i += 50) {
    const std::uint32_t color = i == 0 ? 0xff96adbf : 0xff708574;
    grid.push_back({static_cast<float>(i), 0.15f, -2000, color});
    grid.push_back({static_cast<float>(i), 0.15f, 2000, color});
    grid.push_back({-2000, 0.15f, static_cast<float>(i), color});
    grid.push_back({2000, 0.15f, static_cast<float>(i), color});
  }
  grid_ = bgfx::createVertexBuffer(
      bgfx::copy(grid.data(), static_cast<std::uint32_t>(grid.size() * sizeof(UnlitVertex))),
      unlitLayout_);

  for (const auto handle : {terrainVertices_, grid_})
    if (!bgfx::isValid(handle)) throw std::runtime_error("Environment GPU upload failed");
  if (!bgfx::isValid(terrainIndices_)) throw std::runtime_error("Environment GPU upload failed");
}

void Renderer::destroyEnvironment() {
  for (auto* handle : {&terrainVertices_, &houses_, &roofs_, &grid_}) {
    if (bgfx::isValid(*handle)) bgfx::destroy(*handle);
    handle->idx = bgfx::kInvalidHandle;
  }
  for (auto& layer : airfieldGround_) if (bgfx::isValid(layer.buffer)) bgfx::destroy(layer.buffer);
  airfieldGround_.clear();
  for (auto& part : airfieldParts_) if (bgfx::isValid(part.buffer)) bgfx::destroy(part.buffer);
  airfieldParts_.clear();
  for (auto& part : baseParts_) if (bgfx::isValid(part.buffer)) bgfx::destroy(part.buffer);
  baseParts_.clear();
  baseHealth_.clear();
  baseShown_ = false;
  if (bgfx::isValid(terrainIndices_)) bgfx::destroy(terrainIndices_);
  terrainIndices_.idx = bgfx::kInvalidHandle;
  for (auto& species : treeMeshes_) for (auto& mesh : species) {
    if (bgfx::isValid(mesh.vertices)) bgfx::destroy(mesh.vertices);
    mesh = {};
  }
  for (auto& [key, chunk] : treeChunks_) {
    (void)key;
    if (bgfx::isValid(chunk.instances)) bgfx::destroy(chunk.instances);
  }
  treeChunks_.clear();
  for (auto* texture : {&terrainAlbedo_, &terrainNormal_, &landMap_, &lakeMap_, &waterNormal_, &mapTexture_, &heightMap_}) {
    if (bgfx::isValid(*texture)) bgfx::destroy(*texture);
    texture->idx = bgfx::kInvalidHandle;
  }
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

bool Renderer::sphereVisible(const glm::vec3& center, float radius, float maxDistance) const {
  if (glm::length(center - cameraEye_) - radius > maxDistance) return false;
  const auto row = [&](int i) { return glm::vec4(viewProj_[0][i], viewProj_[1][i], viewProj_[2][i], viewProj_[3][i]); };
  const auto w = row(3);
  for (int axis = 0; axis < 2; ++axis) for (float sign : {-1.f, 1.f}) {
    const auto plane = w + row(axis) * sign;
    if (glm::dot(plane, glm::vec4(center, 1)) < -radius * glm::length(glm::vec3(plane))) return false;
  }
  // Behind the camera.
  return glm::dot(center - cameraEye_, cameraForward_) > -radius;
}

void Renderer::drawEnvironment() {
  const glm::mat4 environment = glm::translate(glm::mat4{1}, localPosition({}, origin_));
  const std::uint32_t wrap = settings_.anisotropic ? BGFX_SAMPLER_MIN_ANISOTROPIC : BGFX_SAMPLER_NONE;

  // Terrain program: natural land and the paved layers that lie in it.
  const auto ground = [&](bgfx::VertexBufferHandle buffer, float kind, float layer, const glm::vec3& tint) {
    bindFrame(viewProj_);
    bindLighting();
    bgfx::setTexture(1, uniforms_.terrainAlbedo, terrainAlbedo_, wrap);
    bgfx::setTexture(2, uniforms_.terrainNormal, terrainNormal_, wrap);
    bgfx::setTexture(3, uniforms_.landMap, landMap_);
    bgfx::setTexture(4, uniforms_.lakeMap, lakeMap_);
    bgfx::setTexture(5, uniforms_.waterNormal, waterNormal_, wrap);
    bgfx::setTexture(12, uniforms_.heightMap, heightMap_);
    bgfx::setUniform(uniforms_.terrainMap, glm::value_ptr(glm::vec4(
        2.f * Landscape::kExtent / float(landscape_->heightSize()), float(heightMapLevels_ - 1), Landscape::kHeightBase,
        Landscape::kHeightRange)));
    bgfx::setUniform(uniforms_.model, glm::value_ptr(environment));
    bgfx::setUniform(uniforms_.surface, glm::value_ptr(glm::vec4(kind, layer, 0, 0)));
    bgfx::setUniform(uniforms_.baseColor, glm::value_ptr(glm::vec4(tint, 1)));
    bgfx::setState(kOpaqueState | BGFX_STATE_MSAA);
    bgfx::setVertexBuffer(0, buffer);
  };
  ground(terrainVertices_, 0, 0, glm::vec3(1));
  bgfx::setIndexBuffer(terrainIndices_);
  bgfx::submit(kViewWorld, programs_.terrain);
  ++stats_.drawCalls;
  stats_.triangles += terrainIndexCount_ / 3;
  const auto paved = [&](bgfx::VertexBufferHandle buffer, float kind, float layer, const glm::vec3& tint) {
    ground(buffer, kind, layer, tint);
    bgfx::submit(kViewWorld, programs_.terrain);
    ++stats_.drawCalls;
  };
  for (const auto& layer : airfieldGround_) paved(layer.buffer, layer.kind, layer.layer, layer.tint);

  // Structures: untextured geometry weathered procedurally in the PBR shader.
  const auto structure = [&](bgfx::VertexBufferHandle buffer, glm::vec4 color, float metallic, float roughness,
                             float detail, glm::vec3 emissive, float glass) {
    if (!bgfx::isValid(buffer)) return;
    Material material;
    material.baseColor[0] = color.r; material.baseColor[1] = color.g; material.baseColor[2] = color.b;
    material.baseColor[3] = color.a; material.metallic = metallic; material.roughness = roughness;
    material.emissive[0] = emissive.r; material.emissive[1] = emissive.g; material.emissive[2] = emissive.b;
    material.environmentReflection = glass;
    bindFrame(viewProj_);
    bindLighting();
    bgfx::setUniform(uniforms_.model, glm::value_ptr(environment));
    bgfx::setUniform(uniforms_.normalMatrix, glm::value_ptr(glm::mat3{1}));
    bgfx::setState(kOpaqueState | BGFX_STATE_MSAA);
    bgfx::setVertexBuffer(0, buffer);
    applyMaterial(material, nullptr, detail);
    bgfx::submit(kViewWorld, programs_.pbr);
    ++stats_.drawCalls;
  };
  std::uint32_t structureVertices = houseVertices_ + roofVertices_;
  // The airfield's buildings are only worth drawing from where they can be made out.
  for (const auto* parts : {&airfieldParts_, &baseParts_})
    for (const auto& part : *parts) {
      if (glm::length(cameraEye_ - glm::vec3(environment[3]) - part.centre) > 30000.f + part.radius) continue;
      const auto look = lookOf(part.material);
      structure(part.buffer, look.color, look.metallic, look.roughness, look.detail, look.emissive, look.glass);
      structureVertices += part.vertices;
    }
  // Limewashed walls under tiled roofs.
  structure(houses_, {.60f, .56f, .47f, 1}, 0, .9f, 1, {}, 0);
  structure(roofs_, {.30f, .115f, .07f, 1}, 0, .8f, 1, {}, 0);
  stats_.triangles += structureVertices / 3;

  if (settings_.vegetation) drawTrees(kViewWorld, programs_.tree, nullptr);
}

void Renderer::setStructures(std::span<const std::uint8_t> health) {
  // Only whether each stands matters to how it is drawn.
  std::vector<std::uint8_t> standing(health.size());
  for (std::size_t i = 0; i < health.size(); ++i) standing[i] = health[i] > 0;
  if (baseShown_ == !health.empty() && standing == baseHealth_) return;
  for (auto& part : baseParts_) if (bgfx::isValid(part.buffer)) bgfx::destroy(part.buffer);
  baseParts_.clear();
  baseHealth_ = std::move(standing);
  baseShown_ = !health.empty();
  if (!baseShown_) return;
  for (const auto& part : buildStructures(baseHealth_)) {
    if (part.vertices.empty()) continue;
    // The structures lie all along the valley: no one sphere is worth culling by.
    baseParts_.push_back({bgfx::createVertexBuffer(bgfx::copy(part.vertices.data(),
                              static_cast<std::uint32_t>(part.vertices.size() * sizeof(SurfaceVertex))), surfaceLayout_),
                          part.material, static_cast<std::uint32_t>(part.vertices.size()), {0, 0, 0}, 40000});
  }
}

// ---------------------------------------------------------------------------
// Forest
// ---------------------------------------------------------------------------

void Renderer::updateTreeChunks() {
  if (!settings_.vegetation) return;
  if (treeChunkDensity_ != settings_.treeDensity) {
    for (auto& [key, chunk] : treeChunks_) {
      (void)key;
      if (bgfx::isValid(chunk.instances)) bgfx::destroy(chunk.instances);
    }
    treeChunks_.clear();
    treeChunkDensity_ = settings_.treeDensity;
  }
  const double size = Landscape::kTreeChunk;
  const glm::dvec3 eye = glm::dvec3(cameraEye_) + glm::dvec3(origin_.y, -origin_.z, -origin_.x);
  const float reach = settings_.sceneryDistance + 800.f;
  const int span = static_cast<int>(std::ceil(reach / size));
  const int centreX = static_cast<int>(std::floor(eye.x / size)), centreZ = static_cast<int>(std::floor(eye.z / size));

  // Missing chunks, nearest first, a few per frame so flying into new country
  // never stalls a frame.
  std::vector<std::tuple<double, int, int>> missing;
  for (int dz = -span; dz <= span; ++dz) for (int dx = -span; dx <= span; ++dx) {
    const int cx = centreX + dx, cz = centreZ + dz;
    const double distance = std::hypot((cx + .5) * size - eye.x, (cz + .5) * size - eye.z);
    if (distance > reach) continue;
    const auto found = treeChunks_.find({cx, cz});
    if (found == treeChunks_.end()) missing.emplace_back(distance, cx, cz);
    else found->second.lastUsed = frameIndex_;
  }
  std::sort(missing.begin(), missing.end());
  // Everything in view is built on the first frame; afterwards the budget only
  // has to keep up with the aircraft.
  const std::size_t budget = treeChunks_.empty() ? missing.size() : 6;
  std::vector<TreeInstance> trees;
  for (std::size_t i = 0; i < std::min(budget, missing.size()); ++i) {
    const auto [distance, cx, cz] = missing[i];
    (void)distance;
    trees.clear();
    landscape_->treesInChunk(cx, cz, treeChunkDensity_, trees);
    std::stable_partition(trees.begin(), trees.end(), [](const TreeInstance& tree) { return tree.conifer < .5f; });
    TreeChunk chunk;
    chunk.lastUsed = frameIndex_;
    chunk.center = {static_cast<float>((cx + .5) * size), 0, static_cast<float>((cz + .5) * size)};
    chunk.radius = static_cast<float>(size) * .75f;
    if (!trees.empty()) {
      float low = trees.front().up, high = trees.front().up;
      std::vector<float> data;
      data.reserve(trees.size() * 8);
      for (const TreeInstance& tree : trees) {
        low = std::min(low, tree.up);
        high = std::max(high, tree.up + tree.height);
        chunk.broadleaf += tree.conifer < .5f;
        data.insert(data.end(), {tree.east, tree.up, tree.south, tree.height, tree.spread, tree.yaw, tree.tint, tree.conifer});
      }
      chunk.conifer = static_cast<std::uint32_t>(trees.size()) - chunk.broadleaf;
      chunk.center.y = .5f * (low + high);
      chunk.radius = std::hypot(static_cast<float>(size) * .7072f, .5f * (high - low)) + 6.f;
      chunk.instances = bgfx::createVertexBuffer(
          bgfx::copy(data.data(), static_cast<std::uint32_t>(data.size() * sizeof(float))), instanceLayout_);
    }
    treeChunks_[{cx, cz}] = chunk;
  }
  // Release country left far behind.
  for (auto it = treeChunks_.begin(); it != treeChunks_.end();) {
    if (frameIndex_ - it->second.lastUsed > 600) {
      if (bgfx::isValid(it->second.instances)) bgfx::destroy(it->second.instances);
      it = treeChunks_.erase(it);
    } else {
      ++it;
    }
  }
  stats_.treeChunks = static_cast<std::uint32_t>(treeChunks_.size());
}

void Renderer::drawTrees(bgfx::ViewId view, bgfx::ProgramHandle program, const Cascade* cascade) {
  const glm::vec3 offset = localPosition({}, origin_);
  const glm::mat4 environment = glm::translate(glm::mat4{1}, offset);
  for (const auto& [key, chunk] : treeChunks_) {
    (void)key;
    if (!bgfx::isValid(chunk.instances)) continue;
    const glm::vec3 center = chunk.center + offset;
    const float distance = glm::length(center - cameraEye_);
    if (distance - chunk.radius > settings_.sceneryDistance) continue;
    if (cascade) {
      const glm::vec3 toCascade = center - cascade->center;
      const glm::vec3 lateral = toCascade - sun_ * glm::dot(toCascade, sun_);
      if (glm::length(lateral) > cascade->radius + chunk.radius) continue;
    } else if (!sphereVisible(center, chunk.radius, settings_.sceneryDistance)) {
      continue;
    }
    // Full meshes near the camera; beyond that a tree covers a few pixels.
    const int lod = distance - chunk.radius * .5f < (cascade ? 500.f : 1300.f) * std::exp2(-settings_.lodBias) ? 0 : 1;
    for (int species = 0; species < 2; ++species) {
      const std::uint32_t count = species == 0 ? chunk.broadleaf : chunk.conifer;
      if (!count) continue;
      const TreeMesh& mesh = treeMeshes_[species][lod];
      if (cascade) {
        bgfx::setUniform(uniforms_.lightViewProj, glm::value_ptr(cascade->lightViewProj));
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
      } else {
        bindFrame(viewProj_);
        bindLighting();
        // Coverage from the shader's leaf cut-out; the target's alpha is left alone.
        bgfx::setState((kOpaqueState & ~BGFX_STATE_WRITE_A) | BGFX_STATE_MSAA | BGFX_STATE_BLEND_ALPHA_TO_COVERAGE);
      }
      bgfx::setUniform(uniforms_.model, glm::value_ptr(environment));
      bgfx::setVertexBuffer(0, mesh.vertices);
      bgfx::setInstanceDataBuffer(chunk.instances, species == 0 ? 0 : chunk.broadleaf, count);
      bgfx::submit(view, program);
      ++stats_.drawCalls;
      stats_.triangles += count * (mesh.count / 3);
      if (!cascade) stats_.treesDrawn += count;
    }
  }
}

}  // namespace ofs::client
