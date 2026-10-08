#include "texture_cost.hpp"
#include "screenshot_pixels.hpp"
#include "renderer.hpp"
#include "renderer_internal.hpp"

#include "coordinates.hpp"
#include "log.hpp"
#include "ofs_shaders.hpp"
#include "platform.hpp"
#include "texture_mips.hpp"
#include <tuple>

#include <imgui.h>
#include <vs_ocornut_imgui.bin.h>
#include <fs_ocornut_imgui.bin.h>

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <chrono>
#include <filesystem>
#include <map>
#include <future>

namespace ofs::client {

// ---------------------------------------------------------------------------
// bgfx callbacks
// ---------------------------------------------------------------------------

void RenderCallbacks::fatal(const char* file, std::uint16_t line, bgfx::Fatal::Enum, const char* message) {
  std::fprintf(stderr, "[RENDER] Fatal %s:%u: %s\n", file, line, message);
  std::abort();
}

void RenderCallbacks::traceVargs(const char*, std::uint16_t, const char* format, va_list args) {
  // bgfx can call this from its render thread. stdio serializes its own stream.
  char message[1024];
  std::vsnprintf(message, sizeof(message), format, args);
  if (std::strstr(message, "WARN") || std::strstr(message, "ERROR") ||
      std::strstr(message, "failed") || std::strstr(message, "Failed"))
    std::fprintf(stderr, "[RENDER] %s", message);
}

void RenderCallbacks::screenShot(const char* path, std::uint32_t w, std::uint32_t h,
                                 std::uint32_t pitch, bgfx::TextureFormat::Enum format,
                                 const void* pixels, std::uint32_t size, bool yflip) {
  if (format != bgfx::TextureFormat::BGRA8 && format != bgfx::TextureFormat::RGBA8) {
    log("RENDER", "Unsupported screenshot format");
    return;
  }
  if(!pixels||!w||!h||std::uint64_t(pitch)<std::uint64_t(w)*4||
      std::uint64_t(size)<std::uint64_t(h-1)*pitch+std::uint64_t(w)*4) {
    log("RENDER", "Invalid screenshot buffer size/pitch");return;
  }
  FILE* file = std::fopen(path, "wb");
  if (!file) {
    log("RENDER", "Cannot write screenshot");
    return;
  }
  std::fprintf(file, "P6\n%u %u\n255\n", w, h);
  const auto* data = static_cast<const std::uint8_t*>(pixels);
  // Screenshot-only allocation; world rendering never allocates per frame.
  std::vector<std::uint8_t> row(static_cast<std::size_t>(w) * 3);
  bool ok = true;
  for (std::uint32_t y = 0; y < h; ++y) {
    const auto* source = data + static_cast<std::size_t>(yflip ? h - 1 - y : y) * pitch;
    screenshotRgbRow({source,static_cast<std::size_t>(w)*4},row,
      format==bgfx::TextureFormat::BGRA8?ScreenshotOrder::BGRA8:ScreenshotOrder::RGBA8);
    ok = std::fwrite(row.data(), 1, row.size(), file) == row.size() && ok;
  }
  ok = std::fclose(file) == 0 && ok;
  screenshotWritten.store(ok);
  log("RENDER", ok ? "Screenshot written" : "Screenshot write failed");
}

namespace {

// glTF asset space (X aft, Y up, Z port) to body FRD (X fwd, Y right, Z down).
// Columns: body +X = asset -X (forward), body +Y = asset -Z (right),
// body +Z = asset -Y (down). Determinant +1, so handedness is preserved. The
// origin anchor is derived in docs/COORDINATES.md; the translation is replaced
// per aircraft in modelTransform().
const glm::mat4 kAssetToBody = [] {
  glm::mat4 m(1.0f);
  m[0] = glm::vec4(-1, 0, 0, 0);
  m[1] = glm::vec4(0, 0, -1, 0);
  m[2] = glm::vec4(0, -1, 0, 0);
  m[3] = glm::vec4(static_cast<float>(ModelAnchor::cgX), static_cast<float>(ModelAnchor::cgZ),
                   static_cast<float>(ModelAnchor::cgY), 1);
  return m;
}();

// Interleaved position(3) + colour(4) + uv(2) for the effect pool, which
// matches the effect vertex layout declared in Renderer.
inline constexpr std::size_t kEffectVertexFloats = 9;

// Radiance of an emissive material with unit emissive factor, in scene units.
// Navigation lights and afterburners were authored against the previous
// renderer's exposure; this keeps them equally bright in daylight.
inline constexpr float kEmissiveRadiance = 2.6f;

bgfx::ProgramHandle makeProgram(const char* name, std::span<const std::uint8_t> vs, std::span<const std::uint8_t> fs) {
  const std::string diagnostic = std::string(name) + " on " + bgfx::getRendererName(bgfx::getRendererType());
  const auto vertex = bgfx::createShader(bgfx::copy(vs.data(), static_cast<std::uint32_t>(vs.size())));
  const auto fragment = bgfx::createShader(bgfx::copy(fs.data(), static_cast<std::uint32_t>(fs.size())));
  if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment)) {
    if (bgfx::isValid(vertex)) bgfx::destroy(vertex);
    if (bgfx::isValid(fragment)) bgfx::destroy(fragment);
    throw std::runtime_error("Shader creation failed: " + diagnostic + "; rebuild ofs_shaders with the pinned shaderc");
  }
  const auto result = bgfx::createProgram(vertex, fragment, true);
  if (!bgfx::isValid(result)) throw std::runtime_error("Shader linking failed: " + diagnostic);
  return result;
}

