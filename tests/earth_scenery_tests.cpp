// The round Earth's scenery, map and sun (client/src/earth_objects.hpp,
// earth_map.hpp, settings.hpp); docs/WHOLE_EARTH_PLAN.md. The suites that need
// the terrain pack and land cover are skipped without them, as in CI.
#include "earth_map.hpp"
#include "earth_objects.hpp"
#include "earth_pattern.hpp"
#include "settings.hpp"

#include "ofs/theatres.hpp"
#include "ofs/water.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

using namespace ofs;
using namespace ofs::client;

namespace {
void check(bool ok, const std::string& what) {
  if (!ok) throw std::runtime_error(what);
}
constexpr double kDeg = kPi / 180;

std::unique_ptr<EarthTerrain> terrain;
std::unique_ptr<EarthCover> cover;

EarthNode tileAt(double latitude, double longitude) {
  const Vec3 up{std::cos(latitude * kDeg) * std::cos(longitude * kDeg), std::cos(latitude * kDeg) * std::sin(longitude * kDeg),
                std::sin(latitude * kDeg)};
  return EarthObjects::tileOf(up);
}

// The sun from the time and the place, against almanac values.
void sun() {
  const auto at = [](int year, int month, int day, int hour, int minute) {
    std::tm t{};
    t.tm_year = year - 1900, t.tm_mon = month - 1, t.tm_mday = day, t.tm_hour = hour, t.tm_min = minute;
#ifdef _WIN32
    return _mkgmtime(&t);
#else
    return timegm(&t);
#endif
  };
  float elevation, azimuth;
  // Vienna at the June solstice, local solar noon (about 10:54 UTC): 65.3 degrees, due south.
  sunPositionUtc(at(2026, 6, 21, 10, 54), 48.21, 16.37, elevation, azimuth);
  std::printf("Vienna, midsummer noon: %.2f deg, bearing %.1f\n", elevation, azimuth);
  check(std::abs(elevation - 65.3) < .4 && std::abs(azimuth - 180) < 3, "the midsummer sun over Vienna");
  // Dubai at the December solstice, 08:16 UTC (local noon): 90 - 25.25 - 23.44 = 41.3 degrees.
  sunPositionUtc(at(2026, 12, 21, 8, 16), 25.25, 55.37, elevation, azimuth);
  std::printf("Dubai, midwinter noon: %.2f deg, bearing %.1f\n", elevation, azimuth);
  check(std::abs(elevation - 41.3) < .4 && std::abs(azimuth - 180) < 3, "the midwinter sun over Dubai");
  // The sun crosses the horizon in Geneva at the equinox about 05:40 UTC (solar
  // noon 11:42 less six hours), in the east.
  sunPositionUtc(at(2026, 3, 20, 5, 40), 46.2, 6.15, elevation, azimuth);
  std::printf("Geneva, equinox sunrise: %.2f deg, bearing %.1f\n", elevation, azimuth);
  check(std::abs(elevation) < 1.5 && std::abs(azimuth - 90) < 3, "the sun rises in the east at the equinox");
}

// The C++ hash and noise give the values the shader's do for the same input.
void patterns() {
  check(earthHash(0, 0) >= 0 && earthHash(0, 0) < 1, "a hash is a fraction");
  float low = 1, high = 0;
  for (int i = 0; i < 10000; ++i) {
    const float n = earthNoise(i * .37f, i * .11f);
    low = std::min(low, n), high = std::max(high, n);
  }
  check(low >= 0 && high <= 1 && high - low > .8f, "noise spans 0..1");
  // A point on a field's edge is on its edge from either side.
  const EarthField field = earthField(1234.5f, 6789.25f);
  const EarthField edge = earthField(field.hedgeX, field.hedgeY);
  check(std::abs(edge.edge) < 1, "the nearest point of the edge is on it");
  check(earthStreetLot(3, 0) && earthStreetLot(-1, 5) && !earthStreetLot(0, 0) && !earthStreetLot(-2, 6), "streets every fourth lot");
}

// Two builds of the same tile are the same, byte for byte.
void determinism() {
  const EarthObjects first(*terrain, *cover), second(*terrain, *cover);
  for (const auto& [name, lat, lon] : {std::tuple{"Vienna", 48.2, 16.37}, std::tuple{"Innsbruck", 47.27, 11.39},
                                       std::tuple{"farmland near Wels", 48.16, 14.03}, std::tuple{"Dubai", 25.2, 55.27}}) {
    const EarthNode node = tileAt(lat, lon);
    const auto a = first.build(node, 650), b = second.build(node, 650);
    std::size_t vertices = 0;
    for (const auto& part : a.parts) vertices += part.size();
    std::printf("%-20s %6zu trees, %4u buildings, %7zu vertices\n", name, a.trees.size(), a.buildings, vertices);
    check(a.trees.size() == b.trees.size() && a.broadleaf == b.broadleaf, "the same trees");
    check(std::memcmp(a.trees.data(), b.trees.data(), a.trees.size() * sizeof(TreeInstance)) == 0, "trees in the same places");
    for (std::size_t k = 0; k < kEarthParts; ++k)
      check(a.parts[k].size() == b.parts[k].size() &&
                std::memcmp(a.parts[k].data(), b.parts[k].data(), a.parts[k].size() * sizeof(SurfaceVertex)) == 0,
            "the same buildings");
  }
  // A city is built up and farmland is not.
  const auto city = first.build(tileAt(48.2, 16.37), 650), farm = first.build(tileAt(48.16, 14.03), 650);
  check(city.buildings > 200, "Vienna is built up");
  check(farm.trees.size() > 50, "farmland has hedgerows and woods");

  // A tile's buildings are drawn as one: every vertex kept, in its part's
  // look, those that cast shadows first.
  EarthObjectTile merged = city;
  mergeEarthParts(merged);
  std::size_t total = 0, casting = 0;
  for (std::size_t k = 0; k < kEarthParts; ++k) {
    total += city.parts[k].size();
    if (earthPartCastsShadow(EarthPart(k))) casting += city.parts[k].size();
  }
  check(merged.merged.size() == total && merged.casters == casting && casting < total, "the merged buildings keep every vertex, casters first");
  check(std::all_of(merged.parts.begin(), merged.parts.end(), [](const auto& part) { return part.empty(); }), "the parts are let go");
  const auto& wall = city.parts[std::size_t(EarthPart::HouseWalls)];
  const auto found = std::find_if(merged.merged.begin(), merged.merged.end(),
                                  [&](const EarthBuildingVertex& v) { return v.x == wall[0].x && v.y == wall[0].y && v.z == wall[0].z; });
  const EarthPartLook look = earthPartLook(EarthPart::HouseWalls);
  check(found != merged.merged.end() && std::abs(float(found->color[0]) / 255 * float(found->color[0]) / 255 - look.color[0]) < .01f &&
            std::abs(float(found->look[1]) / 255 - look.roughness) < .01f,
        "a wall keeps its colour and roughness");
}

// Nothing stands on a runway, and an airport keeps its runway's strip clear.
void airports() {
  const EarthObjects objects(*terrain, *cover);
  const TheatreAirport& vienna = findTheatre("austria")->airports.front();
  const EarthRunway& runway = *theatreRunway(*terrain, vienna);
  const auto& layout = objects.airport(std::size_t(&runway - terrain->runways().data()));
  check(layout.primary && layout.kind == 3 && layout.taxiOffset > 0, "Vienna is laid out as a large airport");
  std::size_t checked = 0, buildings = 0;
  for (double t = -runway.length * .5; t <= runway.length * .5; t += 400)
    for (const double side : {-1., 1.}) {
      const Vec3 across = runway.along.cross(runway.up).normalized();
      const Vec3 point = runway.centre + runway.along * t + across * (side * 300);
      const Surface s = surfaceFromEcef(point);
      const EarthObjectTile tile = objects.build(EarthObjects::tileOf({s.cosLat * s.cosLon, s.cosLat * s.sinLon, s.sinLat}), 650);
      buildings += tile.buildings;
      for (const auto& tree : tile.trees) {
        const Vec3 at = tile.centre + tile.east * double(tree.east) + tile.up * double(tree.up) + tile.south * double(tree.south);
        const Vec3 d = at - runway.centre;
        const double along = d.dot(runway.along), off = std::abs(d.dot(across));
        check(!(std::abs(along) < runway.length * .5 && off < runway.width * .5 + 80), "no tree on the runway's strip");
        ++checked;
      }
      for (const auto& part : tile.parts)
        for (const auto& v : part) {
          const Vec3 at = tile.centre + tile.east * double(v.x) + tile.up * double(v.y) + tile.south * double(v.z);
          const Vec3 d = at - runway.centre;
          check(!(std::abs(d.dot(runway.along)) < runway.length * .5 && std::abs(d.dot(across)) < runway.width * .5 + 40),
                "no building on the runway");
        }
    }
  std::printf("%zu trees and %zu buildings checked about Vienna's runway %.0f m\n", checked, buildings, runway.length);
  check(buildings > 0, "the airport has buildings");
}

// The map shows water on a lake and land beside it.
void map() {
  const EarthMapView view{46.45 * kDeg, 6.55 * kDeg, 20000};
  const EarthMapImage image = buildEarthMap(*terrain, *cover, view, 128);
  const auto pixel = [&](double lat, double lon) {
    double east, north;
    view.offsets(lat * kDeg, lon * kDeg, east, north);
    const int column = int((east / (2 * view.halfSpan) + .5) * image.size), row = int((.5 - north / (2 * view.halfSpan)) * image.size);
    const std::uint8_t* p = &image.rgba[(std::size_t(row) * image.size + column) * 4];
    return std::tuple{int(p[0]), int(p[1]), int(p[2])};
  };
  const auto [lr, lg, lb] = pixel(46.45, 6.55);  // the middle of Lake Geneva
  const auto [gr, gg, gb] = pixel(46.6, 6.3);    // the Jura's foot, dry
  std::printf("lake %d %d %d, land %d %d %d\n", lr, lg, lb, gr, gg, gb);
  check(lb > lr + 40 && lb > lg, "the lake is blue");
  check(!(gb > gr + 40 && gb > gg), "the land is not");
  check(!cover->airports().empty() && !cover->towns().empty(), "the cover file lists places");
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const std::string suite = argc > 1 ? argv[1] : "sun";
    if (suite == "sun") sun();
    else if (suite == "patterns") patterns();
    else {
      const std::filesystem::path pack = argc > 2 ? argv[2] : "", land = argc > 3 ? argv[3] : "";
      if (!std::filesystem::is_regular_file(pack) || !std::filesystem::is_regular_file(land)) {
        std::printf("[NOT RUN] no Earth terrain pack or land cover; build them with scripts/build_earth_terrain.py\n");
        return 77;
      }
      terrain = std::make_unique<EarthTerrain>(pack);
      cover = std::make_unique<EarthCover>(land);
      if (suite == "determinism") determinism();
      else if (suite == "airports") airports();
      else if (suite == "map") map();
      else throw std::runtime_error("unknown suite " + suite);
    }
    std::printf("scenery.%s passed\n", suite.c_str());
    return 0;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "earth scenery test failed: %s\n", e.what());
    return 1;
  }
}
