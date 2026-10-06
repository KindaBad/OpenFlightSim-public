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
  // north. Converted to the render frame (+X east, +Y up, +Z south) and then
  // negated so it points from the sun toward the scene.
  constexpr double kDeg2Rad = kPi / 180.0;
  const double elevation = sky.sunElevationDeg * kDeg2Rad;
  const double azimuth = sky.sunAzimuthDeg * kDeg2Rad;
  const double east = std::sin(azimuth) * std::cos(elevation);
  const double up = std::sin(elevation);
  const double south = std::cos(azimuth) * std::cos(elevation);
  const double length = std::sqrt(east * east + up * up + south * south);
  const double scale = length > 1e-9 ? 1.0 / length : 0.0;
  out[0] = static_cast<float>(-east * scale);
  out[1] = static_cast<float>(-up * scale);
  out[2] = static_cast<float>(-south * scale);
}

void GraphicsSettings::load(const std::string& path) {
  configPath = path;
  std::ifstream file(path);
  if (!file) return;  // First run: defaults are the configuration.
  const ConfigTable table = parseConfig(file);

  read(table, "vsync", vsync);
  read(table, "msaa", msaaSamples);
  read(table, "fullscreen", fullscreen);
  read(table, "width", windowWidth);
  read(table, "height", windowHeight);
  read(table, "renderDistance", renderDistance);
  read(table, "nearPlane", nearPlane);
  read(table,"cockpitFov",cockpitFov);
  read(table,"bloom",bloom);read(table,"bloomStrength",bloomStrength);
  read(table,"textureMaxSize",textureMaxSize);read(table,"anisotropic",anisotropic);
  read(table,"lodBias",lodBias);
  int cloudQuality = static_cast<int>(clouds);
  read(table,"clouds",cloudQuality);
  read(table,"cloudCoverage",cloudCoverage); read(table,"cloudBase",cloudBase);
  read(table,"cloudThickness",cloudThickness); read(table,"cloudShadows",cloudShadows);
  read(table,"vegetation",vegetation); read(table,"sceneryDistance",sceneryDistance);
  int shadowQuality = static_cast<int>(shadows);
  read(table, "shadows", shadowQuality);
  int effectsLevel = static_cast<int>(effects);
  read(table, "effects", effectsLevel);
  read(table, "shadowMapSize", shadowMapSize);
  read(table, "shadowExtent", shadowExtent);
  read(table, "shadowBias", shadowBias);
  read(table, "shadowStrength", shadowStrength);
  read(table, "hud", showHud);
  read(table, "playerLabels", showPlayerLabels);
  read(table, "playerLabelMaxDistance", playerLabelMaxDistance);
  read(table, "debugGrid", showDebugGrid);
  read(table, "physicsGeometry", showPhysicsGeometry);
  read(table, "devOverlay", showDevOverlay);
  read(table, "contrails", contrails);
  read(table, "wingVapor", wingVapor);
  read(table, "relativeHumidity", relativeHumidity);
  relativeHumidity = clampSetting(relativeHumidity, 0.f, 1.f);
  read(table, "engineHeat", engineHeat);
  read(table, "wireframeAircraft", wireframeAircraft);

  read(table, "sunElevation", sky.sunElevationDeg);
  read(table, "sunAzimuth", sky.sunAzimuthDeg);
  read(table, "sunIntensity", sky.sunIntensity);
  read(table, "zenithR", sky.zenithR); read(table, "zenithG", sky.zenithG);
  read(table, "zenithB", sky.zenithB);
  read(table, "horizonR", sky.horizonR); read(table, "horizonG", sky.horizonG);
  read(table, "horizonB", sky.horizonB);
  read(table, "groundR", sky.groundR); read(table, "groundG", sky.groundG);
  read(table, "groundB", sky.groundB);
  read(table, "horizonSharpness", sky.horizonSharpness);
  read(table, "groundBlend", sky.groundBlend);
  read(table, "skyAmbientR", sky.skyAmbientR);
  read(table, "skyAmbientG", sky.skyAmbientG);
  read(table, "skyAmbientB", sky.skyAmbientB);
  read(table, "groundAmbientR", sky.groundAmbientR);
  read(table, "groundAmbientG", sky.groundAmbientG);
  read(table, "groundAmbientB", sky.groundAmbientB);
  read(table, "exposure", sky.exposure);

  read(table, "fog", fog.enabled);
  read(table, "fogDensity", fog.density);
  read(table, "fogHeightFalloff", fog.heightFalloff);
  read(table, "fogGroundFade", fog.groundFade);
  read(table, "fogColorR", fog.colorR); read(table, "fogColorG", fog.colorG);
  read(table, "fogColorB", fog.colorB);

  // A hand-edited file must not be able to produce an unusable configuration.
  msaaSamples = clampSetting(msaaSamples, 1, 16);
  windowWidth = clampSetting(windowWidth, 320, 16384);
  windowHeight = clampSetting(windowHeight, 240, 16384);
  renderDistance = clampSetting(renderDistance, 500.0f, 120000.0f);
  nearPlane = clampSetting(nearPlane, 0.02f, 20.0f);
  cockpitFov=clampSetting(cockpitFov,40.f,100.f);
  bloomStrength=clampSetting(bloomStrength,0.f,.4f);
  textureMaxSize=clampSetting(textureMaxSize,512,8192);
  lodBias=clampSetting(lodBias,-2.f,2.f);
  clouds = static_cast<CloudQuality>(clampSetting(cloudQuality,0,3));
  cloudCoverage=clampSetting(cloudCoverage,0.f,1.f);
  cloudBase=clampSetting(cloudBase,500.f,10000.f);
  cloudThickness=clampSetting(cloudThickness,200.f,3000.f);
  sceneryDistance=clampSetting(sceneryDistance,1000.f,15000.f);
  shadowMapSize = clampSetting(shadowMapSize, 512, 4096);
  shadowExtent = clampSetting(shadowExtent, 30.0f, 2000.0f);
  shadowBias = clampSetting(shadowBias, 0.00005f, 0.05f);
  shadowStrength = clampSetting(shadowStrength, 0.0f, 1.0f);
  playerLabelMaxDistance = clampSetting(playerLabelMaxDistance, 100.0f, 100000.0f);
  sky.sunElevationDeg = clampSetting(sky.sunElevationDeg, -5.0f, 89.0f);
  sky.sunIntensity = clampSetting(sky.sunIntensity, 0.0f, 64.0f);
  sky.exposure = clampSetting(sky.exposure, 0.05f, 8.0f);
  fog.density = clampSetting(fog.density, 0.0f, 0.01f);
  fog.heightFalloff = clampSetting(fog.heightFalloff, 50.0f, 20000.0f);
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
  write(file, "vsync", vsync);
  write(file, "msaa", msaaSamples);
  write(file, "fullscreen", fullscreen);
  write(file, "width", windowWidth);
  write(file, "height", windowHeight);
  write(file, "renderDistance", renderDistance);
  write(file, "nearPlane", nearPlane);
  write(file,"cockpitFov",cockpitFov);
  write(file,"bloom",bloom);write(file,"bloomStrength",bloomStrength);
  write(file,"textureMaxSize",textureMaxSize);write(file,"anisotropic",anisotropic);
  write(file,"lodBias",lodBias);
  write(file,"clouds",static_cast<int>(clouds));
  write(file,"cloudCoverage",cloudCoverage); write(file,"cloudBase",cloudBase);
  write(file,"cloudThickness",cloudThickness); write(file,"cloudShadows",cloudShadows);
  write(file,"vegetation",vegetation); write(file,"sceneryDistance",sceneryDistance);
  write(file, "shadows", static_cast<int>(shadows));
  write(file, "effects", static_cast<int>(effects));
  write(file, "shadowMapSize", shadowMapSize);
  write(file, "shadowExtent", shadowExtent);
  write(file, "shadowBias", shadowBias);
  write(file, "shadowStrength", shadowStrength);
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

  write(file, "sunElevation", sky.sunElevationDeg);
  write(file, "sunAzimuth", sky.sunAzimuthDeg);
  write(file, "sunIntensity", sky.sunIntensity);
  write(file, "zenithR", sky.zenithR); write(file, "zenithG", sky.zenithG);
  write(file, "zenithB", sky.zenithB);
  write(file, "horizonR", sky.horizonR); write(file, "horizonG", sky.horizonG);
  write(file, "horizonB", sky.horizonB);
  write(file, "groundR", sky.groundR); write(file, "groundG", sky.groundG);
  write(file, "groundB", sky.groundB);
  write(file, "horizonSharpness", sky.horizonSharpness);
  write(file, "groundBlend", sky.groundBlend);
  write(file, "skyAmbientR", sky.skyAmbientR);
  write(file, "skyAmbientG", sky.skyAmbientG);
  write(file, "skyAmbientB", sky.skyAmbientB);
  write(file, "groundAmbientR", sky.groundAmbientR);
  write(file, "groundAmbientG", sky.groundAmbientG);
  write(file, "groundAmbientB", sky.groundAmbientB);
  write(file, "exposure", sky.exposure);

  write(file, "fog", fog.enabled);
  write(file, "fogDensity", fog.density);
  write(file, "fogHeightFalloff", fog.heightFalloff);
  write(file, "fogGroundFade", fog.groundFade);
  write(file, "fogColorR", fog.colorR);
  write(file, "fogColorG", fog.colorG);
  write(file, "fogColorB", fog.colorB);
  return static_cast<bool>(file);
}

}  // namespace ofs::client
