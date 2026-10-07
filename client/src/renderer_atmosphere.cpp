// Renderer: atmosphere tables, sky, sun shadow cascades, volumetric clouds, rain
// and the display chain. See renderer.hpp for the frame graph.

#include "renderer.hpp"
#include "renderer_internal.hpp"

#include "coordinates.hpp"
#include "log.hpp"
#include "procedural.hpp"
#include "texture_mips.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ofs::client {
namespace {

constexpr std::uint32_t kClampLinear = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
constexpr std::uint32_t kClampPoint = kClampLinear | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
constexpr std::uint64_t kWriteColor = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;

// IEEE 754 binary16 from binary32, for non-negative finite table values.
std::uint16_t halfFromFloat(float value) {
  if (!(value > 0)) return 0;
  if (value >= 65504.f) return 0x7bff;
  std::uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  const int exponent = static_cast<int>((bits >> 23) & 0xff) - 127 + 15;
  const std::uint32_t mantissa = bits & 0x7fffffu;
  if (exponent <= 0) {
    // Subnormal half: shift the implicit one into the mantissa.
    if (exponent < -10) return 0;
    const std::uint32_t shifted = (mantissa | 0x800000u) >> (1 - exponent);
    return static_cast<std::uint16_t>((shifted + 0x1000u) >> 13);
  }
  return static_cast<std::uint16_t>(((static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13)) + ((mantissa >> 12) & 1u));
}

const bgfx::Memory* halfFloats(const std::vector<float>& values) {
  const bgfx::Memory* memory = bgfx::alloc(static_cast<std::uint32_t>(values.size() * sizeof(std::uint16_t)));
  auto* out = reinterpret_cast<std::uint16_t*>(memory->data);
  for (std::size_t i = 0; i < values.size(); ++i) out[i] = halfFromFloat(values[i]);
  return memory;
}

// Box-filtered mip chain for a single-channel cube, uploaded level by level.
void uploadVolume(bgfx::TextureHandle texture, std::vector<std::uint8_t> level, int size) {
  for (std::uint8_t mip = 0; size >= 1; ++mip) {
    bgfx::updateTexture3D(texture, mip, 0, 0, 0, static_cast<std::uint16_t>(size), static_cast<std::uint16_t>(size),
                          static_cast<std::uint16_t>(size), bgfx::copy(level.data(), static_cast<std::uint32_t>(level.size())));
    if (size == 1) break;
    const int half = size / 2;
    std::vector<std::uint8_t> next(std::size_t(half) * half * half);
    for (int z = 0; z < half; ++z) for (int y = 0; y < half; ++y) for (int x = 0; x < half; ++x) {
      unsigned sum = 0;
      for (int dz = 0; dz < 2; ++dz) for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
        sum += level[(std::size_t(z * 2 + dz) * size + (y * 2 + dy)) * size + (x * 2 + dx)];
      next[(std::size_t(z) * half + y) * half + x] = static_cast<std::uint8_t>((sum + 4) / 8);
    }
    level = std::move(next);
    size = half;
  }
}

}  // namespace

bool Renderer::cloudsEnabled() const {
  return settings_.clouds != CloudQuality::Off && (settings_.cloudCoverage > 0 || settings_.cirrusCoverage > 0);
}

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