glm::mat4 makeProjection(float fovDegrees, float aspect, float nearPlane, float farPlane) {
  const auto fov = static_cast<float>(glm::radians(static_cast<double>(fovDegrees)));
  return bgfx::getCaps()->homogeneousDepth
             ? glm::perspectiveRH_NO(fov, aspect, nearPlane, farPlane)
             : glm::perspectiveRH_ZO(fov, aspect, nearPlane, farPlane);
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Renderer::Renderer(const Platform& platform, const GraphicsSettings& settings)
    : settings_(settings) {
  glareDown_.fill(bgfx::FrameBufferHandle{bgfx::kInvalidHandle});
  glareUp_.fill(bgfx::FrameBufferHandle{bgfx::kInvalidHandle});
  combat_.setQuality(settings_.effects);
  try {
    if (!initialize(platform)) throw std::runtime_error("bgfx initialization failed");
  } catch (...) {
    destroy();
    throw;
  }
}

Renderer::~Renderer() { destroy(); }

void Renderer::createPrograms() {
  const bool direct3d = bgfx::getRendererType() == bgfx::RendererType::Direct3D11;
  if (!direct3d && bgfx::getRendererType() != bgfx::RendererType::OpenGL)
    throw std::runtime_error(std::string("No compiled shader set for active backend: ") + backend());
#ifdef _WIN32
#define OFS_SHADER(name) (direct3d ? std::span<const std::uint8_t>(name##_dx11) : std::span<const std::uint8_t>(name##_glsl))
  const auto imguiVs = direct3d ? std::span<const std::uint8_t>(vs_ocornut_imgui_dxbc) : std::span<const std::uint8_t>(vs_ocornut_imgui_glsl);
  const auto imguiFs = direct3d ? std::span<const std::uint8_t>(fs_ocornut_imgui_dxbc) : std::span<const std::uint8_t>(fs_ocornut_imgui_glsl);
#else
#define OFS_SHADER(name) std::span<const std::uint8_t>(name##_glsl)
  const auto imguiVs = std::span<const std::uint8_t>(vs_ocornut_imgui_glsl);
  const auto imguiFs = std::span<const std::uint8_t>(fs_ocornut_imgui_glsl);
#endif
  programs_.pbr = makeProgram("pbr", OFS_SHADER(pbr_vs), OFS_SHADER(pbr_fs));
  programs_.terrain = makeProgram("terrain", OFS_SHADER(terrain_vs), OFS_SHADER(terrain_fs));
  programs_.tree = makeProgram("tree", OFS_SHADER(tree_vs), OFS_SHADER(tree_fs));
  programs_.sky = makeProgram("sky", OFS_SHADER(sky_vs), OFS_SHADER(sky_fs));
  programs_.skyTable = makeProgram("skyview", OFS_SHADER(fullscreen_vs), OFS_SHADER(skyview_fs));
  programs_.aerial = makeProgram("aerial", OFS_SHADER(fullscreen_vs), OFS_SHADER(aerial_fs));
  programs_.clouds = makeProgram("cloud", OFS_SHADER(fullscreen_vs), OFS_SHADER(cloud_fs));
  programs_.cloudResolve = makeProgram("cloud_resolve", OFS_SHADER(fullscreen_vs), OFS_SHADER(cloud_resolve_fs));
  programs_.cloudComposite = makeProgram("cloud_composite", OFS_SHADER(fullscreen_vs), OFS_SHADER(cloud_composite_fs));
  programs_.unlit = makeProgram("unlit", OFS_SHADER(unlit_vs), OFS_SHADER(unlit_fs));
  programs_.effect = makeProgram("effect", OFS_SHADER(effect_vs), OFS_SHADER(effect_fs));
  programs_.flame = makeProgram("flame", OFS_SHADER(flame_vs), OFS_SHADER(flame_fs));
  programs_.rain = makeProgram("rain", OFS_SHADER(fullscreen_vs), OFS_SHADER(rain_fs));
  programs_.shadow = makeProgram("shadow", OFS_SHADER(shadow_vs), OFS_SHADER(shadow_fs));
  programs_.shadowTree = makeProgram("shadow_tree", OFS_SHADER(shadow_tree_vs), OFS_SHADER(shadow_tree_fs));
  programs_.glare = makeProgram("bloom", OFS_SHADER(fullscreen_vs), OFS_SHADER(bloom_fs));
  programs_.display = makeProgram("post", OFS_SHADER(fullscreen_vs), OFS_SHADER(post_fs));
  programs_.edgeFilter = makeProgram("fxaa", OFS_SHADER(fullscreen_vs), OFS_SHADER(fxaa_fs));
  programs_.imgui = makeProgram("imgui", imguiVs, imguiFs);
#undef OFS_SHADER
}

void Renderer::createUniforms() {
  const auto vec4 = bgfx::UniformType::Vec4;
  const auto mat4 = bgfx::UniformType::Mat4;
  const auto sampler = bgfx::UniformType::Sampler;
  uniforms_.frame = bgfx::createUniform("u_frame", vec4, sizeof(FrameConstants) / sizeof(glm::vec4));
  uniforms_.model = bgfx::createUniform("u_ofsModel", mat4);
  uniforms_.viewProj = bgfx::createUniform("u_ofsViewProj", mat4);
  uniforms_.invViewProj = bgfx::createUniform("u_ofsInvViewProj", mat4);
  uniforms_.prevViewProj = bgfx::createUniform("u_prevViewProj", mat4);
  uniforms_.normalMatrix = bgfx::createUniform("u_normalMatrix", bgfx::UniformType::Mat3);
  uniforms_.lightViewProj = bgfx::createUniform("u_lightViewProj", mat4);
  uniforms_.shadowMatrix = bgfx::createUniform("u_shadowMatrix", mat4, kMaxCascades);
  uniforms_.damage = bgfx::createUniform("u_damage", vec4, 7);
  uniforms_.baseColor = bgfx::createUniform("u_baseColor", vec4);
  uniforms_.metallicRoughness = bgfx::createUniform("u_metallicRoughness", vec4);
  uniforms_.emissive = bgfx::createUniform("u_emissive", vec4);
  uniforms_.doubleSided = bgfx::createUniform("u_doubleSided", vec4);
  uniforms_.normalSettings = bgfx::createUniform("u_normalSettings", vec4);
  uniforms_.textureFlags = bgfx::createUniform("u_textureFlags", vec4);
  uniforms_.alphaSettings = bgfx::createUniform("u_alphaSettings", vec4);
  uniforms_.surface = bgfx::createUniform("u_surface", vec4);
  uniforms_.flame = bgfx::createUniform("u_flame", vec4);
  uniforms_.effectParams = bgfx::createUniform("u_effectParams", vec4);
  uniforms_.cloudRender = bgfx::createUniform("u_cloudRender", vec4);
  uniforms_.cloudResolve = bgfx::createUniform("u_cloudResolve", vec4);
  uniforms_.postSettings = bgfx::createUniform("u_postSettings", vec4);
  uniforms_.postStep = bgfx::createUniform("u_postStep", vec4);
  uniforms_.rain = bgfx::createUniform("u_rain", vec4);
  uniforms_.rainSide = bgfx::createUniform("u_rainSide", vec4);
  uniforms_.shadowAtlas = bgfx::createUniform("s_shadowAtlas", sampler);
  uniforms_.baseTexture = bgfx::createUniform("s_baseColor", sampler);
  uniforms_.mrTexture = bgfx::createUniform("s_metallicRoughness", sampler);
  uniforms_.emissiveTexture = bgfx::createUniform("s_emissive", sampler);
  uniforms_.normalTexture = bgfx::createUniform("s_normal", sampler);
  uniforms_.occlusionTexture = bgfx::createUniform("s_occlusion", sampler);
  uniforms_.transmittance = bgfx::createUniform("s_transmittance", sampler);
  uniforms_.skyView = bgfx::createUniform("s_skyView", sampler);
  uniforms_.aerial = bgfx::createUniform("s_aerial", sampler);
  uniforms_.multiScatter = bgfx::createUniform("s_multiScatter", sampler);
  uniforms_.weatherMap = bgfx::createUniform("s_weatherMap", sampler);
  uniforms_.noise = bgfx::createUniform("s_noise", sampler);
  uniforms_.terrainAlbedo = bgfx::createUniform("s_terrainAlbedo", sampler);
  uniforms_.terrainNormal = bgfx::createUniform("s_terrainNormal", sampler);
  uniforms_.landMap = bgfx::createUniform("s_landMap", sampler);
  uniforms_.lakeMap = bgfx::createUniform("s_lakeMap", sampler);
  uniforms_.waterNormal = bgfx::createUniform("s_waterNormal", sampler);
  uniforms_.cloudShape = bgfx::createUniform("s_cloudShape", sampler);
  uniforms_.cloudDetail = bgfx::createUniform("s_cloudDetail", sampler);
  uniforms_.sceneRange = bgfx::createUniform("s_sceneRange", sampler);
  uniforms_.cloudLayer = bgfx::createUniform("s_cloudLayer", sampler);
  uniforms_.cloudDepth = bgfx::createUniform("s_cloudDepth", sampler);
  uniforms_.cloudCurrent = bgfx::createUniform("s_cloudCurrent", sampler);
  uniforms_.cloudHistory = bgfx::createUniform("s_cloudHistory", sampler);
  uniforms_.cloudHistoryDepth = bgfx::createUniform("s_cloudHistoryDepth", sampler);
  uniforms_.sceneTexture = bgfx::createUniform("s_scene", sampler);
  uniforms_.bloomTexture = bgfx::createUniform("s_bloom", sampler);
  uniforms_.distortion = bgfx::createUniform("s_distortion", sampler);
  uiSampler_ = bgfx::createUniform("s_tex", sampler);
}

bool Renderer::initialize(const Platform& platform) {
  int w = 0, h = 0;
  if (!SDL_GetWindowSizeInPixels(platform.window(), &w, &h))
    throw std::runtime_error(SDL_GetError());
  width_ = static_cast<std::uint16_t>(std::clamp(w, 1, 65535));
  height_ = static_cast<std::uint16_t>(std::clamp(h, 1, 65535));

  bgfx::Init init;
#ifdef _WIN32
  init.type = bgfx::RendererType::Direct3D11;
#else
  init.type = bgfx::RendererType::OpenGL;
#endif
  swapChain_ = platform.nativeHandles();
  swapChain_.width = width_;
  swapChain_.height = height_;
  swapChain_.formatColor = bgfx::TextureFormat::BGRA8;
  swapChain_.formatDepthStencil = bgfx::TextureFormat::D24S8;
  swapChain_.numBackBuffers = 2;
  // The scene is antialiased in its own multisampled HDR target; the swap
  // chain only ever receives the finished, resolved image.
  swapChain_.flags = BGFX_SWAP_CHAIN_NONE;
  init.swapChain = swapChain_;
  init.fallback = false;
  init.callback = &callbacks_;
  init.reset = resetFlags();
  if (!bgfx::init(init)) return false;
  initialized_ = true;
  if (bgfx::getRendererType() != init.type)
    log("RENDER", std::string("Falling back to ") + bgfx::getRendererName(bgfx::getRendererType()));

  surfaceLayout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float)
      .end();
  terrainLayout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
      .end();
  // Per-instance data travels in the texture-coordinate slots bgfx reserves
  // for it (i_data0 = TEXCOORD7, i_data1 = TEXCOORD6).
  instanceLayout_.begin()
      .add(bgfx::Attrib::TexCoord7, 4, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord6, 4, bgfx::AttribType::Float)
      .end();
  unlitLayout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();
  // Effects share the unlit shape but carry a float colour, which is what the
  // per-quad fade needs.
  effectLayout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .end();
  // ImGui's own layout: position, uv and packed colour, matching ImDrawVert.
  uiLayout_.begin()
      .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  createPrograms();
  createUniforms();

  const std::uint32_t white = 0xffffffff;
  whiteTexture_ = bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(&white,4));

  unsigned char* pixels = nullptr;
  int fw = 0, fh = 0;
  ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);
  font_ = bgfx::createTexture2D(static_cast<std::uint16_t>(fw), static_cast<std::uint16_t>(fh), false, 1,
                                bgfx::TextureFormat::RGBA8, 0,
                                bgfx::copy(pixels, static_cast<std::uint32_t>(fw * fh * 4)));
  if (!bgfx::isValid(font_) || !bgfx::isValid(uiSampler_) || !bgfx::isValid(whiteTexture_))
    throw std::runtime_error("GPU resource creation failed");
  auxiliaryTextureBytes_=4+std::uint64_t(fw)*fh*4;
  log("TEXTURE", "builtin white source=1x1 format=RGBA8 mips=1 estimated_gpu_bytes=4");
  log("TEXTURE", "builtin font source="+std::to_string(fw)+"x"+std::to_string(fh)+" format=RGBA8 mips=1 estimated_gpu_bytes="+std::to_string(std::uint64_t(fw)*fh*4));
  ImGui::GetIO().Fonts->SetTexID(static_cast<ImTextureID>(font_.idx) + 1);
  ImGui::GetIO().BackendRendererName = "OpenFlightSim_bgfx";
  ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

  const UnlitVertex triangle[3] = {{-1, -1, 0, 0xffffffff}, {3, -1, 0, 0xffffffff}, {-1, 3, 0, 0xffffffff}};
  screenTriangle_ = bgfx::createVertexBuffer(
      bgfx::copy(triangle, static_cast<std::uint32_t>(sizeof(triangle))), unlitLayout_);
  createStoreMeshes();

  synthesis_ = std::async(std::launch::async, synthesise, settings_);

  const bgfx::Caps* caps = bgfx::getCaps();
  stats_.backend = bgfx::getRendererName(bgfx::getRendererType());
  // This bgfx exposes enumerated GPUs but no name lookup, so report the
  // renderer type and a stable device identifier instead of a marketing name.
  {
    char device[32];
    std::snprintf(device, sizeof(device), "0x%04x:0x%04x", caps->gpu[0].vendorId,
                  caps->gpu[0].deviceId);
    stats_.adapter = device;
  }
  log("RENDER", std::string("bgfx ") + stats_.backend + " | " + stats_.adapter + " | MSAA x" +
                    std::to_string(settings_.msaaSamples) + " | maxTex " +
                    std::to_string(caps->limits.maxTextureSize) + " | maxViews " +
                    std::to_string(caps->limits.maxViews));
  return true;
}

Renderer::Synthesis Renderer::synthesise(const GraphicsSettings& settings) {
  const auto start = std::chrono::steady_clock::now();
  Synthesis data;
  data.weather = settings.weather;
  // Rain carries its own haze: visibility closes in with intensity.
  data.weather.visibilityKm = std::min(data.weather.visibilityKm,
                                       glm::mix(data.weather.visibilityKm, 7.f, data.weather.precipitation));
  data.atmosphere = std::make_unique<AtmosphereModel>(AtmosphereParameters::fromWeather(
      data.weather.visibilityKm, data.weather.fogDensity, data.weather.fogHeight));
  data.landscape = std::make_unique<Landscape>();
  data.map = buildMapImage(*data.landscape);
  data.cloudShape = procedural::cloudShapeVolume(kCloudShapeSize);
  data.cloudDetail = procedural::cloudDetailVolume(kCloudDetailSize);
  data.weatherMap = procedural::weatherMap(kWeatherMapSize);
  data.noiseTile = procedural::noiseTile(kNoiseTileSize);
  data.waterNormal = procedural::waterNormalTile(kWaterTileSize);
  data.layers = procedural::terrainLayers(settings.terrain == TerrainQuality::Low ? 256 : 512);
  log("RENDER", "Environment synthesised in " +
                    std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start).count()) + " ms");
  return data;
}

void Renderer::finishEnvironment() {
  Synthesis data = synthesis_.get();
  createAtmosphereResources(data);
  buildEnvironment(data);
  stats_.environmentTextureBytes = static_cast<std::size_t>(auxiliaryTextureBytes_);
  log("RENDER", "Procedural environment textures " + std::to_string(auxiliaryTextureBytes_ / (1024 * 1024)) + " MB");
}

std::uint32_t Renderer::resetFlags() const {
  std::uint32_t reset = BGFX_RESET_NONE;
  if (settings_.vsync) reset |= BGFX_RESET_VSYNC;
  // Without this flag bgfx leaves the maximum anisotropy at zero, and samplers
  // that request anisotropic filtering silently fall back to trilinear.
  if (settings_.anisotropic) reset |= BGFX_RESET_MAXANISOTROPY;
  return reset;
}

void Renderer::destroy() {
  if (!initialized_) return;
  // Never leave the synthesis thread running past the renderer.
  if (synthesis_.valid()) synthesis_.wait();
  destroyEnvironment();
  destroyAtmosphereResources();

  for (auto& [type, asset] : models_) {
    (void)type;
  for (GpuLevel& level : asset.levels) {
    if (bgfx::isValid(level.indexBuffer)) bgfx::destroy(level.indexBuffer);
    if (bgfx::isValid(level.vertexBuffer)) bgfx::destroy(level.vertexBuffer);
    level = GpuLevel{};
  }
  // Handles may be shared by material slots; texture uploads are cached per asset.
  std::vector<std::uint16_t> destroyed;
  for (const auto texture : asset.textures)
    if (bgfx::isValid(texture) && std::find(destroyed.begin(),destroyed.end(),texture.idx)==destroyed.end()) {
      bgfx::destroy(texture); destroyed.push_back(texture.idx);
    }
  }
  if (bgfx::isValid(whiteTexture_)) bgfx::destroy(whiteTexture_);
  // Uniforms is a plain block of handles, so it can be released as an array.
  static_assert(sizeof(Uniforms) % sizeof(bgfx::UniformHandle) == 0);
  const auto* handles = reinterpret_cast<const bgfx::UniformHandle*>(&uniforms_);
  for (std::size_t i = 0; i < sizeof(Uniforms) / sizeof(bgfx::UniformHandle); ++i)
    if (bgfx::isValid(handles[i])) bgfx::destroy(handles[i]);
  if (bgfx::isValid(uiSampler_)) bgfx::destroy(uiSampler_);
  if (bgfx::isValid(font_)) bgfx::destroy(font_);
  static_assert(sizeof(Programs) % sizeof(bgfx::ProgramHandle) == 0);
  const auto* programs = reinterpret_cast<const bgfx::ProgramHandle*>(&programs_);
  for (std::size_t i = 0; i < sizeof(Programs) / sizeof(bgfx::ProgramHandle); ++i)
    if (bgfx::isValid(programs[i])) bgfx::destroy(programs[i]);
  for (auto& type : storeMeshes_) for (auto& detail : type) for (auto& part : detail)
    if (bgfx::isValid(part.vertices)) bgfx::destroy(part.vertices);
  if (bgfx::isValid(pylonMesh_.vertices)) bgfx::destroy(pylonMesh_.vertices);
  if (bgfx::isValid(flameMesh_)) bgfx::destroy(flameMesh_);
  if (bgfx::isValid(screenTriangle_)) bgfx::destroy(screenTriangle_);
  bgfx::shutdown();
  initialized_ = false;
  log("RENDER", "bgfx shutdown complete");
}

