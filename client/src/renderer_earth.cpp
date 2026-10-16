// Renderer: the round Earth, drawn from the terrain pack. See earth_scenery.hpp.

#include "renderer.hpp"
#include "renderer_internal.hpp"
#include "log.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ofs::client {
namespace {
// A node is split while the camera is nearer than this many of its edge
// lengths, and merged again only once it is a margin further off, so a node
// at the boundary does not flip between the two every frame.
constexpr double kSplit = 2.4, kMerge = kSplit * 1.15;
// Chunks built but not drawn are kept up to this many.
constexpr std::size_t kKeptChunks = 3000;
constexpr std::size_t kUploadsPerFrame = 24;
// The nearest runways are painted on the ground, pixel by pixel.
constexpr std::uint16_t kDrawnRunways = 8;
// So are the streets of the nearest towns and villages.
constexpr std::uint16_t kDrawnTowns = 16;
constexpr double kTownReach = 25000;
// Buildings are drawn further than trees: towns show from further off.
float earthBuildingReach(float sceneryDistance) { return std::clamp(sceneryDistance * 1.8f, 5000.f, 16000.f); }
// Object tiles uploaded per frame, and kept once out of reach.
constexpr std::size_t kObjectUploadsPerFrame = 4;
constexpr std::uint64_t kObjectTileLife = 600;

}  // namespace



void Renderer::useEarth(const EarthWorld& world, const EarthTerrain& terrain, const std::filesystem::path& cover) {
  auto state = std::make_unique<EarthState>();
  state->world = &world;
  state->terrain = &terrain;
  state->cover = std::make_unique<EarthCover>(cover);
  state->layout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();
  if (state->layout.getStride() != sizeof(EarthVertex)) throw std::runtime_error("Earth vertex layout mismatch");
  state->buildingLayout.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .add(bgfx::Attrib::TexCoord1, 4, bgfx::AttribType::Uint8, true)
      .end();
  if (state->buildingLayout.getStride() != sizeof(EarthBuildingVertex)) throw std::runtime_error("Earth building layout mismatch");
  const auto indices = EarthChunkBuilder::indices();
  state->indexCount = std::uint32_t(indices.size());
  state->indices = bgfx::createIndexBuffer(bgfx::copy(indices.data(), std::uint32_t(indices.size() * sizeof(std::uint16_t))));
  state->builder = std::make_unique<EarthChunkBuilder>(terrain, *state->cover);
  state->runways = bgfx::createUniform("u_earthRunways", bgfx::UniformType::Vec4, 2 * kDrawnRunways);
  state->airports = bgfx::createUniform("u_earthAirports", bgfx::UniformType::Vec4, 2 * kDrawnRunways);
  state->towns = bgfx::createUniform("u_earthTowns", bgfx::UniformType::Vec4, 3 * kDrawnTowns);
  state->chunkPattern = bgfx::createUniform("u_earthChunk", bgfx::UniformType::Vec4);
  state->counts = bgfx::createUniform("u_earthCounts", bgfx::UniformType::Vec4);
  state->objects = std::make_unique<EarthObjects>(terrain, *state->cover);
  state->objectBuilder = std::make_unique<EarthObjectBuilder>(*state->objects);
  earth_ = std::move(state);
  // The coarsest nodes are built now, so there is always a whole planet to draw.
  for (std::uint8_t face = 0; face < 6; ++face)
    for (std::uint32_t y = 0; y < 3; ++y)
      for (std::uint32_t x = 0; x < 3; ++x) {
        EarthChunkMesh mesh = earth_->builder->build({face, 0, x, y});
        EarthState::Chunk chunk;
        chunk.centre = mesh.centre;
        chunk.radius = mesh.radius;
        chunk.patternU = mesh.patternU;
        chunk.patternV = mesh.patternV;
        chunk.buffer = bgfx::createVertexBuffer(
            bgfx::copy(mesh.vertices.data(), std::uint32_t(mesh.vertices.size() * sizeof(EarthVertex))), earth_->layout);
        earth_->chunks[mesh.node.key()] = chunk;
      }
  log("RENDER", "Round Earth: " + std::to_string(terrain.tileCount()) + " pack tiles, " +
                    std::to_string(terrain.runways().size()) + " runways");
}