void Renderer::createAtmosphereResources(Synthesis& data) {
  atmosphereWeather_ = data.weather;
  atmosphere_ = std::move(data.atmosphere);
  transmittanceTexture_ = bgfx::createTexture2D(AtmosphereModel::kTransmittanceWidth, AtmosphereModel::kTransmittanceHeight,
                                                false, 1, bgfx::TextureFormat::RGBA16F, kClampLinear);
  multiScatterTexture_ = bgfx::createTexture2D(AtmosphereModel::kMultiScatterSize, AtmosphereModel::kMultiScatterSize,
                                               false, 1, bgfx::TextureFormat::RGBA16F, kClampLinear);
  skyTableBuffer_ = bgfx::createFrameBuffer(kSkyTableWidth, kSkyTableHeight, bgfx::TextureFormat::RGBA16F, kClampLinear);
  aerialBuffer_ = bgfx::createFrameBuffer(kAerialTile * kAerialColumns, kAerialTile * kAerialRows,
                                          bgfx::TextureFormat::RGBA16F, kClampLinear);
  if (!bgfx::isValid(transmittanceTexture_) || !bgfx::isValid(multiScatterTexture_) ||
      !bgfx::isValid(skyTableBuffer_) || !bgfx::isValid(aerialBuffer_))
    throw std::runtime_error("Atmosphere table creation failed");
  uploadAtmosphereTables();

  // Cloud density volumes, the weather map and the shared noise tile.
  cloudShape_ = bgfx::createTexture3D(kCloudShapeSize, kCloudShapeSize, kCloudShapeSize, true, bgfx::TextureFormat::R8, 0);
  cloudDetail_ = bgfx::createTexture3D(kCloudDetailSize, kCloudDetailSize, kCloudDetailSize, true, bgfx::TextureFormat::R8, 0);
  if (!bgfx::isValid(cloudShape_) || !bgfx::isValid(cloudDetail_)) throw std::runtime_error("Cloud volume creation failed");
  uploadVolume(cloudShape_, std::move(data.cloudShape), kCloudShapeSize);
  uploadVolume(cloudDetail_, std::move(data.cloudDetail), kCloudDetailSize);
  const auto tile = [&](const std::vector<std::uint8_t>& base, int size, std::uint64_t flags, const char* name) {
    const auto chain = textureMipChain(base, size, size, TextureRole::Linear);
    const auto handle = bgfx::createTexture2D(static_cast<std::uint16_t>(size), static_cast<std::uint16_t>(size), true, 1,
                                              bgfx::TextureFormat::RGBA8, flags,
                                              bgfx::copy(chain.data(), static_cast<std::uint32_t>(chain.size())));
    if (!bgfx::isValid(handle)) throw std::runtime_error(std::string("Texture creation failed: ") + name);
    auxiliaryTextureBytes_ += chain.size();
    return handle;
  };
  weatherMap_ = tile(data.weatherMap, kWeatherMapSize, 0, "weather map");
  noiseTile_ = tile(data.noiseTile, kNoiseTileSize, BGFX_SAMPLER_MIN_ANISOTROPIC, "noise tile");
  auxiliaryTextureBytes_ += std::uint64_t(kCloudShapeSize) * kCloudShapeSize * kCloudShapeSize * 8 / 7 +
                            std::uint64_t(kCloudDetailSize) * kCloudDetailSize * kCloudDetailSize * 8 / 7 +
                            (AtmosphereModel::kTransmittanceWidth * AtmosphereModel::kTransmittanceHeight +
                             AtmosphereModel::kMultiScatterSize * AtmosphereModel::kMultiScatterSize +
                             kSkyTableWidth * kSkyTableHeight + kAerialTile * kAerialColumns * kAerialTile * kAerialRows) * 8ull;
}

void Renderer::uploadAtmosphereTables() {
  bgfx::updateTexture2D(transmittanceTexture_, 0, 0, 0, 0, AtmosphereModel::kTransmittanceWidth,
                        AtmosphereModel::kTransmittanceHeight, halfFloats(atmosphere_->transmittanceTable()));
  bgfx::updateTexture2D(multiScatterTexture_, 0, 0, 0, 0, AtmosphereModel::kMultiScatterSize,
                        AtmosphereModel::kMultiScatterSize, halfFloats(atmosphere_->multiScatterTable()));
}

void Renderer::destroyAtmosphereResources() {
  const auto release = [](auto& handle) {
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
    handle.idx = bgfx::kInvalidHandle;
  };
  for (auto* buffer : {&skyTableBuffer_, &aerialBuffer_, &hdrBuffer_, &atmosphereBuffer_, &displayBuffer_,
                       &refractionBuffer_, &cloudBuffer_, &cloudHistory_[0], &cloudHistory_[1], &shadowBuffer_})
    release(*buffer);
  for (auto& buffer : glareDown_) release(buffer);
  for (auto& buffer : glareUp_) release(buffer);
  for (auto* texture : {&transmittanceTexture_, &multiScatterTexture_, &cloudShape_, &cloudDetail_, &weatherMap_, &noiseTile_})
    release(*texture);
  shadowAtlas_.idx = bgfx::kInvalidHandle;
}

