#pragma once
// bgfx renderer.
//
// Owns GPU resources and the frame graph. It is independent of the authoritative
// simulation: it reads State/Controls values and the aircraft model asset, and
// never writes to either.
//
// Frame graph, in order:
//   sky-view table, aerial-perspective atlas   (atmosphere, per frame)
//   sun shadow cascades                        (depth atlas)
//   world                                      (HDR colour + range + depth, MSAA)
//   cloud march, cloud temporal resolve        (reduced resolution)
//   atmosphere: cloud composite, particles, plumes, rain
//   cockpit                                    (own aircraft in its own depth range)
//   heat refraction                            (offset buffer)
//   glare pyramid, display transform, edge filter, UI
// The implementation is split by subsystem: renderer.cpp (frame, aircraft,
// effects), renderer_atmosphere.cpp (sky, clouds, shadows, post) and
// renderer_environment.cpp (terrain, airfield, trees).

#include "airfield.hpp"
#include "animation.hpp"
#include "atmosphere_model.hpp"
#include "breakaway.hpp"
#include "camera.hpp"
#include "damage_visuals.hpp"
#include "effects.hpp"
#include "ejection.hpp"
#include "ofs/weapons.hpp"
#include "gltf.hpp"
#include "landscape.hpp"
#include "map.hpp"
#include "mesh.hpp"
#include "procedural.hpp"
#include "ofs/simulator.hpp"
#include "render_callbacks.hpp"
#include "settings.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <future>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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
  unsigned kills{}, deaths{};
};

// Server-authoritative combat events for one frame. The renderer turns these
// into effects and never derives combat state itself.
struct CombatVisuals {
  // `carrier` is the firing aircraft's velocity when it is known.
  struct Shot { Vec3 position, velocity; double lifetime; bool ownAircraft; std::uint64_t projectile{}; Vec3 carrier{}; };
  struct Hit { Vec3 position; bool ownAircraft; std::uint64_t projectile{}; Vec3 targetVelocity{}; };
  // An aircraft blowing up. With `airframe` set, its wings and fin are thrown
  // clear from where `state` last had it; `entity` is 0 for the pilot's own.
  struct Destruction {
    Vec3 position, velocity;
    bool airframe{};
    std::uint64_t entity{};
    AircraftType type{AircraftType::A320};
    State state{};
  };
  struct Line {
    Vec3 start, end;
    std::uint32_t color;
    bool xray{};
    float halfWidth{.13f};
  };
  // A missile in flight, as the effects need it: where its motor is and what
  // the motor is doing.
  struct MissileVisual {
    std::uint64_t id{};
    Vec3 position, velocity;
    Quat attitude;
    double length{}, diameter{}, age{};
    bool powered{};
  };
  // One missile airframe to draw, hanging on a pylon or flying.
  struct Store {
    Vec3 position;
    Quat attitude;
    weapons::WeaponType type{weapons::WeaponType::Infrared};
    bool onLocalAircraft{}; // drawn with the pilot's own airframe from the flight deck
    bool burning{};         // its motor is lit: the nozzle glows
  };
  // The pylon a station hangs from, which stays after its missile has gone.
  struct Pylon {
    Vec3 position; // store centreline at mid-length
    Quat attitude;
    AircraftType aircraft{AircraftType::Typhoon};
    std::uint8_t station{};
    weapons::WeaponType type{weapons::WeaponType::Infrared};
    bool onLocalAircraft{};
  };
  std::vector<MissileVisual> missiles;
  std::vector<Store> stores;
  std::vector<Pylon> pylons;
  std::vector<Vec3> missileDetonations;
  // A flare or a bundle of chaff released this frame.
  struct Decoy { weapons::DecoyType type{weapons::DecoyType::Flare}; Vec3 position, velocity; };
  std::vector<Decoy> decoys;
  std::vector<Line> lines;
  std::vector<Shot> shots;
  std::vector<Hit> hits;
  std::vector<Destruction> destructions;
  std::vector<Simulator::GroundImpact> groundImpacts;
  // Pilots who have left their aircraft, as they are this frame.
  std::span<const EjectedPilot> pilots;
  // Life state for the HUD, straight from the server's replicated values.
  bool localDestroyed{};
  double localHealth{100};
  double localRespawnSeconds{};
  // Gun solution: the aircraft gun axis in world NED, projected by the HUD.
  bool gunPointValid{};
  Vec3 gunPoint{};
};

