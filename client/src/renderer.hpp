#pragma once
// bgfx renderer.
//
// Owns GPU resources and the frame graph. It is independent of the authoritative
// simulation: it reads State/Controls values and the aircraft model asset, and
// never writes to either.

#include "camera.hpp"
#include "animation.hpp"
#include "effects.hpp"
#include "gltf.hpp"
#include "mesh.hpp"
#include "ofs/simulator.hpp"
#include "render_callbacks.hpp"
#include "settings.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <array>
#include <cstring>

namespace ofs::client {

class Platform;

// One remote aircraft as the renderer needs it: pose plus presentation data.
struct RemoteAircraft {
  State state;
  std::uint64_t entity{};
  std::string name;
  double health{100};
  bool alive{true};
  AircraftType type{AircraftType::A320};
  Controls controls;
  double load{1};
};

// Server-authoritative combat events for one frame. The renderer turns these
// into effects and never derives combat state itself.
struct CombatVisuals {
  struct Shot { Vec3 position, velocity; double lifetime; bool ownAircraft; std::uint64_t projectile{}; };
  struct Hit { Vec3 position; bool ownAircraft; std::uint64_t projectile{}; };
  struct Destruction { Vec3 position, velocity; };
  struct Line {
    Vec3 start, end;
    std::uint32_t color;
    bool xray{};
    float halfWidth{.13f};
  };
  struct MissileVisual {
    Vec3 position;
    Quat attitude;
    double length{}, diameter{};
    bool powered{};
  };
  std::vector<MissileVisual> missiles;
  std::vector<Vec3> missileDetonations;
  std::vector<Line> lines;
  std::vector<Shot> shots;
  std::vector<Hit> hits;
  std::vector<Destruction> destructions;
  std::vector<Simulator::GroundImpact> groundImpacts;
  // Life state for the HUD, straight from the server's replicated values.
  bool localDestroyed{};
  double localHealth{100};
  double localRespawnSeconds{};
  // Gun solution: the aircraft gun axis in world NED, projected by the HUD.
  bool gunPointValid{};
  Vec3 gunPoint{};
};

class Renderer {
 public:
  Renderer(const Platform& platform, const GraphicsSettings& settings);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  // Loads the aircraft model. Returns false and logs on failure; the renderer
  // stays usable so the developer grid and HUD still run.
  bool loadAircraft(const std::string& path, AircraftType type);
  void setAircraftType(AircraftType type) { localType_ = type; }
  bool hasAircraft() const { return model(localType_).loaded; }
  const GpuMesh& aircraftMesh() const { return model(localType_).mesh; }
  const std::vector<std::string>& assetReport() const { return model(localType_).report; }
  const std::string& aircraftName() const { return model(localType_).name; }

  // Applies settings that need a bgfx reset (size, MSAA, VSync).
  void clearEffects() { pool_.clear(); }
  void applySettings(const GraphicsSettings& settings, const Platform& platform);
  const GraphicsSettings& graphics() const { return settings_; }
  bool resize(SDL_Window* window);

  // Renders one frame. `origin` is the shared double-precision render origin,
  // read fresh each frame because the simulation rebases it.
  void render(const Camera& camera, const State& local, const Controls& controls,
              std::span<const RemoteAircraft> remotes, const CombatVisuals& combat,
              const Vec3& origin, double dt, double load = 1, const Weather& weather = {});

  struct Stats {
    double fps{};
    double cpuFrameMs{};
    double gpuFrameMs{};
    double preparationMs{};
    std::uint32_t drawCalls{};
    std::uint32_t triangles{};
    std::uint32_t activeParticles{};
    std::uint32_t aircraftDrawn{};
    std::uint32_t shadowMapSize{};
    std::size_t gpuMemory{};
    std::string backend;
    std::string adapter;
    std::string lodTier;
    std::size_t particlePeak{};
    std::array<unsigned, kMaxLodCount> lodCounts{};
  };
  const Stats& stats() const { return stats_; }

  const char* backend() const { return bgfx::getRendererName(bgfx::getRendererType()); }
  bool screenshotWritten() const { return callbacks_.screenshotWritten.load(); }
  // Submits ImGui's draw data in its own view, after the world.
  void ui();
  void screenshot(const std::string& path) {
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path.c_str());
  }