void Renderer::ensureSceneBuffers() {
  const bool edgeFilter = settings_.fxaa;
  const bool refraction = settings_.heatDistortion && settings_.effects != EffectsQuality::Off;
  const bool glare = settings_.bloom && settings_.bloomStrength > 0;
  if (bgfx::isValid(hdrBuffer_) && hdrWidth_ == width_ && hdrHeight_ == height_ && hdrSamples_ == settings_.msaaSamples &&
      sceneHasEdgeFilter_ == edgeFilter && sceneHasRefraction_ == refraction && sceneHasGlare_ == glare)
    return;
  const auto release = [](bgfx::FrameBufferHandle& handle) {
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
    handle.idx = bgfx::kInvalidHandle;
  };
  release(atmosphereBuffer_);
  release(hdrBuffer_);
  release(displayBuffer_);
  release(refractionBuffer_);
  for (auto& buffer : glareDown_) release(buffer);
  for (auto& buffer : glareUp_) release(buffer);
  hdrWidth_ = width_;
  hdrHeight_ = height_;
  hdrSamples_ = settings_.msaaSamples;
  sceneHasEdgeFilter_ = edgeFilter;
  sceneHasRefraction_ = refraction;
  sceneHasGlare_ = glare;

  const std::uint64_t sampleFlags = hdrSamples_ >= 8 ? BGFX_TEXTURE_RT_MSAA_X8 :
      hdrSamples_ >= 4 ? BGFX_TEXTURE_RT_MSAA_X4 : hdrSamples_ >= 2 ? BGFX_TEXTURE_RT_MSAA_X2 : 0;
  std::uint64_t flags = BGFX_TEXTURE_RT | kClampLinear | sampleFlags;
  if (!bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RGBA16F, flags) ||
      !bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::R16F, flags)) {
    flags &= ~BGFX_TEXTURE_RT_MSAA_MASK;
    log("RENDER", "HDR float MSAA unavailable; HDR target uses one sample");
  }
  const auto color = bgfx::createTexture2D(width_, height_, false, 1, bgfx::TextureFormat::RGBA16F, flags);
  const auto depth = bgfx::createTexture2D(width_, height_, false, 1, bgfx::TextureFormat::D24S8, flags | BGFX_TEXTURE_RT_WRITE_ONLY);
  // Eye distance of the nearest opaque surface. It resolves like colour under
  // MSAA, which a multisampled depth texture cannot do portably, and feeds the
  // cloud march, soft particles, rain and heat refraction.
  const auto range = bgfx::createTexture2D(width_, height_, false, 1, bgfx::TextureFormat::R16F, flags);
  if (!bgfx::isValid(color) || !bgfx::isValid(depth) || !bgfx::isValid(range)) {
    for (auto h : {color, depth, range}) if (bgfx::isValid(h)) bgfx::destroy(h);
    throw std::runtime_error("HDR colour/depth target creation failed");
  }
  const bgfx::TextureHandle attachments[]{color, range, depth};
  hdrBuffer_ = bgfx::createFrameBuffer(3, attachments, true);
  const bgfx::TextureHandle atmosphereAttachments[]{color, depth};
  atmosphereBuffer_ = bgfx::createFrameBuffer(2, atmosphereAttachments, false);
  if (!bgfx::isValid(hdrBuffer_) || !bgfx::isValid(atmosphereBuffer_))
    throw std::runtime_error("HDR framebuffer creation failed");

  if (glare) {
    const auto format = bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::RG11B10F, BGFX_TEXTURE_RT | kClampLinear)
                            ? bgfx::TextureFormat::RG11B10F : bgfx::TextureFormat::RGBA16F;
    for (int i = 0; i < kGlareLevels; ++i) {
      const auto w = static_cast<std::uint16_t>(std::max(1, width_ >> (i + 1)));
      const auto h = static_cast<std::uint16_t>(std::max(1, height_ >> (i + 1)));
      glareDown_[i] = bgfx::createFrameBuffer(w, h, format, kClampLinear);
      if (i < kGlareLevels - 1) glareUp_[i] = bgfx::createFrameBuffer(w, h, format, kClampLinear);
      if (!bgfx::isValid(glareDown_[i]) || (i < kGlareLevels - 1 && !bgfx::isValid(glareUp_[i])))
        throw std::runtime_error("Glare framebuffer creation failed");
    }
  }
  if (edgeFilter) {
    displayBuffer_ = bgfx::createFrameBuffer(width_, height_, bgfx::TextureFormat::RGBA8, kClampLinear);
    if (!bgfx::isValid(displayBuffer_)) throw std::runtime_error("Display framebuffer creation failed");
  }
  if (refraction) {
    refractionBuffer_ = bgfx::createFrameBuffer(static_cast<std::uint16_t>(std::max(1, width_ / 2)),
                                                static_cast<std::uint16_t>(std::max(1, height_ / 2)),
                                                bgfx::TextureFormat::RG16F, kClampLinear);
    if (!bgfx::isValid(refractionBuffer_)) throw std::runtime_error("Refraction framebuffer creation failed");
  }
}

// ---------------------------------------------------------------------------
// Atmosphere tables and sky
// ---------------------------------------------------------------------------

void Renderer::drawAtmosphereTables() {
  const auto pass = [&](bgfx::ViewId view, const char* name, bgfx::FrameBufferHandle target, int w, int h,
                        bgfx::ProgramHandle program) {
    bgfx::setViewName(view, name);
    bgfx::setViewRect(view, 0, 0, static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h));
    bgfx::setViewFrameBuffer(view, target);
    bgfx::setViewClear(view, BGFX_CLEAR_NONE);
    bindFrame(viewProj_);
    bgfx::setUniform(uniforms_.invViewProj, glm::value_ptr(invViewProj_));
    bgfx::setTexture(6, uniforms_.transmittance, transmittanceTexture_, kClampLinear);
    bgfx::setTexture(11, uniforms_.multiScatter, multiScatterTexture_, kClampLinear);
    fullscreenPass(view, program, kWriteColor);
  };
  pass(kViewSkyTable, "Sky-view table", skyTableBuffer_, kSkyTableWidth, kSkyTableHeight, programs_.skyTable);
  pass(kViewAerial, "Aerial perspective", aerialBuffer_, kAerialTile * kAerialColumns, kAerialTile * kAerialRows,
       programs_.aerial);
}