// ---------------------------------------------------------------------------
// Aircraft asset
// ---------------------------------------------------------------------------

namespace {
Renderer::AircraftSource prepareAircraftSource(const std::string& path, AircraftType type,
                                               unsigned maximum, bool anisotropic) {
  Renderer::AircraftSource source;
  source.type = type;
  source.path = path;
  Mesh& mesh = source.mesh;
  try {
    mesh = loadGltf(path);
    // Cell sizes tuned against the measured A320 primitive histogram: 0.15 m
    // keeps the silhouette within 0.02 m, 0.45 m is a distant-aircraft proxy.
    const float cells[kLodCount - 1] = {0.15f, 0.45f};
    const auto& lodAssets=aircraftDefinition(type).lodAssets;
    source.gpu = buildGpuMesh(mesh, cells, lodAssets[0].empty() ? kLodCount : 1);
    if (!lodAssets[0].empty()) {
      for (const auto lodPath : lodAssets) {
        if (lodPath.empty()) throw std::runtime_error("incomplete authored LOD chain");
        auto reduced=loadGltf((std::filesystem::path(path).parent_path()/std::filesystem::path(lodPath).filename()).string());
        for (auto& primitive : reduced.primitives) {
          const auto& name=reduced.materials.at(primitive.material).name;
          auto mat=std::find_if(mesh.materials.begin(),mesh.materials.end(),[&](const auto& m){return m.name==name;});
          if (mat==mesh.materials.end()) throw std::runtime_error("LOD material absent from LOD0");
          primitive.material=static_cast<unsigned>(mat-mesh.materials.begin());
          if (primitive.transformNode>=0) {
            const auto& nodeName=reduced.nodes.at(primitive.transformNode).name;
            auto node=std::find_if(mesh.nodes.begin(),mesh.nodes.end(),[&](const auto& n){return n.name==nodeName;});
            if (node==mesh.nodes.end()) throw std::runtime_error("LOD rig node absent from LOD0");
            primitive.transformNode=static_cast<int>(node-mesh.nodes.begin());
          }
        }
        reduced.materials=mesh.materials;
        auto built=buildGpuMesh(reduced,nullptr,1);
        if (built.levels[0].triangleCount>=source.gpu.levels.back().triangleCount)
          throw std::runtime_error("authored LOD chain must strictly reduce triangles");
        source.gpu.levels.push_back(std::move(built.levels[0]));
      }
    }
  } catch (const std::exception& error) {
    source.error = error.what();
    return source;
  }

  std::map<std::tuple<int, std::uint64_t, TextureRole, bool>, int> cache;
  std::vector<std::future<std::vector<std::uint8_t>>> chains;
  for (const auto& texture : mesh.textures) {
    int upload = -1;
    if (texture.image >= 0 && texture.image < static_cast<int>(mesh.images.size())) {
      const auto& image = mesh.images[texture.image];
      std::uint64_t flags = 0;
      if (texture.wrapS==33071) flags|=BGFX_SAMPLER_U_CLAMP;
      if (texture.wrapT==33071) flags|=BGFX_SAMPLER_V_CLAMP;
      if (texture.wrapS==33648) flags|=BGFX_SAMPLER_U_MIRROR;
      if (texture.wrapT==33648) flags|=BGFX_SAMPLER_V_MIRROR;
      if (texture.magFilter==9728) flags|=BGFX_SAMPLER_MAG_POINT;
      if (texture.minFilter==9728 || texture.minFilter==9984 || texture.minFilter==9986) flags|=BGFX_SAMPLER_MIN_POINT;
      const auto role=textureRole(mesh,texture.image);
      const bool mipmaps=texture.minFilter>=9984 && texture.minFilter<=9987;
      if(anisotropic && mipmaps && !(flags&BGFX_SAMPLER_MIN_POINT)) flags|=BGFX_SAMPLER_MIN_ANISOTROPIC;
      if(texture.minFilter==9984 || texture.minFilter==9985) flags|=BGFX_SAMPLER_MIP_POINT;
      const auto key = std::tuple{texture.image,flags,role,mipmaps};
      if (cache.contains(key)) upload=cache.at(key);
      else if (!image.rgba.empty()) {
        Renderer::AircraftSource::Upload entry;
        entry.width=image.width;entry.height=image.height;
        while(entry.width>maximum || entry.height>maximum) {
          entry.width=std::max(1u,entry.width/2);entry.height=std::max(1u,entry.height/2);
        }
        entry.mipmaps=mipmaps;entry.flags=flags;
        const auto cost=textureAllocation(image.width,image.height,mipmaps,maximum);
        entry.report=path+" | "+image.source+" source="+std::to_string(image.width)+"x"+std::to_string(image.height)+
          " upload="+std::to_string(cost.width)+"x"+std::to_string(cost.height)+" format=RGBA8 mips="+std::to_string(cost.mips)+
          " estimated_gpu_bytes="+std::to_string(cost.bytes);
        // Mip filtering is as costly as decoding, so each chain gets a thread too.
        chains.push_back(std::async(std::launch::async,[&image,role,mipmaps,maximum] {
          unsigned uploadWidth=image.width,uploadHeight=image.height;
          auto pixels=(mipmaps || uploadWidth>maximum || uploadHeight>maximum)?textureMipChain(image.rgba,uploadWidth,uploadHeight,role):image.rgba;
          std::size_t skip=0;
          while(uploadWidth>maximum || uploadHeight>maximum) {
            skip+=std::size_t(uploadWidth)*uploadHeight*4;
            uploadWidth=std::max(1u,uploadWidth/2);uploadHeight=std::max(1u,uploadHeight/2);
          }
          if(skip)pixels.erase(pixels.begin(),pixels.begin()+skip);
          if(!mipmaps)pixels.resize(std::size_t(uploadWidth)*uploadHeight*4);
          pixels.shrink_to_fit();
          return pixels;
        }));
        upload=static_cast<int>(source.uploads.size());
        source.uploads.push_back(std::move(entry));
        cache.emplace(key,upload);
      }
    }
    source.textureUpload.push_back(upload);
  }
  auto sorted=aircraftTextureCosts(mesh,maximum);
  std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.allocation.bytes>b.allocation.bytes;});
  for(std::size_t i=0;i<std::min(std::size_t(5),sorted.size());++i)
    source.largest.push_back("largest "+path+" | "+sorted[i].source+" bytes="+std::to_string(sorted[i].allocation.bytes));
  // Wait for every chain before any can throw: the workers read mesh.images.
  for(auto& chain:chains) chain.wait();
  for(std::size_t i=0;i<chains.size();++i) source.uploads[i].pixels=chains[i].get();
  for(auto& image:mesh.images) std::vector<std::uint8_t>().swap(image.rgba);
  return source;
}
}  // namespace

std::future<Renderer::AircraftSource> Renderer::prepareAircraft(const std::string& path, AircraftType type) const {
  const unsigned maximum=std::min<unsigned>(unsigned(std::clamp(settings_.textureMaxSize,512,8192)),bgfx::getCaps()->limits.maxTextureSize);
  return std::async(std::launch::async, prepareAircraftSource, path, type, maximum, settings_.anisotropic);
}

bool Renderer::loadAircraft(const std::string& path, AircraftType type) {
  if (model(type).loaded) return true;
  return finishAircraft(prepareAircraft(path, type).get());
}

void Renderer::loadingFrame(std::string_view status) {
  bgfx::setViewRect(0, 0, 0, width_, height_);
  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x0b1016ffu, 1.0f, 0);
  bgfx::touch(0);
  bgfx::setDebug(BGFX_DEBUG_TEXT);
  bgfx::dbgTextClear();
  // The debug text grid uses 8x16 pixel cells.
  const int column = std::max(0, (width_ / 8 - static_cast<int>(status.size())) / 2);
  bgfx::dbgTextPrintf(static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(height_ / 32), 0x0f,
                      "%.*s", static_cast<int>(status.size()), status.data());
  bgfx::frame();
  bgfx::setDebug(BGFX_DEBUG_NONE);
  bgfx::resetView(0);
}

