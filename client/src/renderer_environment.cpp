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
    for (double spacing = 700.; radii.back() < kTerrainDrawRadius; spacing *= 1.045)
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
        // Smooth shading normals; the collision surface uses the exact faces.
        const double dn = (terrainElevation(ned.x + 4, ned.y) - terrainElevation(ned.x - 4, ned.y)) / 8;
        const double de = (terrainElevation(ned.x, ned.y + 4) - terrainElevation(ned.x, ned.y - 4)) / 8;
        const glm::vec3 position = renderDirection(ned);
        const auto normal = glm::normalize(glm::vec3(-de, 1, dn));
        vertices[std::size_t(ring) * kTerrainSegments + segment] = {position.x, position.y, position.z,
                                                                    normal.x, normal.y, normal.z};
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
    for (int ring = 0; ring + 1 < rings; ++ring) for (int segment = 0; segment < kTerrainSegments; ++segment) {
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
  std::vector<SurfaceVertex> vertices;
  const auto upload = [&](const std::vector<SurfaceVertex>& data) {
    return bgfx::createVertexBuffer(bgfx::copy(data.data(),
        static_cast<std::uint32_t>(data.size() * sizeof(SurfaceVertex))), surfaceLayout_);
  };
  const auto box = [&](float x, float z, float w, float d, float y, float h) {
    const float l=x-w/2, r=x+w/2, f=z-d/2, b=z+d/2, t=y+h;
    addQuad(vertices,{l,t,f},{l,t,b},{r,t,b},{r,t,f},{0,1,0});
    addQuad(vertices,{l,y,f},{r,y,f},{r,t,f},{l,t,f},{0,0,-1});
    addQuad(vertices,{r,y,b},{l,y,b},{l,t,b},{r,t,b},{0,0,1});
    addQuad(vertices,{l,y,b},{l,y,f},{l,t,f},{l,t,b},{-1,0,0});
    addQuad(vertices,{r,y,f},{r,y,b},{r,t,b},{r,t,f},{1,0,0});
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
    ground(airfield.ground.roads, 1, 1, {.066f, .065f, .062f});
    ground(airfield.ground.concrete, 3, 1.25f, {.215f, .212f, .200f});
    ground(airfield.ground.taxiways, 1, 1.5f, {.092f, .092f, .094f});
    ground(airfield.ground.runway, 1, 1.75f, {.074f, .075f, .078f});
    ground(airfield.ground.whitePaint, 2, 2.5f, {.80f, .81f, .78f});
    ground(airfield.ground.yellowPaint, 2, 2.5f, {.72f, .47f, .035f});
    for (const auto& part : airfield.parts) {
      if (part.vertices.empty()) continue;
      airfieldParts_.push_back({upload(part.vertices), part.material, static_cast<std::uint32_t>(part.vertices.size())});
      if (!bgfx::isValid(airfieldParts_.back().buffer)) throw std::runtime_error("Airfield structure upload failed");
    }
  }

  // Hamlets on dry, open ground around the field.
  std::vector<SurfaceVertex> houses;
  unsigned seed = 0;
  for (int iz = -4; iz <= 4; ++iz) for (int ix = -4; ix <= 4; ++ix) {
    ++seed;
    const float cx = ix * 1750.f + (sceneryRandom(seed * 53) - .5f) * 400;
    const float cz = iz * 1750.f + (sceneryRandom(seed * 71) - .5f) * 400;
    if (std::hypot(cx, cz) > 9000 || (ix + iz) % 3 != 0 || std::hypot(cx, cz) <= 2800) continue;
    for (int house = 0; house < 5; ++house) {
      const float x = cx + house * 33.f - 65.f, z = cz + 610.f;
      if (landscape_->underWater(-z, x)) continue;
      const float y = float(-groundHeightNed(-z, x));
      vertices.clear();
      box(x, z, 18, 26, y, 6);
      const glm::vec3 a{x-10,y+6,z-14}, b{x-10,y+6,z+14};
      const glm::vec3 c{x,y+11,z+14}, d{x,y+11,z-14};
      addQuad(vertices,a,b,c,d,glm::normalize(glm::vec3(-.5f,1,0)));
      addQuad(vertices,d,c,{x+10,y+6,z+14},{x+10,y+6,z-14},glm::normalize(glm::vec3(.5f,1,0)));
      addQuad(vertices,a,d,{x+10,y+6,z-14},a,{0,0,-1});
      addQuad(vertices,{x+10,y+6,z+14},c,b,b,{0,0,1});
      houses.insert(houses.end(), vertices.begin(), vertices.end());
    }
  }
  if (!houses.empty()) houses_ = upload(houses);
  houseVertices_ = static_cast<std::uint32_t>(houses.size());

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
  for (auto* handle : {&terrainVertices_, &houses_, &grid_}) {
    if (bgfx::isValid(*handle)) bgfx::destroy(*handle);
    handle->idx = bgfx::kInvalidHandle;
  }
  for (auto& layer : airfieldGround_) if (bgfx::isValid(layer.buffer)) bgfx::destroy(layer.buffer);
  airfieldGround_.clear();
  for (auto& part : airfieldParts_) if (bgfx::isValid(part.buffer)) bgfx::destroy(part.buffer);
  airfieldParts_.clear();
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
  for (auto* texture : {&terrainAlbedo_, &terrainNormal_, &landMap_, &lakeMap_, &waterNormal_, &mapTexture_}) {
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
  std::uint32_t structureVertices = houseVertices_;
  // The airfield's buildings are only worth drawing from where they can be made out.
  const bool airfieldInView = glm::length(cameraEye_ - glm::vec3(environment[3])) < 30000.f;
  if (airfieldInView)
    for (const auto& part : airfieldParts_) {
      const auto look = lookOf(part.material);
      structure(part.buffer, look.color, look.metallic, look.roughness, look.detail, look.emissive, look.glass);
      structureVertices += part.vertices;
    }
  structure(houses_, {.42f, .37f, .31f, 1}, 0, .85f, 1, {}, 0);
  stats_.triangles += structureVertices / 3;

  if (settings_.vegetation) drawTrees(kViewWorld, programs_.tree, nullptr);
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
        bgfx::setState(kOpaqueState | BGFX_STATE_MSAA);
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