void Renderer::drawSky() {
  bindFrame(viewProj_);
  bindLighting();
  bgfx::setUniform(uniforms_.invViewProj, glm::value_ptr(invViewProj_));
  // The sky vertex shader emits z = 1 with LEQUAL testing against a cleared
  // depth of 1, so it fills the frame and never occludes world geometry.
  fullscreenPass(kViewWorld, programs_.sky, kWriteColor | BGFX_STATE_DEPTH_TEST_LEQUAL);
}

// ---------------------------------------------------------------------------
// Sun shadows
// ---------------------------------------------------------------------------

void Renderer::computeCascades(const Camera& camera, bool cockpit) {
  (void)camera;
  cascadeCount_ = 0;
  frame_.shadowParams = glm::vec4(0);
  frame_.shadowTexel = glm::vec4(1, 1, 1, .08f);
  // Below the horizon there is no direct light to shadow.
  if (settings_.shadows == ShadowQuality::Off || sun_.y < .02f || settings_.shadowStrength <= 0) return;

  const std::uint32_t tile = settings_.shadows == ShadowQuality::High ? 2048 : settings_.shadows == ShadowQuality::Medium ? 1536 : 1024;
  if (!bgfx::isValid(shadowBuffer_) || shadowTileSize_ != tile) {
    if (bgfx::isValid(shadowBuffer_)) bgfx::destroy(shadowBuffer_);
    // The framebuffer owns the depth texture returned by getTexture().
    shadowBuffer_ = bgfx::createFrameBuffer(static_cast<std::uint16_t>(tile * kMaxCascades), static_cast<std::uint16_t>(tile),
                                            bgfx::TextureFormat::D32F, kClampLinear | BGFX_SAMPLER_COMPARE_LEQUAL);
    shadowTileSize_ = tile;
    shadowAtlas_.idx = bgfx::kInvalidHandle;
    if (!bgfx::isValid(shadowBuffer_)) {
      log("RENDER", "Shadow atlas creation failed; shadows disabled");
      return;
    }
    shadowAtlas_ = bgfx::getTexture(shadowBuffer_, 0);
  }
  if (!bgfx::isValid(shadowAtlas_)) return;

  // Cascade far distances. The first cascade is sized for the subject: the
  // cockpit around the pilot, or the aircraft a chase camera is looking at.
  const int count = settings_.shadows == ShadowQuality::Low ? 2 : 3;
  const float reach = std::max(settings_.shadowDistance, 200.f);
  float splits[kMaxCascades + 1] = {cameraNear_, cockpit ? 10.f : 44.f, cockpit ? 150.f : 270.f, reach};
  if (count == 2) { splits[1] = cockpit ? 14.f : 62.f; splits[2] = reach * .6f; }

  const float aspect = static_cast<float>(width_) / static_cast<float>(std::max<std::uint16_t>(1, height_));
  const float tanHalf = std::tan(glm::radians(cameraFovDeg_) * .5f);
  const float k = std::sqrt(1 + aspect * aspect) * tanHalf;
  const glm::vec3 reference = std::abs(sun_.y) > .99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
  const glm::vec3 lightRight = glm::normalize(glm::cross(reference, sun_));
  const glm::vec3 lightUp = glm::cross(sun_, lightRight);
  const glm::dvec3 originOffset(origin_.y, -origin_.z, -origin_.x);

  for (int i = 0; i < count; ++i) {
    const float n = splits[i], f = splits[i + 1];
    // Smallest sphere around the frustum slice; a sphere keeps the cascade the
    // same size as the camera turns, so shadow edges do not shimmer.
    float along, radius;
    if (k * k >= (f - n) / (f + n)) {
      along = f;
      radius = f * k;
    } else {
      along = .5f * (f + n) * (1 + k * k);
      radius = .5f * std::sqrt((f - n) * (f - n) + 2 * (f * f + n * n) * k * k + (f + n) * (f + n) * k * k * k * k);
    }
    radius = std::ceil(radius * 8.f) / 8.f;
    glm::vec3 center = cameraEye_ + cameraForward_ * along;
    // Snap to whole texels in light space, measured in absolute coordinates so
    // the grid survives origin rebasing.
    const float texel = 2 * radius / static_cast<float>(tile);
    const glm::dvec3 absolute = glm::dvec3(center) + originOffset;
    const double x = glm::dot(absolute, glm::dvec3(lightRight)), y = glm::dot(absolute, glm::dvec3(lightUp));
    center += lightRight * static_cast<float>(std::round(x / texel) * texel - x) +
              lightUp * static_cast<float>(std::round(y / texel) * texel - y);

    // Casters above the slice, toward the sun, still shadow it.
    const float extra = std::max(120.f, radius * 2.5f);
    const float depthRange = 2 * radius + extra;
    const glm::mat4 lightView = glm::lookAt(center + sun_ * (radius + extra), center, lightUp);
    const glm::mat4 lightProjection = makeOrtho(-radius, radius, -radius, radius, 0.f, depthRange);
    Cascade& cascade = cascades_[i];
    cascade.lightViewProj = lightProjection * lightView;
    cascade.center = center;
    cascade.radius = radius;
    cascade.texelWorld = texel;

    glm::mat4 bias(1);
    bias[0][0] = .5f / kMaxCascades;
    bias[3][0] = (.5f + static_cast<float>(i)) / kMaxCascades;
    bias[1][1] = bgfx::getCaps()->originBottomLeft ? .5f : -.5f;  // D3D texture origin is top-left.
    bias[3][1] = .5f;
    if (bgfx::getCaps()->homogeneousDepth) {
      bias[2][2] = .5f;
      bias[3][2] = .5f;
    }
    // A depth bias of about a texel and a half, expressed in this cascade's range.
    bias[3][2] -= 1.5f * texel / depthRange;
    cascade.atlas = bias * cascade.lightViewProj;
    frame_.shadowTexel[i] = texel;
  }
  cascadeCount_ = count;
  frame_.shadowParams = glm::vec4(static_cast<float>(count), 1.f / static_cast<float>(tile), 0.f, settings_.shadowStrength);
}