void Renderer::destroyEarth() {
  if (!earth_) return;
  if (earth_->placesJob.valid()) earth_->placesJob.wait();
  earth_->builder.reset();
  earth_->objectBuilder.reset();
  for (auto& [key, tile] : earth_->objectTiles) {
    if (bgfx::isValid(tile.trees)) bgfx::destroy(tile.trees);
    if (bgfx::isValid(tile.buildings)) bgfx::destroy(tile.buildings);
  }
  for (auto& map : earth_->maps) {
    if (map.job.valid()) map.job.wait();
    if (bgfx::isValid(map.texture)) bgfx::destroy(map.texture);
  }
  for (auto* uniform : {&earth_->airports, &earth_->towns, &earth_->chunkPattern, &earth_->counts})
    if (bgfx::isValid(*uniform)) bgfx::destroy(*uniform);
  for (auto& [key, chunk] : earth_->chunks)
    if (bgfx::isValid(chunk.buffer)) bgfx::destroy(chunk.buffer);
  if (bgfx::isValid(earth_->indices)) bgfx::destroy(earth_->indices);
  if (bgfx::isValid(earth_->runways)) bgfx::destroy(earth_->runways);
  earth_.reset();
}

void Renderer::drawEarth() {
  EarthState& e = *earth_;
  const EarthWorld& w = *e.world;
  ++e.frame;

  for (auto& mesh : e.builder->finished(kUploadsPerFrame)) {
    EarthState::Chunk chunk;
    chunk.centre = mesh.centre;
    chunk.radius = mesh.radius;
    chunk.patternU = mesh.patternU;
    chunk.patternV = mesh.patternV;
    chunk.used = e.frame;
    chunk.buffer = bgfx::createVertexBuffer(
        bgfx::copy(mesh.vertices.data(), std::uint32_t(mesh.vertices.size() * sizeof(EarthVertex))), e.layout);
    if (!e.everBuilt.insert(mesh.node.key()).second) ++e.rebuilt;
    if (const auto old = e.chunks.find(mesh.node.key()); old != e.chunks.end() && bgfx::isValid(old->second.buffer))
      bgfx::destroy(old->second.buffer);
    e.chunks[mesh.node.key()] = chunk;
  }

  const Vec3 eye = w.toEcef(lastCamera_.eye);
  const double height = std::max(w.altitude(lastCamera_.eye), 10.);
  const double radius = kEarthEquatorialRadius;
  const double horizon = std::sqrt(2 * radius * height + height * height) + std::sqrt(2 * radius * 9000.) + 10000;
  // Planes of the view frustum, for spheres in render space.
  std::array<glm::vec4, 4> planes;
  for (int i = 0; i < 4; ++i) {
    const int axis = i / 2;
    const float sign = i % 2 ? -1.f : 1.f;
    planes[std::size_t(i)] = glm::vec4(viewProj_[0][3] + sign * viewProj_[0][axis], viewProj_[1][3] + sign * viewProj_[1][axis],
                                       viewProj_[2][3] + sign * viewProj_[2][axis], viewProj_[3][3] + sign * viewProj_[3][axis]);
    planes[std::size_t(i)] /= glm::length(glm::vec3(planes[std::size_t(i)]));
  }
  const auto inView = [&](const Vec3& centre, double sphere) {
    const glm::vec3 c = localPosition(w.fromEcef(centre), origin_);
    for (const auto& plane : planes)
      if (glm::dot(glm::vec3(plane), c) + plane.w < -float(sphere)) return false;
    return true;
  };
  const auto ready = [&](const EarthNode& node) { return e.chunks.contains(node.key()); };

  e.builder->clearQueue();
  e.drawn.clear();
  e.nextSplit.clear();
  const auto visit = [&](auto& self, const EarthNode& node) -> void {
    const auto found = e.chunks.find(node.key());
    const double size = EarthChunkBuilder::nodeSize(node.level);
    const Vec3 centre = found != e.chunks.end() ? found->second.centre : EarthChunkBuilder::nodeCentre(node);
    const double sphere = found != e.chunks.end() ? found->second.radius : size * .75 + 9000;
    const double distance = (centre - eye).norm();
    if (distance - sphere > horizon || !inView(centre, sphere)) return;
    if (found != e.chunks.end()) found->second.used = e.frame;
    const double reach = (e.split.contains(node.key()) ? kMerge : kSplit) * size;
    if (node.level < kEarthMaxLevel && distance - sphere < reach) {
      bool all = true;
      for (int i = 0; i < 4; ++i) {
        const EarthNode child = node.child(i);
        // Children out of view still decide whether this node splits, so they are kept.
        if (const auto kept = e.chunks.find(child.key()); kept != e.chunks.end()) kept->second.used = e.frame;
        if (!ready(child)) {
          all = false;
          e.builder->request(child, (EarthChunkBuilder::nodeCentre(child) - eye).norm() / EarthChunkBuilder::nodeSize(child.level));
        }
      }
      if (all) {
        e.nextSplit.insert(node.key());
        for (int i = 0; i < 4; ++i) self(self, node.child(i));
        return;
      }
    }
    if (found != e.chunks.end()) {
      e.drawn.push_back(node);
    } else {
      e.builder->request(node, distance / size);
    }
  };
  for (std::uint8_t face = 0; face < 6; ++face)
    for (std::uint32_t y = 0; y < 3; ++y)
      for (std::uint32_t x = 0; x < 3; ++x) visit(visit, {face, 0, x, y});
  std::swap(e.split, e.nextSplit);

  if (e.frame % 300 == 1) {
    std::size_t trees = 0, buildings = 0;
    for (const auto& [key, tile] : e.objectTiles) {
      trees += tile.broadleaf + tile.conifer;
      buildings += tile.vertices;
    }
    log("EARTH", "frame " + std::to_string(e.frame) + ": " + std::to_string(stats_.fps) + " fps, " + std::to_string(stats_.drawCalls) +
                     " draws last frame; " + std::to_string(e.objectTiles.size()) + " object tiles, " + std::to_string(trees) + " trees, " +
                     std::to_string(buildings / 3) + " building triangles; " + std::to_string(e.objectBuilder->built_.load()) +
                     " tiles built, " +
                     std::to_string(e.objectBuilder->built_.load() ? 1000 * e.objectBuilder->buildSeconds_.load() / double(e.objectBuilder->built_.load()) : 0.) +
                     " ms each");
    int finest = 0;
    for (const auto& node : e.drawn) finest = std::max<int>(finest, node.level);
    log("EARTH", "frame " + std::to_string(e.frame) + ": " + std::to_string(e.drawn.size()) + " chunks drawn of " +
                     std::to_string(e.chunks.size()) + " built, finest level " + std::to_string(finest) + "; eye " +
                     std::to_string(height) + " m; " + std::to_string(e.builder->built_.load()) + " chunks built, " +
                     std::to_string(e.builder->built_.load() ? 1000 * e.builder->buildSeconds_.load() / double(e.builder->built_.load()) : 0.) +
                     " ms each, " + std::to_string(e.rebuilt) + " rebuilt");
  }
  // ECEF axes to render axes, the same for every chunk.
  glm::mat4 rotation{1};
  rotation[0] = glm::vec4(renderDirection(w.directionFromEcef({1, 0, 0})), 0);
  rotation[1] = glm::vec4(renderDirection(w.directionFromEcef({0, 1, 0})), 0);
  rotation[2] = glm::vec4(renderDirection(w.directionFromEcef({0, 0, 1})), 0);
  // Each runway as two render-space ends: the first with the half width, the
  // second with the length; none closer than 60 km leaves the slot empty.
  std::array<glm::vec4, 2 * kDrawnRunways> runways{}, airports{};
  int runwayCount = 0, townCount = 0;
  // Spheres reaching as far as each runway's airport and each town, to tell
  // which of them each chunk has to look at.
  std::array<std::pair<Vec3, double>, kDrawnRunways> runwayReach{};
  std::array<std::pair<Vec3, double>, kDrawnTowns> townReach{};
  {
    std::vector<std::pair<double, const EarthRunway*>> near;
    for (const auto& r : e.terrain->runways()) {
      const double d = (r.centre - eye).norm();
      if (d < 60000) near.push_back({d, &r});
    }
    std::sort(near.begin(), near.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    runwayCount = int(std::min<std::size_t>(near.size(), kDrawnRunways));
    for (std::size_t i = 0; i < near.size() && i < kDrawnRunways; ++i) {
      const EarthRunway& r = *near[i].second;
      runwayReach[i] = {r.centre + r.up * r.altitude[0], r.length * .5 + 1200};
      const EarthAirportLayout& l = e.objects->airport(std::size_t(near[i].second - e.terrain->runways().data()));
      if (l.primary) {
        airports[2 * i] = glm::vec4(l.side, l.taxiOffset, l.taxiHalfWidth, float(l.kind));
        airports[2 * i + 1] = glm::vec4(l.apronAlong, l.apronHalfLength, l.apronDepth, 1);
      }
      for (int end = 0; end < 2; ++end) {
        const Vec3 at = r.ends[end] + r.up * r.altitude[end];
        runways[2 * i + std::size_t(end)] = glm::vec4(localPosition(w.fromEcef(at), origin_),
                                                       end == 0 ? float(r.width * .5) : float(r.length));
      }
    }
  }
  // The nearest towns and villages: centre and radius, then the axes of their streets.
  std::array<glm::vec4, 3 * kDrawnTowns> towns{};
  {
    const Vec3 below = eye - eye.normalized() * height;
    // Looked up again on a background thread once the camera has moved on.
    if (e.placesJob.valid() && e.placesJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) e.places = e.placesJob.get();
    if (!e.placesJob.valid() && (e.placesAsked || (e.placesAt - below).norm() > 1500)) {
      e.placesAt = below;
      e.placesAsked = false;
      e.placesJob = std::async(std::launch::async, [objects = e.objects.get(), below] { return objects->placesNear(below, kTownReach); });
    }
    for (std::size_t i = 0; i < e.places.size() && townCount < int(kDrawnTowns); ++i) {
      const EarthPlace& place = e.places[i];
      // Only those reaching into view range are worth a loop in the shader.
      if ((place.centre - below).norm() > place.radius * 1.25 + std::max(15000., height * 3)) continue;
      const std::size_t slot = std::size_t(townCount++);
      townReach[slot] = {place.centre, place.radius * 1.3 + 3000};
      towns[3 * slot] = glm::vec4(localPosition(w.fromEcef(place.centre), origin_), place.radius);
      towns[3 * slot + 1] = glm::vec4(renderDirection(w.directionFromEcef(place.east)), float(place.kind));
      towns[3 * slot + 2] = glm::vec4(renderDirection(w.directionFromEcef(place.south)), 0);
    }
  }
  const float buildingReach = earthBuildingReach(settings_.sceneryDistance);
  const float latitude = float(std::abs(w.geodetic(lastCamera_.eye).latitude) * 180 / kPi);
  const std::uint32_t wrap = settings_.anisotropic ? BGFX_SAMPLER_MIN_ANISOTROPIC : BGFX_SAMPLER_NONE;
  // Trees and buildings first, then the ground nearest first, so the depth
  // test turns away the ground's dear pixels wherever something stands before
  // them.
  drawEarthObjects(kViewWorld, nullptr);
  std::vector<std::pair<double, std::size_t>> order(e.drawn.size());
  for (std::size_t i = 0; i < e.drawn.size(); ++i) {
    const auto& chunk = e.chunks.at(e.drawn[i].key());
    order[i] = {(chunk.centre - eye).norm() - chunk.radius, i};
  }
  std::sort(order.begin(), order.end());
  bool first = true;
  for (const auto& [away, index] : order) {
    const EarthNode& node = e.drawn[index];
    const auto& chunk = e.chunks.at(node.key());
    glm::mat4 model = rotation;
    model[3] = glm::vec4(localPosition(w.fromEcef(chunk.centre), origin_), 1);
    // Textures are bound for each draw; uniforms the same for every chunk
    // are set with the first, and kept by bgfx for the rest, which follow it
    // in this sequential view.
    bindLighting();
    bgfx::setTexture(1, uniforms_.terrainAlbedo, terrainAlbedo_, wrap);
    bgfx::setTexture(2, uniforms_.terrainNormal, terrainNormal_, wrap);
    bgfx::setTexture(5, uniforms_.waterNormal, waterNormal_, wrap);
    bgfx::setUniform(uniforms_.model, glm::value_ptr(model));
    if (first) {
      first = false;
      bindFrame(viewProj_);
      bgfx::setUniform(e.runways, runways.data(), 2 * kDrawnRunways);
      bgfx::setUniform(e.airports, airports.data(), 2 * kDrawnRunways);
      bgfx::setUniform(e.towns, towns.data(), 3 * kDrawnTowns);
    }
    // Which towns and runways reach this chunk, as bits.
    float townMask = 0, runwayMask = 0;
    for (int i = 0; i < townCount; ++i)
      if ((townReach[std::size_t(i)].first - chunk.centre).norm() < townReach[std::size_t(i)].second + chunk.radius) townMask += float(1 << i);
    for (int i = 0; i < runwayCount; ++i)
      if ((runwayReach[std::size_t(i)].first - chunk.centre).norm() < runwayReach[std::size_t(i)].second + chunk.radius) runwayMask += float(1 << i);
    bgfx::setUniform(e.counts, glm::value_ptr(glm::vec4(townMask, runwayMask, 0, 0)));
    bgfx::setUniform(e.chunkPattern, glm::value_ptr(glm::vec4(chunk.patternU, chunk.patternV, buildingReach, latitude)));
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                   BGFX_STATE_MSAA);
    bgfx::setVertexBuffer(0, chunk.buffer);
    bgfx::setIndexBuffer(e.indices);
    bgfx::submit(kViewWorld, programs_.earth);
    ++stats_.drawCalls;
    stats_.triangles += e.indexCount / 3;
  }

  // Forget the chunks least lately drawn once there are too many, never the roots.
  if (e.chunks.size() > kKeptChunks) {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> byAge;
    for (const auto& [key, chunk] : e.chunks)
      if ((key >> 48 & 0xff) != 0 && chunk.used + 2 < e.frame) byAge.push_back({chunk.used, key});
    std::sort(byAge.begin(), byAge.end());
    for (std::size_t i = 0; i < byAge.size() && e.chunks.size() > kKeptChunks; ++i) {
      bgfx::destroy(e.chunks.at(byAge[i].second).buffer);
      e.chunks.erase(byAge[i].second);
    }
  }
}

