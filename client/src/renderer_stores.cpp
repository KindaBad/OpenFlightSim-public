// Missiles and the pylons they hang from.
//
// A store is a small procedural airframe drawn with the same surface shader as
// the aircraft. It is placed by a body-to-render matrix, so a missile on a
// pylon and the same missile in flight are one mesh under two transforms.

#include "renderer.hpp"
#include "renderer_internal.hpp"

#include "coordinates.hpp"
#include "log.hpp"
#include "missile_mesh.hpp"
#include "nuclear_cloud.hpp"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace ofs::client {
namespace {

// Pylon proportions relative to the store it carries.
constexpr float kPylonLength = .46f, kPylonWidth = .075f;
// Beyond this the paint bands and nozzle are sub-pixel and are not drawn.
constexpr double kDetailRange = 250, kTrimRange = 900;

Material storeMaterial(weapons::WeaponType type, StorePart part) {
  const bool infrared = type == weapons::WeaponType::Infrared;
  Material material;
  const auto set = [&](float r, float g, float b, float metallic, float roughness) {
    material.baseColor[0] = r; material.baseColor[1] = g; material.baseColor[2] = b;
    material.metallic = metallic; material.roughness = roughness;
  };
  if (weapons::isBomb(type)) {
    // Olive drab with a yellow nose band; the big one is white with a red band.
    const bool nuclear = type == weapons::WeaponType::Nuclear;
    if (part == StorePart::Band) { if (nuclear) set(.70f, .06f, .04f, 0, .6f); else set(.80f, .62f, .05f, 0, .6f); }
    else if (nuclear) set(.82f, .82f, .80f, .1f, .4f);
    else set(.20f, .23f, .14f, 0, .7f);
    return material;
  }
  switch (part) {
    case StorePart::Body:
      if (infrared) set(.56f, .58f, .60f, .15f, .42f); else set(.74f, .75f, .74f, .1f, .46f);
      break;
    case StorePart::Seeker:
      // A dark glass dome over a heat seeker; a ceramic radome over a radar.
      if (infrared) { set(.012f, .016f, .022f, 0, .05f); material.environmentReflection = 1; }
      else set(.70f, .66f, .56f, 0, .55f);
      break;
    case StorePart::Band:
      if (infrared) set(.72f, .50f, .04f, 0, .6f); else set(.38f, .22f, .10f, 0, .6f);
      break;
    default:
      set(.035f, .033f, .032f, .6f, .7f);
      break;
  }
  return material;
}

glm::mat4 storeMatrix(const Vec3& position, const Quat& attitude, const Vec3& origin) {
  glm::mat4 m{1};
  m[0] = glm::vec4(renderDirection(attitude.rotate({1, 0, 0})), 0);
  m[1] = glm::vec4(renderDirection(attitude.rotate({0, 1, 0})), 0);
  m[2] = glm::vec4(renderDirection(attitude.rotate({0, 0, 1})), 0);
  m[3] = glm::vec4(localPosition(position, origin), 1);
  return m;
}

}  // namespace