void Renderer::drawShadowAtlas(const std::vector<const Instance*>& casters) {
  const glm::mat4 environment = glm::translate(glm::mat4{1}, localPosition({}, origin_));
  const glm::vec3 environmentOffset = localPosition({}, origin_);
  for (int i = 0; i < cascadeCount_; ++i) {
    const Cascade& cascade = cascades_[i];
    const bgfx::ViewId view = static_cast<bgfx::ViewId>(kViewShadow0 + i);
    bgfx::setViewName(view, i == 0 ? "Shadow cascade 0" : i == 1 ? "Shadow cascade 1" : "Shadow cascade 2");
    bgfx::setViewRect(view, static_cast<std::uint16_t>(shadowTileSize_ * i), 0, static_cast<std::uint16_t>(shadowTileSize_),
                      static_cast<std::uint16_t>(shadowTileSize_));
    bgfx::setViewFrameBuffer(view, shadowBuffer_);
    bgfx::setViewClear(view, BGFX_CLEAR_DEPTH, 0x000000ffu, 1.0f, 0);
    bgfx::touch(view);
    // A caster matters if its shadow can reach the cascade: test the distance
    // from the cascade's axis along the light, not from its centre.
    const auto reaches = [&](const glm::vec3& position, float radius) {
      const glm::vec3 offset = position - cascade.center;
      const glm::vec3 lateral = offset - sun_ * glm::dot(offset, sun_);
      return glm::length(lateral) < cascade.radius + radius && glm::dot(offset, sun_) > -cascade.radius - radius;
    };
    // No colour write and no culling: the A320 has open gear doors and a hollow
    // nacelle, and either would punch holes in the map.
    const std::uint64_t state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
    for (const Instance* instance : casters) {
      const auto& asset = model(instance->type);
      if (!asset.loaded) continue;
      const float radius = static_cast<float>(aircraftDefinition(instance->type).visual.radius);
      if (!reaches(localPosition(instance->state->pos_ned, origin_), radius)) continue;
      const glm::mat4 base = modelTransform(*instance->state, instance->type);
      // Distant cascades cover metres per texel; coarser geometry is enough.
      const auto& level = asset.levels[std::min(instance->lod + static_cast<std::size_t>(i), asset.levels.size() - 1)];
      for (const Batch& batch : level.batches) {
        if (asset.materials.at(batch.material).alpha == Material::Alpha::Blend) continue;
        applyMaterial(asset.materials.at(batch.material), &asset, 0);
        bgfx::setVertexBuffer(0, level.vertexBuffer);
        bgfx::setState(state);
        bgfx::setUniform(uniforms_.lightViewProj, glm::value_ptr(cascade.lightViewProj));
        const auto matrix = batch.transformNode < 0 ? base : base * glm::make_mat4(instance->deltas[batch.transformNode].data());
        bgfx::setUniform(uniforms_.model, glm::value_ptr(matrix));
        bgfx::setIndexBuffer(level.indexBuffer, batch.firstIndex, batch.indexCount);
        bgfx::submit(view, programs_.shadow);
        ++stats_.drawCalls;
        stats_.triangles += batch.indexCount / 3;
      }
    }
    const auto scenery = [&](bgfx::VertexBufferHandle buffer, std::uint32_t count, const glm::vec3& center, float radius) {
      if (!bgfx::isValid(buffer) || !reaches(center + environmentOffset, radius)) return;
      applyMaterial(Material{}, nullptr, 0);
      bgfx::setVertexBuffer(0, buffer);
      bgfx::setState(state);
      bgfx::setUniform(uniforms_.model, glm::value_ptr(environment));
      bgfx::setUniform(uniforms_.lightViewProj, glm::value_ptr(cascade.lightViewProj));
      bgfx::submit(view, programs_.shadow);
      ++stats_.drawCalls;
      stats_.triangles += count / 3;
    };
    scenery(buildings_, buildingVertices_, {-280, 20, -600}, 900);
    scenery(props_, propVertices_, {-20, 2, 0}, 2800);
    scenery(houses_, houseVertices_, {0, 100, 0}, 9000);
    if (settings_.vegetation) drawTrees(view, programs_.shadowTree, &cascade);
  }
}