// ---------------------------------------------------------------------------
// Trees, towns and airport buildings
// ---------------------------------------------------------------------------

void Renderer::updateEarthObjects() {
  EarthState& e = *earth_;
  const EarthWorld& w = *e.world;
  const int density = settings_.vegetation ? settings_.treeDensity : 0;
  const auto release = [](EarthState::ObjectTile& tile) {
    if (bgfx::isValid(tile.trees)) bgfx::destroy(tile.trees);
    if (bgfx::isValid(tile.buildings)) bgfx::destroy(tile.buildings);
  };
  if (density != e.objectDensity) {
    e.objectBuilder->clearQueue();
    for (auto& tile : e.objectBuilder->finished(1000)) (void)tile;
    for (auto& [key, tile] : e.objectTiles) release(tile);
    e.objectTiles.clear();
    e.objectDensity = density;
  }
  for (auto& built : e.objectBuilder->finished(kObjectUploadsPerFrame)) {
    EarthState::ObjectTile tile;
    tile.centre = built.centre;
    tile.east = built.east;
    tile.up = built.up;
    tile.south = built.south;
    tile.radius = built.radius;
    tile.low = built.low;
    tile.high = built.high;
    tile.used = frameIndex_;
    if (!built.trees.empty()) {
      std::vector<float> data;
      data.reserve(built.trees.size() * 8);
      for (const TreeInstance& t : built.trees) data.insert(data.end(), {t.east, t.up, t.south, t.height, t.spread, t.yaw, t.tint, t.conifer});
      tile.trees = bgfx::createVertexBuffer(bgfx::copy(data.data(), std::uint32_t(data.size() * sizeof(float))), instanceLayout_);
      tile.broadleaf = built.broadleaf;
      tile.conifer = std::uint32_t(built.trees.size()) - built.broadleaf;
    }
    if (!built.merged.empty()) {
      tile.buildings = bgfx::createVertexBuffer(
          bgfx::copy(built.merged.data(), std::uint32_t(built.merged.size() * sizeof(EarthBuildingVertex))), e.buildingLayout);
      tile.vertices = std::uint32_t(built.merged.size());
      tile.casters = built.casters;
    }
    if (const auto old = e.objectTiles.find(built.node.key()); old != e.objectTiles.end()) release(old->second);
    e.objectTiles[built.node.key()] = tile;
  }

  // The tiles within reach of the camera: sampled on a grid about the point
  // below it, so tiles across a face's edge are found as well.
  const Vec3 eye = w.toEcef(lastCamera_.eye);
  const double height = std::max(w.altitude(lastCamera_.eye), 0.);
  const double reach = earthBuildingReach(settings_.sceneryDistance) + 1000;
  const Surface surface = surfaceFromEcef(eye);
  const Vec3 up{surface.cosLat * surface.cosLon, surface.cosLat * surface.sinLon, surface.sinLat};
  const Vec3 east{-surface.sinLon, surface.cosLon, 0}, north = up.cross(east);
  const double ground = std::sqrt(std::max(0., reach * reach - height * height));
  // Searched again only once the camera has moved on a little, or now and then.
  const bool search = (eye - e.searchedAt).norm() > 150 || frameIndex_ - e.searchedFrame > 30;
  if (search) {
    e.searchedAt = eye;
    e.searchedFrame = frameIndex_;
    e.objectBuilder->clearQueue();
    e.wanted.clear();
  }
  for (const auto key : e.wanted)
    if (const auto found = e.objectTiles.find(key); found != e.objectTiles.end()) found->second.used = frameIndex_;
  if (search && ground > 0) {
    const double gridStep = 700;
    const int span = int(std::ceil(ground / gridStep));
    std::unordered_set<std::uint64_t> seen;
    for (int j = -span; j <= span; ++j)
      for (int i = -span; i <= span; ++i) {
        const double x = i * gridStep, y = j * gridStep;
        if (x * x + y * y > (ground + gridStep) * (ground + gridStep)) continue;
        const Vec3 at = (up * kEarthEquatorialRadius + east * x + north * y).normalized();
        const EarthNode node = EarthObjects::tileOf(at);
        if (!seen.insert(node.key()).second) continue;
        e.wanted.push_back(node.key());
        if (const auto found = e.objectTiles.find(node.key()); found != e.objectTiles.end()) {
          found->second.used = frameIndex_;
        } else {
          e.objectBuilder->request(node, std::hypot(x, y), density);
        }
      }
  }
  for (auto it = e.objectTiles.begin(); it != e.objectTiles.end();) {
    if (frameIndex_ - it->second.used > kObjectTileLife) {
      release(it->second);
      it = e.objectTiles.erase(it);
    } else {
      ++it;
    }
  }
  // Each tile's frame in render space, for this frame.
  for (auto& [key, tile] : e.objectTiles) {
    glm::mat4 model{1};
    model[0] = glm::vec4(renderDirection(w.directionFromEcef(tile.east)), 0);
    model[1] = glm::vec4(renderDirection(w.directionFromEcef(tile.up)), 0);
    model[2] = glm::vec4(renderDirection(w.directionFromEcef(tile.south)), 0);
    model[3] = glm::vec4(localPosition(w.fromEcef(tile.centre), origin_), 1);
    tile.model = model;
    tile.middle = glm::vec3(model[3]) + glm::vec3(model[1]) * (.5f * (tile.low + tile.high));
  }
}