bool Renderer::finishAircraft(AircraftSource source) {
  Model& asset = model(source.type);
  if (asset.loaded) return true;
  if (!source.error.empty()) {
    log("ASSET", std::string("Failed to load aircraft model: ") + source.error);
    asset.loaded = false;
    return false;
  }
  Mesh& mesh = source.mesh;
  asset.report = mesh.report;
  asset.name = std::string(aircraftDefinition(source.type).displayName);
  asset.materials = mesh.materials;
  asset.nodes = std::move(mesh.nodes);
  asset.mesh = std::move(source.gpu);

  std::vector<bgfx::TextureHandle> handles;
  for (const auto& upload : source.uploads) {
    const auto handle=bgfx::createTexture2D(static_cast<std::uint16_t>(upload.width),static_cast<std::uint16_t>(upload.height),
        upload.mipmaps,1,bgfx::TextureFormat::RGBA8,upload.flags,
        bgfx::copy(upload.pixels.data(),static_cast<std::uint32_t>(upload.pixels.size())));
    if(bgfx::isValid(handle)) {
      asset.textureBytes+=upload.pixels.size();
      log("TEXTURE",upload.report);
    }
    handles.push_back(handle);
  }
  for (const int upload : source.textureUpload)
    asset.textures.push_back(upload<0 ? bgfx::TextureHandle(BGFX_INVALID_HANDLE) : handles[upload]);

  {
    std::uint64_t aircraftBytes=0;
    for(const auto& d:aircraftDefinitions())aircraftBytes+=model(d.type).textureBytes;
    log("TEXTURE", "aircraft_texture_gpu_bytes="+std::to_string(aircraftBytes)+
      " total_texture_gpu_bytes="+std::to_string(aircraftBytes+auxiliaryTextureBytes_)+
      " (includes white/font/cloud-noise; excludes framebuffer attachments/driver allocation)");
    for(const auto& line:source.largest) log("TEXTURE", line);
  }
  for (GpuLevel& level : asset.levels) {
    if (bgfx::isValid(level.indexBuffer)) bgfx::destroy(level.indexBuffer);
    if (bgfx::isValid(level.vertexBuffer)) bgfx::destroy(level.vertexBuffer);
    level = GpuLevel{};
  }
  asset.levels.resize(asset.mesh.levels.size());
  for (std::size_t i = 0; i < asset.levels.size(); ++i) {
    const LodLevel& source = asset.mesh.levels[i];
    GpuLevel& target = asset.levels[i];
    target.batches = source.batches;
    // bgfx takes ownership of the copied memory, so it is released by handing
    // the pointer to createVertexBuffer and never destroyed here.
    target.vertexBuffer = bgfx::createVertexBuffer(
        bgfx::copy(source.vertices.data(),
                   static_cast<std::uint32_t>(source.vertices.size() * sizeof(float))),
        surfaceLayout_);
    target.indexBuffer = bgfx::createIndexBuffer(
        bgfx::copy(source.indices.data(),
                   static_cast<std::uint32_t>(source.indices.size() * sizeof(std::uint32_t))),
        BGFX_BUFFER_INDEX32);
  }
  asset.loaded = !asset.levels[0].batches.empty();
  for (const auto& level : asset.levels)
    asset.loaded = asset.loaded && bgfx::isValid(level.vertexBuffer) && bgfx::isValid(level.indexBuffer);
  if (!asset.loaded) {
    log("ASSET", "Aircraft GPU upload failed");
    return false;
  }
  asset.pylonHeight = measurePylonHeights(asset.mesh, source.type);
  if (asset.pylonHeight[0] > 0 || asset.pylonHeight[2] > 0) {
    std::string heights;
    for (std::size_t i = 0; i < 4; ++i) heights += " " + std::to_string(asset.pylonHeight[i]);
    log("ASSET", asset.name + ": pylon heights" + heights + " m");
  }
  std::string tierTriangles;
  for (const auto& level:asset.mesh.levels) {
    if (!tierTriangles.empty()) tierTriangles+="/";
    tierTriangles+=std::to_string(level.triangleCount);
  }
  log("ASSET", asset.name + ": " + std::to_string(asset.materials.size()) + " materials, " +
                   std::to_string(mesh.primitives.size()) + " primitives -> " +
                   std::to_string(asset.mesh.batchCount()) + " draw calls; LOD triangles " +
                   tierTriangles + "; mesh GPU " +
                   std::to_string(gpuBytes(asset.mesh) / (1024 * 1024)) + " MB; textures " +
                   std::to_string(asset.textureBytes/(1024*1024)) + " MB");
  for (const std::string& note : asset.report) log("ASSET", "loader: " + note);
  return true;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

void Renderer::applySettings(const GraphicsSettings& settings, const Platform& platform) {
  const std::uint32_t previous = resetFlags();
  const bool effectsChanged = settings.effects != settings_.effects;
  settings_ = settings;
  if (effectsChanged) combat_.setQuality(settings_.effects);
  if (!initialized_) return;
  int w = 0, h = 0;
  if (!SDL_GetWindowSizeInPixels(platform.window(), &w, &h)) return;
  const auto width = static_cast<std::uint16_t>(std::clamp(w, 1, 65535));
  const auto height = static_cast<std::uint16_t>(std::clamp(h, 1, 65535));
  const std::uint32_t reset = resetFlags();
  if (width == width_ && height == height_ && reset == previous) return;
  width_ = width;
  height_ = height;
  swapChain_.width = width_;
  swapChain_.height = height_;
  bgfx::reset(reset, &swapChain_);
  log("RENDER", "Framebuffer " + std::to_string(width_) + "x" + std::to_string(height_) +
                    " MSAA x" + std::to_string(settings_.msaaSamples) +
                    (settings_.vsync ? " vsync" : " no-vsync"));
}

bool Renderer::resize(SDL_Window* window) {
  int w = 0, h = 0;
  if (!SDL_GetWindowSizeInPixels(window, &w, &h) || w <= 0 || h <= 0 ||
      (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED))
    return false;
  const auto width = static_cast<std::uint16_t>(std::min(w, 65535));
  const auto height = static_cast<std::uint16_t>(std::min(h, 65535));
  if (width != width_ || height != height_) {
    width_ = width;
    height_ = height;
    swapChain_.width = width_;
    swapChain_.height = height_;
    bgfx::reset(resetFlags(), &swapChain_);
    log("RENDER", "Framebuffer resized");
  }
  return true;
}

// ---------------------------------------------------------------------------
// Transforms
// ---------------------------------------------------------------------------

glm::mat4 Renderer::bodyTransform(const State& state) const {
  return aircraftMatrix(state, origin_);
}

glm::mat4 Renderer::modelTransform(const State& state) const {
  return modelTransform(state, localType_);
}
glm::mat4 Renderer::modelTransform(const State& state, AircraftType type) const {
  auto transform = kAssetToBody;
  const auto cg = aircraftDefinition(type).visual.assetCg;
  const auto shift=loadedCg(aircraftDefinition(type).flight,state);
  transform[3] = glm::vec4(cg.x-shift.x,cg.z-shift.y,cg.y-shift.z,1);
  return bodyTransform(state)*transform;
}

// ---------------------------------------------------------------------------
// Frame constants
// ---------------------------------------------------------------------------

void Renderer::updateFrameConstants(const Camera& camera, const State& local, const Weather& weather, double dt) {
  (void)camera;
  (void)local;
  const double step = std::clamp(dt, 0.0, .1);
  weatherTime_ += step;

  // ---- Air ----
  // Rain carries its own haze: visibility closes in with intensity.
  WeatherSettings air = settings_.weather;
  air.visibilityKm = std::min(air.visibilityKm, glm::mix(air.visibilityKm, 7.f, air.precipitation));
  atmosphereRebuildTimer_ += step;
  const bool airChanged = air.visibilityKm != atmosphereWeather_.visibilityKm ||
                          air.fogDensity != atmosphereWeather_.fogDensity ||
                          air.fogHeight != atmosphereWeather_.fogHeight;
  // The tables take a few milliseconds; while a slider is dragged, rebuild at
  // most a few times a second.
  if (airChanged && atmosphereRebuildTimer_ > .2) {
    atmosphereWeather_ = air;
    atmosphereRebuildTimer_ = 0;
    atmosphere_->rebuild(AtmosphereParameters::fromWeather(air.visibilityKm, air.fogDensity, air.fogHeight));
    uploadAtmosphereTables();
  }
  const AtmosphereParameters& p = atmosphere_->parameters();

  float sunVector[3];
  ofs::client::sunDirection(settings_.sky, sunVector);
  sun_ = glm::vec3(sunVector[0], sunVector[1], sunVector[2]);
  const float eyeAltitude = std::max(cameraEye_.y + static_cast<float>(-origin_.z), 1.f);
  lighting_ = atmosphere_->lighting(eyeAltitude, {sun_.x, sun_.y, sun_.z});

  // ---- Exposure ----
  const bool clouds = cloudsEnabled();
  // Under a deck of cloud the ground receives far less direct light than the
  // clear-sky meter reading; expose for what the camera is actually in.
  const float under = clouds && eyeAltitude < settings_.cloudBase + settings_.cloudThickness * .5f
                          ? std::clamp((settings_.cloudCoverage - .55f) / .4f, 0.f, 1.f) : 0.f;
  const float metered = settings_.sky.autoExposure ? lighting_.meteredIrradiance * (1.f - .55f * under) : 9.5f;
  const float target = exposureFromIrradiance(metered, settings_.sky.exposureCompensation);
  // The eye and a camera both take a moment to adapt.
  exposure_ = exposurePrimed_ ? exposure_ + (target - exposure_) * static_cast<float>(1 - std::exp(-step / .45)) : target;
  exposurePrimed_ = true;

  // ---- Clouds ----
  glm::dvec2 wind(weather.wind_ned.y, -weather.wind_ned.x);
  // Still air would freeze the sky; give the layer a gentle default drift.
  if (glm::dot(wind, wind) < 1.) wind = {8.5, 3.2};
  cloudDrift_ += wind * step;

  frame_.cameraPos = glm::vec4(cameraEye_, static_cast<float>(std::fmod(weatherTime_, 3600.)));
  frame_.worldOrigin = glm::vec4(renderDirection(origin_), exposure_);
  frame_.sunDirection = glm::vec4(sun_, kEmissiveRadiance);
  frame_.sunIrradiance = glm::vec4(lighting_.sunIrradiance.r, lighting_.sunIrradiance.g, lighting_.sunIrradiance.b,
                                    settings_.cloudShadows && clouds ? 1.f : 0.f);
  const auto rgb = [](const Rgb& c) { return glm::vec4(c.r, c.g, c.b, 0); };
  frame_.ambient[0] = rgb(lighting_.ambientConstant);
  frame_.ambient[1] = rgb(lighting_.ambientX);
  frame_.ambient[2] = rgb(lighting_.ambientY);
  frame_.ambient[3] = rgb(lighting_.ambientZ);
  frame_.viewport = glm::vec4(width_, height_, 1.f / width_, 1.f / height_);
  frame_.atmoGeometry = glm::vec4(p.planetRadius, p.atmosphereHeight, p.rayleighScaleHeight, p.mieScaleHeight);
  frame_.rayleigh = glm::vec4(p.rayleighScattering.r, p.rayleighScattering.g, p.rayleighScattering.b, p.mieAnisotropy);
  frame_.mie = glm::vec4(p.mieScattering, p.mieExtinction, p.fogExtinction, p.fogScaleHeight);
  frame_.ozone = glm::vec4(p.ozoneAbsorption.r, p.ozoneAbsorption.g, p.ozoneAbsorption.b, eyeAltitude);
  frame_.groundAlbedo = glm::vec4(p.groundAlbedo.r, p.groundAlbedo.g, p.groundAlbedo.b, settings_.renderDistance);
  frame_.solar = glm::vec4(p.solarIrradiance.r, p.solarIrradiance.g, p.solarIrradiance.b,
                           static_cast<float>(std::fmod(weatherTime_, 3600.)));
  frame_.cloudLayer = glm::vec4(settings_.cloudCoverage, settings_.cloudBase, settings_.cloudThickness, clouds ? 1.f : 0.f);
  frame_.cloudWeather = glm::vec4(static_cast<float>(cloudDrift_.x), static_cast<float>(cloudDrift_.y),
                                  kCloudExtinction, 1.f / kWeatherMapExtent);
  const float marchRange = settings_.clouds == CloudQuality::Low ? 45000.f
                         : settings_.clouds == CloudQuality::Medium ? 70000.f : 110000.f;
  frame_.cloudShape = glm::vec4(clouds ? settings_.cirrusCoverage : 0.f, kCirrusAltitude,
                                std::min(marchRange, settings_.renderDistance), 0.f);
  frame_.misc = glm::vec4(static_cast<float>(std::fmod(double(frameIndex_) * 0.6180339887, 1.0)),
                          bgfx::getCaps()->originBottomLeft ? 1.f : 0.f, settings_.weather.precipitation,
                          settings_.weather.precipitation);
  frame_.cameraForward = glm::vec4(cameraForward_, kRangeScale);
  // Relief shadows only matter, and only cost anything, while the sun is low.
  const bool relief = settings_.terrainShadows && settings_.terrain == TerrainQuality::High &&
                      settings_.sky.sunElevationDeg < 42.f && sun_.y > 0;
  frame_.quality = glm::vec4(static_cast<float>(settings_.terrain), settings_.water ? 1.f : 0.f, 1.f, relief ? 8.f : 0.f);
  const glm::vec3 windRender = renderDirection(weather.wind_ned);
  frame_.wind = glm::vec4(glm::dot(windRender, windRender) < 1.f ? glm::vec3(4.f, 0, 1.5f) : windRender,
                          static_cast<float>(std::fmod(weatherTime_ * .37, 6.2831853)));

  stats_.exposure = exposure_;
  stats_.sunIlluminanceLux = lighting_.sunIrradiance.luminance() * 10000.f;
  stats_.skyIlluminanceLux = lighting_.skyIrradianceUp.luminance() * 10000.f;
}

void Renderer::bindFrame(const glm::mat4& viewProj) {
  bgfx::setUniform(uniforms_.frame, &frame_, sizeof(FrameConstants) / sizeof(glm::vec4));
  bgfx::setUniform(uniforms_.viewProj, glm::value_ptr(viewProj));
}

void Renderer::bindLighting() {
  glm::mat4 atlas[kMaxCascades];
  for (int i = 0; i < kMaxCascades; ++i) atlas[i] = cascades_[i].atlas;
  bgfx::setUniform(uniforms_.shadowMatrix, atlas, kMaxCascades);
  const std::uint32_t clamp = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
  if (bgfx::isValid(shadowAtlas_)) bgfx::setTexture(0, uniforms_.shadowAtlas, shadowAtlas_);
  bgfx::setTexture(6, uniforms_.transmittance, transmittanceTexture_, clamp);
  bgfx::setTexture(7, uniforms_.skyView, bgfx::getTexture(skyTableBuffer_), clamp);
  bgfx::setTexture(8, uniforms_.aerial, bgfx::getTexture(aerialBuffer_), clamp);
  bgfx::setTexture(9, uniforms_.weatherMap, weatherMap_);
  bgfx::setTexture(10, uniforms_.noise, noiseTile_);
}

void Renderer::fullscreenPass(bgfx::ViewId view, bgfx::ProgramHandle program, std::uint64_t state) {
  bgfx::setVertexBuffer(0, screenTriangle_);
  bgfx::setState(state);
  bgfx::submit(view, program);
  ++stats_.drawCalls;
  ++stats_.triangles;
}

void Renderer::applyMaterial(const Material& material, const Model* asset, float detail) {
  const auto textureFor = [&](int index) {
    return asset && index>=0 && index<static_cast<int>(asset->textures.size()) && bgfx::isValid(asset->textures[index])
        ? asset->textures[index] : whiteTexture_;
  };
  bgfx::setTexture(1,uniforms_.baseTexture,textureFor(material.baseColorTexture));
  bgfx::setTexture(2,uniforms_.mrTexture,textureFor(material.metallicRoughnessTexture));
  bgfx::setTexture(3,uniforms_.emissiveTexture,textureFor(material.emissiveTexture));
  bgfx::setTexture(4,uniforms_.normalTexture,textureFor(material.normalTexture));
  bgfx::setTexture(5,uniforms_.occlusionTexture,textureFor(material.occlusionTexture));
  bgfx::setUniform(uniforms_.normalSettings,glm::value_ptr(glm::vec4(material.normalScale,material.environmentReflection,material.occlusionTexture>=0?material.occlusionStrength:0.f,0)));
  const glm::vec4 flags(textureFor(material.baseColorTexture).idx!=whiteTexture_.idx ? 1.f:0.f,
      textureFor(material.metallicRoughnessTexture).idx!=whiteTexture_.idx ? 1.f:0.f,
      textureFor(material.emissiveTexture).idx!=whiteTexture_.idx ? 1.f:0.f,
      textureFor(material.normalTexture).idx!=whiteTexture_.idx ? 1.f:0.f);
  bgfx::setUniform(uniforms_.textureFlags,glm::value_ptr(flags));
  bgfx::setUniform(uniforms_.alphaSettings,glm::value_ptr(glm::vec4(
      material.alpha==Material::Alpha::Mask ? 1.f:0.f,material.alphaCutoff,
      material.alpha==Material::Alpha::Blend ? 1.f:0.f,0)));
  bgfx::setUniform(uniforms_.baseColor,
                   glm::value_ptr(glm::vec4(material.baseColor[0], material.baseColor[1],
                                            material.baseColor[2], material.baseColor[3])));
  bgfx::setUniform(uniforms_.metallicRoughness,
                   glm::value_ptr(glm::vec4(material.metallic, material.roughness, detail, 0)));
  bgfx::setUniform(uniforms_.emissive,
                   glm::value_ptr(glm::vec4(material.emissive[0], material.emissive[1],
                                            material.emissive[2], 0)));
  bgfx::setUniform(uniforms_.doubleSided,
                   glm::value_ptr(glm::vec4(material.doubleSided ? 1.0f : 0.0f, 0, 0, 0)));
  // Undamaged unless the caller says otherwise: drawAircraft overrides this.
  static const glm::vec4 kIntact[7]{};
  bgfx::setUniform(uniforms_.damage, kIntact, 7);
}

void Renderer::drawGrid() {
  bindFrame(viewProj_);
  bindLighting();
  bgfx::setUniform(uniforms_.model, glm::value_ptr(glm::translate(glm::mat4{1}, localPosition({}, origin_))));
  bgfx::setVertexBuffer(0, grid_);
  bgfx::setState(kOpaqueState | BGFX_STATE_PT_LINES);
  bgfx::submit(kViewWorld, programs_.unlit);
  ++stats_.drawCalls;
}

void Renderer::drawAircraft(const Instance& instance, bool hide, bgfx::ViewId view, const glm::mat4& viewProj) {
  const auto& asset = model(instance.type);
  if (hide || aircraftCrashed(*instance.state) || !asset.loaded) return;
  const std::size_t lod = instance.lod;
  const GpuLevel& level = asset.levels[lod];
  if (level.batches.empty() || !bgfx::isValid(level.vertexBuffer)) return;
  activeLod_ = lod;
  const glm::mat4 base = modelTransform(*instance.state, instance.type);
  // Battle damage, in the layout pbr_fs.glsl documents for u_damage.
  const DamageView& damage = instance.damage;
  const bool damaged = damage.any();
  // Torn wings and fins are open: their insides must be drawn too.
  const bool torn = damage[DamagePart::LeftWing] > 0 || damage[DamagePart::RightWing] > 0 || damage[DamagePart::Tail] > 0;
  glm::vec4 damageUniform[7]{};
  if (damaged) {
    const auto& definition = aircraftDefinition(instance.type);
    const auto geometry = damageGeometry(instance.type);
    const auto cg = definition.visual.assetCg;
    const auto left = engineBay(definition, 0), right = engineBay(definition, 1);
    damageUniform[0] = glm::vec4(damage[DamagePart::LeftWing], damage[DamagePart::RightWing], damage[DamagePart::Tail],
                                 damage[DamagePart::Fuselage]);
    damageUniform[1] = glm::vec4(damage[DamagePart::LeftEngine], damage[DamagePart::RightEngine], instance.damageSeed, 1);
    damageUniform[2] = glm::vec4(cg.x, cg.y, cg.z, geometry.wingRoot);
    damageUniform[3] = glm::vec4(geometry.wingTip, geometry.wingAft, geometry.wingFore, geometry.finFore);
    damageUniform[4] = glm::vec4(geometry.finBase, geometry.finTop, left.fore.x - left.aft.x, left.radius);
    damageUniform[5] = glm::vec4(left.aft.x, left.aft.y, left.aft.z, 0);
    damageUniform[6] = glm::vec4(right.aft.x, right.aft.y, right.aft.z, 0);
  }
  // Write all opaque depth before drawing thin transparent glass and decals.
  for (const bool translucent : {false,true}) for (const Batch& batch : level.batches) {
    if (batch.material >= asset.materials.size()) continue;
    if ((asset.materials[batch.material].alpha==Material::Alpha::Blend)!=translucent) continue;
    bindFrame(viewProj);
    bindLighting();
    bgfx::setVertexBuffer(0, level.vertexBuffer);
    const auto modelMatrix = batch.transformNode<0 ? base : base*glm::make_mat4(instance.deltas[batch.transformNode].data());
    bgfx::setUniform(uniforms_.model, glm::value_ptr(modelMatrix));
    bgfx::setUniform(uniforms_.normalMatrix, glm::value_ptr(glm::inverseTranspose(glm::mat3(modelMatrix))));
    Material material = asset.materials[batch.material];
    applyMaterial(material,&asset);
    if (damaged) bgfx::setUniform(uniforms_.damage, damageUniform, 7);
    const auto renderState = material.alpha==Material::Alpha::Blend ? kBlendState : kOpaqueState;
    const bool bothSides = material.doubleSided || (torn && !translucent);
    bgfx::setState((bothSides ? (renderState & ~BGFX_STATE_CULL_MASK) : renderState) | BGFX_STATE_MSAA | (settings_.wireframeAircraft ? BGFX_STATE_PT_LINES : 0));
    bgfx::setIndexBuffer(level.indexBuffer, batch.firstIndex, batch.indexCount);
    bgfx::submit(view, programs_.pbr);
    stats_.triangles += batch.indexCount / 3;
    ++stats_.drawCalls;
  }
  ++stats_.aircraftDrawn;
  ++stats_.lodCounts[lod];
}

void Renderer::drawEffects(const CombatVisuals& combat) {
  if (pool_.size() == 0 && combat.lines.empty()) return;
  // Effects are camera-facing quads rebuilt every frame, so they go into a
  // transient vertex buffer: no persistent buffer, no upload call and nothing
  // to resize. The bounded pool bounds the vertex count.
  const auto maxVertices = static_cast<std::uint32_t>((pool_.size() * 4 + combat.lines.size()) * 6);
  if (bgfx::getAvailTransientVertexBuffer(maxVertices, effectLayout_) < maxVertices) return;

  effectScratch_.clear();
  effectScratch_.reserve(static_cast<std::size_t>(maxVertices) * kEffectVertexFloats);
  auto emitQuad = [&](const glm::vec3& centre, const glm::vec3& right, const glm::vec3& up,
                      float halfRight, float halfUp, std::uint32_t color, float style = 0) {
    const glm::vec3 corners[4] = {centre - right * halfRight - up * halfUp,
                                  centre + right * halfRight - up * halfUp,
                                  centre + right * halfRight + up * halfUp,
                                  centre - right * halfRight + up * halfUp};
    // The effect program is unlit and reads a_color0, so the colour rides in
    // the same slot the aircraft surface layout would use for its uv.
    const float channels[4] = {
        static_cast<float>((color >> 0) & 0xff) / 255.0f,
        static_cast<float>((color >> 8) & 0xff) / 255.0f,
        static_cast<float>((color >> 16) & 0xff) / 255.0f,
        static_cast<float>((color >> 24) & 0xff) / 255.0f};
    for (const int i : {0, 1, 2, 0, 2, 3})
    {
      const glm::vec2 uv[4]={{0,0},{1,0},{1,1},{0,1}};
      effectScratch_.insert(effectScratch_.end(),
                            {corners[i].x, corners[i].y, corners[i].z, channels[0], channels[1],
                             channels[2], channels[3],uv[i].x+style*2.f,uv[i].y});
    }
  };

  for (const Effect& effect : pool_.effects()) {
    const float t = std::min(1.0f, effect.age / std::max(effect.lifetime, 1e-4f));
    const glm::vec3 centre = localPosition(effect.position, origin_);
    std::uint32_t color = effect.tint;
    const auto alpha = static_cast<std::uint32_t>((color >> 24) & 0xff);
    const float formation=effect.kind==EffectKind::Contrail ? std::clamp(effect.age/.12f,0.f,1.f) :
        effect.kind==EffectKind::Vapor ? std::clamp(t/.12f,0.f,1.f) : 1.f;
    const auto faded = static_cast<std::uint32_t>(alpha * formation * (1.0f - t * t));

    if (!effect.billboard && effect.kind == EffectKind::Tracer) {
      const glm::vec3 velocity = renderDirection(effect.velocity);
      const float speed = glm::length(velocity);
      if (speed < .5f) continue;
      const glm::vec3 axis = velocity / speed;
      // A round is not drawn behind the muzzle it has only just left.
      const float halfLength = std::min(effect.stretch, speed * effect.age + .5f) * .5f;
      glm::vec3 across=glm::cross(axis,cameraEye_-centre);
      if (glm::dot(across,across)<1e-8f) across=basisRight_;
      across=glm::normalize(across);
      // Minimum angular width keeps distant rounds legible at game resolution.
      const float range=glm::length(cameraEye_-centre);
      const float radius=std::max(effect.size*.5f,range*.00032f);
      // The burning compound leaves a faint line of smoke that soon thins out.
      const float wake=std::min(60.f,speed*effect.age);
      if (wake>4.f && range<1500.f)
        emitQuad(centre-axis*(halfLength*2+wake*.5f),axis,across,wake*.5f,radius*1.8f,
                 0x00c8ccd0u|(static_cast<std::uint32_t>(faded*.10f)<<24),1);
      const auto glowAlpha=static_cast<std::uint32_t>(faded*.42f);
      emitQuad(centre-axis*halfLength,axis,across,halfLength,radius*4.2f,
               (color&0x00ffffffu)|(glowAlpha<<24),2);
      emitQuad(centre-axis*halfLength,axis,across,halfLength,radius,
               0x00d8f4ffu|(faded<<24),2);
      // A hot tip faces the camera even when the streak is seen end-on.
      emitQuad(centre,basisRight_,basisUp_,radius*2.6f,radius*2.6f,
               0x00c8ecffu|(faded<<24),3);
      continue;
    }
    if (effect.kind == EffectKind::Trail) {
      // One length of a smoke trail: a soft tube that swells and thins as it
      // ages, overlapping its neighbours so the whole reads as one plume.
      const glm::vec3 axis = glm::normalize(renderDirection(effect.axis));
      glm::vec3 across = glm::cross(axis, cameraEye_ - centre);
      if (glm::dot(across, across) < 1e-8f) across = basisUp_;
      const float radius = effect.size * (.6f + 2.6f * std::sqrt(t));
      const auto alphaNow = static_cast<std::uint32_t>(alpha * (1.f - t) * (1.f - t));
      // Each length reaches well into the next, so their soft ends sum to an even column.
      emitQuad(centre, axis, glm::normalize(across), effect.stretch * 1.15f + radius, radius,
               (color & 0x00ffffffu) | (alphaNow << 24), 4);
      continue;
    }
    if (effect.kind == EffectKind::Shockwave) {
      // A thin ring racing outward and fading as it goes.
      const float grown = 1.f - (1.f - t) * (1.f - t);
      const auto ringAlpha = static_cast<std::uint32_t>(alpha * (1.f - t) * (1.f - t));
      emitQuad(centre, basisRight_, basisUp_, effect.size * (.1f + .9f * grown), effect.size * (.1f + .9f * grown),
               (color & 0x00ffffffu) | (ringAlpha << 24), 6);
      continue;
    }
    if (effect.kind == EffectKind::Flash || effect.kind == EffectKind::MuzzleFlash) {
      // A star of light, turned by its seed so no two flashes look alike.
      const float turn = effect.seed * 6.2831853f, c = std::cos(turn), sn = std::sin(turn);
      const float radius = effect.size * (effect.kind == EffectKind::Flash ? .45f + .55f * (1.f - t) : .55f + .95f * (1.f - t));
      emitQuad(centre, basisRight_ * c + basisUp_ * sn, basisUp_ * c - basisRight_ * sn, radius, radius,
               (color & 0x00ffffffu) | (faded << 24), 7);
      continue;
    }
    if (effect.kind == EffectKind::Debris || effect.kind == EffectKind::Spark) {
      const glm::vec3 velocity = renderDirection(effect.velocity);
      const float speed = glm::length(velocity);
      if (speed < 0.5f) continue;
      const glm::vec3 axis = velocity / speed;
      glm::vec3 across = effect.billboard ? glm::cross(axis, cameraEye_ - centre) :
          glm::cross(renderDirection(effect.normal), axis);
      if (glm::dot(across, across) < 1e-8f) across = basisRight_;
      across = glm::normalize(across);
      color = (color & 0x00ffffffu) | (faded << 24);
      emitQuad(centre, axis, across, effect.stretch * 0.5f, effect.size * 0.5f, color, effect.kind==EffectKind::Spark?2.f:0.f);
      continue;
    }

    float radius = effect.size;
    if (effect.kind == EffectKind::Contrail) {
      radius = effect.size * (0.55f + 2.0f * t);
    } else if (effect.kind == EffectKind::Vapor) {
      radius = effect.size * (.85f + .4f*t);
    } else if (effect.kind == EffectKind::Smoke || effect.kind == EffectKind::Dust) {
      radius = effect.size * (0.35f + 0.95f * t);
    } else if (effect.kind == EffectKind::Fire) {
      radius = effect.size*(.7f+.6f*std::sin(t*float(kPi)));
    } else if (effect.kind == EffectKind::Explosion) {
      radius = effect.size * (0.30f + 1.30f * std::sin(t * static_cast<float>(kPi)));
    } else if (effect.kind == EffectKind::MuzzleFlash) {
      radius = effect.size * (0.55f + 0.95f * (1.0f - t));
    } else if (effect.kind == EffectKind::EngineHeat) {
      radius = effect.size * (0.6f + 0.5f * t);
    } else {
      radius = effect.size * (0.5f + 0.6f * t);
    }
    color = (color & 0x00ffffffu) | (faded << 24);
    if (effect.kind == EffectKind::Vapor && effect.stretch > 0) {
      const glm::vec3 axis = glm::normalize(renderDirection(effect.axis));
      glm::vec3 across = glm::cross(axis, cameraEye_ - centre);
      if (glm::dot(across, across) < 1e-8f) across = basisUp_;
      emitQuad(centre, axis, glm::normalize(across), effect.stretch*.5f, radius, color, 1);
      continue;
    }
    float style=0;
    if (effect.kind==EffectKind::MuzzleFlash || effect.kind==EffectKind::Impact || effect.kind==EffectKind::Light) style=3;
    if (effect.kind==EffectKind::Smoke || effect.kind==EffectKind::Dust) style=4;
    if (effect.kind==EffectKind::Fire || effect.kind==EffectKind::Explosion) style=5;
    emitQuad(centre, basisRight_, basisUp_, radius, radius, color,style);
  }

  // Keep diagnostics visible through the mesh, so the CG and axes inside the
  // fuselage can be inspected. Ordinary effects retain their depth test.
  const auto submit = [&](bool xray) {
    const auto vertexCount = static_cast<std::uint32_t>(effectScratch_.size() / kEffectVertexFloats);
    if (vertexCount == 0) return;
    bindFrame(viewProj_);
    bindLighting();
    bgfx::TransientVertexBuffer transient;
    bgfx::allocTransientVertexBuffer(&transient, vertexCount, effectLayout_);
    std::memcpy(transient.data, effectScratch_.data(),
                static_cast<std::size_t>(vertexCount) * kEffectVertexFloats * sizeof(float));
    bgfx::setVertexBuffer(0, &transient);
    bgfx::setUniform(uniforms_.model, glm::value_ptr(glm::mat4{1}));
    // The resolved scene range softens particles against geometry, and the
    // accumulated cloud layer hides the ones that lie behind cloud.
    const std::uint32_t point = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT;
    const bool clouds = cloudsEnabled() && bgfx::isValid(cloudHistory_[cloudHistoryIndex_]);
    bgfx::setTexture(13, uniforms_.sceneRange, bgfx::getTexture(hdrBuffer_, 1), point);
    bgfx::setTexture(14, uniforms_.cloudLayer, clouds ? bgfx::getTexture(cloudHistory_[cloudHistoryIndex_], 0) : whiteTexture_, BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    bgfx::setTexture(15, uniforms_.cloudDepth, clouds ? bgfx::getTexture(cloudHistory_[cloudHistoryIndex_], 1) : whiteTexture_, point);
    bgfx::setUniform(uniforms_.effectParams, glm::value_ptr(glm::vec4(clouds ? 1.f : 0.f, 0, 0, 0)));
    bgfx::setState((xray?(kBlendState & ~BGFX_STATE_DEPTH_TEST_MASK):kBlendState) | BGFX_STATE_MSAA);
    bgfx::submit(kViewAtmosphere, programs_.effect);
    ++stats_.drawCalls;
    stats_.activeParticles += vertexCount / 6;
  };
  const auto drawLine = [&](const CombatVisuals::Line& line) {
    const glm::vec3 a = localPosition(line.start, origin_), b = localPosition(line.end, origin_);
    const glm::vec3 delta = b-a;
    const float length = glm::length(delta);
    if (length < .001f) return;
    const glm::vec3 axis = delta/length, centre=(a+b)*.5f;
    glm::vec3 across=glm::cross(axis,cameraEye_-centre);
    if (glm::length(across)<.001f) across=basisRight_;
    emitQuad(centre, axis, glm::normalize(across), length * .5f, line.halfWidth,
             line.color);
  };
  for(const auto& line:combat.lines)if(!line.xray)drawLine(line);
  submit(false);
  effectScratch_.clear();
  for(const auto& line:combat.lines)if(line.xray)drawLine(line);
  submit(true);
}

void Renderer::ensureFlameMesh() {
  if (bgfx::isValid(flameMesh_)) return;
  // Fixed shared open volumetric shells: no per-frame vertex uploads or network
  // particle stream. A unit cone of rings, shaped in the vertex shader, and
  // one quad for the glow at the nozzle.
  std::vector<float> data;
  constexpr unsigned rings=24, sectors=24;
  const auto vertex=[&](unsigned j,unsigned i) {
    const float t=float(j)/rings, u=float(i)/sectors;
    const float angle=u*float(2*kPi);
    data.insert(data.end(),{-t,std::cos(angle),std::sin(angle),1,1,1,1,u,t});
  };
  for(unsigned j=0;j<rings;++j) for(unsigned i=0;i<sectors;++i) {
    vertex(j,i);vertex(j+1,i);vertex(j+1,i+1);
    vertex(j,i);vertex(j+1,i+1);vertex(j,i+1);
  }
  flameVertices_=static_cast<unsigned>(data.size()/kEffectVertexFloats);
  for (const unsigned i:{0u,1u,2u,0u,2u,3u}) {
    const glm::vec2 p[4]={{-1,-1},{1,-1},{1,1},{-1,1}};
    data.insert(data.end(),{p[i].x,p[i].y,0,.72f,.65f,1,.18f,(p[i].x+1)*.5f,(p[i].y+1)*.5f});
  }
  flameMesh_=bgfx::createVertexBuffer(bgfx::copy(data.data(),static_cast<unsigned>(data.size()*sizeof(float))),effectLayout_);
}

void Renderer::drawFlame(const glm::mat4& matrix, const glm::vec3& glowCentre, float glowRadius, float intensity,
                         float seed, bool rocket) {
  // Three translucent layers per flame, drawn after the clouds against the
  // world's depth. The palette is chosen per draw: uniforms keep whatever an
  // earlier draw left in them.
  const glm::vec4 palette(rocket ? 1.f : 0.f, 0, 0, 0);
  for(unsigned layer=0;layer<3;++layer) {
    bindFrame(viewProj_);
    bgfx::setUniform(uniforms_.flame,glm::value_ptr(glm::vec4(intensity,float(flameTime_),float(layer),seed)));
    bgfx::setUniform(uniforms_.effectParams,glm::value_ptr(palette));
    bgfx::setUniform(uniforms_.model,glm::value_ptr(matrix));
    bgfx::setVertexBuffer(0,flameMesh_,0,flameVertices_);
    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_DEPTH_TEST_LEQUAL|
      BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,BGFX_STATE_BLEND_ONE)|BGFX_STATE_MSAA);
    bgfx::submit(kViewAtmosphere,programs_.flame);
    ++stats_.drawCalls;stats_.triangles+=flameVertices_/3;
  }
  glm::mat4 glow{1};glow[0]=glm::vec4(basisRight_*glowRadius,0);
  glow[1]=glm::vec4(basisUp_*glowRadius,0);glow[3]=glm::vec4(glowCentre,1);
  bindFrame(viewProj_);
  bgfx::setUniform(uniforms_.flame,glm::value_ptr(glm::vec4(intensity,float(flameTime_),3,seed)));
  bgfx::setUniform(uniforms_.effectParams,glm::value_ptr(palette));
  bgfx::setUniform(uniforms_.model,glm::value_ptr(glow));
  bgfx::setVertexBuffer(0,flameMesh_,flameVertices_,6);
  bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_DEPTH_TEST_LEQUAL|
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,BGFX_STATE_BLEND_ONE)|BGFX_STATE_MSAA);
  bgfx::submit(kViewAtmosphere,programs_.flame);++stats_.drawCalls;stats_.triangles+=2;
}