// ---------------------------------------------------------------------------
// Clouds
// ---------------------------------------------------------------------------

void Renderer::drawClouds() {
  if (!cloudsEnabled()) {
    cloudHistoryValid_ = false;
    return;
  }
  // Low marches a ninth of the display pixels; other tiers a quarter, with a
  // fixed upper budget so a 4K display cannot make the march four times dearer.
  const unsigned divisor = settings_.clouds == CloudQuality::Low ? 3 : 2;
  const float scale = std::min({1.f, 960.f / std::max(1.f, float(width_) / divisor),
                                540.f / std::max(1.f, float(height_) / divisor)});
  const unsigned w = std::max(1u, unsigned(width_ / divisor * scale));
  const unsigned h = std::max(1u, unsigned(height_ / divisor * scale));
  if (!bgfx::isValid(cloudBuffer_) || w != cloudWidth_ || h != cloudHeight_) {
    for (auto* buffer : {&cloudBuffer_, &cloudHistory_[0], &cloudHistory_[1]}) {
      if (bgfx::isValid(*buffer)) bgfx::destroy(*buffer);
      // Radiance with opacity, and beside it the mean depth of the cloud and
      // whether geometry cut the march short. The second target is what lets
      // the accumulation reproject and reject history correctly.
      const bgfx::TextureHandle targets[]{
          bgfx::createTexture2D(static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h), false, 1,
                                bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | kClampLinear),
          bgfx::createTexture2D(static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h), false, 1,
                                bgfx::TextureFormat::RG16F, BGFX_TEXTURE_RT | kClampPoint)};
      *buffer = bgfx::createFrameBuffer(2, targets, true);
      if (!bgfx::isValid(*buffer)) throw std::runtime_error("Cloud framebuffer creation failed");
    }
    cloudWidth_ = w;
    cloudHeight_ = h;
    cloudHistoryValid_ = false;
  }

  bgfx::setViewName(kViewCloud, "Volumetric clouds");
  bgfx::setViewRect(kViewCloud, 0, 0, static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h));
  bgfx::setViewFrameBuffer(kViewCloud, cloudBuffer_);
  bgfx::setViewClear(kViewCloud, BGFX_CLEAR_NONE);
  bindFrame(viewProj_);
  bindLighting();
  bgfx::setUniform(uniforms_.invViewProj, glm::value_ptr(invViewProj_));
  bgfx::setTexture(11, uniforms_.cloudShape, cloudShape_);
  bgfx::setTexture(12, uniforms_.cloudDetail, cloudDetail_);
  bgfx::setTexture(13, uniforms_.sceneRange, bgfx::getTexture(hdrBuffer_, 1), kClampPoint);
  const glm::vec4 budget = settings_.clouds == CloudQuality::Low ? glm::vec4(28, 3, .7f, 0)
                         : settings_.clouds == CloudQuality::Medium ? glm::vec4(44, 4, 1, 0) : glm::vec4(64, 5, 1, 0);
  bgfx::setUniform(uniforms_.cloudRender, glm::value_ptr(budget));
  fullscreenPass(kViewCloud, programs_.clouds, kWriteColor);

  // Accumulate into the other history buffer. The previous view-projection is
  // expressed relative to the previous origin; fold the rebase into it.
  const unsigned next = cloudHistoryIndex_ ^ 1u;
  const glm::mat4 previous = previousViewProj_ * glm::translate(glm::mat4{1}, renderDirection(origin_ - previousOrigin_));
  bgfx::setViewName(kViewCloudResolve, "Cloud accumulation");
  bgfx::setViewRect(kViewCloudResolve, 0, 0, static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h));
  bgfx::setViewFrameBuffer(kViewCloudResolve, cloudHistory_[next]);
  bgfx::setViewClear(kViewCloudResolve, BGFX_CLEAR_NONE);
  bindFrame(viewProj_);
  bgfx::setUniform(uniforms_.invViewProj, glm::value_ptr(invViewProj_));
  bgfx::setUniform(uniforms_.prevViewProj, glm::value_ptr(previous));
  bgfx::setTexture(0, uniforms_.cloudCurrent, bgfx::getTexture(cloudBuffer_, 0), kClampLinear);
  bgfx::setTexture(1, uniforms_.cloudDepth, bgfx::getTexture(cloudBuffer_, 1), kClampPoint);
  bgfx::setTexture(2, uniforms_.cloudHistory, bgfx::getTexture(cloudHistory_[cloudHistoryIndex_], 0), kClampLinear);
  bgfx::setTexture(3, uniforms_.cloudHistoryDepth, bgfx::getTexture(cloudHistory_[cloudHistoryIndex_], 1), kClampPoint);
  const float weight = !cloudHistoryValid_ ? 0.f : settings_.clouds == CloudQuality::Low ? .82f : .90f;
  bgfx::setUniform(uniforms_.cloudResolve, glm::value_ptr(glm::vec4(1.f / w, 1.f / h, weight, 0)));
  fullscreenPass(kViewCloudResolve, programs_.cloudResolve, kWriteColor);
  cloudHistoryIndex_ = next;
  cloudHistoryValid_ = true;
}

