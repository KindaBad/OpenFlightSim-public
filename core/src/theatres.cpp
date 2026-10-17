#include "ofs/theatres.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ofs {
namespace {

constexpr std::array<TheatreAirport, 6> kAustria{{
    {"LOWW", "Vienna", 48.110298, 16.5697},
    {"LOWS", "Salzburg", 47.793301, 13.0043},
    {"LOWI", "Innsbruck", 47.260201, 11.344},
    {"LOWG", "Graz", 46.9911, 15.4396},
    {"LOWL", "Linz", 48.235362, 14.188128},
    {"LOWK", "Klagenfurt", 46.642502, 14.3377},
}};
// The western Alps: Switzerland, the Aosta and Po side of Italy and Savoy.
constexpr std::array<TheatreAirport, 7> kAlps{{
    {"LSGG", "Geneva", 46.238098, 6.10895},
    {"LSZH", "Zurich", 47.458056, 8.548056},
    {"LSGS", "Sion", 46.219166, 7.326944},
    {"LIMC", "Milan Malpensa", 45.6306, 8.72811},
    {"LIMF", "Turin", 45.200802, 7.64963},
    {"LFLS", "Grenoble", 45.3629, 5.32937},
    {"LFLB", "Chambery", 45.6381, 5.88023},
}};
constexpr std::array<TheatreAirport, 8> kGulf{{
    {"OMDB", "Dubai", 25.24979, 55.370992},
    {"OMAA", "Abu Dhabi", 24.440966, 54.649237},
    {"OMSJ", "Sharjah", 25.3286, 55.5172},
    {"OTHH", "Doha", 25.273056, 51.608056},
    {"OBBI", "Bahrain", 26.267295, 50.63764},
    {"OEDF", "Dammam", 26.4691, 49.798209},
    {"OIKB", "Bandar Abbas", 27.2183, 56.3778},
    {"OIBB", "Bushehr", 28.9447994232, 50.8345985413},
}};
constexpr std::array<Theatre, 3> kTheatres{{
    {"austria", "Austria", 47.6, 13.8, kAustria},
    {"alps", "Western Alps", 46.1, 7.4, kAlps},
    {"gulf", "Persian Gulf", 26.2, 53.0, kGulf},
}};

}  // namespace

std::span<const Theatre> theatres() { return kTheatres; }

const Theatre* findTheatre(std::string_view id) {
  for (const auto& theatre : kTheatres)
    if (theatre.id == id) return &theatre;
  return nullptr;
}

const TheatreAirport* findTheatreAirport(const Theatre& theatre, std::string_view ident) {
  for (const auto& airport : theatre.airports)
    if (airport.ident == ident) return &airport;
  return nullptr;
}

std::size_t teamAirport(const Theatre& theatre, Team team) {
  if (team != Team::Blue) return 0;
  const auto& first = theatre.airports.front();
  const Vec3 home = ecefFromGeodetic({first.latitude * kDeg2Rad, first.longitude * kDeg2Rad, 0});
  std::size_t farthest = 0;
  double distance = 0;
  for (std::size_t i = 1; i < theatre.airports.size(); ++i) {
    const auto& airport = theatre.airports[i];
    const double d = (ecefFromGeodetic({airport.latitude * kDeg2Rad, airport.longitude * kDeg2Rad, 0}) - home).norm();
    if (d > distance) distance = d, farthest = i;
  }
  return farthest;
}

const EarthRunway* theatreRunway(const EarthTerrain& terrain, const TheatreAirport& airport) {
  const Vec3 place = ecefFromGeodetic({airport.latitude * kDeg2Rad, airport.longitude * kDeg2Rad, 0});
  const EarthRunway* best = nullptr;
  for (const auto& runway : terrain.runways())
    if ((runway.centre - place).norm() < 5000 && (!best || runway.length > best->length)) best = &runway;
  return best;
}

RunwayStart runwayStart(const EarthRunway& runway, double fromEnd) {
  return {runway.centre - runway.along * (runway.length * .5 - fromEnd), runway.along};
}

std::string earthRegionName(double latitudeDeg, double longitudeDeg) {
  const int south = int(std::floor(latitudeDeg / 10)) * 10, west = int(std::floor(longitudeDeg / 10)) * 10;
  char name[16];
  std::snprintf(name, sizeof name, "%c%02d%c%03d", south >= 0 ? 'N' : 'S', std::abs(south), west >= 0 ? 'E' : 'W', std::abs(west));
  return name;
}

std::vector<std::string> theatreRegions(const Theatre& theatre) {
  constexpr double reach = 4.5;
  const double east = reach / std::cos(theatre.latitude * kDeg2Rad);
  const int south = int(std::floor((theatre.latitude - reach) / 10)), north = int(std::floor((theatre.latitude + reach) / 10));
  const int west = int(std::floor((theatre.longitude - east) / 10)), far = int(std::floor((theatre.longitude + east) / 10));
  std::vector<std::string> names;
  for (int a = south; a <= north; ++a)
    for (int b = west; b <= far; ++b) names.push_back(earthRegionName(10. * a, 10. * b));
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  return names;
}

std::vector<std::string> loadEarthDetail(EarthTerrain& terrain, const std::filesystem::path& pack, const Theatre* theatre) {
  std::vector<std::filesystem::path> files;
  if (theatre) {
    for (const auto& region : theatreRegions(*theatre))
      if (const auto path = EarthTerrain::detailPath(pack, region); std::filesystem::exists(path)) files.push_back(path);
  } else if (std::error_code error; std::filesystem::is_directory(EarthTerrain::detailFolder(pack), error)) {
    for (const auto& entry : std::filesystem::directory_iterator(EarthTerrain::detailFolder(pack))) {
      const std::string name = entry.path().filename().string();
      if (name.starts_with("earth-detail-") && name.ends_with(".ofsd")) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
  }
  for (const auto& file : files) terrain.addDetail(file);
  return terrain.detailRegions();
}

}  // namespace ofs