void Renderer::drawAfterburners(bool localDestroyed) {
  if (settings_.effects==EffectsQuality::Off) return;
  ensureFlameMesh();
  if (!bgfx::isValid(flameMesh_)) return;
  for(const auto& [id,instance] : instances_) {
    if (!instance.state || (id==0 && localDestroyed) || aircraftCrashed(*instance.state)) continue;
    const auto& definition=aircraftDefinition(instance.type);
    if (!instance.state || definition.flight.afterburner_thrust_each<=0 ||
        (lastCamera_.eye-instance.state->pos_ned).norm()>1200) continue;
    for(unsigned e=0;e<definition.flight.engine_count;++e) {
      const float intensity=static_cast<float>(instance.state->afterburner[e]);
      const float spool=static_cast<float>(instance.state->n1[e]);
      if (intensity<.005f && spool<.25f) continue;
      const auto& engine=definition.flight.engines[e];
      const auto cg=loadedCg(definition.flight,*instance.state);
      const auto pivot=engine.articulated_nozzle?engine.nozzle_pivot:engine.position;
      const auto arm=pivot-cg;
      const auto relative=definition.visual.exhaust[e]-pivot;
      const double angle=instance.state->nozzle_angle[e];
      const auto axis=engine.vector_axis.normalized();
      const auto point=arm+relative*std::cos(angle)+axis.cross(relative)*std::sin(angle)+
          axis*(axis.dot(relative)*(1-std::cos(angle)));
      const auto matrix=bodyTransform(*instance.state)*glm::translate(glm::mat4{1},glm::vec3(arm.x,arm.y,arm.z))*
        glm::rotate(glm::mat4{1},float(angle),glm::vec3(axis.x,axis.y,axis.z))*
        glm::translate(glm::mat4{1},glm::vec3(relative.x,relative.y,relative.z))*
        glm::scale(glm::mat4{1},glm::vec3(definition.visual.exhaustLengthScale,
          definition.visual.exhaustRadiusScale,definition.visual.exhaustRadiusScale));
      const float seed=float((id%97)*3+e*11);
      if (settings_.heatDistortion && settings_.engineHeat && sceneHasRefraction_ && spool>.25f) {
        // The hot exhaust column bends light: it is drawn as an image offset
        // into the refraction buffer, not as colour.
        const float density=static_cast<float>(isaAtAltitude(-instance.state->pos_ned.z).rho/1.225);
        const float heat=(.25f+.75f*intensity)*spool*std::sqrt(std::max(.04f,density));
        bindFrame(viewProj_);
        bgfx::setUniform(uniforms_.flame,glm::value_ptr(glm::vec4(heat,float(flameTime_),4,seed)));
        bgfx::setUniform(uniforms_.model,glm::value_ptr(matrix));
        bgfx::setUniform(uniforms_.effectParams,glm::value_ptr(glm::vec4(0,.035f,float(std::max(1,width_/2)),float(std::max(1,height_/2)))));
        bgfx::setTexture(13,uniforms_.sceneRange,bgfx::getTexture(hdrBuffer_,1),BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
        bgfx::setVertexBuffer(0,flameMesh_,0,flameVertices_);
        bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_ONE));
        bgfx::submit(kViewRefraction,programs_.flame);++stats_.drawCalls;stats_.triangles+=flameVertices_/3;
      }
      if (intensity<.005f) continue;
      drawFlame(matrix,localPosition(instance.state->pos_ned+instance.state->att.rotate(point),origin_),
                .68f*static_cast<float>(definition.visual.exhaustRadiusScale)*std::sqrt(intensity),intensity,seed,false);
    }
  }
  bgfx::discard();
}