void Renderer::compositeClouds() {
  bgfx::setViewName(kViewAtmosphere, "Clouds and effects");
  bgfx::setViewRect(kViewAtmosphere, 0, 0, width_, height_);
  bgfx::setViewFrameBuffer(kViewAtmosphere, atmosphereBuffer_);
  bgfx::setViewClear(kViewAtmosphere, BGFX_CLEAR_NONE);
  bgfx::setViewMode(kViewAtmosphere, bgfx::ViewMode::Sequential);
  bgfx::touch(kViewAtmosphere);
  if (!cloudsEnabled()) return;
  bindFrame(viewProj_);
  bgfx::setUniform(uniforms_.cloudResolve, glm::value_ptr(glm::vec4(1.f / cloudWidth_, 1.f / cloudHeight_, frame_.cloudShape.z, 0)));
  bgfx::setTexture(0, uniforms_.cloudLayer, bgfx::getTexture(cloudHistory_[cloudHistoryIndex_], 0), kClampPoint);
  bgfx::setTexture(1, uniforms_.cloudDepth, bgfx::getTexture(cloudHistory_[cloudHistoryIndex_], 1), kClampPoint);
  bgfx::setTexture(13, uniforms_.sceneRange, bgfx::getTexture(hdrBuffer_, 1), kClampPoint);
  // Premultiplied: the layer's own light plus the scene seen through it.
  fullscreenPass(kViewAtmosphere, programs_.cloudComposite,
                 kWriteColor | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA));
}

void Renderer::drawRain(const State& local, const Weather& weather) {
  const float intensity = settings_.weather.precipitation;
  if (intensity <= .01f || settings_.effects == EffectsQuality::Off) return;
  // Rain falls from the cloud layer; above it the air is dry.
  const float eyeAltitude = frame_.ozone.w;
  const float below = 1 - glm::smoothstep(settings_.cloudBase, settings_.cloudBase + settings_.cloudThickness * .5f, eyeAltitude);
  if (below <= 0) return;
  // Raindrops fall at about nine metres per second and drift with the wind.
  // What the eye sees is their motion relative to the camera.
  const Vec3 cameraVelocity = lastCamera_.mode == CameraMode::Free ? Vec3{} : local.vel_ned;
  const glm::vec3 relative = renderDirection(Vec3{weather.wind_ned.x, weather.wind_ned.y, 9.} - cameraVelocity);
  const float speed = glm::length(relative);
  const glm::vec3 axis = speed > 1e-3f ? relative / speed : glm::vec3(0, -1, 0);
  const glm::vec3 side = glm::normalize(glm::cross(axis, std::abs(axis.y) > .9f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)));
  bindFrame(viewProj_);
  bindLighting();
  bgfx::setUniform(uniforms_.invViewProj, glm::value_ptr(invViewProj_));
  bgfx::setUniform(uniforms_.rain, glm::value_ptr(glm::vec4(axis, speed)));
  bgfx::setUniform(uniforms_.rainSide, glm::value_ptr(glm::vec4(side, intensity * below)));
  bgfx::setTexture(13, uniforms_.sceneRange, bgfx::getTexture(hdrBuffer_, 1), kClampPoint);
  fullscreenPass(kViewAtmosphere, programs_.rain,
                 kWriteColor | BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
}

// ---------------------------------------------------------------------------
// Display chain
// ---------------------------------------------------------------------------

