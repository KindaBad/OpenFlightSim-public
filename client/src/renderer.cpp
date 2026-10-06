#include "texture_cost.hpp"
#include "screenshot_pixels.hpp"
#include "renderer.hpp"
#include "scenery.hpp"

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

namespace ofs::client {

// ---------------------------------------------------------------------------
// bgfx callbacks
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint64_t kOpaqueState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
    BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CCW | BGFX_STATE_FRONT_CCW;

constexpr std::uint64_t kBlendState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL |
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

// Maps a requested sample count to the reset flag. bgfx exposes MSAA as a mask
// of exclusive bits rather than a count on the swap chain.
std::uint32_t msaaFlag(int samples) {
  switch (samples) {
    case 2: return BGFX_SWAP_CHAIN_MSAA_X2;
    case 4: return BGFX_SWAP_CHAIN_MSAA_X4;
    case 8: return BGFX_SWAP_CHAIN_MSAA_X8;
    default: return 16 <= samples ? BGFX_SWAP_CHAIN_MSAA_X16 : BGFX_SWAP_CHAIN_NONE;
  }
}

}  // namespace

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
// origin anchor is derived in docs/COORDINATES.md.
const glm::mat4 kAssetToBody = [] {
  glm::mat4 m(1.0f);
  m[0] = glm::vec4(-1, 0, 0, 0);
  m[1] = glm::vec4(0, 0, -1, 0);
  m[2] = glm::vec4(0, -1, 0, 0);
  // The rotation alone leaves the model in asset space, where the nose is at
  // X = 0 and the tail at X = 37.57. Applying it to the asset origin therefore
  // lands the model roughly 19.91 m ahead of the CG, which puts the body origin
  // inside the forward fuselage and hides the airframe. The CG anchor (see
  // coordinates.hpp for how it was measured) moves the model so the CG sits at
  // the body origin:
  //
  //   body.x = -(asset.x - cg.x)  =>  body.x = -asset.x + cg.x
  //   body.y = -(asset.z - cg.z)  =>  body.y = -asset.z + cg.z
  //   body.z = -(asset.y - cg.y)  =>  body.z = -asset.y + cg.y
  //
  // glm's column 3 is a plain translation applied after the rotation, so it is
  // the constant term of those three expressions. Note that render-space up is
  // body +Y, and body +Y comes from the model's lateral axis, which is why
  // cg.y lands the wheels on the ground plane via the body-Z axis instead.
  m[3] = glm::vec4(static_cast<float>(ModelAnchor::cgX), static_cast<float>(ModelAnchor::cgZ),
                   static_cast<float>(ModelAnchor::cgY), 1);
  return m;
}();

// Interleaved position(3) + colour(4) for the effect pool, which matches the
// effect vertex layout declared in Renderer.
inline constexpr std::size_t kEffectVertexFloats = 9;

// Position(3) + packed colour, used by the sky triangle and the developer grid.
struct UnlitVertex {
  float x, y, z;
  std::uint32_t color;
};

template <std::size_t N, std::size_t M>
bgfx::ProgramHandle makeProgram(const char* name, const std::uint8_t (&vs)[N], const std::uint8_t (&fs)[M]) {
  const std::string diagnostic = std::string(name) + " on " + bgfx::getRendererName(bgfx::getRendererType());
  const auto vertex = bgfx::createShader(bgfx::copy(vs, static_cast<std::uint32_t>(N)));
  const auto fragment = bgfx::createShader(bgfx::copy(fs, static_cast<std::uint32_t>(M)));
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

glm::mat4 makeOrtho(float l, float r, float b, float t, float n, float f) {
  return bgfx::getCaps()->homogeneousDepth ? glm::orthoRH_NO(l, r, b, t, n, f)
                                           : glm::orthoRH_ZO(l, r, b, t, n, f);
}

// Test airfield in render axes (+X east, +Y up, +Z south), metres. This is a
// landing field for takeoff and landing, not the Earth terrain milestone.
constexpr double kRunwayHalfWidth = 22.5;
constexpr double kRunwayLength = 2600.0;

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
// Construction
// ---------------------------------------------------------------------------

Renderer::Renderer(const Platform& platform, const GraphicsSettings& settings)
    : settings_(settings) {
  combat_.setQuality(settings_.effects);
  try {
    if (!initialize(platform)) throw std::runtime_error("bgfx initialization failed");
  } catch (...) {
    destroy();
    throw;
  }
}

Renderer::~Renderer() { destroy(); }

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
  swapChain_.flags = msaaFlag(settings_.msaaSamples);
  init.swapChain = swapChain_;
  init.fallback = false;
  init.callback = &callbacks_;
  init.reset = resetFlags() & ~BGFX_RESET_MSAA_MASK;
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
  unlitLayout_.begin()
      .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
      .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
      .end();

  switch (bgfx::getRendererType()) {
    case bgfx::RendererType::OpenGL:
      programs_.pbr = makeProgram("pbr", pbr_vs_glsl, pbr_fs_glsl);
      programs_.sky = makeProgram("sky", sky_vs_glsl, sky_fs_glsl);
      programs_.clouds = makeProgram("cloud", cloud_vs_glsl, cloud_fs_glsl);
      programs_.cloudComposite = makeProgram("cloud_composite", cloud_composite_vs_glsl, cloud_composite_fs_glsl);
      programs_.unlit = makeProgram("unlit", unlit_vs_glsl, unlit_fs_glsl);
      programs_.effect = makeProgram("effect", effect_vs_glsl, effect_fs_glsl);
      programs_.shadow = makeProgram("shadow", shadow_vs_glsl, shadow_fs_glsl);
      programs_.flame = makeProgram("flame", flame_vs_glsl, flame_fs_glsl);
      programs_.post = makeProgram("post", post_vs_glsl, post_fs_glsl);
      programs_.bloom = makeProgram("bloom", bloom_vs_glsl, bloom_fs_glsl);
      programs_.imgui = makeProgram("imgui", vs_ocornut_imgui_glsl, fs_ocornut_imgui_glsl);
      break;
#ifdef _WIN32
    case bgfx::RendererType::Direct3D11:
      programs_.pbr = makeProgram("pbr", pbr_vs_dx11, pbr_fs_dx11);
      programs_.sky = makeProgram("sky", sky_vs_dx11, sky_fs_dx11);
      programs_.clouds = makeProgram("cloud", cloud_vs_dx11, cloud_fs_dx11);
      programs_.cloudComposite = makeProgram("cloud_composite", cloud_composite_vs_dx11, cloud_composite_fs_dx11);
      programs_.unlit = makeProgram("unlit", unlit_vs_dx11, unlit_fs_dx11);
      programs_.effect = makeProgram("effect", effect_vs_dx11, effect_fs_dx11);
      programs_.shadow = makeProgram("shadow", shadow_vs_dx11, shadow_fs_dx11);
      programs_.flame = makeProgram("flame", flame_vs_dx11, flame_fs_dx11);
      programs_.post = makeProgram("post", post_vs_dx11, post_fs_dx11);
      programs_.bloom = makeProgram("bloom", bloom_vs_dx11, bloom_fs_dx11);
      programs_.imgui = makeProgram("imgui", vs_ocornut_imgui_dxbc, fs_ocornut_imgui_dxbc);
      break;
#endif
    default:
      throw std::runtime_error(std::string("No compiled shader set for active backend: ") + backend());
  }
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

  const auto vec4 = bgfx::UniformType::Vec4;
  const auto mat4 = bgfx::UniformType::Mat4;
  uniforms_.model = bgfx::createUniform("u_ofsModel", mat4);
  uniforms_.viewProj = bgfx::createUniform("u_ofsViewProj", mat4);
  uniforms_.invViewProj = bgfx::createUniform("u_ofsInvViewProj", mat4);
  uniforms_.normalMatrix = bgfx::createUniform("u_normalMatrix", bgfx::UniformType::Mat3);
  uniforms_.cameraPos = bgfx::createUniform("u_cameraPos", vec4);
  uniforms_.worldOrigin = bgfx::createUniform("u_worldOrigin", vec4);
  uniforms_.sunDirection = bgfx::createUniform("u_sunDirection", vec4);
  uniforms_.sunColor = bgfx::createUniform("u_sunColor", vec4);
  uniforms_.skyAmbient = bgfx::createUniform("u_skyAmbient", vec4);
  uniforms_.groundAmbient = bgfx::createUniform("u_groundAmbient", vec4);
  uniforms_.baseColor = bgfx::createUniform("u_baseColor", vec4);
  uniforms_.metallicRoughness = bgfx::createUniform("u_metallicRoughness", vec4);
  uniforms_.emissive = bgfx::createUniform("u_emissive", vec4);
  uniforms_.doubleSided = bgfx::createUniform("u_doubleSided", vec4);
  uniforms_.shadowMatrix = bgfx::createUniform("u_shadowMatrix", mat4);
  uniforms_.shadowMap = bgfx::createUniform("u_shadowMap", bgfx::UniformType::Sampler);
  uniforms_.shadowTexel = bgfx::createUniform("u_shadowTexel", vec4);
  uniforms_.shadowBias = bgfx::createUniform("u_shadowBias", vec4);
  uniforms_.shadowStrength = bgfx::createUniform("u_shadowStrength", vec4);
  uniforms_.fogColor = bgfx::createUniform("u_fogColor", vec4);
  uniforms_.fogDensity = bgfx::createUniform("u_fogDensity", vec4);
  uniforms_.fogHeightFalloff = bgfx::createUniform("u_fogHeightFalloff", vec4);
  uniforms_.fogGroundFade = bgfx::createUniform("u_fogGroundFade", vec4);
  uniforms_.fogEnabled = bgfx::createUniform("u_fogEnabled", vec4);
  uniforms_.exposure = bgfx::createUniform("u_exposure", vec4);
  uniforms_.zenithColor = bgfx::createUniform("u_zenithColor", vec4);
  uniforms_.horizonColor = bgfx::createUniform("u_horizonColor", vec4);
  uniforms_.groundColor = bgfx::createUniform("u_groundColor", vec4);
  uniforms_.sunIntensity = bgfx::createUniform("u_sunIntensity", vec4);
  uniforms_.horizonSharpness = bgfx::createUniform("u_horizonSharpness", vec4);
  uniforms_.groundBlend = bgfx::createUniform("u_groundBlend", vec4);
  uiSampler_ = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
  uniforms_.baseTexture = bgfx::createUniform("s_baseColor", bgfx::UniformType::Sampler);
  uniforms_.mrTexture = bgfx::createUniform("s_metallicRoughness", bgfx::UniformType::Sampler);
  uniforms_.emissiveTexture = bgfx::createUniform("s_emissive", bgfx::UniformType::Sampler);
  uniforms_.normalTexture = bgfx::createUniform("s_normal", bgfx::UniformType::Sampler);
  uniforms_.occlusionTexture = bgfx::createUniform("s_occlusion", bgfx::UniformType::Sampler);
  uniforms_.normalSettings = bgfx::createUniform("u_normalSettings", vec4);
  uniforms_.flame = bgfx::createUniform("u_flame", vec4);
  uniforms_.sceneTexture=bgfx::createUniform("s_scene",bgfx::UniformType::Sampler);
  uniforms_.bloomTexture=bgfx::createUniform("s_bloom",bgfx::UniformType::Sampler);
  uniforms_.postSettings=bgfx::createUniform("u_postSettings",vec4);
  uniforms_.postStep=bgfx::createUniform("u_postStep",vec4);
  uniforms_.cloudParams=bgfx::createUniform("u_cloudParams",vec4);
  uniforms_.weather=bgfx::createUniform("u_weather",vec4);
  uniforms_.cloudRender=bgfx::createUniform("u_cloudRender",vec4);
  uniforms_.cloudNoise=bgfx::createUniform("s_cloudNoise",bgfx::UniformType::Sampler);
  uniforms_.cloudLayer=bgfx::createUniform("s_cloudLayer",bgfx::UniformType::Sampler);
  uniforms_.sceneDepth=bgfx::createUniform("s_sceneDepth",bgfx::UniformType::Sampler);
  std::vector<std::uint8_t> noise(64*64*64);
  for (unsigned i=0; i<noise.size(); ++i) {
    std::uint32_t h=i+0x9e3779b9u; h^=h>>16; h*=0x7feb352du; h^=h>>15; h*=0x846ca68bu; h^=h>>16;
    noise[i]=static_cast<std::uint8_t>(h);
  }
  cloudNoise_=bgfx::createTexture3D(64,64,64,false,bgfx::TextureFormat::R8,0,
      bgfx::copy(noise.data(),static_cast<std::uint32_t>(noise.size())));
  if (!bgfx::isValid(cloudNoise_)) throw std::runtime_error("Cloud density upload failed");
  uniforms_.textureFlags = bgfx::createUniform("u_textureFlags", vec4);
  uniforms_.alphaSettings = bgfx::createUniform("u_alphaSettings", vec4);
  const std::uint32_t white = 0xffffffff;
  whiteTexture_ = bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(&white,4));

  unsigned char* pixels = nullptr;
  int fw = 0, fh = 0;
  ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &fw, &fh);
  font_ = bgfx::createTexture2D(static_cast<std::uint16_t>(fw), static_cast<std::uint16_t>(fh), false, 1,
                                bgfx::TextureFormat::RGBA8, 0,
                                bgfx::copy(pixels, static_cast<std::uint32_t>(fw * fh * 4)));
  if (!bgfx::isValid(font_) || !bgfx::isValid(uiSampler_))
    throw std::runtime_error("GPU resource creation failed");
  auxiliaryTextureBytes_=noise.size()+4+std::uint64_t(fw)*fh*4;
  log("TEXTURE", "builtin cloud-noise source=64x64x64 format=R8 mips=1 estimated_gpu_bytes="+std::to_string(noise.size()));
  log("TEXTURE", "builtin white source=1x1 format=RGBA8 mips=1 estimated_gpu_bytes=4");
  log("TEXTURE", "builtin font source="+std::to_string(fw)+"x"+std::to_string(fh)+" format=RGBA8 mips=1 estimated_gpu_bytes="+std::to_string(std::uint64_t(fw)*fh*4));
  ImGui::GetIO().Fonts->SetTexID(static_cast<ImTextureID>(font_.idx) + 1);
  ImGui::GetIO().BackendRendererName = "OpenFlightSim_bgfx";
  ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;

  buildEnvironment();

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