// Distance from the top of each station's store up to the airframe skin, found
// by casting straight up through the full-detail mesh. The pylon is stretched
// to that gap, so it meets the wing whatever the model's section is there.
std::array<float, 8> measurePylonHeights(const GpuMesh& mesh, AircraftType type) {
  std::array<float, 8> heights{};
  weapons::Inventory inventory;
  inventory.reset(type);
  if (inventory.stations.empty() || mesh.levels.empty()) return heights;
  const LodLevel& level = mesh.levels.front();
  const Vec3 cg = aircraftDefinition(type).visual.assetCg;
  for (std::size_t i = 0; i < inventory.stations.size() && i < heights.size(); ++i) {
    const auto& station = inventory.stations[i];
    const auto& definition = weapons::missileDefinition(station.mounted);
    const double top = station.position.z - definition.diameter * .5;
    double gap = 1e9;
    for (const double along : {-.4, 0., .4}) {
      // Body reference axes to asset axes: X aft, Y up, Z port.
      const double x = cg.x - (station.position.x + along * kPylonLength * definition.length);
      const double z = cg.z - station.position.y;
      for (const Batch& batch : level.batches) {
        if (batch.transformNode >= 0) continue;  // gear and control surfaces move
        if (x < batch.boundsMin[0] || x > batch.boundsMax[0] || z < batch.boundsMin[2] || z > batch.boundsMax[2]) continue;
        for (std::uint32_t index = batch.firstIndex; index + 2 < batch.firstIndex + batch.indexCount; index += 3) {
          const float* a = &level.vertices[level.indices[index] * kMeshVertexFloats];
          const float* b = &level.vertices[level.indices[index + 1] * kMeshVertexFloats];
          const float* c = &level.vertices[level.indices[index + 2] * kMeshVertexFloats];
          const auto edge = [&](const float* p, const float* q) {
            return (q[0] - p[0]) * (z - p[2]) - (q[2] - p[2]) * (x - p[0]);
          };
          const double w0 = edge(b, c), w1 = edge(c, a), w2 = edge(a, b), sum = w0 + w1 + w2;
          if (std::abs(sum) < 1e-12 || !((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))) continue;
          // Height in the body's down axis; only skin above the store counts.
          const double down = cg.y - (w0 * a[1] + w1 * b[1] + w2 * c[1]) / sum;
          if (down < top + .02) gap = std::min(gap, top - down);
        }
      }
    }
    // A store drawn into the skin gets no pylon; one far below gets a stub.
    heights[i] = gap > 1e8 ? .14f : static_cast<float>(std::clamp(gap, 0., .6));
  }
  return heights;
}

void Renderer::createStoreMeshes() {
  const auto upload = [&](const std::vector<SurfaceVertex>& data) {
    PartBuffer buffer;
    if (data.empty()) return buffer;
    buffer.vertices = bgfx::createVertexBuffer(
        bgfx::copy(data.data(), static_cast<std::uint32_t>(data.size() * sizeof(SurfaceVertex))), surfaceLayout_);
    buffer.count = static_cast<std::uint32_t>(data.size());
    return buffer;
  };
  for (int type = 0; type < int(storeMeshes_.size()); ++type)
    for (int detail = 0; detail < 2; ++detail) {
      const StoreMesh mesh = buildStoreMesh(weapons::WeaponType(type + 1), detail);
      for (std::size_t part = 0; part < mesh.parts.size(); ++part)
        storeMeshes_[type][detail][part] = upload(mesh.parts[part]);
    }
  pylonMesh_ = upload(buildPylonMesh());
  for (std::size_t part = 0; part < cloudMeshes_.size(); ++part) cloudMeshes_[part] = upload(buildCloudPart(CloudPart(part)));
}

void Renderer::drawNuclearClouds() {
  for (const auto& cloud : combat_.nuclearClouds()) {
    const glm::vec3 ground = localPosition(cloud.ground, origin_);
    for (const auto& part : cloudParts(cloud.age)) {
      const PartBuffer& mesh = cloudMeshes_[std::size_t(part.part)];
      if (!bgfx::isValid(mesh.vertices) || part.radius <= 0 || part.height <= 0) continue;
      glm::mat4 model = glm::translate(glm::mat4{1}, ground + glm::vec3(0, float(part.up), 0));
      model = glm::rotate(model, float(part.turn), glm::vec3(0, 1, 0));
      model = glm::scale(model, glm::vec3(float(part.radius), float(part.height), float(part.radius)));
      Material material;
      material.metallic = 0;
      material.roughness = 1;
      for (int i = 0; i < 3; ++i) { material.baseColor[i] = part.color[i]; material.emissive[i] = part.emissive[i]; }
      material.baseColor[3] = part.alpha;
      const bool fading = part.alpha < .995f;
      if (fading) material.alpha = Material::Alpha::Blend;
      bindFrame(viewProj_);
      bindLighting();
      bgfx::setUniform(uniforms_.model, glm::value_ptr(model));
      bgfx::setUniform(uniforms_.normalMatrix, glm::value_ptr(glm::inverseTranspose(glm::mat3(model))));
      bgfx::setState((fading ? kBlendState : kOpaqueState) | BGFX_STATE_MSAA);
      bgfx::setVertexBuffer(0, mesh.vertices);
      applyMaterial(material);
      bgfx::submit(kViewWorld, programs_.pbr);
      ++stats_.drawCalls;
      stats_.triangles += mesh.count / 3;
    }
  }
}