void Renderer::compositeDisplay() {
  const auto scene = bgfx::getTexture(hdrBuffer_, 0);
  if (sceneHasRefraction_) {
    // Cleared to zero offset every frame, whether or not anything draws into it.
    bgfx::setViewName(kViewRefraction, "Heat refraction");
    bgfx::setViewRect(kViewRefraction, 0, 0, static_cast<std::uint16_t>(std::max(1, width_ / 2)),
                      static_cast<std::uint16_t>(std::max(1, height_ / 2)));
    bgfx::setViewFrameBuffer(kViewRefraction, refractionBuffer_);
    bgfx::setViewClear(kViewRefraction, BGFX_CLEAR_COLOR, 0x00000000u, 1.0f, 0);
    bgfx::touch(kViewRefraction);
  }

  bgfx::TextureHandle glare = whiteTexture_;
  if (sceneHasGlare_) {
    bgfx::ViewId view = kViewGlare;
    const auto level = [&](bgfx::FrameBufferHandle target, int index, bgfx::TextureHandle source, bgfx::TextureHandle add,
                           const glm::vec4& step) {
      bgfx::setViewName(view, "Glare pyramid");
      bgfx::setViewRect(view, 0, 0, static_cast<std::uint16_t>(std::max(1, width_ >> (index + 1))),
                        static_cast<std::uint16_t>(std::max(1, height_ >> (index + 1))));
      bgfx::setViewFrameBuffer(view, target);
      bgfx::setViewClear(view, BGFX_CLEAR_NONE);
      bindFrame(viewProj_);
      bgfx::setUniform(uniforms_.postStep, glm::value_ptr(step));
      bgfx::setTexture(0, uniforms_.sceneTexture, source, kClampLinear);
      bgfx::setTexture(1, uniforms_.bloomTexture, add, kClampLinear);
      fullscreenPass(view++, programs_.glare, kWriteColor);
    };
    const auto texel = [&](int index) {  // index -1 is the full-resolution scene
      return glm::vec2(1.f / std::max(1, width_ >> (index + 1)), 1.f / std::max(1, height_ >> (index + 1)));
    };
    for (int i = 0; i < kGlareLevels; ++i)
      level(glareDown_[i], i, i == 0 ? scene : bgfx::getTexture(glareDown_[i - 1]), whiteTexture_,
            glm::vec4(texel(i - 1), i == 0 ? 0.f : 1.f, 0));
    for (int i = kGlareLevels - 2; i >= 0; --i)
      level(glareUp_[i], i, i == kGlareLevels - 2 ? bgfx::getTexture(glareDown_[i + 1]) : bgfx::getTexture(glareUp_[i + 1]),
            bgfx::getTexture(glareDown_[i]), glm::vec4(texel(i + 1), 2.f, 0));
    glare = bgfx::getTexture(glareUp_[0]);
  }

  bgfx::setViewName(kViewDisplay, "Display transform");
  bgfx::setViewRect(kViewDisplay, 0, 0, width_, height_);
  bgfx::setViewFrameBuffer(kViewDisplay, sceneHasEdgeFilter_ ? displayBuffer_ : bgfx::FrameBufferHandle{bgfx::kInvalidHandle});
  bgfx::setViewClear(kViewDisplay, BGFX_CLEAR_NONE);
  bindFrame(viewProj_);
  bgfx::setUniform(uniforms_.postSettings, glm::value_ptr(glm::vec4(
      exposure_, sceneHasGlare_ ? settings_.bloomStrength : 0.f, sceneHasRefraction_ ? 1.f : 0.f, 1.06f)));
  bgfx::setUniform(uniforms_.postStep, glm::value_ptr(glm::vec4(1.f / kGlareLevels, 0, 0, 0)));
  bgfx::setTexture(0, uniforms_.sceneTexture, scene, kClampLinear);
  bgfx::setTexture(1, uniforms_.bloomTexture, glare, kClampLinear);
  bgfx::setTexture(2, uniforms_.distortion, sceneHasRefraction_ ? bgfx::getTexture(refractionBuffer_) : whiteTexture_, kClampLinear);
  fullscreenPass(kViewDisplay, programs_.display, kWriteColor);

  if (sceneHasEdgeFilter_) {
    bgfx::setViewName(kViewEdgeFilter, "Edge filter");
    bgfx::setViewRect(kViewEdgeFilter, 0, 0, width_, height_);
    bgfx::setViewFrameBuffer(kViewEdgeFilter, BGFX_INVALID_HANDLE);
    bgfx::setViewClear(kViewEdgeFilter, BGFX_CLEAR_NONE);
    bindFrame(viewProj_);
    bgfx::setUniform(uniforms_.postStep, glm::value_ptr(glm::vec4(1.f / width_, 1.f / height_, 0, 0)));
    bgfx::setTexture(0, uniforms_.sceneTexture, bgfx::getTexture(displayBuffer_), kClampLinear);
    fullscreenPass(kViewEdgeFilter, programs_.edgeFilter, kWriteColor);
  }
}

}  // namespace ofs::client