void Renderer::drawEarthObjects(bgfx::ViewId view, const Cascade* cascade) {
  EarthState& e = *earth_;
  const float buildingReach = earthBuildingReach(settings_.sceneryDistance);
  for (auto& [key, tile] : e.objectTiles) {
    const float radius = std::hypot(tile.radius, .5f * (tile.high - tile.low));
    const float distance = glm::length(tile.middle - cameraEye_);
    if (distance - radius > buildingReach) continue;
    if (cascade) {
      const glm::vec3 offset = tile.middle - cascade->center;
      const glm::vec3 lateral = offset - sun_ * glm::dot(offset, sun_);
      if (glm::length(lateral) > cascade->radius + radius) continue;
    } else if (!sphereVisible(tile.middle, radius, buildingReach)) {
      continue;
    }
    // The buildings, in one draw; the shadow pass draws those that cast one,
    // which come first.
    if (bgfx::isValid(tile.buildings) && (!cascade || tile.casters)) {
      bgfx::setUniform(uniforms_.model, glm::value_ptr(tile.model));
      if (cascade) {
        applyMaterial(Material{}, nullptr, 0);
        bgfx::setVertexBuffer(0, tile.buildings, 0, tile.casters);
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::setUniform(uniforms_.lightViewProj, glm::value_ptr(cascade->lightViewProj));
        bgfx::submit(view, programs_.shadow);
      } else {
        const glm::mat3 normalMatrix(tile.model);
        bindFrame(viewProj_);
        bindLighting();
        bgfx::setUniform(uniforms_.normalMatrix, glm::value_ptr(normalMatrix));
        bgfx::setState(kOpaqueState | BGFX_STATE_MSAA);
        bgfx::setVertexBuffer(0, tile.buildings);
        bgfx::submit(view, programs_.earthBuilding);
      }
      ++stats_.drawCalls;
      stats_.triangles += (cascade ? tile.casters : tile.vertices) / 3;
    }
    // Trees, only as far as the valley's scenery reaches.
    if (!settings_.vegetation || !bgfx::isValid(tile.trees) || distance - radius > settings_.sceneryDistance) continue;
    const int lod = distance - radius * .5f < (cascade ? 500.f : 1300.f) * std::exp2(-settings_.lodBias) ? 0 : 1;
    for (int species = 0; species < 2; ++species) {
      const std::uint32_t count = species == 0 ? tile.broadleaf : tile.conifer;
      if (!count) continue;
      const TreeMesh& mesh = treeMeshes_[species][lod];
      if (cascade) {
        bgfx::setUniform(uniforms_.lightViewProj, glm::value_ptr(cascade->lightViewProj));
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
      } else {
        bindFrame(viewProj_);
        bindLighting();
        bgfx::setState((kOpaqueState & ~BGFX_STATE_WRITE_A) | BGFX_STATE_MSAA | BGFX_STATE_BLEND_ALPHA_TO_COVERAGE);
      }
      bgfx::setUniform(uniforms_.model, glm::value_ptr(tile.model));
      bgfx::setVertexBuffer(0, mesh.vertices);
      bgfx::setInstanceDataBuffer(tile.trees, species == 0 ? 0 : tile.broadleaf, count);
      bgfx::submit(view, cascade ? programs_.shadowTree : programs_.tree);
      ++stats_.drawCalls;
      stats_.triangles += count * (mesh.count / 3);
      if (!cascade) stats_.treesDrawn += count;
    }
  }
}