void Renderer::drawStores(const CombatVisuals& combat, const Camera& camera, bool flightDeck) {
  if (combat.stores.empty() && combat.pylons.empty()) return;
  const bgfx::ViewId view = flightDeck ? kViewCockpit : kViewWorld;
  const glm::mat4& viewProj = flightDeck ? cockpitViewProj_ : viewProj_;
  const bool ownHidden = camera.hidesOwnAircraft();
  const auto submit = [&](const PartBuffer& part, const glm::mat4& model, const Material& material) {
    if (!bgfx::isValid(part.vertices)) return;
    bindFrame(viewProj);
    bindLighting();
    bgfx::setUniform(uniforms_.model, glm::value_ptr(model));
    bgfx::setUniform(uniforms_.normalMatrix, glm::value_ptr(glm::inverseTranspose(glm::mat3(model))));
    bgfx::setState(kOpaqueState | BGFX_STATE_MSAA);
    bgfx::setVertexBuffer(0, part.vertices);
    applyMaterial(material);
    bgfx::submit(view, programs_.pbr);
    ++stats_.drawCalls;
    stats_.triangles += part.count / 3;
  };
  for (const auto& store : combat.stores) {
    if ((store.onLocalAircraft && ownHidden) != flightDeck) continue;
    const double distance = (store.position - camera.eye).norm();
    if (distance > settings_.renderDistance) continue;
    const auto index = std::size_t(store.type) - 1;
    if (index >= storeMeshes_.size()) continue;
    const auto& parts = storeMeshes_[index][distance < kDetailRange];
    const glm::mat4 model = storeMatrix(store.position, store.attitude, origin_);
    for (std::size_t part = 0; part < parts.size(); ++part) {
      if (distance > kTrimRange && part > std::size_t(StorePart::Seeker)) continue;
      Material material = storeMaterial(store.type, StorePart(part));
      if (store.burning && StorePart(part) == StorePart::Nozzle) {
        // White-hot inside the throat while the motor burns.
        material.emissive[0] = 9; material.emissive[1] = 5.5f; material.emissive[2] = 2;
      }
      submit(parts[part], model, material);
    }
  }
  Material metal;
  metal.baseColor[0] = .30f; metal.baseColor[1] = .32f; metal.baseColor[2] = .34f;
  metal.metallic = .3f; metal.roughness = .55f;
  for (const auto& pylon : combat.pylons) {
    if ((pylon.onLocalAircraft && ownHidden) != flightDeck) continue;
    const auto asset = models_.find(pylon.aircraft);
    if (asset == models_.end() || pylon.station >= asset->second.pylonHeight.size()) continue;
    const float height = asset->second.pylonHeight[pylon.station];
    if (height < .02f || (pylon.position - camera.eye).norm() > kTrimRange * 2) continue;
    const auto& definition = weapons::missileDefinition(pylon.type);
    glm::mat4 model = storeMatrix(pylon.position, pylon.attitude, origin_);
    // The skin end is sunk a little so the pylon never shows a gap on a curved wing.
    model = glm::translate(model, glm::vec3(0, 0, -static_cast<float>(definition.diameter * .5)));
    model = glm::scale(model, glm::vec3(kPylonLength * static_cast<float>(definition.length), kPylonWidth, height + .03f));
    submit(pylonMesh_, model, metal);
  }
}

void Renderer::createChuteMeshes() {
  const ChuteMesh mesh = buildChuteMesh();
  for (std::size_t part = 0; part < mesh.parts.size(); ++part) {
    const auto& data = mesh.parts[part];
    if (data.empty()) continue;
    chuteMeshes_[part].vertices = bgfx::createVertexBuffer(
        bgfx::copy(data.data(), static_cast<std::uint32_t>(data.size() * sizeof(SurfaceVertex))), surfaceLayout_);
    chuteMeshes_[part].count = static_cast<std::uint32_t>(data.size());
  }
}

