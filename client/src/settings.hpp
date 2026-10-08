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
enum class TerrainQuality : std::uint8_t { Low = 0, Medium = 1, High = 2 };
// A preset sets every cost-related option at once; Custom means the options
// were edited individually afterwards.
enum class GraphicsPreset : std::uint8_t { Low = 0, Medium = 1, High = 2, Ultra = 3, Custom = 4 };

// Time of day and camera exposure. Sky colour, sunlight and ambient light are
// all derived from the sun's position by the atmosphere model.
struct SkySettings {
  float sunElevationDeg{38};
  float sunAzimuthDeg{125};
  // Follows this computer's clock and calendar: the two angles above are then
  // set from the local time of day instead of by hand.
  bool realTime{false};
  // Meters the scene like a camera. When off, the compensation alone sets the
  // exposure relative to a clear-noon reference.
  bool autoExposure{true};
  float exposureCompensation{0.f};  // photographic stops
};

// The air itself, in meteorological terms.
struct WeatherSettings {
  float visibilityKm{70.f};   // visual range at sea level in clear air
  float fogDensity{0.f};      // 0 none .. 1 dense ground fog
  float fogHeight{180.f};     // metres; scale height of the fog layer
  float precipitation{0.f};   // 0 dry .. 1 heavy rain
};

struct GraphicsSettings {
  GraphicsPreset preset{GraphicsPreset::High};

  bool vsync{true};
  // Requested MSAA sample count. Clamped to what the backend reports.
  int msaaSamples{4};
  // Edge filter on the final image, for what MSAA cannot reach: very bright
  // edges, foliage and shader detail.
  bool fxaa{true};
  bool fullscreen{};
  int windowWidth{1280};
  int windowHeight{800};
  // How far terrain and haze are drawn, metres.
  float renderDistance{160000.0f};
  // Near plane for exterior views; the cockpit is drawn in its own depth range.
  float nearPlane{0.5f};
  float cockpitFov{70.f}; // Vertical degrees; physical eye point is independent.
  bool bloom{true};
  // Fraction of light scattered into glare around bright sources.
  float bloomStrength{.045f};
  int textureMaxSize{2048}; // Per-map limit, applied at asset upload.
  bool anisotropic{true};
  float lodBias{0.f}; // Log2 distance bias; positive selects cheaper geometry.

  CloudQuality clouds{CloudQuality::High};
  float cloudCoverage{.42f}, cloudBase{1500.f}, cloudThickness{1500.f};
  float cirrusCoverage{.35f};
  bool cloudShadows{true};

  TerrainQuality terrain{TerrainQuality::High};
  bool water{true};
  // Long shadows cast by hills and mountains at low sun.
  bool terrainShadows{true};
  bool vegetation{true};
  float sceneryDistance{7000.f};  // trees are drawn to this range, metres
  int treeDensity{650};           // candidate trees per square kilometre

  ShadowQuality shadows{ShadowQuality::High};
  // Sun shadows are drawn out to this range from the camera, metres.
  float shadowDistance{1600.0f};
  float shadowStrength{1.0f};

  EffectsQuality effects{EffectsQuality::High};
  // Refraction through hot exhaust.
  bool heatDistortion{true};

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

  // Pointer flying: the mouse places an aim point and the aircraft is flown to
  // it. Keyboard and gamepad deflections still override their own axes.
  bool mouseAim{false};
  float mouseAimSensitivity{1.f};
  // The pursuit camera widens its view while gaining speed and narrows it while slowing.
  bool dynamicFov{true};
  bool showMinimap{true};

  // Sound. Each volume is 0..1; the master applies on top of the other four.
  bool sound{true};
  float soundVolume{.8f};
  float engineVolume{1.f}, weaponVolume{1.f}, airframeVolume{1.f}, cockpitVolume{1.f};
  // Keeps playing while another window has the keyboard.
  bool soundInBackground{false};

  SkySettings sky;
  WeatherSettings weather;

  // Persistent key/value settings, in the same style as the existing build.
  std::string configPath;
  // Reads the file if present; a missing file is not an error.
  void load(const std::string& path);
  // Writes the file. Failures are reported, never fatal.
  bool save() const;
  // Sets every cost-related option to the preset's value. Scene choices (time
  // of day, weather, cloud cover) and display options are left alone.
  void applyPreset(GraphicsPreset preset);
};

// Unit vector from the scene toward the sun, in the render frame (+X east,
// +Y up, +Z south).
void sunDirection(const SkySettings&, float outDirection[3]);

// Where the sun stands at a local solar time, for a day of the year (1..366)
// and a latitude in degrees. Elevation above the horizon and azimuth clockwise
// from north, both in degrees.
void sunPosition(double hour, int dayOfYear, double latitudeDeg, float& elevationDeg, float& azimuthDeg);
// Sets the sun from this computer's clock when the sky follows real time.
void applyRealTime(SkySettings&);

}  // namespace ofs::client
