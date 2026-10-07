#include "settings.hpp"

#include "log.hpp"
#include "ofs/math.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

namespace ofs::client {
namespace {

float toFloat(std::string_view text, float fallback) {
  if (text.empty()) return fallback;
  const std::string copy(text);
  char* end = nullptr;
  const float value = std::strtof(copy.c_str(), &end);
  return end == copy.c_str() || *end != '\0' || !std::isfinite(value) ? fallback : value;
}

int toInt(std::string_view text, int fallback) {
  if (text.empty()) return fallback;
  const std::string copy(text);
  char* end = nullptr;
  const long value = std::strtol(copy.c_str(), &end, 10);
  return end == copy.c_str() || *end != '\0' ? fallback : static_cast<int>(value);
}

bool toBool(std::string_view text, bool fallback) {
  if (text == "0" || text == "false") return false;
  if (text == "1" || text == "true") return true;
  return fallback;
}

// The file is parsed once into a key/value table. Scanning a shared stream per
// key would leave it positioned mid-file after the first hit, so every later key
// would silently miss; a table makes every lookup independent of file order.
using ConfigTable = std::map<std::string, std::string, std::less<>>;

std::string_view trim(std::string_view text) {
  const auto isSpace = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  while (!text.empty() && isSpace(text.front())) text.remove_prefix(1);
  while (!text.empty() && isSpace(text.back())) text.remove_suffix(1);
  return text;
}

ConfigTable parseConfig(std::istream& file) {
  ConfigTable table;
  std::string line;
  while (std::getline(file, line)) {
    const std::string_view view = trim(line);
    // A comment or a blank line carries no setting.
    if (view.empty() || view.front() == '#') continue;
    const std::size_t equals = view.find('=');
    if (equals == std::string_view::npos) continue;
    const std::string_view key = trim(view.substr(0, equals));
    if (key.empty()) continue;
    table[std::string(key)] = std::string(trim(view.substr(equals + 1)));
  }
  return table;
}

template <typename T>
void read(const ConfigTable& table, std::string_view key, T& target) {
  const auto entry = table.find(key);
  if (entry == table.end()) return;
  const std::string_view value(entry->second);
  if constexpr (std::is_same_v<T, float>) {
    target = toFloat(value, target);
  } else if constexpr (std::is_same_v<T, int>) {
    target = toInt(value, target);
  } else if constexpr (std::is_same_v<T, bool>) {
    target = toBool(value, target);
  }
}

template <typename T>
void write(std::ostream& stream, std::string_view key, const T& value) {
  if constexpr (std::is_same_v<T, float>) {
    stream << key << '=' << value << '\n';
  } else if constexpr (std::is_same_v<T, bool>) {
    stream << key << '=' << (value ? 1 : 0) << '\n';
  } else {
    stream << key << '=' << value << '\n';
  }
}

// Clamps in place and returns the clamped value, so it can be used inline.
template <typename T>
T clampSetting(T value, T low, T high) {
  if (!std::isfinite(static_cast<double>(value))) return low;
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

}  // namespace

void sunDirection(const SkySettings& sky, float out[3]) {
  // Elevation is measured above the horizon; azimuth is measured clockwise from
  // north. Converted to the render frame (+X east, +Y up, +Z south): north is
  // -Z, so a sun in the north has a negative Z component.
  constexpr double kDeg2Rad = kPi / 180.0;
  const double elevation = sky.sunElevationDeg * kDeg2Rad;
  const double azimuth = sky.sunAzimuthDeg * kDeg2Rad;
  out[0] = static_cast<float>(std::sin(azimuth) * std::cos(elevation));
  out[1] = static_cast<float>(std::sin(elevation));
  out[2] = static_cast<float>(-std::cos(azimuth) * std::cos(elevation));
}

void GraphicsSettings::applyPreset(GraphicsPreset chosen) {
  preset = chosen;
  switch (chosen) {
    case GraphicsPreset::Low:
      msaaSamples = 1; fxaa = true; textureMaxSize = 1024; lodBias = 1.5f;
      shadows = ShadowQuality::Off; shadowDistance = 800.f;
      effects = EffectsQuality::Low; heatDistortion = false; bloom = false;
      clouds = CloudQuality::Low; cloudShadows = false;
      terrain = TerrainQuality::Low; terrainShadows = false; water = true;
      vegetation = true; treeDensity = 250; sceneryDistance = 3500.f;
      renderDistance = 80000.f;
      break;
    case GraphicsPreset::Medium:
      msaaSamples = 2; fxaa = true; textureMaxSize = 2048; lodBias = .5f;
      shadows = ShadowQuality::Medium; shadowDistance = 1200.f;
      effects = EffectsQuality::Medium; heatDistortion = true; bloom = true;
      clouds = CloudQuality::Medium; cloudShadows = true;
      terrain = TerrainQuality::Medium; terrainShadows = false; water = true;
      vegetation = true; treeDensity = 450; sceneryDistance = 5500.f;
      renderDistance = 120000.f;
      break;
    case GraphicsPreset::High:
      msaaSamples = 4; fxaa = true; textureMaxSize = 2048; lodBias = 0.f;
      shadows = ShadowQuality::High; shadowDistance = 1600.f;
      effects = EffectsQuality::High; heatDistortion = true; bloom = true;
      clouds = CloudQuality::High; cloudShadows = true;
      terrain = TerrainQuality::High; terrainShadows = true; water = true;
      vegetation = true; treeDensity = 650; sceneryDistance = 7000.f;
      renderDistance = 160000.f;
      break;
    case GraphicsPreset::Ultra:
      msaaSamples = 8; fxaa = true; textureMaxSize = 4096; lodBias = -.5f;
      shadows = ShadowQuality::High; shadowDistance = 2400.f;
      effects = EffectsQuality::High; heatDistortion = true; bloom = true;
      clouds = CloudQuality::High; cloudShadows = true;
      terrain = TerrainQuality::High; terrainShadows = true; water = true;
      vegetation = true; treeDensity = 900; sceneryDistance = 10000.f;
      renderDistance = 220000.f;
      break;
    case GraphicsPreset::Custom:
      break;
  }
}

void GraphicsSettings::load(const std::string& path) {
  configPath = path;
  std::ifstream file(path);
  if (!file) return;  // First run: defaults are the configuration.
  const ConfigTable table = parseConfig(file);

  // Keys whose meaning changed with the physically based renderer have new
  // names, so a file written by an earlier build (or an earlier launcher) keeps
  // its still-valid choices and silently falls back to defaults for the rest.
  int presetIndex = static_cast<int>(preset);
  read(table, "preset", presetIndex);
  read(table, "vsync", vsync);
  read(table, "msaa", msaaSamples);
  read(table, "fxaa", fxaa);
  read(table, "fullscreen", fullscreen);
  read(table, "width", windowWidth);
  read(table, "height", windowHeight);
  read(table, "drawDistance", renderDistance);
  read(table, "nearPlane", nearPlane);
  read(table, "cockpitFov", cockpitFov);
  read(table, "bloom", bloom);
  read(table, "glare", bloomStrength);
  read(table, "textureMaxSize", textureMaxSize);
  read(table, "anisotropic", anisotropic);
  read(table, "lodBias", lodBias);
  int cloudQuality = static_cast<int>(clouds);
  read(table, "clouds", cloudQuality);
  read(table, "cloudCoverage", cloudCoverage);
  read(table, "cloudBase", cloudBase);
  read(table, "cloudThickness", cloudThickness);
  read(table, "cirrusCoverage", cirrusCoverage);
  read(table, "cloudShadows", cloudShadows);
  int terrainQuality = static_cast<int>(terrain);
  read(table, "terrain", terrainQuality);
  read(table, "water", water);
  read(table, "terrainShadows", terrainShadows);
  read(table, "vegetation", vegetation);
  read(table, "sceneryDistance", sceneryDistance);
  read(table, "treeDensity", treeDensity);
  int shadowQuality = static_cast<int>(shadows);
  read(table, "shadows", shadowQuality);
  int effectsLevel = static_cast<int>(effects);
  read(table, "effects", effectsLevel);
  read(table, "shadowDistance", shadowDistance);
  read(table, "shadowStrength", shadowStrength);
  read(table, "heatDistortion", heatDistortion);
  read(table, "hud", showHud);
  read(table, "playerLabels", showPlayerLabels);
  read(table, "playerLabelMaxDistance", playerLabelMaxDistance);
  read(table, "debugGrid", showDebugGrid);
  read(table, "physicsGeometry", showPhysicsGeometry);
  read(table, "devOverlay", showDevOverlay);
  read(table, "contrails", contrails);
  read(table, "wingVapor", wingVapor);
  read(table, "relativeHumidity", relativeHumidity);
  read(table, "engineHeat", engineHeat);
  read(table, "wireframeAircraft", wireframeAircraft);
  read(table, "mouseAim", mouseAim);
  read(table, "mouseAimSensitivity", mouseAimSensitivity);

  read(table, "sunElevation", sky.sunElevationDeg);
  read(table, "sunAzimuth", sky.sunAzimuthDeg);
  read(table, "autoExposure", sky.autoExposure);
  read(table, "exposureCompensation", sky.exposureCompensation);

  read(table, "visibilityKm", weather.visibilityKm);
  read(table, "fogDensity01", weather.fogDensity);
  read(table, "fogHeight", weather.fogHeight);
  read(table, "precipitation", weather.precipitation);

  // A hand-edited file must not be able to produce an unusable configuration.
  preset = static_cast<GraphicsPreset>(clampSetting(presetIndex, 0, 4));
  msaaSamples = clampSetting(msaaSamples, 1, 16);
  windowWidth = clampSetting(windowWidth, 320, 16384);
  windowHeight = clampSetting(windowHeight, 240, 16384);
  renderDistance = clampSetting(renderDistance, 40000.0f, 250000.0f);
  nearPlane = clampSetting(nearPlane, 0.02f, 20.0f);
  cockpitFov = clampSetting(cockpitFov, 40.f, 100.f);
  bloomStrength = clampSetting(bloomStrength, 0.f, .2f);
  textureMaxSize = clampSetting(textureMaxSize, 512, 8192);
  lodBias = clampSetting(lodBias, -2.f, 2.f);
  clouds = static_cast<CloudQuality>(clampSetting(cloudQuality, 0, 3));
  cloudCoverage = clampSetting(cloudCoverage, 0.f, 1.f);
  cloudBase = clampSetting(cloudBase, 300.f, 8000.f);
  cloudThickness = clampSetting(cloudThickness, 300.f, 4000.f);
  cirrusCoverage = clampSetting(cirrusCoverage, 0.f, 1.f);
  terrain = static_cast<TerrainQuality>(clampSetting(terrainQuality, 0, 2));
  sceneryDistance = clampSetting(sceneryDistance, 1000.f, 15000.f);
  treeDensity = clampSetting(treeDensity, 50, 1500);
  shadowDistance = clampSetting(shadowDistance, 200.0f, 5000.0f);
  shadowStrength = clampSetting(shadowStrength, 0.0f, 1.0f);
  playerLabelMaxDistance = clampSetting(playerLabelMaxDistance, 100.0f, 100000.0f);
  relativeHumidity = clampSetting(relativeHumidity, 0.f, 1.f);
  mouseAimSensitivity = clampSetting(mouseAimSensitivity, .2f, 3.f);
  sky.sunElevationDeg = clampSetting(sky.sunElevationDeg, -10.0f, 89.0f);
  sky.exposureCompensation = clampSetting(sky.exposureCompensation, -4.0f, 4.0f);
  weather.visibilityKm = clampSetting(weather.visibilityKm, 2.0f, 300.0f);
  weather.fogDensity = clampSetting(weather.fogDensity, 0.0f, 1.0f);
  weather.fogHeight = clampSetting(weather.fogHeight, 20.0f, 2000.0f);
  weather.precipitation = clampSetting(weather.precipitation, 0.0f, 1.0f);
  shadows = static_cast<ShadowQuality>(clampSetting(shadowQuality, 0, 3));
  effects = static_cast<EffectsQuality>(clampSetting(effectsLevel, 0, 3));
}

bool GraphicsSettings::save() const {
  if (configPath.empty()) return false;
  std::ofstream file(configPath);
  if (!file) {
    log("SETTINGS", "Cannot write " + configPath);
    return false;
  }
  file << "# OpenFlightSim graphics settings\n";
  write(file, "preset", static_cast<int>(preset));
  write(file, "vsync", vsync);
  write(file, "msaa", msaaSamples);
  write(file, "fxaa", fxaa);
  write(file, "fullscreen", fullscreen);
  write(file, "width", windowWidth);
  write(file, "height", windowHeight);
  write(file, "drawDistance", renderDistance);
  write(file, "nearPlane", nearPlane);
  write(file, "cockpitFov", cockpitFov);
  write(file, "bloom", bloom);
  write(file, "glare", bloomStrength);
  write(file, "textureMaxSize", textureMaxSize);
  write(file, "anisotropic", anisotropic);
  write(file, "lodBias", lodBias);
  write(file, "clouds", static_cast<int>(clouds));
  write(file, "cloudCoverage", cloudCoverage);
  write(file, "cloudBase", cloudBase);
  write(file, "cloudThickness", cloudThickness);
  write(file, "cirrusCoverage", cirrusCoverage);
  write(file, "cloudShadows", cloudShadows);
  write(file, "terrain", static_cast<int>(terrain));
  write(file, "water", water);
  write(file, "terrainShadows", terrainShadows);
  write(file, "vegetation", vegetation);
  write(file, "sceneryDistance", sceneryDistance);
  write(file, "treeDensity", treeDensity);
  write(file, "shadows", static_cast<int>(shadows));
  write(file, "effects", static_cast<int>(effects));
  write(file, "shadowDistance", shadowDistance);
  write(file, "shadowStrength", shadowStrength);
  write(file, "heatDistortion", heatDistortion);
  write(file, "hud", showHud);
  write(file, "playerLabels", showPlayerLabels);
  write(file, "playerLabelMaxDistance", playerLabelMaxDistance);
  write(file, "debugGrid", showDebugGrid);
  write(file, "physicsGeometry", showPhysicsGeometry);
  write(file, "devOverlay", showDevOverlay);
  write(file, "contrails", contrails);
  write(file, "wingVapor", wingVapor);
  write(file, "relativeHumidity", relativeHumidity);
  write(file, "engineHeat", engineHeat);
  write(file, "wireframeAircraft", wireframeAircraft);
  write(file, "mouseAim", mouseAim);
  write(file, "mouseAimSensitivity", mouseAimSensitivity);

  write(file, "sunElevation", sky.sunElevationDeg);
  write(file, "sunAzimuth", sky.sunAzimuthDeg);
  write(file, "autoExposure", sky.autoExposure);
  write(file, "exposureCompensation", sky.exposureCompensation);

  write(file, "visibilityKm", weather.visibilityKm);
  write(file, "fogDensity01", weather.fogDensity);
  write(file, "fogHeight", weather.fogHeight);
  write(file, "precipitation", weather.precipitation);
  return static_cast<bool>(file);
}

}  // namespace ofs::client