// Constants every shader reads, uploaded as one vec4 array. The layout mirrors
// client/shaders/frame.glsl entry for entry.
struct FrameConstants {
  glm::vec4 cameraPos;      // xyz eye, w time
  glm::vec4 worldOrigin;    // xyz origin offset, w exposure
  glm::vec4 sunDirection;   // xyz toward the sun, w emissive radiance scale
  glm::vec4 sunIrradiance;  // rgb at the camera altitude, w cloud shadow strength
  glm::vec4 ambient[4];     // L1 irradiance: constant, x, y, z
  glm::vec4 viewport;       // w, h, 1/w, 1/h
  glm::vec4 atmoGeometry;   // planet radius, atmosphere height, Rayleigh H, Mie H
  glm::vec4 rayleigh;       // rgb scattering, w Mie anisotropy
  glm::vec4 mie;            // Mie scattering, Mie extinction, fog extinction, fog H
  glm::vec4 ozone;          // rgb absorption, w eye altitude
  glm::vec4 groundAlbedo;   // rgb, w aerial range
  glm::vec4 solar;          // rgb, w water time
  glm::vec4 cloudLayer;     // coverage, base, thickness, enabled
  glm::vec4 cloudWeather;   // wind offset xz, extinction, weather-map 1/size
  glm::vec4 cloudShape;     // cirrus coverage, cirrus altitude, march range, type bias
  glm::vec4 shadowParams;   // cascades, 1/tile size, depth bias, strength
  glm::vec4 shadowTexel;    // world texel size per cascade, blend band
  glm::vec4 misc;           // frame jitter, origin bottom-left, precipitation, wetness
  glm::vec4 cameraForward;  // xyz, w metres per stored range unit
  glm::vec4 quality;        // terrain detail, water, specular AA, relief shadow steps
  glm::vec4 wind;           // xyz render-space wind, w gust phase
};
static_assert(sizeof(FrameConstants) == 24 * sizeof(glm::vec4));

class Renderer {
 public:
  Renderer(const Platform& platform, const GraphicsSettings& settings);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  // Loads the aircraft model. Returns false and logs on failure; the renderer
  // stays usable so the developer grid and HUD still run.
  bool loadAircraft(const std::string& path, AircraftType type);
  // The same load split in two, so startup can decode every aircraft on worker
  // threads while the window keeps responding. prepareAircraft touches no GPU
  // state; finishAircraft uploads on the render thread.
  struct AircraftSource {
    struct Upload {
      std::vector<std::uint8_t> pixels;
      unsigned width{}, height{};
      bool mipmaps{};
      std::uint64_t flags{};
      std::string report;
    };
    AircraftType type{AircraftType::A320};
    std::string path, error;
    Mesh mesh;  // image pixels are released once their mip chains exist
    GpuMesh gpu;
    std::vector<Upload> uploads;
    std::vector<int> textureUpload;  // per glTF texture; -1 without an image
    std::vector<std::string> largest;
  };
  std::future<AircraftSource> prepareAircraft(const std::string& path, AircraftType type) const;
  bool finishAircraft(AircraftSource source);
  // Presents a plain status frame while assets are still being prepared.
  void loadingFrame(std::string_view status);
  void setAircraftType(AircraftType type) { localType_ = type; }
  bool hasAircraft() const { return model(localType_).loaded; }
  const GpuMesh& aircraftMesh() const { return model(localType_).mesh; }
  const std::vector<std::string>& assetReport() const { return model(localType_).report; }
  const std::string& aircraftName() const { return model(localType_).name; }