std::uint32_t Renderer::resetFlags() const {
  std::uint32_t reset = msaaFlag(settings_.msaaSamples);
  if (settings_.vsync) reset |= BGFX_RESET_VSYNC;
  return reset;
}

void Renderer::destroy() {
  if (!initialized_) return;
  destroyEnvironment();
  if(bgfx::isValid(cloudBuffer_)) bgfx::destroy(cloudBuffer_);
  if(bgfx::isValid(cloudNoise_)) bgfx::destroy(cloudNoise_);
  if(bgfx::isValid(atmosphereBuffer_))bgfx::destroy(atmosphereBuffer_);
  if(bgfx::isValid(hdrBuffer_))bgfx::destroy(hdrBuffer_);
  for(auto handle:bloomBuffer_)if(bgfx::isValid(handle))bgfx::destroy(handle);
  if (bgfx::isValid(shadowBuffer_)) bgfx::destroy(shadowBuffer_);

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
  for (const auto handle : {uniforms_.model,uniforms_.viewProj,uniforms_.invViewProj,uniforms_.normalMatrix,
       uniforms_.cameraPos,uniforms_.worldOrigin,uniforms_.sunDirection,uniforms_.sunColor,uniforms_.skyAmbient,
       uniforms_.groundAmbient,uniforms_.baseColor,uniforms_.metallicRoughness,uniforms_.emissive,uniforms_.doubleSided,
       uniforms_.shadowMatrix,uniforms_.shadowMap,uniforms_.shadowTexel,uniforms_.shadowBias,uniforms_.shadowStrength,
       uniforms_.fogColor,uniforms_.fogDensity,uniforms_.fogHeightFalloff,uniforms_.fogGroundFade,uniforms_.fogEnabled,
       uniforms_.exposure,uniforms_.zenithColor,uniforms_.horizonColor,uniforms_.groundColor,uniforms_.sunIntensity,
       uniforms_.horizonSharpness,uniforms_.groundBlend,uniforms_.baseTexture,uniforms_.mrTexture,
       uniforms_.emissiveTexture,uniforms_.normalTexture,uniforms_.occlusionTexture,uniforms_.normalSettings,uniforms_.flame,uniforms_.sceneTexture,uniforms_.bloomTexture,uniforms_.postSettings,uniforms_.postStep,uniforms_.cloudParams,uniforms_.weather,uniforms_.cloudRender,uniforms_.cloudNoise,uniforms_.cloudLayer,uniforms_.sceneDepth,uniforms_.textureFlags,uniforms_.alphaSettings,uiSampler_})
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
  if (bgfx::isValid(font_)) bgfx::destroy(font_);
  if (bgfx::isValid(programs_.pbr)) bgfx::destroy(programs_.pbr);
  if (bgfx::isValid(programs_.sky)) bgfx::destroy(programs_.sky);
  if (bgfx::isValid(programs_.clouds)) bgfx::destroy(programs_.clouds);
  if (bgfx::isValid(programs_.cloudComposite)) bgfx::destroy(programs_.cloudComposite);
  if (bgfx::isValid(programs_.unlit)) bgfx::destroy(programs_.unlit);
  if (bgfx::isValid(programs_.effect)) bgfx::destroy(programs_.effect);
  if (bgfx::isValid(programs_.imgui)) bgfx::destroy(programs_.imgui);
  if (bgfx::isValid(programs_.shadow)) bgfx::destroy(programs_.shadow);
  if (bgfx::isValid(programs_.post))bgfx::destroy(programs_.post);
  if (bgfx::isValid(programs_.bloom))bgfx::destroy(programs_.bloom);
  if (bgfx::isValid(programs_.flame)) bgfx::destroy(programs_.flame);
  if (bgfx::isValid(flameMesh_)) bgfx::destroy(flameMesh_);
  bgfx::shutdown();
  initialized_ = false;
  log("RENDER", "bgfx shutdown complete");
}

// ---------------------------------------------------------------------------
// Environment geometry
// ---------------------------------------------------------------------------