void Renderer::drawBreakaways() {
  for (const BreakawayPiece& piece : breakaways_.pieces()) {
    const auto found = models_.find(piece.type);
    if (found == models_.end() || !found->second.loaded || found->second.levels.empty()) continue;
    const Model& asset = found->second;
    const auto& definition = aircraftDefinition(piece.type);
    const double distance = (piece.position - lastCamera_.eye).norm();
    if (distance > 8000) continue;
    const GpuLevel& level = asset.levels[std::min(asset.levels.size() - 1,
        lodForDistance(static_cast<float>(definition.visual.radius), distance * std::exp2(settings_.lodBias)))];
    if (level.batches.empty() || !bgfx::isValid(level.vertexBuffer)) continue;
    // The mesh is placed as it was on the aircraft, about the piece's own centre.
    State pose;
    pose.pos_ned = piece.position;
    pose.att = piece.attitude;
    auto anchor = kAssetToBody;
    const Vec3 cg = definition.visual.assetCg;
    anchor[3] = glm::vec4(cg.x - piece.pivot.x, cg.z - piece.pivot.y, cg.y - piece.pivot.z, 1);
    const glm::mat4 base = aircraftMatrix(pose, origin_) * anchor;
    const auto geometry = damageGeometry(piece.type);
    const auto left = engineBay(definition, 0), right = engineBay(definition, 1);
    const bool fin = piece.part == DamagePart::Tail, wreck = piece.wreck();
    if (wreck && piece.grounded) continue;
    // u_damage in its broken-away form (see pbr_fs.glsl); a wreck is the
    // ordinary form with every part destroyed.
    const glm::vec4 damage[7]{
        wreck ? glm::vec4(1) : glm::vec4(fin ? 2.f : piece.part == DamagePart::RightWing ? 1.f : 0.f, piece.inner, piece.outer, 0),
        wreck ? glm::vec4(1, 1, piece.seed, 1) : glm::vec4(0, 0, piece.seed, 2),
        glm::vec4(cg.x, cg.y, cg.z, geometry.wingRoot),
        glm::vec4(geometry.wingTip, geometry.wingAft, geometry.wingFore, geometry.finFore),
        glm::vec4(geometry.finBase, geometry.finTop, left.fore.x - left.aft.x, left.radius),
        glm::vec4(left.aft.x, left.aft.y, left.aft.z, 0), glm::vec4(right.aft.x, right.aft.y, right.aft.z, 0)};
    for (const Batch& batch : level.batches) {
      if (batch.material >= asset.materials.size() || asset.materials[batch.material].alpha == Material::Alpha::Blend) continue;
      // Skip the batches that cannot contain any of the piece. Body y is
      // asset -z and body z is asset -y, about the asset's centre of gravity.
      if (!wreck && (fin ? cg.y - batch.boundsMax[1] > -geometry.finBase
                         : piece.part == DamagePart::RightWing ? cg.z - batch.boundsMin[2] < geometry.wingRoot
                                                               : cg.z - batch.boundsMax[2] > -geometry.wingRoot))
        continue;
      bindFrame(viewProj_);
      bindLighting();
      bgfx::setVertexBuffer(0, level.vertexBuffer);
      const bool posed = batch.transformNode >= 0 && static_cast<std::size_t>(batch.transformNode) < piece.deltas.size();
      const auto modelMatrix = posed ? base * glm::make_mat4(piece.deltas[batch.transformNode].data()) : base;
      bgfx::setUniform(uniforms_.model, glm::value_ptr(modelMatrix));
      bgfx::setUniform(uniforms_.normalMatrix, glm::value_ptr(glm::inverseTranspose(glm::mat3(modelMatrix))));
      applyMaterial(asset.materials[batch.material], &asset);
      bgfx::setUniform(uniforms_.damage, damage, 7);
      bgfx::setState((kOpaqueState & ~BGFX_STATE_CULL_MASK) | BGFX_STATE_MSAA);
      bgfx::setIndexBuffer(level.indexBuffer, batch.firstIndex, batch.indexCount);
      bgfx::submit(kViewWorld, programs_.pbr);
      stats_.triangles += batch.indexCount / 3;
      ++stats_.drawCalls;
    }
  }
}

