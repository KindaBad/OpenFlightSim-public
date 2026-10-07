// Renderer: terrain, lakes, the airfield and the forest. See renderer.hpp for
// the frame graph.

#include "renderer.hpp"
#include "renderer_internal.hpp"

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

// Test airfield in render axes (+X east, +Y up, +Z south), metres.
constexpr double kRunwayHalfWidth = 22.5;
constexpr double kRunwayLength = 2600.0;

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
  if (!bgfx::isValid(landMap_) || !bgfx::isValid(lakeMap_) || !bgfx::isValid(waterNormal_))
    throw std::runtime_error("Land cover texture creation failed");
  auxiliaryTextureBytes_ += landChain.size() + lake.size() * sizeof(float) + waterChain.size();
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
  // Paving and paint are coplanar with the ground. Their draw order is fixed by
  // a per-layer depth offset in terrain_vs; the small lifts below only keep the
  // surfaces apart for the shadow pass and for anything looking along them.
  constexpr float kRunwayLift = 0.05f;
  const float asphalt = kRunwayLift;
  // Counter-clockwise seen from above; see addQuad.
  addQuad(vertices,
          {static_cast<float>(-kRunwayHalfWidth), asphalt, static_cast<float>(-kRunwayLength / 2)},
          {static_cast<float>(-kRunwayHalfWidth), asphalt, static_cast<float>(kRunwayLength / 2)},
          {static_cast<float>(kRunwayHalfWidth), asphalt, static_cast<float>(kRunwayLength / 2)},
          {static_cast<float>(kRunwayHalfWidth), asphalt, static_cast<float>(-kRunwayLength / 2)},
          {0, 1, 0});
  runway_ = upload(vertices);

  vertices.clear();
  const float paint = kRunwayLift + 0.05f;
  // Centreline dashes.
  for (double z = -kRunwayLength / 2 + 150; z < kRunwayLength / 2 - 150; z += 60.0)
    addQuad(vertices, {-0.45f, paint, static_cast<float>(z)},
            {-0.45f, paint, static_cast<float>(z + 30)},
            {0.45f, paint, static_cast<float>(z + 30)}, {0.45f, paint, static_cast<float>(z)},
            {0, 1, 0});
  // Edge lines.
  for (const double edge : {-kRunwayHalfWidth + 1.2, kRunwayHalfWidth - 1.2})
    addQuad(vertices, {static_cast<float>(edge - 0.5), paint, static_cast<float>(-kRunwayLength / 2 + 60)},
            {static_cast<float>(edge - 0.5), paint, static_cast<float>(kRunwayLength / 2 - 60)},
            {static_cast<float>(edge + 0.5), paint, static_cast<float>(kRunwayLength / 2 - 60)},
            {static_cast<float>(edge + 0.5), paint, static_cast<float>(-kRunwayLength / 2 + 60)},
            {0, 1, 0});
  // Threshold bars and aiming blocks at both ends.
  for (const int end : {-1, 1}) {
    const double zBase = end * (kRunwayLength / 2 - 170);
    for (int i = 0; i < 8; ++i) {
      const double pitch = (2 * kRunwayHalfWidth - 6.0) / 8.0;
      const double x = -kRunwayHalfWidth + 3.0 + pitch * (i + 0.5);
      addQuad(vertices,
              {static_cast<float>(x - pitch * 0.28), paint, static_cast<float>(zBase - end * 50.0)},
              {static_cast<float>(x + pitch * 0.28), paint, static_cast<float>(zBase - end * 50.0)},
              {static_cast<float>(x + pitch * 0.28), paint, static_cast<float>(zBase)},
              {static_cast<float>(x - pitch * 0.28), paint, static_cast<float>(zBase)},
              {0, 1, 0});
    }
    const double zAim = end * (kRunwayLength / 2 - 420);
    for (const double x : {-9.0, 9.0})
      addQuad(vertices, {static_cast<float>(x - 1.4), paint, static_cast<float>(zAim - end * 32.0)},
              {static_cast<float>(x + 1.4), paint, static_cast<float>(zAim - end * 32.0)},
              {static_cast<float>(x + 1.4), paint, static_cast<float>(zAim)},
              {static_cast<float>(x - 1.4), paint, static_cast<float>(zAim)}, {0, 1, 0});
  }
  runwayPaint_ = upload(vertices);

  const auto slab = [&](float x0, float z0, float x1, float z1, float y) {
    addQuad(vertices, {x0,y,z0}, {x0,y,z1}, {x1,y,z1}, {x1,y,z0}, {0,1,0});
  };
  const auto box = [&](float x, float z, float w, float d, float y, float h) {
    const float l=x-w/2, r=x+w/2, f=z-d/2, b=z+d/2, t=y+h;
    addQuad(vertices,{l,t,f},{l,t,b},{r,t,b},{r,t,f},{0,1,0});
    addQuad(vertices,{l,y,f},{r,y,f},{r,t,f},{l,t,f},{0,0,-1});
    addQuad(vertices,{r,y,b},{l,y,b},{l,t,b},{r,t,b},{0,0,1});
    addQuad(vertices,{l,y,b},{l,y,f},{l,t,f},{l,t,b},{-1,0,0});
    addQuad(vertices,{r,y,f},{r,y,b},{r,t,b},{r,t,f},{1,0,0});
  };
  vertices.clear();
  slab(-420,-1100,-65,180,.04f);
  slab(-95,-1220,-65,1220,.06f);
  for (const float z : {-850.f,0.f,850.f}) slab(-95,z-14,-22.5f,z+14,.07f);
  apron_ = upload(vertices);
  vertices.clear();
  // Taxiway centrelines and stand lead-in lines.
  slab(-80.2f,-1200,-79.8f,1200,.12f);
  for (const float z : {-850.f,0.f,850.f}) slab(-80,z-.2f,-25,z+.2f,.13f);
  for (int i=0; i<6; ++i) slab(-255,static_cast<float>(i*110-1040)-.2f,-80,static_cast<float>(i*110-1040)+.2f,.13f);
  taxiPaint_ = upload(vertices);
  vertices.clear();
  for (int i=0; i<5; ++i) {
    const float z=static_cast<float>(i*130-1040);
    box(-350,z,110,86,0,17);
    box(-350,z,114,90,17,1.5f);
  }
  box(-210,-400,95,150,0,12);  // terminal
  box(-160,-300,12,12,0,38);   // control tower
  box(-160,-300,23,23,38,7);
  box(-160,-300,27,27,45,1.5f);
  buildings_ = upload(vertices);
  buildingVertices_ = static_cast<std::uint32_t>(vertices.size());
  vertices.clear();
  for (int i=0; i<5; ++i) {
    const float z=static_cast<float>(i*130-1040);
    box(-294.8f,z,0.4f,66,0,12); // recessed hangar doors
    for (int j=0;j<5;++j) box(-294.5f,z-28+j*14,0.2f,0.25f,0,12);
  }
  box(-160,-300,23.4f,23.4f,39,4.5f); // tower glazing
  box(-161.8f,-400,0.4f,138,3.5f,5);
  windows_ = upload(vertices);
  vertices.clear();
  for (int i=-24;i<=24;++i) {
    for (float x : {-24.f,24.f}) box(x,static_cast<float>(i*50),.35f,.35f,.15f,.28f);
    box(-96,static_cast<float>(i*50),.35f,.35f,.15f,.28f);
  }
  lights_ = upload(vertices);

  // Service roads, fence posts and utility sheds give the field a human scale.
  vertices.clear();
  slab(455,-2800,469,2800,.065f);
  slab(-650,300,462,313,.065f);
  roads_ = upload(vertices);
  vertices.clear();
  for (int i=-44;i<=44;++i) {
    box(430,float(i*60),.18f,.18f,0,2.2f);
    box(-480,float(i*60),.18f,.18f,0,2.2f);
  }
  for (int i=0;i<3;++i) box(-535,float(i*45+450),22,30,0,5);
  props_ = upload(vertices);
  propVertices_ = static_cast<std::uint32_t>(vertices.size());

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

  for (const auto handle : {terrainVertices_, runway_, runwayPaint_, apron_, taxiPaint_, roads_, buildings_, windows_,
                            lights_, props_, grid_})
    if (!bgfx::isValid(handle)) throw std::runtime_error("Environment GPU upload failed");
  if (!bgfx::isValid(terrainIndices_)) throw std::runtime_error("Environment GPU upload failed");
}