void Renderer::buildEnvironment() {
  std::vector<SurfaceVertex> vertices;

  // Ground: rings with quadratic spacing, so detail concentrates around the
  // airfield while distant rings cover the horizon in few triangles.
  for (int ring = 0; ring < kTerrainRings; ++ring) {
    for (int segment = 0; segment < kTerrainSegments; ++segment) {
      const glm::vec3 a = renderDirection(terrainVertex(ring,segment));
      const glm::vec3 b = renderDirection(terrainVertex(ring,segment+1));
      const glm::vec3 c = renderDirection(terrainVertex(ring+1,segment+1));
      const glm::vec3 d = renderDirection(terrainVertex(ring+1,segment));
      const std::size_t start = vertices.size();
      addQuad(vertices,a,b,c,d,{0,1,0});
      // Smooth shading normals; the collision surface uses the exact faces.
      for (std::size_t i=start;i<vertices.size();++i) {
        auto& v=vertices[i];
        const double dn=(terrainElevation(-v.z+4,v.x)-terrainElevation(-v.z-4,v.x))/8;
        const double de=(terrainElevation(-v.z,v.x+4)-terrainElevation(-v.z,v.x-4))/8;
        const auto n=glm::normalize(glm::vec3(-de,1,dn));
        v.nx=n.x;v.ny=n.y;v.nz=n.z;
      }
    }
  }
  ground_ = bgfx::createVertexBuffer(
      bgfx::copy(vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(SurfaceVertex))),
      surfaceLayout_);

  // Runway: an asphalt surface plus painted markings, both static. The two are
  // separate buffers because they use different materials.
  vertices.clear();
  // The runway surface, its paint and the grass are all coplanar at y = 0 apart
  // from these offsets. The gaps have to be large enough to survive the depth
  // buffer at the far end of the 15000 m view distance: centimetres apart
  // z-fight into wide shimmering bands once the runway is more than a few
  // hundred metres away.
  constexpr float kRunwayLift = 0.05f;
  const float asphalt = kRunwayLift;
  // Counter-clockwise seen from above; see addQuad.
  addQuad(vertices,
          {static_cast<float>(-kRunwayHalfWidth), asphalt, static_cast<float>(-kRunwayLength / 2)},
          {static_cast<float>(-kRunwayHalfWidth), asphalt, static_cast<float>(kRunwayLength / 2)},
          {static_cast<float>(kRunwayHalfWidth), asphalt, static_cast<float>(kRunwayLength / 2)},
          {static_cast<float>(kRunwayHalfWidth), asphalt, static_cast<float>(-kRunwayLength / 2)},
          {0, 1, 0});
  markings_ = bgfx::createVertexBuffer(
      bgfx::copy(vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(SurfaceVertex))),
      surfaceLayout_);

  vertices.clear();
  const float paint = kRunwayLift + 0.05f;
  // Every quad below is listed counter-clockwise seen from above, matching
  // addQuad's contract. Listing them the other way round gives the triangles a
  // -Y geometric normal, which back-face culling then removes: the markings
  // vanish and leave only the grass and asphalt underneath, which reads as
  // broad green bands across the runway.
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
  paintBuffer_ = bgfx::createVertexBuffer(
      bgfx::copy(vertices.data(), static_cast<std::uint32_t>(vertices.size() * sizeof(SurfaceVertex))),
      surfaceLayout_);

  // Airfield scenery. All surfaces share the same origin transform as the runway.
  const auto upload = [&](const std::vector<SurfaceVertex>& data) {
    return bgfx::createVertexBuffer(bgfx::copy(data.data(),
        static_cast<std::uint32_t>(data.size() * sizeof(SurfaceVertex))), surfaceLayout_);
  };
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

  // Service road, fence posts and utility sheds give the field a human scale.
  vertices.clear();
  slab(455,-2800,469,2800,.065f);
  slab(-650,300,462,313,.065f);
  roadside_=upload(vertices);
  vertices.clear();
  for (int i=-44;i<=44;++i) {
    box(430,float(i*60),.18f,.18f,0,2.2f);
    box(-480,float(i*60),.18f,.18f,0,2.2f);
  }
  for (int i=0;i<3;++i) box(-535,float(i*45+450),22,30,0,5);
  // Append utility structures to their own scenery patch, batched once.
  SceneryPatch utilities; utilities.center={-400,4,200};utilities.radius=3100;
  utilities.wood=upload(vertices);utilities.woodCount=vertices.size();
  scenery_.push_back(utilities);

  const auto random=[](unsigned seed) {
    seed^=seed>>16;seed*=0x7feb352du;seed^=seed>>15;seed*=0x846ca68bu;seed^=seed>>16;
    return float(seed&0xffffff)/float(0xffffff);
  };
  // Closed irregular stone silhouettes, shared by scattered roadside rocks.
  const auto crown=[](std::vector<SurfaceVertex>& out,glm::vec3 center,float width,float height,int sides,bool pine,bool distant) {
    const int tiers=distant?1:3;
    for (int tier=0;tier<tiers;++tier) {
      const float y0=pine?float(tier)*height*.20f:float(tier)*height/tiers;
      const float y1=pine?height*(.65f+float(tier)*.175f):float(tier+1)*height/tiers;
      const float r0=pine?width*(1.f-float(tier)*.22f):width*(tier==0?.45f:1.f);
      const float r1=pine?0.f:width*(tier==tiers-1?.08f:1.f);
      for (int side=0;side<sides;++side) {
        const float a0=float(side)*float(2*kPi)/sides,a1=float(side+1)*float(2*kPi)/sides;
        const glm::vec3 a=center+glm::vec3(std::cos(a0)*r0,y0,std::sin(a0)*r0);
        const glm::vec3 b=center+glm::vec3(std::cos(a1)*r0,y0,std::sin(a1)*r0);
        const glm::vec3 c=center+glm::vec3(std::cos(a1)*r1,y1,std::sin(a1)*r1);
        const glm::vec3 d=center+glm::vec3(std::cos(a0)*r1,y1,std::sin(a0)*r1);
        const glm::vec3 normal=glm::normalize(glm::vec3(std::cos((a0+a1)*.5f),width/height,std::sin((a0+a1)*.5f)));
        addQuad(out,a,b,c,d,normal);
      }
    }
    // Close the underside so crowns also cast shadows viewed from below.
    for(int side=0;side<sides;++side) {
      const float a0=float(side)*float(2*kPi)/sides,a1=float(side+1)*float(2*kPi)/sides;
      const float r=pine?width:width*.45f;
      const auto a=center+glm::vec3(std::cos(a0)*r,0,std::sin(a0)*r);
      const auto b=center+glm::vec3(std::cos(a1)*r,0,std::sin(a1)*r);
      addQuad(out,center,a,b,center,{0,-1,0});
    }
  };
  unsigned seed=0;
  for(int iz=-4;iz<=4;++iz) for(int ix=-4;ix<=4;++ix) {
    ++seed;
    const float cx=ix*1750.f+(random(seed*53)-.5f)*400;
    const float cz=iz*1750.f+(random(seed*71)-.5f)*400;
    if(std::hypot(cx,cz)>9000) continue;
    SceneryPatch patch;patch.center={cx,float(-groundHeightNed(-cz,cx)),cz};patch.radius=1280;
    std::vector<SurfaceVertex> foliage,distant,wood,rocks;
    for(unsigned tree=0;tree<340;++tree) {
      const unsigned key=seed*1301+tree*17;
      const float x=cx+(random(key)-.5f)*1700,z=cz+(random(key+1)-.5f)*1700;
      // Keep the runway, taxiways, buildings and service roads unobstructed.
      if ((std::abs(x)<115 && std::abs(z)<1850) ||
          (x>-610 && x<-40 && z>-1450 && z<650) ||
          (x>420 && x<490 && std::abs(z)<2850) ||
          (x>-680 && x<500 && z>260 && z<340)) continue;
      const float woodland=sceneryNoise(x*.0011,z*.0011)*.7f+sceneryNoise(x*.0031+17,z*.0031)*.3f;
      if(random(key+8) > std::clamp((woodland-.32f)*2.7f,.03f,.92f)) continue;
      const float y=float(-groundHeightNed(-z,x));
      const float height=8.f+random(key+2)*13.f,width=height*(.20f+random(key+3)*.10f);
      const bool pine=random(key+4)>.46f;
      appendTree(foliage,distant,wood,{x,y,z},height,width,pine,key);
      if(tree%9==0) {
        const glm::vec3 stone{x+width*2,y,z-width};
        crown(rocks,stone,1.f+random(key+5)*2.5f,1.f+random(key+6)*2.f,5,false,false);
      }
    }
    if ((ix+iz)%3==0 && std::hypot(cx,cz)>2800) {
      for (int house=0;house<5;++house) {
        const float x=cx+house*33.f-65.f,z=cz+610.f;
        const float y=float(-groundHeightNed(-z,x));
        vertices.clear();box(x,z,18,26,y,6);
        const glm::vec3 a{x-10,y+6,z-14},b{x-10,y+6,z+14};
        const glm::vec3 c{x,y+11,z+14},d{x,y+11,z-14};
        addQuad(vertices,a,b,c,d,glm::normalize(glm::vec3(-.5f,1,0)));
        addQuad(vertices,d,c,{x+10,y+6,z+14},{x+10,y+6,z-14},glm::normalize(glm::vec3(.5f,1,0)));
        addQuad(vertices,a,d,{x+10,y+6,z-14},a,{0,0,-1});
        addQuad(vertices,{x+10,y+6,z+14},c,b,b,{0,0,1});
        rocks.insert(rocks.end(),vertices.begin(),vertices.end());
      }
    }
    const auto optionalUpload=[&](const auto& data){return data.empty()?bgfx::VertexBufferHandle{bgfx::kInvalidHandle}:upload(data);};
    patch.foliage=optionalUpload(foliage);patch.distant=optionalUpload(distant);
    patch.wood=optionalUpload(wood);patch.rocks=optionalUpload(rocks);
    patch.foliageCount=foliage.size();patch.distantCount=distant.size();
    patch.woodCount=wood.size();patch.rockCount=rocks.size();
    patch.radius+=250; // elevation spread and tree height in sloped patches
    scenery_.push_back(patch);
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

  const UnlitVertex sky[3] = {{-1, -1, 0, 0xffffffff}, {3, -1, 0, 0xffffffff}, {-1, 3, 0, 0xffffffff}};
  skyTriangle_ = bgfx::createVertexBuffer(
      bgfx::copy(sky, static_cast<std::uint32_t>(sizeof(sky))), unlitLayout_);

  if (!bgfx::isValid(ground_) || !bgfx::isValid(markings_) || !bgfx::isValid(paintBuffer_) ||
      !bgfx::isValid(grid_) || !bgfx::isValid(skyTriangle_) || !bgfx::isValid(apron_) ||
      !bgfx::isValid(taxiPaint_) || !bgfx::isValid(buildings_) || !bgfx::isValid(windows_) ||
      !bgfx::isValid(lights_) || !bgfx::isValid(roadside_))
    throw std::runtime_error("Environment GPU upload failed");
}

void Renderer::destroyEnvironment() {
  if (bgfx::isValid(ground_)) bgfx::destroy(ground_);
  if (bgfx::isValid(markings_)) bgfx::destroy(markings_);
  if (bgfx::isValid(paintBuffer_)) bgfx::destroy(paintBuffer_);
  if (bgfx::isValid(grid_)) bgfx::destroy(grid_);
  if (bgfx::isValid(skyTriangle_)) bgfx::destroy(skyTriangle_);
  for (auto& patch : scenery_)
    for(auto handle:{patch.foliage,patch.distant,patch.wood,patch.rocks})
      if(bgfx::isValid(handle)) bgfx::destroy(handle);
  scenery_.clear();
  if(bgfx::isValid(roadside_)) bgfx::destroy(roadside_);
  roadside_=BGFX_INVALID_HANDLE;
  for (const auto handle : {apron_, taxiPaint_, buildings_, windows_, lights_})
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
  apron_ = taxiPaint_ = buildings_ = windows_ = lights_ = BGFX_INVALID_HANDLE;
  ground_ = markings_ = paintBuffer_ = grid_ = skyTriangle_ = BGFX_INVALID_HANDLE;
}