void Renderer::ui() {
  const ImDrawData* data = ImGui::GetDrawData();
  if (!data || data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0) return;
  const auto projection = bgfx::getCaps()->homogeneousDepth
      ? glm::orthoRH_NO(data->DisplayPos.x, data->DisplayPos.x + data->DisplaySize.x,
                        data->DisplayPos.y + data->DisplaySize.y, data->DisplayPos.y, 0.f, 1.f)
      : glm::orthoRH_ZO(data->DisplayPos.x, data->DisplayPos.x + data->DisplaySize.x,
                        data->DisplayPos.y + data->DisplaySize.y, data->DisplayPos.y, 0.f, 1.f);
  bgfx::setViewMode(kViewUi, bgfx::ViewMode::Sequential);
  bgfx::setViewName(kViewUi, "Dear ImGui");
  bgfx::setViewMode(kViewUi, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(kViewUi, 0, 0, width_, height_);
  bgfx::setViewFrameBuffer(kViewUi, BGFX_INVALID_HANDLE);
  bgfx::setViewTransform(kViewUi, nullptr, glm::value_ptr(projection));
  // Scissor state persists across frames in bgfx, and the loop below narrows it
  // to each ImGui command's clip rectangle. Clearing it here releases the last
  // window's rectangle so the next frame's world passes are not clipped to it.
  // UINT16_MAX is bgfx's "no scissor" sentinel.
  bgfx::setScissor(UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX);
  static_assert(sizeof(ImDrawVert) == 20);
  for (int i = 0; i < data->CmdListsCount; ++i) {
    const ImDrawList* list = data->CmdLists[i];
    const auto nv = static_cast<std::uint32_t>(list->VtxBuffer.Size);
    const auto ni = static_cast<std::uint32_t>(list->IdxBuffer.Size);
    if (bgfx::getAvailTransientVertexBuffer(nv, uiLayout_) != nv ||
        bgfx::getAvailTransientIndexBuffer(ni, sizeof(ImDrawIdx) == 4) != ni) {
      log("RENDER", "ImGui transient buffer exhausted");
      break;
    }
    bgfx::TransientVertexBuffer vb;
    bgfx::TransientIndexBuffer ib;
    bgfx::allocTransientVertexBuffer(&vb, nv, uiLayout_);
    bgfx::allocTransientIndexBuffer(&ib, ni, sizeof(ImDrawIdx) == 4);
    std::memcpy(vb.data, list->VtxBuffer.Data, static_cast<std::size_t>(nv) * sizeof(ImDrawVert));
    std::memcpy(ib.data, list->IdxBuffer.Data, static_cast<std::size_t>(ni) * sizeof(ImDrawIdx));
    for (const auto& cmd : list->CmdBuffer) {
      if (cmd.UserCallback) {
        if (cmd.UserCallback != ImDrawCallback_ResetRenderState) cmd.UserCallback(list, &cmd);
        continue;
      }
      const float x = std::clamp((cmd.ClipRect.x - data->DisplayPos.x) * data->FramebufferScale.x, 0.f,
                                 static_cast<float>(width_));
      const float y = std::clamp((cmd.ClipRect.y - data->DisplayPos.y) * data->FramebufferScale.y, 0.f,
                                 static_cast<float>(height_));
      const float right = std::clamp((cmd.ClipRect.z - data->DisplayPos.x) * data->FramebufferScale.x,
                                     0.f, static_cast<float>(width_));
      const float bottom = std::clamp((cmd.ClipRect.w - data->DisplayPos.y) * data->FramebufferScale.y,
                                      0.f, static_cast<float>(height_));
      if (right <= x || bottom <= y || cmd.ElemCount == 0) continue;
      bgfx::setScissor(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y),
                       static_cast<std::uint16_t>(right - x), static_cast<std::uint16_t>(bottom - y));
      bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                     BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA));
      const auto id = cmd.GetTexID();
      const bgfx::TextureHandle texture{static_cast<std::uint16_t>(id ? id - 1 : font_.idx)};
      bgfx::setTexture(0, uiSampler_, texture);
      bgfx::setVertexBuffer(0, &vb, cmd.VtxOffset, nv - cmd.VtxOffset);
      bgfx::setIndexBuffer(&ib, cmd.IdxOffset, cmd.ElemCount);
      bgfx::submit(kViewUi, programs_.imgui);
    }
  }
}

bool Renderer::projectToScreen(const Vec3& world, float& outX, float& outY, float& outDepth) const {
  const glm::vec4 point = viewProj_ * glm::vec4(localPosition(world, origin_), 1.0f);
  if (point.w <= 1e-4f) return false;
  const float ndcX = point.x / point.w;
  const float ndcY = point.y / point.w;
  if (ndcX < -1.05f || ndcX > 1.05f || ndcY < -1.05f || ndcY > 1.05f) return false;
  outX = (ndcX * 0.5f + 0.5f) * static_cast<float>(width_);
  outY = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(height_);
  outDepth = point.w;
  return true;
}

