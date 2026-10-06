#pragma once
// Graphics settings: a small persisted configuration plus the ImGui panel that
// edits it. Deliberately lightweight - a config file and one panel, not a
// settings frontend.

#include <cstdint>
#include <string>

namespace ofs::client {

enum class ShadowQuality : std::uint8_t { Off = 0, Low = 1, Medium = 2, High = 3 };
enum class EffectsQuality : std::uint8_t { Off = 0, Low = 1, Medium = 2, High = 3 };
enum class CloudQuality : std::uint8_t { Off = 0, Low = 1, Medium = 2, High = 3 };

// Atmosphere and lighting parameters, all in renderer units.
struct SkySettings {
  // Sun direction as a unit vector pointing from the sun toward the scene.
  float sunElevationDeg{38};
  float sunAzimuthDeg{125};
  float sunIntensity{3.8};
  float zenithR{0.055f}, zenithG{0.18f}, zenithB{0.42f};
  float horizonR{0.62f}, horizonG{0.75f}, horizonB{0.92f};
  float groundR{0.30f}, groundG{0.33f}, groundB{0.28f};
  float horizonSharpness{0.62f};
  float groundBlend{0.14f};
  float skyAmbientR{0.20f}, skyAmbientG{0.27f}, skyAmbientB{0.40f};
  float groundAmbientR{0.10f}, groundAmbientG{0.10f}, groundAmbientB{0.08f};
  float exposure{1.0f};
};

struct FogSettings {
  float density{0.000035f};
  float heightFalloff{900.0f};
  float groundFade{0.55f};
  float colorR{0.62f}, colorG{0.72f}, colorB{0.85f};
  bool enabled{true};
};

struct GraphicsSettings {
  bool vsync{true};
  // Requested MSAA sample count. Clamped to what the backend reports.
  int msaaSamples{4};
  bool fullscreen{};
  int windowWidth{1280};
  int windowHeight{800};
  float renderDistance{24000.0f};
  // Keep adequate depth precision across the airfield.
  float nearPlane{0.5f};
  float cockpitFov{70.f}; // Vertical degrees; physical eye point is independent.
  bool bloom{true};
  float bloomStrength{.14f};
  int textureMaxSize{2048}; // Per-map limit, applied at asset upload.
  bool anisotropic{true};
  float lodBias{0.f}; // Log2 distance bias; positive selects cheaper geometry.
  CloudQuality clouds{CloudQuality::High};
  float cloudCoverage{.48f}, cloudBase{1800.f}, cloudThickness{950.f};
  bool cloudShadows{true};
  bool vegetation{true};
  float sceneryDistance{9000.f};

  ShadowQuality shadows{ShadowQuality::High};
  EffectsQuality effects{EffectsQuality::High};
  int shadowMapSize{2048};
  // Shadow map covers a box of this half-extent, metres, around the camera.
  float shadowExtent{160.0f};
  float shadowBias{0.0016f};
  float shadowStrength{0.82f};

  bool showHud{true};
  bool showPlayerLabels{true};
  float playerLabelMaxDistance{6000.0f};
  bool showDebugGrid{false};
  bool showPhysicsGeometry{false};
  bool showDevOverlay{false};
  bool contrails{true};
  bool wingVapor{true};
  float relativeHumidity{.75f}; // local visual atmosphere, 0..1

  bool engineHeat{true};
  bool wireframeAircraft{false};

  SkySettings sky;
  FogSettings fog;

  // Persistent key/value settings, in the same style as the existing build.
  std::string configPath;
  // Reads the file if present; a missing file is not an error.
  void load(const std::string& path);
  // Writes the file. Failures are reported, never fatal.
  bool save() const;
};

// Builds the sun's world direction from elevation and azimuth, pointing from
// the sun toward the scene (the shader convention).
void sunDirection(const SkySettings&, float outDirection[3]);

}  // namespace ofs::client