// ---------------------------------------------------------------------------
// Aircraft asset
// ---------------------------------------------------------------------------

bool Renderer::loadAircraft(const std::string& path, AircraftType type) {
  Model& asset = model(type);
  if (asset.loaded) return true;
  Mesh mesh;
  GpuMesh gpu;
  try {
    mesh = loadGltf(path);
    // Cell sizes tuned against the measured A320 primitive histogram: 0.15 m
    // keeps the silhouette within 0.02 m, 0.45 m is a distant-aircraft proxy.
    const float cells[kLodCount - 1] = {0.15f, 0.45f};
    const auto& lodAssets=aircraftDefinition(type).lodAssets;
    gpu = buildGpuMesh(mesh, cells, lodAssets[0].empty() ? kLodCount : 1);
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
        if (built.levels[0].triangleCount>=gpu.levels.back().triangleCount)
          throw std::runtime_error("authored LOD chain must strictly reduce triangles");
        gpu.levels.push_back(std::move(built.levels[0]));
      }
    }
  } catch (const std::exception& error) {
    log("ASSET", std::string("Failed to load aircraft model: ") + error.what());
    asset.loaded = false;
    return false;
  }
  asset.report = mesh.report;
  asset.name = std::string(aircraftDefinition(type).displayName);
  asset.materials = mesh.materials;
  asset.nodes = std::move(mesh.nodes);
  asset.mesh = std::move(gpu);

  std::map<std::tuple<int, std::uint64_t, TextureRole, bool>, bgfx::TextureHandle> cache;
  for (const auto& texture : mesh.textures) {
    bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
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
      if(settings_.anisotropic && mipmaps && !(flags&BGFX_SAMPLER_MIN_POINT)) flags|=BGFX_SAMPLER_MIN_ANISOTROPIC;
      if(texture.minFilter==9984 || texture.minFilter==9985) flags|=BGFX_SAMPLER_MIP_POINT;
      const auto key = std::tuple{texture.image,flags,role,mipmaps};
      if (cache.contains(key)) handle=cache.at(key);
      else if (!image.rgba.empty()) {
        unsigned uploadWidth=image.width,uploadHeight=image.height;
        const unsigned maximum=std::min<unsigned>(unsigned(std::clamp(settings_.textureMaxSize,512,8192)),bgfx::getCaps()->limits.maxTextureSize);
        auto pixels=(mipmaps || uploadWidth>maximum || uploadHeight>maximum)?textureMipChain(image.rgba,uploadWidth,uploadHeight,role):image.rgba;
        std::size_t skip=0;
        while(uploadWidth>maximum || uploadHeight>maximum) {
          skip+=std::size_t(uploadWidth)*uploadHeight*4;
          uploadWidth=std::max(1u,uploadWidth/2);uploadHeight=std::max(1u,uploadHeight/2);
        }
        if(skip)pixels.erase(pixels.begin(),pixels.begin()+skip);
        if(!mipmaps)pixels.resize(std::size_t(uploadWidth)*uploadHeight*4);
        handle=bgfx::createTexture2D(static_cast<std::uint16_t>(uploadWidth),static_cast<std::uint16_t>(uploadHeight),
            mipmaps,1,bgfx::TextureFormat::RGBA8,flags,
            bgfx::copy(pixels.data(),static_cast<std::uint32_t>(pixels.size())));
        if(bgfx::isValid(handle)) {
          asset.textureBytes+=pixels.size();
          const auto cost=textureAllocation(image.width,image.height,mipmaps,maximum);
          log("TEXTURE",path+" | "+image.source+" source="+std::to_string(image.width)+"x"+std::to_string(image.height)+
            " upload="+std::to_string(cost.width)+"x"+std::to_string(cost.height)+" format=RGBA8 mips="+std::to_string(cost.mips)+
            " estimated_gpu_bytes="+std::to_string(cost.bytes));
        }
        cache.emplace(key,handle);
      }
    }
    asset.textures.push_back(handle);
  }

  {
    std::uint64_t aircraftBytes=0;
    for(const auto& d:aircraftDefinitions())aircraftBytes+=model(d.type).textureBytes;
    log("TEXTURE", "aircraft_texture_gpu_bytes="+std::to_string(aircraftBytes)+
      " total_texture_gpu_bytes="+std::to_string(aircraftBytes+auxiliaryTextureBytes_)+
      " (includes white/font/cloud-noise; excludes framebuffer attachments/driver allocation)");
    const auto costs=aircraftTextureCosts(mesh,std::min<unsigned>(std::clamp(settings_.textureMaxSize,512,8192),bgfx::getCaps()->limits.maxTextureSize));
    auto sorted=costs;std::sort(sorted.begin(),sorted.end(),[](const auto& a,const auto& b){return a.allocation.bytes>b.allocation.bytes;});
    for(std::size_t i=0;i<std::min(std::size_t(5),sorted.size());++i)
      log("TEXTURE", "largest "+path+" | "+sorted[i].source+" bytes="+std::to_string(sorted[i].allocation.bytes));
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
  swapChain_.flags = msaaFlag(settings_.msaaSamples);
  bgfx::reset(reset & ~BGFX_RESET_MSAA_MASK, &swapChain_);
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
    bgfx::reset(resetFlags() & ~BGFX_RESET_MSAA_MASK, &swapChain_);
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
// Frame passes
// ---------------------------------------------------------------------------

void Renderer::setFrameUniforms(const Camera& camera) {
  setCloudUniforms();
  cameraEye_ = localPosition(camera.eye, origin_);
  bgfx::setUniform(uniforms_.worldOrigin, glm::value_ptr(glm::vec4(renderDirection(origin_),0)));
  bgfx::setUniform(uniforms_.cameraPos, glm::value_ptr(glm::vec4(cameraEye_, 0.0f)));

  float sunDirection[3];
  ofs::client::sunDirection(settings_.sky, sunDirection);
  const glm::vec3 sun(sunDirection[0], sunDirection[1], sunDirection[2]);
  bgfx::setUniform(uniforms_.sunDirection, glm::value_ptr(glm::vec4(sun, 0.0f)));

  const float daylight=std::clamp((settings_.sky.sunElevationDeg+4.f)/19.f,.0f,1.f);
  const float warmth=std::clamp((settings_.sky.sunElevationDeg-2.f)/24.f,0.f,1.f);
  const float intensity = settings_.sky.sunIntensity*daylight;
  bgfx::setUniform(uniforms_.sunColor,
                   glm::value_ptr(glm::vec4(intensity, intensity * (.56f+.41f*warmth), intensity * (.30f+.61f*warmth), 0.0f)));
  bgfx::setUniform(uniforms_.skyAmbient,
                   glm::value_ptr(glm::vec4(settings_.sky.skyAmbientR*(.08f+.92f*daylight), settings_.sky.skyAmbientG*(.08f+.92f*daylight),
                                             settings_.sky.skyAmbientB*(.08f+.92f*daylight), 0.0f)));
  bgfx::setUniform(uniforms_.groundAmbient,
                   glm::value_ptr(glm::vec4(settings_.sky.groundAmbientR*(.08f+.92f*daylight), settings_.sky.groundAmbientG*(.08f+.92f*daylight),
                                             settings_.sky.groundAmbientB*(.08f+.92f*daylight), 0.0f)));
  bgfx::setUniform(uniforms_.fogColor,
                   glm::value_ptr(glm::vec4(settings_.fog.colorR, settings_.fog.colorG,
                                             settings_.fog.colorB, 0.0f)));
  bgfx::setUniform(uniforms_.fogDensity,
                   glm::value_ptr(glm::vec4(settings_.fog.enabled ? settings_.fog.density : 0.0f, 0, 0, 0)));
  bgfx::setUniform(uniforms_.fogHeightFalloff,
                   glm::value_ptr(glm::vec4(settings_.fog.heightFalloff, 0, 0, 0)));
  bgfx::setUniform(uniforms_.fogGroundFade,
                   glm::value_ptr(glm::vec4(settings_.fog.groundFade, 0, 0, 0)));
  bgfx::setUniform(uniforms_.fogEnabled,
                   glm::value_ptr(glm::vec4(settings_.fog.enabled ? 1.0f : 0.0f, 0, 0, 0)));
  bgfx::setUniform(uniforms_.exposure, glm::value_ptr(glm::vec4(settings_.sky.exposure, 0, 0, 0)));
  // The surface shaders read u_ofsViewProj themselves instead of using bgfx's
  // built-in view and projection constants, so it has to be set every frame.
  // Leaving it unset makes the vertex stage collapse every vertex to the
  // origin, which hides the whole world while the sky (which reconstructs its
  // rays from an inverse matrix) keeps drawing, so the failure looks like a
  // missing terrain rather than a missing uniform.
  bgfx::setUniform(uniforms_.viewProj, glm::value_ptr(viewProj_));
  bgfx::setUniform(uniforms_.shadowStrength,
                   glm::value_ptr(glm::vec4(shadowMapValid_ ? settings_.shadowStrength : 0.0f, 0, 0, 0)));
  bgfx::setTexture(0, uniforms_.shadowMap, shadowMap_);
  bgfx::setUniform(uniforms_.shadowMatrix, glm::value_ptr(shadowMatrix_));
  if (shadowMapSize_ > 0)
    bgfx::setUniform(uniforms_.shadowTexel,
                     glm::value_ptr(glm::vec4(1.0f / static_cast<float>(shadowMapSize_), 0, 0, 0)));
  bgfx::setUniform(uniforms_.shadowBias, glm::value_ptr(glm::vec4(settings_.shadowBias, 0, 0, 0)));

  // Screen basis for billboards, taken from the view-projection rows.
  const glm::mat3 view = glm::mat3(viewProj_);
  basisRight_ = glm::normalize(glm::vec3(view[0][0], view[1][0], view[2][0]));
  basisUp_ = glm::normalize(glm::vec3(view[0][1], view[1][1], view[2][1]));
}

void Renderer::setSkyUniforms() {
  // Shared uniforms are submitted once by setFrameUniforms; bgfx debug
  // rejects setting the same uniform twice before a draw submission.
  setFrameUniforms(lastCamera_);
  bgfx::setUniform(uniforms_.postSettings,glm::value_ptr(glm::vec4(0,0,bgfx::getCaps()->originBottomLeft?1.f:0.f,0)));

  bgfx::setUniform(uniforms_.invViewProj, glm::value_ptr(invViewProj_));
  bgfx::setUniform(uniforms_.zenithColor,
                   glm::value_ptr(glm::vec4(settings_.sky.zenithR, settings_.sky.zenithG,
                                             settings_.sky.zenithB, 0)));
  bgfx::setUniform(uniforms_.horizonColor,
                   glm::value_ptr(glm::vec4(settings_.sky.horizonR, settings_.sky.horizonG,
                                             settings_.sky.horizonB, 0)));
  bgfx::setUniform(uniforms_.groundColor,
                   glm::value_ptr(glm::vec4(settings_.sky.groundR, settings_.sky.groundG,
                                             settings_.sky.groundB, 0)));
  bgfx::setUniform(uniforms_.sunIntensity,
                   glm::value_ptr(glm::vec4(settings_.sky.sunIntensity, 0, 0, 0)));
  bgfx::setUniform(uniforms_.horizonSharpness,
                   glm::value_ptr(glm::vec4(settings_.sky.horizonSharpness, 0, 0, 0)));
  bgfx::setUniform(uniforms_.groundBlend,
                   glm::value_ptr(glm::vec4(settings_.sky.groundBlend, 0, 0, 0)));
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
      material.alpha==Material::Alpha::Mask ? 1.f:0.f,material.alphaCutoff,0,0)));
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
}

