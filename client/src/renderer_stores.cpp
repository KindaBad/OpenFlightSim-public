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
  for (int type = 0; type < 2; ++type)
    for (int detail = 0; detail < 2; ++detail) {
      const StoreMesh mesh = buildStoreMesh(type ? weapons::WeaponType::ActiveRadar : weapons::WeaponType::Infrared, detail);
      for (std::size_t part = 0; part < mesh.parts.size(); ++part)
        storeMeshes_[type][detail][part] = upload(mesh.parts[part]);
    }
  pylonMesh_ = upload(buildPylonMesh());
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
    const auto& parts = storeMeshes_[store.type == weapons::WeaponType::ActiveRadar][distance < kDetailRange];
    const glm::mat4 model = storeMatrix(store.position, store.attitude, origin_);
    for (std::size_t part = 0; part < parts.size(); ++part) {
      if (distance > kTrimRange && part > std::size_t(StorePart::Seeker)) continue;
      submit(parts[part], model, storeMaterial(store.type, StorePart(part)));
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

}  // namespace ofs::client