// ---------------------------------------------------------------------------
// Maps
// ---------------------------------------------------------------------------

void Renderer::requestEarthMap(int which, const EarthMapView& view, int size) {
  if (!earth_) return;
  auto& map = earth_->maps[std::size_t(which)];
  if (map.job.valid()) {
    if (map.job.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    EarthMapImage image = map.job.get();
    if (!bgfx::isValid(map.texture) || map.size != image.size) {
      if (bgfx::isValid(map.texture)) bgfx::destroy(map.texture);
      map.texture = bgfx::createTexture2D(std::uint16_t(image.size), std::uint16_t(image.size), false, 1, bgfx::TextureFormat::RGBA8,
                                          BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
      map.size = image.size;
    }
    bgfx::updateTexture2D(map.texture, 0, 0, 0, 0, std::uint16_t(image.size), std::uint16_t(image.size),
                          bgfx::copy(image.rgba.data(), std::uint32_t(image.rgba.size())));
    map.shown = image.view;
    map.ready = true;
  }
  if (map.ready && map.shown.halfSpan == view.halfSpan && map.shown.latitude == view.latitude &&
      map.shown.longitude == view.longitude && map.size == size)
    return;
  map.job = std::async(std::launch::async, [terrain = earth_->terrain, cover = earth_->cover.get(), view, size] {
    return buildEarthMap(*terrain, *cover, view, size);
  });
}

std::uintptr_t Renderer::earthMapTexture(int which, EarthMapView& shown) const {
  if (!earth_) return 0;
  const auto& map = earth_->maps[std::size_t(which)];
  if (!map.ready || !bgfx::isValid(map.texture)) return 0;
  shown = map.shown;
  return std::uintptr_t(map.texture.idx) + 1;
}

}  // namespace ofs::client