void Renderer::setCloudUniforms() {
  const bool enabled=settings_.clouds!=CloudQuality::Off && settings_.cloudCoverage>0;
  bgfx::setUniform(uniforms_.cloudParams,glm::value_ptr(glm::vec4(settings_.cloudCoverage,
      settings_.cloudBase,settings_.cloudThickness,enabled?1.f:0.f)));
  bgfx::setUniform(uniforms_.weather,glm::value_ptr(glm::vec4(float(weatherTime_*12.),.008f,28000.f,
      settings_.cloudShadows?1.f:0.f)));
  bgfx::setTexture(6,uniforms_.cloudNoise,cloudNoise_);
}

void Renderer::drawClouds() {
  if (settings_.clouds==CloudQuality::Off || settings_.cloudCoverage<=0) return;
  // Low uses 1/16 of the display pixels; other tiers use 1/4, with a fixed
  // upper pixel budget so a 4K display cannot make the march four times dearer.
  const unsigned divisor=settings_.clouds==CloudQuality::Low?4:2;
  const float scale=std::min({1.f,960.f/std::max(1.f,float(width_)/divisor),
      540.f/std::max(1.f,float(height_)/divisor)});
  const unsigned w=std::max(1u,unsigned(width_/divisor*scale));
  const unsigned h=std::max(1u,unsigned(height_/divisor*scale));
  if (!bgfx::isValid(cloudBuffer_) || w!=cloudWidth_ || h!=cloudHeight_) {
    if (bgfx::isValid(cloudBuffer_)) bgfx::destroy(cloudBuffer_);
    cloudWidth_=w;cloudHeight_=h;
    cloudBuffer_=bgfx::createFrameBuffer(w,h,bgfx::TextureFormat::RGBA16F,BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);
    if (!bgfx::isValid(cloudBuffer_)) throw std::runtime_error("Cloud framebuffer creation failed");
  }
  bgfx::setViewName(7,"Volumetric clouds");
  bgfx::setViewRect(7,0,0,w,h);
  bgfx::setViewFrameBuffer(7,cloudBuffer_);
  bgfx::setViewClear(7,BGFX_CLEAR_COLOR,0x00000000);
  setFrameUniforms(lastCamera_);
  bgfx::setUniform(uniforms_.invViewProj,glm::value_ptr(invViewProj_));
  bgfx::setUniform(uniforms_.postSettings,glm::value_ptr(glm::vec4(0,0,bgfx::getCaps()->originBottomLeft?1.f:0.f,0)));
  bgfx::setTexture(8,uniforms_.sceneDepth,bgfx::getTexture(hdrBuffer_,1),BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
  const float steps=settings_.clouds==CloudQuality::Low?12.f:settings_.clouds==CloudQuality::High?36.f:24.f;
  bgfx::setUniform(uniforms_.cloudRender,glm::value_ptr(glm::vec4(steps,w,h,0)));
  bgfx::setVertexBuffer(0,skyTriangle_);
  bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
  bgfx::submit(7,programs_.clouds);++stats_.drawCalls;++stats_.triangles;
  bgfx::discard();
}

void Renderer::compositeClouds() {
  if(!hdrHasRange_)return;
  bgfx::setViewName(9,"Clouds and effects");
  bgfx::setViewRect(9,0,0,width_,height_);
  bgfx::setViewFrameBuffer(9,atmosphereBuffer_);
  bgfx::setViewClear(9,BGFX_CLEAR_NONE);
  bgfx::setViewMode(9,bgfx::ViewMode::Sequential);
  if (settings_.clouds==CloudQuality::Off || settings_.cloudCoverage<=0) return;
  bgfx::setUniform(uniforms_.postSettings,glm::value_ptr(glm::vec4(0,0,bgfx::getCaps()->originBottomLeft?1.f:0.f,0)));
  bgfx::setUniform(uniforms_.cloudRender,glm::value_ptr(glm::vec4(0,cloudWidth_,cloudHeight_,0)));
  bgfx::setTexture(7,uniforms_.cloudLayer,bgfx::getTexture(cloudBuffer_),BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
  bgfx::setTexture(8,uniforms_.sceneDepth,bgfx::getTexture(hdrBuffer_,1),BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
  bgfx::setVertexBuffer(0,skyTriangle_);
  bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA));
  bgfx::submit(9,programs_.cloudComposite);++stats_.drawCalls;++stats_.triangles;
  bgfx::discard();
}

void Renderer::drawSky() {
  bgfx::setViewName(0, "World");
  bgfx::setViewRect(0, 0, 0, width_, height_);
  bgfx::setViewFrameBuffer(0, hdrBuffer_);
  bgfx::setViewTransform(0, nullptr, glm::value_ptr(viewProj_));
  bgfx::setTransform(glm::value_ptr(glm::mat4{1}));
  bgfx::setVertexBuffer(0, skyTriangle_);
  // The sky vertex shader emits z = 1 with LEQUAL testing against a cleared
  // depth of 1, so the dome fills the frame and never occludes world geometry.
  // No depth write, so it needs no sorting against the scene.
  bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LEQUAL);
  setSkyUniforms();
  bgfx::submit(0, programs_.sky);
  ++stats_.drawCalls;
}

void Renderer::ensureHdrBuffers() {
  const bool range=settings_.clouds!=CloudQuality::Off && settings_.cloudCoverage>0;
  if(bgfx::isValid(hdrBuffer_) && hdrWidth_==width_ && hdrHeight_==height_ && hdrSamples_==settings_.msaaSamples && hdrHasRange_==range)return;
  if(bgfx::isValid(atmosphereBuffer_))bgfx::destroy(atmosphereBuffer_);
  if(bgfx::isValid(hdrBuffer_))bgfx::destroy(hdrBuffer_);
  for(auto handle:bloomBuffer_)if(bgfx::isValid(handle))bgfx::destroy(handle);
  hdrWidth_=width_;hdrHeight_=height_;hdrSamples_=settings_.msaaSamples;hdrHasRange_=range;
  atmosphereBuffer_=BGFX_INVALID_HANDLE;
  bloomWidth_=std::max<unsigned>(1,width_/4);bloomHeight_=std::max<unsigned>(1,height_/4);
  const std::uint64_t sampleFlags=hdrSamples_>=8?BGFX_TEXTURE_RT_MSAA_X8:
      hdrSamples_>=4?BGFX_TEXTURE_RT_MSAA_X4:hdrSamples_>=2?BGFX_TEXTURE_RT_MSAA_X2:0;
  std::uint64_t flags=BGFX_TEXTURE_RT|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|sampleFlags;
  if(!bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA16F,flags) ||
      (range && !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::R16F,flags))) {
    flags&=~BGFX_TEXTURE_RT_MSAA_MASK;
    log("RENDER","HDR float MSAA unavailable; HDR target uses one sample");
  }
  const auto color=bgfx::createTexture2D(width_,height_,false,1,bgfx::TextureFormat::RGBA16F,flags);
  const auto depth=bgfx::createTexture2D(width_,height_,false,1,bgfx::TextureFormat::D24S8,flags|BGFX_TEXTURE_RT_WRITE_ONLY);
  const auto distance=range?bgfx::createTexture2D(width_,height_,false,1,bgfx::TextureFormat::R16F,flags):bgfx::TextureHandle{bgfx::kInvalidHandle};
  if(!bgfx::isValid(color) || !bgfx::isValid(depth) || (range && !bgfx::isValid(distance))) {
    for(auto h:{color,depth,distance})if(bgfx::isValid(h))bgfx::destroy(h);
    throw std::runtime_error("HDR colour/depth target creation failed");
  }
  // A compact range attachment resolves like color with MSAA, allowing the
  // volume to stop at geometry without multisample depth-texture support.
  if(range) {
    const bgfx::TextureHandle attachments[]{color,distance,depth};
    hdrBuffer_=bgfx::createFrameBuffer(3,attachments,true);
    const bgfx::TextureHandle atmosphereAttachments[]{color,depth};
    atmosphereBuffer_=bgfx::createFrameBuffer(2,atmosphereAttachments,false);
  } else {
    const bgfx::TextureHandle attachments[]{color,depth};
    hdrBuffer_=bgfx::createFrameBuffer(2,attachments,true);
  }
  for(auto& handle:bloomBuffer_)handle=bgfx::createFrameBuffer(bloomWidth_,bloomHeight_,bgfx::TextureFormat::RGBA16F,
      BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP);
  if(!bgfx::isValid(hdrBuffer_) || (range && !bgfx::isValid(atmosphereBuffer_)) || !bgfx::isValid(bloomBuffer_[0]) || !bgfx::isValid(bloomBuffer_[1]))
    throw std::runtime_error("HDR/bloom framebuffer creation failed");
}