void Renderer::drawPilots(const CombatVisuals& combat) {
  for (const EjectedPilot& pilot : combat.pilots) {
    const double distance = (pilot.position - lastCamera_.eye).norm();
    if (distance > 9000) continue;
    const auto axes = pilotAxes(pilot);
    glm::mat4 body{1};
    for (int axis = 0; axis < 3; ++axis) body[axis] = glm::vec4(renderDirection(axes[axis]), 0);
    body[3] = glm::vec4(localPosition(pilot.position, origin_), 1);
    // The canopy streams out behind as a narrow sleeve, then fills. On the
    // ground it falls slack.
    const float open = static_cast<float>(pilot.canopy);
    const float width = pilot.landed > 0 ? .25f + .75f * open : .1f + .9f * open * open;
    const float length = pilot.landed > 0 ? .12f + .88f * open : .82f + .18f * open;
    const glm::mat4 rigging = glm::scale(body, glm::vec3(width, width, length));
    for (std::size_t index = 0; index < chuteMeshes_.size(); ++index) {
      const ChutePart part = ChutePart(index);
      const bool cloth = part == ChutePart::Panels || part == ChutePart::Stripes || part == ChutePart::Lines;
      if (!bgfx::isValid(chuteMeshes_[index].vertices)) continue;
      if (cloth && pilot.canopy <= 0) continue;
      if (part == ChutePart::Seat && pilot.canopy > .35) continue;
      // From far off only the canopy can be made out.
      if (!cloth && distance > 1500) continue;
      if (part == ChutePart::Lines && distance > 600) continue;
      Material material;
      material.metallic = 0;
      material.roughness = .85f;
      material.doubleSided = cloth;
      const glm::vec3 colour = part == ChutePart::Figure ? glm::vec3(.12f, .15f, .09f)
          : part == ChutePart::Helmet ? glm::vec3(.62f, .64f, .62f)
          : part == ChutePart::Seat ? glm::vec3(.05f, .055f, .06f)
          : part == ChutePart::Panels ? glm::vec3(.78f, .76f, .70f)
          : part == ChutePart::Stripes ? glm::vec3(.82f, .26f, .04f) : glm::vec3(.09f, .09f, .08f);
      material.baseColor[0] = colour.r; material.baseColor[1] = colour.g; material.baseColor[2] = colour.b;
      const glm::mat4& model = cloth ? rigging : body;
      bindFrame(viewProj_);
      bindLighting();
      bgfx::setUniform(uniforms_.model, glm::value_ptr(model));
      bgfx::setUniform(uniforms_.normalMatrix, glm::value_ptr(glm::inverseTranspose(glm::mat3(model))));
      bgfx::setState((cloth ? kOpaqueState & ~BGFX_STATE_CULL_MASK : kOpaqueState) | BGFX_STATE_MSAA);
      bgfx::setVertexBuffer(0, chuteMeshes_[index].vertices);
      applyMaterial(material);
      bgfx::submit(kViewWorld, programs_.pbr);
      ++stats_.drawCalls;
      stats_.triangles += chuteMeshes_[index].count / 3;
    }
  }
}

void Renderer::drawMissilePlumes(const CombatVisuals& combat) {
  if (settings_.effects == EffectsQuality::Off || combat.missiles.empty()) return;
  ensureFlameMesh();
  if (!bgfx::isValid(flameMesh_)) return;
  for (const auto& missile : combat.missiles) {
    if (!missile.powered || (missile.position - lastCamera_.eye).norm() > 6000) continue;
    // A rocket plume several body lengths long, narrower than a jet's and far
    // brighter. It grows over the first instants as the motor comes up.
    const float burn = static_cast<float>(std::clamp(missile.age * 6, .35, 1.));
    const Vec3 nozzle = missile.position + missile.attitude.rotate({-missile.length * .5, 0, 0});
    const float length = static_cast<float>(missile.length * .62), radius = static_cast<float>(missile.diameter * 3.6);
    const glm::mat4 matrix = storeMatrix(nozzle, missile.attitude, origin_) * glm::scale(glm::mat4{1}, glm::vec3(length, radius, radius));
    drawFlame(matrix, localPosition(nozzle, origin_), static_cast<float>(missile.diameter * 2.4), burn,
              static_cast<float>(missile.id % 251), true);
  }
  bgfx::discard();
}

}  // namespace ofs::client