void Renderer::render(const Camera& camera, const State& local, const Controls& controls,
                      std::span<const RemoteAircraft> remotes, const CombatVisuals& combat,
                      const Vec3& origin, double dt, double load, const Weather& weather) {
  if (!initialized_) return;
  if (synthesis_.valid()) finishEnvironment();
  const auto preparationStart=std::chrono::steady_clock::now();
  // The simulation rebases its render origin as the aircraft travels, so it is
  // read fresh every frame rather than captured at construction.
  origin_ = origin;
  lastCamera_ = camera;
  stats_.drawCalls = 0;
  stats_.triangles = 0;
  stats_.activeParticles = 0;
  stats_.aircraftDrawn = 0;
  stats_.treesDrawn = 0;
  stats_.lodCounts = {};
  // An aircraft that blew up throws its wings and fin clear. This comes before
  // its instance is dropped, while the pose of its moving parts is still held.
  for (const CombatVisuals::Destruction& destruction : combat.destructions) {
    if (!destruction.airframe || !validAircraftType(destruction.type)) continue;
    const auto instance = instances_.find(destruction.entity);
    static const std::vector<AssetMatrix> kRestPose;
    const bool posed = instance != instances_.end() && instance->second.type == destruction.type;
    breakaways_.shatter(destruction.entity, destruction.type, destruction.state,
                        posed ? instance->second.deltas : kRestPose, float(destruction.entity % 97) * .37f);
  }
  std::erase_if(instances_,[&](const auto& entry) {
    return entry.first!=0 && std::none_of(remotes.begin(),remotes.end(),[&](const auto& remote){return remote.alive && remote.entity==entry.first;});
  });
  const auto prepare=[&](std::uint64_t id,AircraftType type,const State& state,const Controls& input,double health) {
    auto& instance=instances_[id];
    if (instance.type!=type) { instance={}; instance.type=type; }
    instance.state=&state;
    instance.damage=damageView(state,health);
    instance.damageSeed=float(id%97)*.37f;
    instance.pose.update(state,input,aircraftDefinition(type),dt);
    evaluatePose(model(type).nodes,instance.pose,instance.deltas);
    const double distance=(camera.eye-state.pos_ned).norm();
    // Shorter full-detail range for million-triangle airliners; 15% hysteresis.
    instance.lod=stableAircraftLod(aircraftDefinition(type).visual.radius,distance*std::exp2(settings_.lodBias),instance.lod,model(type).levels.size());
  };
  prepare(0,localType_,local,controls,combat.localHealth);
  for (const auto& remote:remotes) if (remote.alive) prepare(remote.entity,remote.type,remote.state,remote.controls,remote.health);

  // Advance existing effects first, so newly arrived muzzle flashes survive
  // a long frame and shot/hit pairs retire the correct tracer immediately.
  const double effectDt=std::clamp(dt,0.0,.1);
  pool_.update(effectDt);
  // ---- Effects, driven by simulation combat events ----
  for (const CombatVisuals::Shot& shot : combat.shots)
    combat_.onShot(shot.position, shot.velocity, shot.lifetime, shot.ownAircraft,shot.projectile,shot.carrier);
  for (const CombatVisuals::Hit& hit : combat.hits)
    combat_.onHit(hit.position, hit.ownAircraft,hit.projectile,hit.targetVelocity);
  for (const CombatVisuals::Destruction& destruction : combat.destructions)
    combat_.onDestroyed(destruction.position, destruction.velocity);
  for(const auto& impact:combat.groundImpacts) combat_.onGroundImpact(impact);
  combat_.setEmissions(settings_.contrails, settings_.engineHeat);
  combat_.setCondensation(settings_.wingVapor, settings_.relativeHumidity);
  for (const auto &missile : combat.missiles)
    combat_.updateMissile(missile.id, missile.position, missile.velocity, missile.attitude, missile.length,
                          missile.diameter, missile.age, missile.powered, effectDt);
  combat_.retireMissiles(combat.missiles.size());
  for (const auto &position : combat.missileDetonations)
    combat_.onDetonation(position);
  // Wings and fins that have gone since the last frame leave as pieces.
  const auto shed=[&](std::uint64_t id) {
    const auto& instance=instances_.at(id);
    const std::size_t released=breakaways_.observe(id,instance.type,*instance.state,instance.damage,instance.deltas,instance.damageSeed);
    const auto& pieces=breakaways_.pieces();
    for (std::size_t i=pieces.size()-std::min(released,pieces.size());i<pieces.size();++i)
      combat_.onPartLost(pieces[i].position,pieces[i].velocity);
  };
  if (!combat.localDestroyed) shed(0);
  for (const RemoteAircraft& remote : remotes) if (remote.alive) shed(remote.entity);
  breakaways_.endFrame();
  breakaways_.update(effectDt,weather.wind_ned);
  for (BreakawayPiece& piece : breakaways_.pieces()) {
    // A wreck burns until it reaches the ground, and goes up when it does.
    if (piece.struck && piece.wreck()) combat_.onDestroyed(piece.position, {});
    if (!piece.grounded && (piece.wreck() || piece.age<7) && piece.smoke>=(piece.wreck() ? .035 : .06)) {
      piece.smoke=0;
      combat_.onPieceSmoke(piece.position,piece.velocity,piece.wreck());
    }
  }
  flameTime_+=effectDt;
  if (!combat.localDestroyed) combat_.updateAircraft(local, effectDt, localType_, 0, load,combat.localHealth,weather);
  for (const RemoteAircraft& remote : remotes)
    if (remote.alive) combat_.updateAircraft(remote.state, effectDt, remote.type, remote.entity, remote.load,remote.health,weather);
  stats_.particlePeak = std::max<std::size_t>(stats_.particlePeak, pool_.size());

  // ---- Camera and frame matrices ----
  const float aspect =
      static_cast<float>(width_) / static_cast<float>(std::max<std::uint32_t>(1, height_));
  const glm::vec3 eye = localPosition(camera.eye, origin_);
  const glm::vec3 target = localPosition(camera.target, origin_);
  // The camera's own up comes from its orientation, so the horizon rolls with
  // the airframe in first person and stays world-level in chase and orbit.
  // Render-space up is the projection of body -Z (see localPosition: {0,0,-1}
  // maps to render +Y, which is the convention the coordinate tests pin down).
  const glm::vec3 up = glm::normalize(renderDirection(camera.renderOrientation().rotate({0, 0, -1})));
  // A chase camera looking straight up or down would make lookAt degenerate.
  glm::vec3 forward = target - eye;
  if (glm::dot(forward, forward) < 1e-8f) forward = {0.0f, 0.0f, -1.0f};
  glm::vec3 upAxis = up;
  if (std::abs(glm::dot(glm::normalize(forward), glm::normalize(upAxis))) > 0.999f)
    upAxis = {0.0f, 0.0f, 1.0f};
  view_ = glm::lookAt(eye, target, upAxis);
  // Seen from the flight deck, the airframe is centimetres from the eye and
  // the horizon is a hundred kilometres away. No single depth buffer resolves
  // both, so the pilot's own aircraft is drawn afterwards in its own depth
  // range and the world keeps a near plane that leaves it usable precision.
  const bool cockpit = camera.hidesOwnAircraft();
  cockpitPass_ = cockpit && aircraftDefinition(localType_).visual.cockpitGeometry && !combat.localDestroyed;
  cameraNear_ = cockpit ? 1.5f : std::max(2.5f, settings_.nearPlane);
  cameraFovDeg_ = static_cast<float>(camera.fov);
  const float far = std::max(40000.0f, settings_.renderDistance) * 1.05f;
  projection_ = makeProjection(cameraFovDeg_, aspect, cameraNear_, far);
  viewProj_ = projection_ * view_;
  invViewProj_ = glm::inverse(viewProj_);
  cockpitViewProj_ = makeProjection(cameraFovDeg_, aspect, .05f, 150.f) * view_;
  cameraEye_ = eye;
  cameraForward_ = glm::normalize(forward);
  // Screen basis for billboards, taken from the view matrix rows.
  basisRight_ = glm::normalize(glm::vec3(view_[0][0], view_[1][0], view_[2][0]));
  basisUp_ = glm::normalize(glm::vec3(view_[0][1], view_[1][1], view_[2][1]));

  updateFrameConstants(camera, local, weather, dt);
  ensureSceneBuffers();
  computeCascades(camera, cockpit);
  bgfx::setUniform(uniforms_.invViewProj, glm::value_ptr(invViewProj_));
  drawAtmosphereTables();

  // ---- Shadow pass ----
  std::vector<const Instance*> casters;
  casters.reserve(9);
  if (!combat.localDestroyed && !aircraftCrashed(local)) casters.push_back(&instances_.at(0));
  for (const auto& remote:remotes) {
    if (casters.size()>=9) break;
    if (remote.alive && !aircraftCrashed(remote.state) && (remote.state.pos_ned-camera.eye).norm()<settings_.shadowDistance*1.5)
      casters.push_back(&instances_.at(remote.entity));
  }
  updateTreeChunks();
  drawShadowAtlas(casters);

  // ---- World ----
  bgfx::setViewName(kViewWorld, "World");
  bgfx::setViewRect(kViewWorld, 0, 0, width_, height_);
  bgfx::setViewFrameBuffer(kViewWorld, hdrBuffer_);
  bgfx::setViewClear(kViewWorld, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x000000ffu, 1.0f, 0);
  bgfx::setViewMode(kViewWorld, bgfx::ViewMode::Sequential);
  drawSky();
  drawEnvironment();
  if (settings_.showDebugGrid) drawGrid();
  if (!cockpit) drawAircraft(instances_.at(0), combat.localDestroyed, kViewWorld, viewProj_);
  for (const RemoteAircraft& remote : remotes)
    if (remote.alive && (remote.state.pos_ned-camera.eye).norm()<settings_.renderDistance)
      drawAircraft(instances_.at(remote.entity), false, kViewWorld, viewProj_);
  drawStores(combat, camera, false);
  drawBreakaways();

  // ---- Atmosphere: clouds, particles, plumes, rain ----
  drawClouds();
  compositeClouds();
  drawEffects(combat);
  drawAfterburners(combat.localDestroyed);
  drawMissilePlumes(combat);
  drawRain(local, weather);

  // ---- Flight deck ----
  if (cockpitPass_) {
    bgfx::setViewName(kViewCockpit, "Flight deck");
    bgfx::setViewRect(kViewCockpit, 0, 0, width_, height_);
    bgfx::setViewFrameBuffer(kViewCockpit, hdrBuffer_);
    bgfx::setViewClear(kViewCockpit, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
    bgfx::setViewMode(kViewCockpit, bgfx::ViewMode::Sequential);
    drawAircraft(instances_.at(0), false, kViewCockpit, cockpitViewProj_);
    drawStores(combat, camera, true);
  }
  compositeDisplay();
  bgfx::discard();
  stats_.preparationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-preparationStart).count();

  previousViewProj_ = viewProj_;
  previousOrigin_ = origin_;
  ++frameIndex_;

  // ---- Statistics ----
  const bgfx::Stats* bgfxStats = bgfx::getStats();
  if (bgfxStats->cpuTimerFreq > 0)
    stats_.cpuFrameMs =
        static_cast<double>(bgfxStats->cpuTimeFrame) / static_cast<double>(bgfxStats->cpuTimerFreq) * 1000.0;
  if (bgfxStats->gpuTimerFreq > 0 && bgfxStats->gpuTimeEnd > bgfxStats->gpuTimeBegin)
    stats_.gpuFrameMs = static_cast<double>(bgfxStats->gpuTimeEnd - bgfxStats->gpuTimeBegin) /
                        static_cast<double>(bgfxStats->gpuTimerFreq) * 1000.0;
  stats_.gpuMemory = static_cast<std::size_t>(bgfxStats->gpuMemoryUsed);
  fpsTimer_ += dt;
  fpsFrames_++;
  if (fpsTimer_ >= 0.5) {
    stats_.fps = fpsFrames_ / fpsTimer_;
    fpsTimer_ = 0;
    fpsFrames_ = 0;
  }
  stats_.shadowMapSize = cascadeCount_ > 0 ? shadowTileSize_ : 0;
  stats_.shadowCascades = static_cast<std::uint32_t>(cascadeCount_);
  stats_.cloudWidth = cloudWidth_;
  stats_.cloudHeight = cloudHeight_;
  stats_.lodTier = "LOD" + std::to_string(activeLod_);
}

}  // namespace ofs::client