void Renderer::compositeHdr() {
  const auto scene=bgfx::getTexture(hdrBuffer_);
  const glm::vec4 settings(settings_.sky.exposure,settings_.bloom?settings_.bloomStrength:0.f,
      bgfx::getCaps()->originBottomLeft?1.f:0.f,0.f);
  const auto submit=[&](bgfx::ViewId view,bgfx::FrameBufferHandle target,bgfx::TextureHandle source,const glm::vec4& step){
    bgfx::setViewName(view,"HDR bloom");bgfx::setViewRect(view,0,0,bloomWidth_,bloomHeight_);
    bgfx::setViewFrameBuffer(view,target);bgfx::setViewClear(view,BGFX_CLEAR_NONE);
    bgfx::setUniform(uniforms_.postSettings,glm::value_ptr(settings));
    bgfx::setUniform(uniforms_.postStep,glm::value_ptr(step));
    bgfx::setTexture(0,uniforms_.sceneTexture,source);
    bgfx::setVertexBuffer(0,skyTriangle_);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
    bgfx::submit(view,programs_.bloom);++stats_.drawCalls;++stats_.triangles;
  };
  if(settings_.bloom && settings_.bloomStrength>0) {
    submit(4,bloomBuffer_[0],scene,{1.f/width_,1.f/height_,1,0});
    submit(5,bloomBuffer_[1],bgfx::getTexture(bloomBuffer_[0]),{1.f/bloomWidth_,0,0,0});
    submit(6,bloomBuffer_[0],bgfx::getTexture(bloomBuffer_[1]),{0,1.f/bloomHeight_,0,0});
  }
  bgfx::setViewName(3,"HDR display transform");bgfx::setViewRect(3,0,0,width_,height_);
  bgfx::setViewFrameBuffer(3,BGFX_INVALID_HANDLE);bgfx::setViewClear(3,BGFX_CLEAR_NONE);
  bgfx::setUniform(uniforms_.postSettings,glm::value_ptr(settings));
  bgfx::setTexture(0,uniforms_.sceneTexture,scene);
  bgfx::setTexture(1,uniforms_.bloomTexture,settings_.bloom?bgfx::getTexture(bloomBuffer_[0]):whiteTexture_);
  bgfx::setVertexBuffer(0,skyTriangle_);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
  bgfx::submit(3,programs_.post);++stats_.drawCalls;++stats_.triangles;
  bgfx::discard();
}

bool Renderer::visiblePatch(const SceneryPatch& patch,float distance) const {
  const glm::vec3 center=patch.center+localPosition({},origin_);
  if (glm::length(center-cameraEye_)-patch.radius>distance) return false;
  const auto row=[&](int i){return glm::vec4(viewProj_[0][i],viewProj_[1][i],viewProj_[2][i],viewProj_[3][i]);};
  const auto w=row(3);
  for (int axis=0;axis<3;++axis) for (float sign:{-1.f,1.f}) {
    const auto plane=w+row(axis)*sign;
    if(glm::dot(plane,glm::vec4(center,1)) < -patch.radius*glm::length(glm::vec3(plane))) return false;
  }
  return true;
}

void Renderer::drawEnvironment() {
  const glm::mat4 environment = glm::translate(glm::mat4{1}, localPosition({}, origin_));

  static const Material kGrass{{}, {0.118f, 0.170f, 0.082f, 1}, 0.0f, 0.96f, {}, Material::Alpha::Opaque,
                               0.5f, false, 0};
  static const Material kAsphalt{{}, {0.049f, 0.050f, 0.053f, 1}, 0.0f, 0.80f, {}, Material::Alpha::Opaque,
                                 0.5f, false, 0};
  static const Material kPaint{{}, {0.86f, 0.87f, 0.84f, 1}, 0.0f, 0.72f, {}, Material::Alpha::Opaque,
                               0.5f, false, 0};

  const auto draw = [&](bgfx::VertexBufferHandle buffer, const Material& material, float detail) {
    setFrameUniforms(lastCamera_);
    bgfx::setTransform(glm::value_ptr(environment));
    bgfx::setUniform(uniforms_.model,glm::value_ptr(environment));
    bgfx::setUniform(uniforms_.normalMatrix,glm::value_ptr(glm::mat3{1}));
    bgfx::setState(kOpaqueState | BGFX_STATE_MSAA);
    bgfx::setVertexBuffer(0,buffer);
    applyMaterial(material,nullptr,detail);
    bgfx::submit(0,programs_.pbr);
    ++stats_.drawCalls;
  };
  draw(ground_,kGrass,1);
  stats_.triangles+=kTerrainRings*kTerrainSegments*2;
  draw(markings_,kAsphalt,2);
  draw(paintBuffer_,kPaint,0);
  const auto drawScenery = [&](bgfx::VertexBufferHandle buffer, glm::vec4 color,
                                float roughness, float detail, glm::vec3 emissive) {
    Material material;
    material.baseColor[0]=color.r; material.baseColor[1]=color.g; material.baseColor[2]=color.b;
    material.baseColor[3]=color.a; material.metallic=0; material.roughness=roughness;
    material.emissive[0]=emissive.r; material.emissive[1]=emissive.g; material.emissive[2]=emissive.b;
    draw(buffer,material,detail);
  };
  drawScenery(apron_, {.095f,.10f,.105f,1}, .9f, 2, {});
  drawScenery(taxiPaint_, {.75f,.49f,.035f,1}, .8f, 0, {});
  drawScenery(buildings_, {.31f,.34f,.35f,1}, .7f, 0, {});
  drawScenery(windows_, {.025f,.065f,.095f,1}, .15f, 0, {});
  drawScenery(lights_, {.72f,.82f,.9f,1}, .5f, 0, {1.4f,1.7f,2.2f});
  drawScenery(roadside_, {.045f,.043f,.04f,1}, .95f, 2, {});
  if(settings_.vegetation) for(const auto& patch:scenery_) {
    if(!visiblePatch(patch,std::min(settings_.sceneryDistance,settings_.renderDistance))) continue;
    const float distance=glm::length(patch.center+localPosition({},origin_)-cameraEye_);
    const bool nearby=distance<1800.f;
    const auto leaves=nearby?patch.foliage:patch.distant;
    if(bgfx::isValid(leaves)) {
      drawScenery(leaves,{.038f,.105f,.022f,1},.96f,3,{});
      stats_.triangles+=(nearby?patch.foliageCount:patch.distantCount)/3;
    }
    if(distance<3200.f && bgfx::isValid(patch.wood)) {
      drawScenery(patch.wood,{.15f,.095f,.052f,1},1,4,{});
      stats_.triangles+=patch.woodCount/3;
    }
    if(distance<6000.f && bgfx::isValid(patch.rocks)) {
      drawScenery(patch.rocks,{.25f,.25f,.22f,1},1,5,{});
      stats_.triangles+=patch.rockCount/3;
    }
  }
  bgfx::discard();
}

void Renderer::drawGrid() {
  bgfx::setTransform(glm::value_ptr(glm::mat4{1}));
  bgfx::setUniform(uniforms_.model, glm::value_ptr(glm::translate(glm::mat4{1}, localPosition({}, origin_))));
  bgfx::setUniform(uniforms_.viewProj, glm::value_ptr(viewProj_));
  bgfx::setVertexBuffer(0, grid_);
  bgfx::setState(kOpaqueState | BGFX_STATE_PT_LINES);
  bgfx::submit(0, programs_.unlit);
  ++stats_.drawCalls;
}

void Renderer::drawAircraft(const Instance& instance, bool hide) {
  const auto& asset = model(instance.type);
  if (hide || aircraftCrashed(*instance.state) || !asset.loaded) return;
  const std::size_t lod = instance.lod;
  const GpuLevel& level = asset.levels[lod];
  if (level.batches.empty() || !bgfx::isValid(level.vertexBuffer)) return;
  activeLod_ = lod;
  const glm::mat4 base = modelTransform(*instance.state, instance.type);
  // Write all opaque depth before drawing thin transparent glass and decals.
  for (const bool translucent : {false,true}) for (const Batch& batch : level.batches) {
    if (batch.material >= asset.materials.size()) continue;
    if ((asset.materials[batch.material].alpha==Material::Alpha::Blend)!=translucent) continue;
    setFrameUniforms(lastCamera_);
    bgfx::setVertexBuffer(0, level.vertexBuffer);
    const auto modelMatrix = batch.transformNode<0 ? base : base*glm::make_mat4(instance.deltas[batch.transformNode].data());
    bgfx::setTransform(glm::value_ptr(modelMatrix));
    bgfx::setUniform(uniforms_.model, glm::value_ptr(modelMatrix));
    bgfx::setUniform(uniforms_.normalMatrix, glm::value_ptr(glm::inverseTranspose(glm::mat3(modelMatrix))));
    Material material = asset.materials[batch.material];
    applyMaterial(material,&asset);
    const auto renderState = material.alpha==Material::Alpha::Blend ? kBlendState : kOpaqueState;
    bgfx::setState((material.doubleSided ? (renderState & ~BGFX_STATE_CULL_MASK) : renderState) | BGFX_STATE_MSAA | (settings_.wireframeAircraft ? BGFX_STATE_PT_LINES : 0));
    bgfx::setIndexBuffer(level.indexBuffer, batch.firstIndex, batch.indexCount);
    bgfx::submit(0, programs_.pbr);
    stats_.triangles += batch.indexCount / 3;
    ++stats_.drawCalls;
  }
  bgfx::discard();
  ++stats_.aircraftDrawn;
  ++stats_.lodCounts[lod];
}