  // Projects a world point to pixels for HUD labels. False when behind the
  // camera or off screen.
  bool projectToScreen(const Vec3& world, float& outX, float& outY, float& outDepth) const;
  // Model transform for the visual aircraft, exposed so tests can verify it.
  glm::mat4 modelTransform(const State& state) const;
  // Body-FRD -> render matrix, so tests and the HUD share one implementation.
  glm::mat4 bodyTransform(const State& state) const;

 private:
  struct Programs {
    bgfx::ProgramHandle pbr{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle sky{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle unlit{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle effect{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle imgui{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle shadow{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle flame{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle post{BGFX_INVALID_HANDLE}, bloom{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle clouds{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle cloudComposite{BGFX_INVALID_HANDLE};
  };
  struct Uniforms {
    Uniforms() { std::memset(this, 0xff, sizeof(*this)); }
    bgfx::UniformHandle model, viewProj, invViewProj, normalMatrix, cameraPos, worldOrigin;
    bgfx::UniformHandle sunDirection, sunColor, skyAmbient, groundAmbient;
    bgfx::UniformHandle baseColor, metallicRoughness, emissive, doubleSided;
    bgfx::UniformHandle shadowMatrix, shadowMap, shadowTexel, shadowBias, shadowStrength;
    bgfx::UniformHandle fogColor, fogDensity, fogHeightFalloff, fogGroundFade, fogEnabled;
    bgfx::UniformHandle exposure;
    bgfx::UniformHandle zenithColor, horizonColor, groundColor, sunIntensity;
    bgfx::UniformHandle horizonSharpness, groundBlend;
    bgfx::UniformHandle baseTexture, mrTexture, emissiveTexture, normalTexture, occlusionTexture, normalSettings, textureFlags, alphaSettings;
    bgfx::UniformHandle flame;
    bgfx::UniformHandle sceneTexture,bloomTexture,postSettings,postStep;
    bgfx::UniformHandle cloudParams, weather, cloudNoise, cloudLayer, cloudRender;
    bgfx::UniformHandle sceneDepth;
  };
  struct GpuLevel {
    bgfx::VertexBufferHandle vertexBuffer{BGFX_INVALID_HANDLE};
    bgfx::IndexBufferHandle indexBuffer{BGFX_INVALID_HANDLE};
    std::vector<Batch> batches;
  };
  struct Model {
    GpuMesh mesh;
    std::vector<GltfNode> nodes;
    std::vector<Material> materials;
    std::vector<bgfx::TextureHandle> textures;
    std::vector<GpuLevel> levels;
    std::vector<std::string> report;
    std::string name;
    bool loaded{};
    std::size_t textureBytes{};
  };
  const Model& model(AircraftType type) const { return models_.at(type); }
  Model& model(AircraftType type) {
    if (!validAircraftType(type)) throw std::invalid_argument("invalid render aircraft type");
    return models_[type];
  }
  struct Instance {
    AircraftType type{AircraftType::A320};
    const State* state{};
    AircraftPose pose;
    std::vector<AssetMatrix> deltas;
    std::size_t lod{};
  };

  bool initialize(const Platform& platform);
  void ensureHdrBuffers();
  void compositeHdr();
  bgfx::FrameBufferHandle hdrBuffer_{BGFX_INVALID_HANDLE};
  bgfx::FrameBufferHandle atmosphereBuffer_{BGFX_INVALID_HANDLE}; // aliases HDR color/depth, excluding range attachment
  bgfx::FrameBufferHandle bloomBuffer_[2]{BGFX_INVALID_HANDLE,BGFX_INVALID_HANDLE};
  unsigned hdrWidth_{},hdrHeight_{},bloomWidth_{},bloomHeight_{};
  int hdrSamples_{};
  bool hdrHasRange_{};
  bgfx::FrameBufferHandle cloudBuffer_{BGFX_INVALID_HANDLE};
  bgfx::TextureHandle cloudNoise_{BGFX_INVALID_HANDLE};
  unsigned cloudWidth_{}, cloudHeight_{};
  double weatherTime_{};
  void drawClouds();
  void compositeClouds();
  void setCloudUniforms();
  void buildEnvironment();
  void destroyEnvironment();
  void destroy();
  void drawSky();
  void drawEnvironment();
  void drawGrid();
  void drawAircraft(const Instance& instance, bool hide);
  void drawEffects(const CombatVisuals& combat);
  void drawAfterburners(bool localDestroyed);
  void drawShadowMap(const std::vector<const Instance*>& casters);
  void setFrameUniforms(const Camera& camera);
  void setSkyUniforms();
  void applyMaterial(const Material& material, const Model* model = nullptr, float detail = 0);
  std::uint32_t resetFlags() const;
  glm::mat4 modelTransform(const State& state, AircraftType type) const;

  RenderCallbacks callbacks_{};
  bgfx::SwapChain swapChain_{};
  bool initialized_{};
  std::uint16_t width_{}, height_{};
  bgfx::VertexLayout surfaceLayout_{}, unlitLayout_{}, effectLayout_{}, uiLayout_{};
  Programs programs_{};
  Uniforms uniforms_{};

  Vec3 origin_{};
  Camera lastCamera_{};
  std::map<AircraftType, Model> models_;
  AircraftType localType_{AircraftType::A320};
  std::map<std::uint64_t, Instance> instances_;
  std::uint64_t auxiliaryTextureBytes_{};
  bgfx::TextureHandle whiteTexture_{BGFX_INVALID_HANDLE};
  bgfx::VertexBufferHandle flameMesh_{BGFX_INVALID_HANDLE};
  unsigned flameVertices_{};
  double flameTime_{};
  std::size_t activeLod_{0};

  bgfx::VertexBufferHandle apron_{BGFX_INVALID_HANDLE}, taxiPaint_{BGFX_INVALID_HANDLE};
  bgfx::VertexBufferHandle buildings_{BGFX_INVALID_HANDLE}, windows_{BGFX_INVALID_HANDLE};
  bgfx::VertexBufferHandle lights_{BGFX_INVALID_HANDLE};
  bgfx::VertexBufferHandle roadside_{BGFX_INVALID_HANDLE};
  struct SceneryPatch {
    glm::vec3 center{};
    float radius{};
    bgfx::VertexBufferHandle foliage{BGFX_INVALID_HANDLE}, distant{BGFX_INVALID_HANDLE};
    bgfx::VertexBufferHandle wood{BGFX_INVALID_HANDLE}, rocks{BGFX_INVALID_HANDLE};
    unsigned foliageCount{}, distantCount{}, woodCount{}, rockCount{};
  };
  std::vector<SceneryPatch> scenery_;
  bool visiblePatch(const SceneryPatch& patch, float distance) const;
  bgfx::VertexBufferHandle ground_{BGFX_INVALID_HANDLE};
  bgfx::VertexBufferHandle markings_{BGFX_INVALID_HANDLE};
  bgfx::VertexBufferHandle paintBuffer_{BGFX_INVALID_HANDLE};
  bgfx::VertexBufferHandle grid_{BGFX_INVALID_HANDLE};
  bgfx::VertexBufferHandle skyTriangle_{BGFX_INVALID_HANDLE};
  bgfx::TextureHandle shadowMap_{BGFX_INVALID_HANDLE};
  bgfx::FrameBufferHandle shadowBuffer_{BGFX_INVALID_HANDLE};
  bgfx::TextureHandle font_{BGFX_INVALID_HANDLE};
  bgfx::UniformHandle uiSampler_{BGFX_INVALID_HANDLE};
  std::uint32_t shadowMapSize_{};

  EffectPool pool_{4096};
  CombatEffects combat_{pool_, EffectsQuality::High};
  GraphicsSettings settings_;
  Stats stats_{};
  glm::mat4 view_{1};
  glm::mat4 projection_{1};
  glm::mat4 viewProj_{1};
  glm::mat4 invViewProj_{1};
  glm::mat4 shadowMatrix_{1};
  glm::vec3 cameraEye_{};
  glm::vec3 basisRight_{1, 0, 0};
  glm::vec3 basisUp_{0, 1, 0};
  bool shadowMapValid_{};
  double fpsTimer_{};
  std::uint32_t fpsFrames_{};
  std::vector<float> effectScratch_;
};

}  // namespace ofs::client