  // Applies settings that need a bgfx reset (size, MSAA, VSync).
  void clearEffects() {
    pool_.clear();
    breakaways_.clear();
  }
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
    // Environment and lighting, for the developer overlay.
    std::uint32_t treesDrawn{};
    std::uint32_t treeChunks{};
    std::uint32_t shadowCascades{};
    std::uint32_t cloudWidth{}, cloudHeight{};
    float exposure{};
    float sunIlluminanceLux{};
    float skyIlluminanceLux{};
    std::size_t environmentTextureBytes{};
    int lakes{};
  };
  const Stats& stats() const { return stats_; }

  // What the last frame did that can be heard: a wing or fin leaving an
  // aircraft (`own` for the pilot's), and a falling wreck reaching the ground.
  struct PartLost { Vec3 position; bool own{}; };
  const std::vector<PartLost>& partsLost() const { return partsLost_; }
  const std::vector<Vec3>& wreckImpacts() const { return wreckImpacts_; }

  const char* backend() const { return bgfx::getRendererName(bgfx::getRendererType()); }
  bool screenshotWritten() const { return callbacks_.screenshotWritten.load(); }
  // Submits ImGui's draw data in its own view, after the world.
  void ui();
  void screenshot(const std::string& path) {
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, path.c_str());
  }

  // The navigation map picture as an ImGui texture id, or 0 before it exists.
  // ui() reads ids as a bgfx texture index plus one.
  std::uintptr_t mapTexture() const { return bgfx::isValid(mapTexture_) ? std::uintptr_t(mapTexture_.idx) + 1 : 0; }

  // Projects a world point to pixels for HUD labels. False when behind the
  // camera or off screen.
  bool projectToScreen(const Vec3& world, float& outX, float& outY, float& outDepth) const;
  // Model transform for the visual aircraft, exposed so tests can verify it.
  glm::mat4 modelTransform(const State& state) const;
  // Body-FRD -> render matrix, so tests and the HUD share one implementation.
  glm::mat4 bodyTransform(const State& state) const;

 private:
  // View ids are also the execution order.
  enum View : bgfx::ViewId {
    kViewSkyTable = 0,
    kViewAerial,
    kViewShadow0, kViewShadow1, kViewShadow2,
    kViewWorld,
    kViewCloud,
    kViewCloudResolve,
    kViewAtmosphere,
    kViewCockpit,
    kViewRefraction,
    kViewGlare,  // one view per pyramid step: kGlareLevels down, kGlareLevels - 1 up
    kViewDisplay = kViewGlare + 16,
    kViewEdgeFilter,
    kViewUi
  };
  static constexpr int kGlareLevels = 5;
  static constexpr int kMaxCascades = 3;
  // Stored scene range is metres divided by this, so a 16-bit float reaches 260 km.
  static constexpr float kRangeScale = 4.f;

  struct Programs {
    bgfx::ProgramHandle pbr{BGFX_INVALID_HANDLE}, terrain{BGFX_INVALID_HANDLE}, tree{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle sky{BGFX_INVALID_HANDLE}, skyTable{BGFX_INVALID_HANDLE}, aerial{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle clouds{BGFX_INVALID_HANDLE}, cloudResolve{BGFX_INVALID_HANDLE}, cloudComposite{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle unlit{BGFX_INVALID_HANDLE}, effect{BGFX_INVALID_HANDLE}, flame{BGFX_INVALID_HANDLE}, rain{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle shadow{BGFX_INVALID_HANDLE}, shadowTree{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle glare{BGFX_INVALID_HANDLE}, display{BGFX_INVALID_HANDLE}, edgeFilter{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle imgui{BGFX_INVALID_HANDLE};
  };
  struct Uniforms {
    Uniforms() { std::memset(this, 0xff, sizeof(*this)); }
    bgfx::UniformHandle frame, model, viewProj, invViewProj, prevViewProj, normalMatrix;
    bgfx::UniformHandle lightViewProj, shadowMatrix;
    bgfx::UniformHandle baseColor, metallicRoughness, emissive, doubleSided, normalSettings, textureFlags, alphaSettings;
    bgfx::UniformHandle surface, flame, effectParams, cloudRender, cloudResolve, postSettings, postStep, rain, rainSide;
    bgfx::UniformHandle damage, terrainMap;
    // Samplers.
    bgfx::UniformHandle shadowAtlas, baseTexture, mrTexture, emissiveTexture, normalTexture, occlusionTexture;
    bgfx::UniformHandle transmittance, skyView, aerial, multiScatter, weatherMap, noise;
    bgfx::UniformHandle terrainAlbedo, terrainNormal, landMap, lakeMap, waterNormal, heightMap;
    bgfx::UniformHandle cloudShape, cloudDetail, sceneRange, cloudLayer, cloudDepth;
    bgfx::UniformHandle cloudCurrent, cloudHistory, cloudHistoryDepth;
    bgfx::UniformHandle sceneTexture, bloomTexture, distortion;
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
    // Gap between each weapon station's store and the skin above it, metres.
    std::array<float, 8> pylonHeight{};
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
    DamageView damage;
    float damageSeed{};  // varies the pattern of holes between aircraft
  };
  // One cascade of the sun shadow atlas.
  struct Cascade {
    glm::mat4 lightViewProj{1};
    glm::mat4 atlas{1};  // world -> atlas texture coordinates and depth
    glm::vec3 center{};
    float radius{};
    float texelWorld{};
  };
  // Trees standing in one square kilometre, as a static instance buffer.
  struct TreeChunk {
    bgfx::VertexBufferHandle instances{BGFX_INVALID_HANDLE};
    std::uint32_t broadleaf{}, conifer{};  // broadleaf instances come first
    glm::vec3 center{};
    float radius{};
    std::uint64_t lastUsed{};
  };
  struct TreeMesh {
    bgfx::VertexBufferHandle vertices{BGFX_INVALID_HANDLE};
    std::uint32_t count{};
  };

  // Everything the environment needs that can be computed without a GPU. It is
  // synthesised on a background thread while the aircraft models load, so it
  // adds nothing to start-up time, and uploaded before the first frame.
  struct Synthesis {
    std::unique_ptr<AtmosphereModel> atmosphere;
    std::unique_ptr<Landscape> landscape;
    MapImage map;
    std::vector<std::uint8_t> cloudShape, cloudDetail, weatherMap, noiseTile, waterNormal;
    procedural::TerrainLayers layers;
    WeatherSettings weather;
  };
  static constexpr int kCloudShapeSize = 96, kCloudDetailSize = 32, kWeatherMapSize = 512;
  static constexpr int kNoiseTileSize = 256, kWaterTileSize = 256;

  // ---- renderer.cpp ----
  bool initialize(const Platform& platform);
  static Synthesis synthesise(const GraphicsSettings& settings);
  void finishEnvironment();
  void createPrograms();
  void createUniforms();
  void destroy();
  void updateFrameConstants(const Camera& camera, const State& local, const Weather& weather, double dt);
  // Sets the constants and lookup textures every lit draw needs.
  void bindFrame(const glm::mat4& viewProj);
  void bindLighting();
  void drawGrid();
  void drawAircraft(const Instance& instance, bool hide, bgfx::ViewId view, const glm::mat4& viewProj);
  void drawEffects(const CombatVisuals& combat);
  void createStoreMeshes();
  // Missiles and pylons; `flightDeck` draws only those on the pilot's own aircraft.
  void drawStores(const CombatVisuals& combat, const Camera& camera, bool flightDeck);
  void drawAfterburners(bool localDestroyed);
  // One engine's flame, or a rocket motor's: three nested shells and a glow.
  void drawFlame(const glm::mat4& matrix, const glm::vec3& glowCentre, float glowRadius, float intensity, float seed,
                 bool rocket);
  void drawMissilePlumes(const CombatVisuals& combat);
  // Wings and fins that have broken away, drawn from their aircraft's own mesh.
  void drawBreakaways();
  void createChuteMeshes();
  // Ejected pilots, their seats and parachutes.
  void drawPilots(const CombatVisuals& combat);
  void ensureFlameMesh();
  void applyMaterial(const Material& material, const Model* model = nullptr, float detail = 0);
  std::uint32_t resetFlags() const;
  glm::mat4 modelTransform(const State& state, AircraftType type) const;
  void fullscreenPass(bgfx::ViewId view, bgfx::ProgramHandle program, std::uint64_t state);

  // ---- renderer_atmosphere.cpp ----
  void createAtmosphereResources(Synthesis& data);
  void destroyAtmosphereResources();
  void uploadAtmosphereTables();
  void ensureSceneBuffers();
  void drawAtmosphereTables();
  void drawSky();
  void computeCascades(const Camera& camera, bool cockpit);
  void drawShadowAtlas(const std::vector<const Instance*>& casters);
  void drawClouds();
  void compositeClouds();
  void drawRain(const State& local, const Weather& weather);
  void compositeDisplay();
  bool cloudsEnabled() const;

  // ---- renderer_environment.cpp ----
  void buildEnvironment(Synthesis& data);
  void destroyEnvironment();
  void createEnvironmentTextures(Synthesis& data);
  void drawEnvironment();
  void updateTreeChunks();
  void drawTrees(bgfx::ViewId view, bgfx::ProgramHandle program, const Cascade* cascade);
  bool sphereVisible(const glm::vec3& center, float radius, float maxDistance) const;

  RenderCallbacks callbacks_{};
  bgfx::SwapChain swapChain_{};
  bool initialized_{};
  std::uint16_t width_{}, height_{};
  bgfx::VertexLayout surfaceLayout_{}, terrainLayout_{}, instanceLayout_{}, unlitLayout_{}, effectLayout_{}, uiLayout_{};
  Programs programs_{};
  Uniforms uniforms_{};
  FrameConstants frame_{};

  Vec3 origin_{}, previousOrigin_{};
  Camera lastCamera_{};
  std::map<AircraftType, Model> models_;
  AircraftType localType_{AircraftType::A320};
  std::map<std::uint64_t, Instance> instances_;
  std::uint64_t auxiliaryTextureBytes_{};
  bgfx::TextureHandle whiteTexture_{BGFX_INVALID_HANDLE};
  // Missile airframes by weapon type, detail level and part, plus the pylon.
  struct PartBuffer {
    bgfx::VertexBufferHandle vertices{BGFX_INVALID_HANDLE};
    std::uint32_t count{};
  };
  std::array<std::array<std::array<PartBuffer, 4>, 2>, 2> storeMeshes_{};
  PartBuffer pylonMesh_{};
  std::array<PartBuffer, std::size_t(ChutePart::Count)> chuteMeshes_{};
  std::vector<PartLost> partsLost_;
  std::vector<Vec3> wreckImpacts_;
  bgfx::VertexBufferHandle flameMesh_{BGFX_INVALID_HANDLE};
  unsigned flameVertices_{};
  double flameTime_{};
  std::size_t activeLod_{0};

  std::future<Synthesis> synthesis_;

  // Atmosphere and lighting.
  std::unique_ptr<AtmosphereModel> atmosphere_;
  WeatherSettings atmosphereWeather_{};
  double atmosphereRebuildTimer_{};
  AtmosphereLighting lighting_{};
  float exposure_{.36f};
  bool exposurePrimed_{};
  glm::vec3 sun_{0, 1, 0};
  bgfx::TextureHandle transmittanceTexture_{BGFX_INVALID_HANDLE}, multiScatterTexture_{BGFX_INVALID_HANDLE};
  bgfx::FrameBufferHandle skyTableBuffer_{BGFX_INVALID_HANDLE}, aerialBuffer_{BGFX_INVALID_HANDLE};

  // Scene targets.
  bgfx::FrameBufferHandle hdrBuffer_{BGFX_INVALID_HANDLE};
  bgfx::FrameBufferHandle atmosphereBuffer_{BGFX_INVALID_HANDLE};  // HDR colour + depth, without the range attachment
  bgfx::FrameBufferHandle displayBuffer_{BGFX_INVALID_HANDLE};     // tone-mapped image awaiting the edge filter
  bgfx::FrameBufferHandle refractionBuffer_{BGFX_INVALID_HANDLE};
  std::array<bgfx::FrameBufferHandle, kGlareLevels> glareDown_;
  std::array<bgfx::FrameBufferHandle, kGlareLevels> glareUp_;
  unsigned hdrWidth_{}, hdrHeight_{};
  int hdrSamples_{};
  bool sceneHasEdgeFilter_{}, sceneHasRefraction_{}, sceneHasGlare_{};

  // Clouds.
  bgfx::FrameBufferHandle cloudBuffer_{BGFX_INVALID_HANDLE};
  bgfx::FrameBufferHandle cloudHistory_[2]{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE};
  bgfx::TextureHandle cloudShape_{BGFX_INVALID_HANDLE}, cloudDetail_{BGFX_INVALID_HANDLE};
  bgfx::TextureHandle weatherMap_{BGFX_INVALID_HANDLE}, noiseTile_{BGFX_INVALID_HANDLE};
  unsigned cloudWidth_{}, cloudHeight_{};
  unsigned cloudHistoryIndex_{};
  bool cloudHistoryValid_{};
  glm::dvec2 cloudDrift_{};
  double weatherTime_{};
  std::uint64_t frameIndex_{};
  glm::mat4 previousViewProj_{1};

  // Sun shadows.
  bgfx::FrameBufferHandle shadowBuffer_{BGFX_INVALID_HANDLE};
  bgfx::TextureHandle shadowAtlas_{BGFX_INVALID_HANDLE};
  std::uint32_t shadowTileSize_{};
  std::array<Cascade, kMaxCascades> cascades_{};
  int cascadeCount_{};

  // Environment.
  std::unique_ptr<Landscape> landscape_;
  bgfx::VertexBufferHandle terrainVertices_{BGFX_INVALID_HANDLE};
  bgfx::IndexBufferHandle terrainIndices_{BGFX_INVALID_HANDLE};
  std::uint32_t terrainIndexCount_{};
  bgfx::TextureHandle terrainAlbedo_{BGFX_INVALID_HANDLE}, terrainNormal_{BGFX_INVALID_HANDLE};
  bgfx::TextureHandle landMap_{BGFX_INVALID_HANDLE}, lakeMap_{BGFX_INVALID_HANDLE}, waterNormal_{BGFX_INVALID_HANDLE};
  bgfx::TextureHandle heightMap_{BGFX_INVALID_HANDLE};
  int heightMapLevels_{};
  bgfx::TextureHandle mapTexture_{BGFX_INVALID_HANDLE};
  // The airfield (airfield.hpp): layers of paving and paint drawn into the
  // terrain, and one batch of solid structure per material.
  struct GroundLayer {
    bgfx::VertexBufferHandle buffer{BGFX_INVALID_HANDLE};
    float kind{}, layer{};
    glm::vec3 tint{1};
  };
  struct StructureBatch {
    bgfx::VertexBufferHandle buffer{BGFX_INVALID_HANDLE};
    AirfieldMaterial material{AirfieldMaterial::Concrete};
    std::uint32_t vertices{};
  };
  std::vector<GroundLayer> airfieldGround_;
  std::vector<StructureBatch> airfieldParts_;
  // Village buildings, in two batches: walls and roofs.
  bgfx::VertexBufferHandle houses_{BGFX_INVALID_HANDLE}, roofs_{BGFX_INVALID_HANDLE};
  std::uint32_t houseVertices_{}, roofVertices_{};
  // Tree meshes: [species][0 near, 1 far].
  TreeMesh treeMeshes_[2][2]{};
  std::map<std::pair<int, int>, TreeChunk> treeChunks_;
  int treeChunkDensity_{};
  bgfx::VertexBufferHandle grid_{BGFX_INVALID_HANDLE};
  bgfx::VertexBufferHandle screenTriangle_{BGFX_INVALID_HANDLE};
  bgfx::TextureHandle font_{BGFX_INVALID_HANDLE};
  bgfx::UniformHandle uiSampler_{BGFX_INVALID_HANDLE};

  Breakaways breakaways_;
  EffectPool pool_{4096};
  CombatEffects combat_{pool_, EffectsQuality::High};
  GraphicsSettings settings_;
  Stats stats_{};
  glm::mat4 view_{1};
  glm::mat4 projection_{1};
  glm::mat4 viewProj_{1};
  glm::mat4 invViewProj_{1};
  glm::mat4 cockpitViewProj_{1};
  bool cockpitPass_{};
  glm::vec3 cameraEye_{};
  glm::vec3 cameraForward_{0, 0, -1};
  glm::vec3 basisRight_{1, 0, 0};
  glm::vec3 basisUp_{0, 1, 0};
  float cameraFovDeg_{60}, cameraNear_{2.5f};
  double fpsTimer_{};
  std::uint32_t fpsFrames_{};
  std::vector<float> effectScratch_;
};

}  // namespace ofs::client