void Renderer::drawEffects(const CombatVisuals& combat) {
  if (pool_.size() == 0 && combat.lines.empty()) return;
  // Effects are camera-facing quads rebuilt every frame, so they go into a
  // transient vertex buffer: no persistent buffer, no upload call and nothing
  // to resize. The bounded pool bounds the vertex count.
  const auto maxVertices = static_cast<std::uint32_t>((pool_.size() * 3 + combat.lines.size()) * 6);
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
      const float halfLength = effect.stretch*.5f;
      glm::vec3 across=glm::cross(axis,cameraEye_-centre);
      if (glm::dot(across,across)<1e-8f) across=basisRight_;
      across=glm::normalize(across);
      // Minimum angular width keeps distant rounds legible at game resolution.
      const float radius=std::max(effect.size*.5f,glm::length(cameraEye_-centre)*.00028f);
      const auto glowAlpha=static_cast<std::uint32_t>(faded*.38f);
      emitQuad(centre-axis*halfLength,axis,across,halfLength,radius*3.5f,
               (color&0x00ffffffu)|(glowAlpha<<24),2);
      emitQuad(centre-axis*halfLength,axis,across,halfLength,radius,
               0x00e8f4ffu|(faded<<24),2);
      // A hot tip faces the camera even when the streak is seen end-on.
      emitQuad(centre,basisRight_,basisUp_,radius*2,radius*2,
               0x00c8e8ffu|(faded<<24),3);
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
    setFrameUniforms(lastCamera_);
    bgfx::TransientVertexBuffer transient;
    bgfx::allocTransientVertexBuffer(&transient, vertexCount, effectLayout_);
    std::memcpy(transient.data, effectScratch_.data(),
                static_cast<std::size_t>(vertexCount) * kEffectVertexFloats * sizeof(float));
    bgfx::setTransform(glm::value_ptr(glm::mat4{1}));
    bgfx::setVertexBuffer(0, &transient);
    bgfx::setUniform(uniforms_.model, glm::value_ptr(glm::mat4{1}));
    bgfx::setState((xray?(kBlendState & ~BGFX_STATE_DEPTH_TEST_MASK):kBlendState) | BGFX_STATE_MSAA);
    bgfx::submit(hdrHasRange_?9:0, programs_.effect);
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

void Renderer::drawAfterburners(bool localDestroyed) {
  if (settings_.effects==EffectsQuality::Off) return;
  // Fixed shared open volumetric shells: no per-frame vertex uploads or network
  // particle stream. Three translucent layers per engine, capped by world count.
  if (!bgfx::isValid(flameMesh_)) {
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
      if (settings_.effects>=EffectsQuality::Medium && spool>.25f) {
        const float density=static_cast<float>(isaAtAltitude(-instance.state->pos_ned.z).rho/1.225);
        const float heat=(.25f+.75f*intensity)*spool*std::sqrt(std::max(.04f,density));
        bgfx::setUniform(uniforms_.flame,glm::value_ptr(glm::vec4(heat,float(flameTime_),4,float((id%97)*3+e*11))));
        bgfx::setUniform(uniforms_.model,glm::value_ptr(matrix));
        bgfx::setUniform(uniforms_.viewProj,glm::value_ptr(viewProj_));
        bgfx::setTransform(glm::value_ptr(matrix));
        bgfx::setVertexBuffer(0,flameMesh_,0,flameVertices_);
        bgfx::setState(kBlendState|BGFX_STATE_MSAA);
        bgfx::submit(hdrHasRange_?9:0,programs_.flame);++stats_.drawCalls;stats_.triangles+=flameVertices_/3;
      }
      if (intensity<.005f) continue;
      for(unsigned layer=0;layer<3;++layer) {
        bgfx::setUniform(uniforms_.flame,glm::value_ptr(glm::vec4(intensity,float(flameTime_),float(layer),float((id%97)*3+e*11))));
        bgfx::setUniform(uniforms_.model,glm::value_ptr(matrix));
        bgfx::setUniform(uniforms_.viewProj,glm::value_ptr(viewProj_));
        bgfx::setTransform(glm::value_ptr(matrix));
        bgfx::setVertexBuffer(0,flameMesh_,0,flameVertices_);
        bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_DEPTH_TEST_LEQUAL|
          BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,BGFX_STATE_BLEND_ONE)|BGFX_STATE_MSAA);
        bgfx::submit(hdrHasRange_?9:0,programs_.flame);
        ++stats_.drawCalls;stats_.triangles+=flameVertices_/3;
      }
      const auto center=localPosition(instance.state->pos_ned+instance.state->att.rotate(point),origin_);
      const float radius=.68f*static_cast<float>(definition.visual.exhaustRadiusScale)*std::sqrt(intensity);
      glm::mat4 glow{1};glow[0]=glm::vec4(basisRight_*radius,0);
      glow[1]=glm::vec4(basisUp_*radius,0);glow[3]=glm::vec4(center,1);
      bgfx::setUniform(uniforms_.flame,glm::value_ptr(glm::vec4(intensity,float(flameTime_),3,float((id%97)*3+e*11))));
      bgfx::setUniform(uniforms_.model,glm::value_ptr(glow));
      bgfx::setUniform(uniforms_.viewProj,glm::value_ptr(viewProj_));
      bgfx::setTransform(glm::value_ptr(glow));
      bgfx::setVertexBuffer(0,flameMesh_,flameVertices_,6);
      bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_DEPTH_TEST_LEQUAL|
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,BGFX_STATE_BLEND_ONE)|BGFX_STATE_MSAA);
      bgfx::submit(hdrHasRange_?9:0,programs_.flame);++stats_.drawCalls;stats_.triangles+=2;
    }
  }
  bgfx::discard();
}

void Renderer::drawShadowMap(const std::vector<const Instance*>& casters) {
  if (settings_.shadows == ShadowQuality::Off || casters.empty()) {
    shadowMapValid_ = false;
    return;
  }
  const auto size = static_cast<std::uint16_t>(std::clamp(settings_.shadowMapSize, 512, 4096));
  if (!bgfx::isValid(shadowBuffer_) || shadowMapSize_ != size) {
    if (bgfx::isValid(shadowBuffer_)) bgfx::destroy(shadowBuffer_);

    // The framebuffer owns the depth texture returned by getTexture().
    shadowBuffer_ = bgfx::createFrameBuffer(
        size, size, bgfx::TextureFormat::D32F,
        BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_COMPARE_LEQUAL);
    shadowMapSize_ = size;
    if (!bgfx::isValid(shadowBuffer_)) {
      shadowMapValid_ = false;
      log("RENDER", "Shadow map creation failed; shadows disabled");
      return;
    }
    shadowMap_ = bgfx::getTexture(shadowBuffer_, 0);
    if (!bgfx::isValid(shadowMap_)) {
      bgfx::destroy(shadowBuffer_);
      shadowBuffer_ = BGFX_INVALID_HANDLE;
      shadowMapValid_ = false;
      log("RENDER", "Shadow map texture handle unavailable; shadows disabled");
      return;
    }
  }

  // The volume follows the camera, snapped to the texel grid so shadow edges do
  // not crawl while flying.
  float sunDirection[3];
  ofs::client::sunDirection(settings_.sky, sunDirection);
  const glm::vec3 light(sunDirection[0], sunDirection[1], sunDirection[2]);
  glm::vec3 focus = localPosition(casters.front()->state->pos_ned, origin_);
  const float extent = std::max(30.0f, settings_.shadowExtent);
  const float depth = extent * 4.0f;
  const float snap = 2.0f * extent / static_cast<float>(size);
  const glm::vec3 lightRight=glm::normalize(glm::cross(light,glm::vec3(0,1,0)));
  const glm::vec3 lightUp=glm::normalize(glm::cross(lightRight,light));
  const glm::dvec3 absolute=glm::dvec3(focus)+glm::dvec3(origin_.y,-origin_.z,-origin_.x);
  const double horizontal=glm::dot(absolute,glm::dvec3(lightRight));
  const double vertical=glm::dot(absolute,glm::dvec3(lightUp));
  focus+=lightRight*float(std::round(horizontal/snap)*snap-horizontal)+
         lightUp*float(std::round(vertical/snap)*snap-vertical);

  const glm::mat4 lightView = glm::lookAt(focus - light * depth, focus, glm::vec3(0, 1, 0));
  const glm::mat4 lightProjection = makeOrtho(-extent, extent, -extent, extent, 0.1f, depth * 2.5f);
  const glm::mat4 lightViewProj = lightProjection * lightView;
  glm::mat4 bias(1);
  bias[0][0] = bias[1][1] = 0.5f;
  bias[3][0] = bias[3][1] = 0.5f;
  if (!bgfx::getCaps()->originBottomLeft) {
    bias[1][1] = -.5f; // D3D texture origin is top-left.
  }
  if (bgfx::getCaps()->homogeneousDepth) {
    bias[2][2] = 0.5f;
    bias[3][2] = 0.5f;
  }
  shadowMatrix_ = bias * lightViewProj;

  bgfx::setViewName(1, "ShadowMap");
  bgfx::setViewRect(1, 0, 0, size, size);
  // The shadow view must render into the map the surface shader samples, or
  // the depth pass has no attachment and the map stays empty.
  bgfx::setViewFrameBuffer(1, shadowBuffer_);
  bgfx::setViewClear(1, BGFX_CLEAR_DEPTH, 0x000000ffu, 1.0f, 0);
  bgfx::setViewTransform(1, glm::value_ptr(lightView), glm::value_ptr(lightProjection));
  // No colour write and no culling: the A320 has open gear doors and a hollow
  // nacelle, and either would punch holes in the map.

  for (const Instance* instance : casters) {
    const auto& asset = model(instance->type);
    if (!asset.loaded) continue;
    const glm::mat4 base = modelTransform(*instance->state,instance->type);
    const std::size_t qualityLevel=settings_.shadows==ShadowQuality::High?1:
        settings_.shadows==ShadowQuality::Medium?2:asset.levels.size()-1;
    const auto& shadowLevel=asset.levels[std::min({instance->lod,qualityLevel,asset.levels.size()-1})];
    for (const Batch& batch : shadowLevel.batches) {
      if(asset.materials.at(batch.material).alpha==Material::Alpha::Blend)continue;
      applyMaterial(asset.materials.at(batch.material),&asset,0);
      bgfx::setVertexBuffer(0, shadowLevel.vertexBuffer);
      bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
      bgfx::setUniform(uniforms_.shadowMatrix, glm::value_ptr(lightViewProj));
      const auto matrix=batch.transformNode<0 ? base : base*glm::make_mat4(instance->deltas[batch.transformNode].data());
      bgfx::setTransform(glm::value_ptr(matrix));
      bgfx::setUniform(uniforms_.model,glm::value_ptr(matrix));
      bgfx::setIndexBuffer(shadowLevel.indexBuffer, batch.firstIndex, batch.indexCount);
      bgfx::submit(1, programs_.shadow);
      ++stats_.drawCalls;
      stats_.triangles+=batch.indexCount/3;
    }
  }
  const glm::mat4 environment=glm::translate(glm::mat4{1},localPosition({},origin_));
  const auto sceneryShadow=[&](bgfx::VertexBufferHandle buffer,unsigned count) {
    if(!bgfx::isValid(buffer))return;
    applyMaterial(Material{},nullptr,0);
    bgfx::setVertexBuffer(0,buffer);
    bgfx::setState(BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_LESS);
    bgfx::setUniform(uniforms_.model,glm::value_ptr(environment));
    bgfx::setUniform(uniforms_.shadowMatrix,glm::value_ptr(lightViewProj));
    bgfx::setTransform(glm::value_ptr(environment));
    bgfx::submit(1,programs_.shadow);++stats_.drawCalls;stats_.triangles+=count/3;
  };
  const glm::vec3 worldFocus=focus-localPosition({},origin_);
  if(glm::length(worldFocus-glm::vec3(-280,20,-600))<extent+1000)
    sceneryShadow(buildings_,5*60+90);
  if(settings_.vegetation) for(const auto& patch:scenery_) {
    if(glm::length(patch.center-worldFocus)>patch.radius+extent*1.5f)continue;
    sceneryShadow(patch.foliage,patch.foliageCount);
    sceneryShadow(patch.wood,patch.woodCount);
  }
  bgfx::discard();
  shadowMapValid_ = true;
}