void Renderer::destroyEnvironment() {
  for (auto* handle : {&terrainVertices_, &runway_, &runwayPaint_, &apron_, &taxiPaint_, &roads_, &buildings_, &windows_,
                       &lights_, &props_, &houses_, &grid_}) {
    if (bgfx::isValid(*handle)) bgfx::destroy(*handle);
    handle->idx = bgfx::kInvalidHandle;
  }
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
  for (auto* texture : {&terrainAlbedo_, &terrainNormal_, &landMap_, &lakeMap_, &waterNormal_}) {
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
  paved(runway_, 1, 1, {.074f, .075f, .078f});
  paved(apron_, 3, 1, {.215f, .212f, .200f});
  paved(roads_, 1, 1, {.066f, .065f, .062f});
  paved(runwayPaint_, 2, 2, {.80f, .81f, .78f});
  paved(taxiPaint_, 2, 2, {.72f, .47f, .035f});

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
  structure(buildings_, {.36f, .38f, .385f, 1}, 0, .68f, 2, {}, 0);
  structure(windows_, {.018f, .028f, .036f, 1}, 0, .06f, 0, {}, 1);
  structure(lights_, {.72f, .82f, .9f, 1}, 0, .5f, 0, {1.4f, 1.7f, 2.2f}, 0);
  structure(props_, {.20f, .19f, .175f, 1}, 0, .9f, 1, {}, 0);
  structure(houses_, {.42f, .37f, .31f, 1}, 0, .85f, 1, {}, 0);
  stats_.triangles += (buildingVertices_ + propVertices_ + houseVertices_) / 3;

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