void Renderer::ui() {
  const ImDrawData* data = ImGui::GetDrawData();
  if (!data || data->DisplaySize.x <= 0 || data->DisplaySize.y <= 0) return;
  const auto projection = bgfx::getCaps()->homogeneousDepth
      ? glm::orthoRH_NO(data->DisplayPos.x, data->DisplayPos.x + data->DisplaySize.x,
                        data->DisplayPos.y + data->DisplaySize.y, data->DisplayPos.y, 0.f, 1.f)
      : glm::orthoRH_ZO(data->DisplayPos.x, data->DisplayPos.x + data->DisplaySize.x,
                        data->DisplayPos.y + data->DisplaySize.y, data->DisplayPos.y, 0.f, 1.f);
  bgfx::setViewMode(2, bgfx::ViewMode::Sequential);
  bgfx::setViewName(2, "Dear ImGui");
  bgfx::setViewMode(2, bgfx::ViewMode::Sequential);
  bgfx::setViewRect(2, 0, 0, width_, height_);
  bgfx::setViewTransform(2, nullptr, glm::value_ptr(projection));
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
      bgfx::submit(2, programs_.imgui);
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
  const auto preparationStart=std::chrono::steady_clock::now();
  // The simulation rebases its render origin as the aircraft travels, so it is
  // read fresh every frame rather than captured at construction.
  origin_ = origin;
  lastCamera_ = camera;
  stats_.drawCalls = 0;
  stats_.triangles = 0;
  stats_.activeParticles = 0;
  stats_.aircraftDrawn = 0;
  stats_.lodCounts = {};
  std::erase_if(instances_,[&](const auto& entry) {
    return entry.first!=0 && std::none_of(remotes.begin(),remotes.end(),[&](const auto& remote){return remote.alive && remote.entity==entry.first;});
  });
  const auto prepare=[&](std::uint64_t id,AircraftType type,const State& state,const Controls& input) {
    auto& instance=instances_[id];
    if (instance.type!=type) { instance={}; instance.type=type; }
    instance.state=&state;
    instance.pose.update(state,input,aircraftDefinition(type),dt);
    evaluatePose(model(type).nodes,instance.pose,instance.deltas);
    const double distance=(camera.eye-state.pos_ned).norm();
    // Shorter full-detail range for million-triangle airliners; 15% hysteresis.
    instance.lod=stableAircraftLod(aircraftDefinition(type).visual.radius,distance*std::exp2(settings_.lodBias),instance.lod,model(type).levels.size());
  };
  prepare(0,localType_,local,controls);
  for (const auto& remote:remotes) if (remote.alive) prepare(remote.entity,remote.type,remote.state,remote.controls);

  // Advance existing effects first, so newly arrived muzzle flashes survive
  // a long frame and shot/hit pairs retire the correct tracer immediately.
  const double effectDt=std::clamp(dt,0.0,.1);
  pool_.update(effectDt);
  // ---- Effects, driven by simulation combat events ----
  for (const CombatVisuals::Shot& shot : combat.shots)
    combat_.onShot(shot.position, shot.velocity, shot.lifetime, shot.ownAircraft,shot.projectile);
  for (const CombatVisuals::Hit& hit : combat.hits) combat_.onHit(hit.position, hit.ownAircraft,hit.projectile);
  for (const CombatVisuals::Destruction& destruction : combat.destructions)
    combat_.onDestroyed(destruction.position, destruction.velocity);
  for(const auto& impact:combat.groundImpacts) combat_.onGroundImpact(impact);
  combat_.setEmissions(settings_.contrails, settings_.engineHeat);
  combat_.setCondensation(settings_.wingVapor, settings_.relativeHumidity);
  for (const auto &missile : combat.missiles)
    combat_.updateMissile(missile.position, missile.attitude, missile.length,
                          missile.diameter, missile.powered, effectDt);
  for (const auto &position : combat.missileDetonations)
    combat_.onDestroyed(position, {});
  flameTime_+=effectDt;
  if (!combat.localDestroyed) combat_.updateAircraft(local, effectDt, localType_, 0, load,combat.localHealth,weather);
  for (const RemoteAircraft& remote : remotes)
    if (remote.alive) combat_.updateAircraft(remote.state, effectDt, remote.type, remote.entity, remote.load,remote.health,weather);
  stats_.particlePeak = std::max<std::size_t>(stats_.particlePeak, pool_.size());

  // ---- Camera and frame matrices ----
  const float aspect =
      static_cast<float>(width_) / static_cast<float>(std::max<std::uint32_t>(1, height_));
  const float far = std::max(1000.0f, settings_.renderDistance);
  // The view matrix is what turns world space into camera space; the
  // projection alone leaves every vertex in world coordinates, which puts the
  // world outside the frustum and renders nothing but the sky (the sky builds
  // its rays from the inverse view-projection, so it keeps looking correct).
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
  projection_ = makeProjection(static_cast<float>(camera.fov), aspect, camera.hidesOwnAircraft() ? std::min(.08f,settings_.nearPlane) : std::max(2.5f, settings_.nearPlane), far);
  viewProj_ = projection_ * view_;
  invViewProj_ = glm::inverse(viewProj_);

  cameraEye_ = eye;
  bgfx::setViewTransform(0, nullptr, glm::value_ptr(viewProj_));

  // ---- Shadow pass ----
  std::vector<const Instance*> casters;
  casters.reserve(8);
  if (!combat.localDestroyed && !aircraftCrashed(local)) casters.push_back(&instances_.at(0));
  for (const auto& remote:remotes) {
    if (casters.size()>=8) break;
    if (remote.alive && !aircraftCrashed(remote.state) && (remote.state.pos_ned-local.pos_ned).norm()<settings_.shadowExtent*3)
      casters.push_back(&instances_.at(remote.entity));
  }
  drawShadowMap(casters);
  ensureHdrBuffers();
  const bgfx::ViewId order[] = {1,0,7,9,4,5,6,3,2,8};
  bgfx::setViewOrder(0,10,order);
  weatherTime_ += std::clamp(dt,0.0,.1);

  // ---- Main pass ----
  // The clear colour matches the fog so a resize never flashes a different
  // background before the sky covers it.
  // bgfx takes the clear colour as 0xRRGGBBAA, so red occupies the high byte.
  // Packing it as 0xAABBGGRR swaps red and blue, which tints the whole
  // background sky-blue and was the cause of a channel-swapped-looking image.
  const auto to8 = [](float value) {
    return static_cast<std::uint32_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
  };
  const auto fogRgba = (to8(settings_.fog.colorR) << 24) |
                       (to8(settings_.fog.colorG) << 16) | (to8(settings_.fog.colorB) << 8) | 0xffu;
  bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, fogRgba, 1.0f, 0);
  bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
  drawSky();
  drawEnvironment();
  if (settings_.showDebugGrid) drawGrid();
  drawAircraft(instances_.at(0), (camera.hidesOwnAircraft() && !aircraftDefinition(localType_).visual.cockpitGeometry) || combat.localDestroyed);
  for (const RemoteAircraft& remote : remotes)
    if (remote.alive && (remote.state.pos_ned-camera.eye).norm()<settings_.renderDistance)
      drawAircraft(instances_.at(remote.entity), false);
  drawClouds();
  compositeClouds();
  drawEffects(combat);
  drawAfterburners(combat.localDestroyed);
  compositeHdr();
  stats_.preparationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-preparationStart).count();

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
  stats_.shadowMapSize = shadowMapValid_ ? shadowMapSize_ : 0;
  stats_.lodTier = "LOD" + std::to_string(activeLod_);
}

}  // namespace ofs::client
