#include "earth_objects.hpp"

#include "earth_pattern.hpp"
#include "mesher.hpp"
#include "ofs/water.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ofs::client {
using namespace geometry;
namespace {

constexpr double kQuarterMeridian = 10001966.;
constexpr int kTilesPerFace = 3 << kEarthObjectLevel;

std::uint32_t mix32(std::uint32_t h) {
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return h;
}
float unit(std::uint32_t h) { return float(h & 0xffffff) / float(0xffffff); }

// East, north and up at a surface, in ECEF.
struct Axes {
  Vec3 east, north, up;
};
Axes axesOf(const Surface& s) {
  return {{-s.sinLon, s.cosLon, 0}, {-s.sinLat * s.cosLon, -s.sinLat * s.sinLon, s.cosLat}, {s.cosLat * s.cosLon, s.cosLat * s.sinLon, s.sinLat}};
}

// The ground beneath an ECEF point, as a point on it.
struct GroundPoint {
  Vec3 point, up;
  Surface surface;
  double ground{}, water{kNoWater};
};

// Where a runway's ends lie, on the ground.
struct RunwayFrame {
  Vec3 centre, along, across, up;  // across points to the airport's side
  double halfLength{}, halfWidth{};
};

}  // namespace

struct EarthObjects::Spot {
  Vec3 point, up;   // on the ground
  Surface surface;
  float ground{}, water{};
  float px{}, py{};  // pattern coordinates
  EarthCoverClass cover;
};

EarthNode EarthObjects::tileOf(const Vec3& up) {
  int face;
  double s, t;
  earthFaceOf(up, face, s, t);
  const auto cell = [](double f) { return std::uint32_t(std::clamp(int(std::floor(f * kTilesPerFace)), 0, kTilesPerFace - 1)); };
  return {std::uint8_t(face), std::uint8_t(kEarthObjectLevel), cell(s), cell(t)};
}

EarthObjects::EarthObjects(const EarthTerrain& terrain, const EarthCover& cover) : terrain_(terrain), cover_(cover) {
  for (const EarthTown& town : cover.towns()) {
    Town t;
    const Surface s{std::sin(town.latitude), std::cos(town.latitude), std::sin(town.longitude), std::cos(town.longitude), 0};
    t.up = axesOf(s).up;
    t.centre = earthSeaLevel(s);
    t.radius = earthTownRadius(town.population);
    t.seed = mix32(std::uint32_t(std::lround(town.latitude * 1e6)) * 0x9e3779b1u ^ std::uint32_t(std::lround(town.longitude * 1e6)));
    towns_.push_back(t);
  }

  // Each runway belongs to the nearest airport within 6 km; an airport's
  // buildings go along its longest runway.
  const auto& runways = terrain.runways();
  layouts_.resize(runways.size());
  airportOf_.assign(runways.size(), -1);
  std::unordered_map<std::int64_t, std::vector<int>> byCell;  // airports by whole degree
  const auto cellKey = [](double lat, double lon) {
    return std::int64_t(std::floor(lat * 180 / kPi)) * 1000 + std::int64_t(std::floor(lon * 180 / kPi));
  };
  std::vector<Vec3> airportAt(cover.airports().size());
  for (std::size_t i = 0; i < cover.airports().size(); ++i) {
    const auto& a = cover.airports()[i];
    airportAt[i] = earthSeaLevel({std::sin(a.latitude), std::cos(a.latitude), std::sin(a.longitude), std::cos(a.longitude), 0});
    byCell[cellKey(a.latitude, a.longitude)].push_back(int(i));
  }
  std::unordered_map<int, std::size_t> longest;  // airport (or -1 - runway) -> runway
  for (std::size_t r = 0; r < runways.size(); ++r) {
    const Surface s = surfaceFromEcef(runways[r].centre);
    const double lat = std::atan2(s.sinLat, s.cosLat), lon = std::atan2(s.sinLon, s.cosLon);
    double best = 6000;
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const auto found = byCell.find(cellKey(lat + dy * kPi / 180, lon + dx * kPi / 180));
        if (found == byCell.end()) continue;
        for (const int a : found->second)
          if (const double d = (airportAt[std::size_t(a)] - runways[r].centre).norm(); d < best) best = d, airportOf_[r] = a;
      }
    const int key = airportOf_[r] >= 0 ? airportOf_[r] : -1 - int(r);
    if (const auto found = longest.find(key); found == longest.end() || runways[found->second].length < runways[r].length)
      longest[key] = r;
  }
  for (std::size_t r = 0; r < runways.size(); ++r) {
    EarthAirportLayout& l = layouts_[r];
    const int key = airportOf_[r] >= 0 ? airportOf_[r] : -1 - int(r);
    const double length = runways[r].length;
    l.kind = airportOf_[r] >= 0 ? cover.airports()[std::size_t(airportOf_[r])].kind : length > 2500 ? 3 : length > 1300 ? 2 : 1;
    // An airfield with a short strip is small whatever it calls itself.
    if (length < 900) l.kind = 1;
    l.primary = longest.at(key) == r;
    if (!l.primary) continue;
    const std::uint32_t h = mix32(std::uint32_t(r) * 0x27d4eb2fu ^ 0x51ed270bu);
    l.side = unit(h) < .5f ? -1.f : 1.f;
    // Away from the airport's other runways, if it has any.
    double others = 0;
    const Vec3 across = runways[r].along.cross(runways[r].up).normalized();
    for (std::size_t o = 0; o < runways.size(); ++o)
      if (o != r && airportOf_[o] >= 0 && airportOf_[o] == airportOf_[r]) others += (runways[o].centre - runways[r].centre).dot(across);
    if (std::abs(others) > 30) l.side = others > 0 ? -1.f : 1.f;
    const float half = float(runways[r].width) * .5f;
    switch (l.kind) {
      case 3: l.taxiOffset = half + 160, l.taxiHalfWidth = 12, l.apronHalfLength = 520, l.apronDepth = 230; break;
      case 2: l.taxiOffset = half + 105, l.taxiHalfWidth = 9, l.apronHalfLength = 210, l.apronDepth = 120; break;
      default: l.taxiOffset = 0, l.taxiHalfWidth = 0, l.apronHalfLength = 45, l.apronDepth = 40; break;
    }
    l.apronHalfLength = std::min(l.apronHalfLength, float(length) * .3f);
    l.apronAlong = (unit(mix32(h ^ 0x9e3779b9u)) - .5f) * float(length) * .2f;
  }
}

EarthObjects::Spot EarthObjects::spot(int face, double s, double t) const {
  Spot out;
  out.up = earthFaceDirection(face, s, t);
  out.surface = earthSurfaceOf(out.up);
  const Vec3 base = earthSeaLevel(out.surface);
  out.ground = float(terrain_.groundAltitude(base, out.surface));
  const double water = terrain_.waterAltitude(out.surface);
  out.water = water > kNoWater + 1 ? float(water) : -10000.f;
  out.point = base + out.up * double(out.ground);
  out.px = earthPattern(s * kEarthFaceMetres);
  out.py = earthPattern(t * kEarthFaceMetres);
  const std::uint32_t c = cover_.colour(std::atan2(out.surface.sinLat, out.surface.cosLat), std::atan2(out.surface.sinLon, out.surface.cosLon));
  out.cover = earthCoverClass(float(c & 0xff) / 255.f, float(c >> 8 & 0xff) / 255.f, float(c >> 16 & 0xff) / 255.f);
  return out;
}

std::vector<std::uint32_t> EarthObjects::runwaysNear(const Vec3& ecef, double reach) const {
  std::vector<std::uint32_t> out;
  const auto& runways = terrain_.runways();
  for (std::uint32_t r = 0; r < runways.size(); ++r)
    if ((runways[r].centre - ecef).norm() < reach + runways[r].length * .5 + 1500) out.push_back(r);
  return out;
}

// Ground an airport keeps clear: each runway's strip, and on its airport's
// side the taxiway, apron and buildings.
bool EarthObjects::cleared(const Vec3& ecef, std::uint32_t r) const {
  const EarthRunway& runway = terrain_.runways()[r];
  const EarthAirportLayout& l = layouts_[r];
  const Vec3 d = ecef - runway.centre;
  const double along = d.dot(runway.along);
  const Vec3 acrossAxis = runway.along.cross(runway.up).normalized();
  const double across = d.dot(acrossAxis) * l.side;
  const double half = runway.length * .5;
  if (std::abs(along) < half + 250 && std::abs(across) < runway.width * .5 + 90) return true;
  if (!l.primary) return false;
  const double side = l.taxiOffset + l.taxiHalfWidth + l.apronDepth + (l.kind == 3 ? 260 : l.kind == 2 ? 170 : 90);
  if (across > 0 && across < side && std::abs(along) < half + 150) {
    if (l.taxiOffset > 0 && std::abs(across - l.taxiOffset) < l.taxiHalfWidth + 25) return true;
    if (std::abs(along - l.apronAlong) < l.apronHalfLength + 160) return true;
  }
  return false;
}

std::vector<EarthPlace> EarthObjects::placesNear(const Vec3& ecef, double reach) const {
  std::vector<std::pair<double, EarthPlace>> found;
  for (const Town& town : towns_) {
    const double d = (town.centre - ecef).norm();
    if (d > reach + town.radius * 1.25) continue;
    EarthPlace p;
    const Surface s = earthSurfaceOf(town.up);
    const Axes a = axesOf(s);
    // Streets run at the town's own angle.
    const double heading = unit(town.seed) * kPi * .5;
    p.east = a.east * std::cos(heading) - a.north * std::sin(heading);
    p.south = (p.east.cross(a.up)).normalized();
    p.centre = town.centre;
    p.radius = town.radius;
    p.kind = town.radius > 2600 ? 2 : 1;
    p.seed = town.seed;
    found.push_back({p.kind == 2 ? d - 1e7 : d, p});
  }
  // Villages: the squares around the point on its face that hold one where
  // the land is farmed.
  {
    const Surface s = surfaceFromEcef(ecef);
    const Vec3 up = axesOf(s).up;
    int face;
    double fs, ft;
    earthFaceOf(up, face, fs, ft);
    const double u = fs * kEarthFaceMetres, v = ft * kEarthFaceMetres;
    const double wrapU = kEarthPatternWrap * std::floor(u / kEarthPatternWrap), wrapV = kEarthPatternWrap * std::floor(v / kEarthPatternWrap);
    const int span = int(std::ceil((reach + 500) / kEarthVillageCell));
    const int cu = int(std::floor((u - wrapU) / kEarthVillageCell)), cv = int(std::floor((v - wrapV) / kEarthVillageCell));
    for (int j = cv - span; j <= cv + span; ++j)
      for (int i = cu - span; i <= cu + span; ++i) {
        const EarthVillage village = earthVillage(float(i), float(j));
        if (!village.present) continue;
        const double vs = (wrapU + village.x) / kEarthFaceMetres, vt = (wrapV + village.y) / kEarthFaceMetres;
        const Vec3 vup = earthFaceDirection(face, vs, vt);
        const Surface vsurf = earthSurfaceOf(vup);
        const Vec3 centre = earthSeaLevel(vsurf);
        const double d = (centre - ecef).norm();
        if (d > reach + village.radius * 1.25) continue;
        const std::uint32_t c = cover_.colour(std::atan2(vsurf.sinLat, vsurf.cosLat), std::atan2(vsurf.sinLon, vsurf.cosLon));
        const auto cls = earthCoverClass(float(c & 0xff) / 255.f, float(c >> 8 & 0xff) / 255.f, float(c >> 16 & 0xff) / 255.f);
        if (cls.fields < .5f) continue;
        const double water = terrain_.waterAltitude(vsurf);
        if (water > kNoWater + 1 && water > terrain_.groundAltitude(centre, vsurf) - 2) continue;
        // Not by an airport.
        bool airfield = false;
        for (const auto r : runwaysNear(centre, village.radius + 1500)) {
          const EarthRunway& runway = terrain_.runways()[r];
          const Vec3 d = centre - runway.centre;
          const double along = std::clamp(d.dot(runway.along), -runway.length * .5, runway.length * .5);
          if ((d - runway.along * along).norm() < village.radius + 900) airfield = true;
        }
        if (airfield) continue;
        // Not where a town already stands.
        bool inTown = false;
        for (const auto& [rank, place] : found)
          if (place.kind > 0 && (place.centre - centre).norm() < place.radius * 1.1 + village.radius) inTown = true;
        if (inTown) continue;
        EarthPlace p;
        const Axes a = axesOf(vsurf);
        p.east = a.east * std::cos(double(village.heading)) - a.north * std::sin(double(village.heading));
        p.south = p.east.cross(a.up).normalized();
        p.centre = centre;
        p.radius = village.radius;
        p.kind = 0;
        p.seed = mix32(std::uint32_t(i) * 0x9e3779b1u ^ std::uint32_t(j) * 0x85ebca77u ^ std::uint32_t(face) * 0xc2b2ae3du);
        found.push_back({d, p});
      }
  }
  std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  std::vector<EarthPlace> out;
  out.reserve(found.size());
  for (auto& [rank, place] : found) out.push_back(place);
  return out;
}

EarthObjectTile EarthObjects::build(const EarthNode& node, int treeDensity) const {
  EarthObjectTile tile;
  tile.node = node;
  const double side = double(kTilesPerFace);
  const double s0 = node.x / side, t0 = node.y / side, step = 1 / side;
  {
    const Spot centre = spot(node.face, s0 + step * .5, t0 + step * .5);
    tile.centre = centre.point;
    const Axes a = axesOf(centre.surface);
    tile.east = a.east;
    tile.up = a.up;
    tile.south = -a.north;
  }
  const auto local = [&](const Vec3& p) {
    const Vec3 d = p - tile.centre;
    return Vec3{d.dot(tile.east), d.dot(tile.up), d.dot(tile.south)};
  };
  // The tile's corners, and its size.
  const Vec3 c00 = earthSeaLevel(earthSurfaceOf(earthFaceDirection(node.face, s0, t0)));
  const Vec3 c11 = earthSeaLevel(earthSurfaceOf(earthFaceDirection(node.face, s0 + step, t0 + step)));
  const Vec3 c10 = earthSeaLevel(earthSurfaceOf(earthFaceDirection(node.face, s0 + step, t0)));
  const double area = (c10 - c00).cross(c11 - c00).norm();  // two triangles' worth
  const double halfDiagonal = (c11 - c00).norm() * .5;
  const auto runways = runwaysNear(tile.centre, halfDiagonal + 2000);
  const auto nearRunway = [&](const Vec3& p) {
    for (const auto r : runways)
      if (cleared(p, r)) return true;
    return false;
  };
  const auto places = placesNear(tile.centre, halfDiagonal);
  // How built up a point is, and by which place.
  const auto builtUp = [&](const Vec3& p, float px, float py, int* which) {
    float best = 0;
    for (std::size_t i = 0; i < places.size(); ++i) {
      const EarthPlace& place = places[i];
      const Vec3 d = p - place.centre;
      const float density = earthTownDensity(float(std::hypot(d.dot(place.east), d.dot(place.south))), place.radius, px, py);
      if (density > best) {
        best = density;
        if (which) *which = int(i);
      }
    }
    return best;
  };

  // ---- Trees ---------------------------------------------------------------
  const int candidates = int(double(treeDensity) * area / 1e6);
  const std::uint32_t base = mix32(std::uint32_t(node.face) * 0x68bc21ebu ^ node.x * 0x9e3779b1u ^ node.y * 0x85ebca77u);
  std::vector<TreeInstance> conifers;
  for (int i = 0; i < candidates; ++i) {
    const std::uint32_t h = mix32(base + std::uint32_t(i) * 0x27d4eb2fu);
    const std::uint32_t h2 = mix32(h ^ 0x165667b1u), h3 = mix32(h2 ^ 0xc2b2ae3du);
    double s = s0 + step * unit(h), t = t0 + step * unit(h2);
    Spot here = spot(node.face, s, t);
    if (here.cover.plants < .05f) continue;
    // The slope, from two nearby samples: steep farmland is wooded.
    const auto steepness = [&](const Spot& at, double as, double at_) {
      const Spot east = spot(node.face, as + 4 / kEarthFaceMetres, at_), north = spot(node.face, as, at_ + 4 / kEarthFaceMetres);
      return float(1 - std::abs((east.point - at.point).cross(north.point - at.point).normalized().dot(at.up)));
    };
    float steep = steepness(here, s, t);
    earthSteepWoods(here.cover, steep);
    float chance = earthWoodland(here.px, here.py, here.cover.wood);
    chance += .010f * here.cover.plants;  // the solitary tree
    bool hedgerow = false;
    if (here.cover.fields > .3f) {
      const EarthField field = earthField(here.px, here.py);
      if (field.wooded && field.edge > 3) chance = std::max(chance, .8f);
      if (unit(h3) > chance) {
        // On farmland the trees not in a wood stand in its hedgerows and belts.
        if (unit(mix32(h3 ^ 0x68bc21ebu)) > (field.reach > 5 ? 1.f : .8f)) continue;
        if (!field.hedged || field.edge > 160 || field.laneGap < 8) continue;
        const double offset = (unit(mix32(h3 ^ 0x02e5be93u)) - .5) * (field.reach > 5 ? 17 : 5);
        const double toX = field.hedgeX - here.px, toY = field.hedgeY - here.py;
        const double length = std::max(1e-6, std::hypot(toX, toY));
        s += (toX + toX / length * offset) / kEarthFaceMetres;
        t += (toY + toY / length * offset) / kEarthFaceMetres;
        if (s < s0 || s >= s0 + step || t < t0 || t >= t0 + step) continue;
        here = spot(node.face, s, t);
        steep = steepness(here, s, t);
        earthSteepWoods(here.cover, steep);
        if (here.cover.fields < .3f) continue;
        const EarthField there = earthField(here.px, here.py);
        if (!there.hedged || std::abs(there.edge) > there.reach + .5f || there.laneGap < 8) continue;
        hedgerow = true;
      }
    } else if (unit(h3) > chance) {
      continue;
    }
    if (here.water > here.ground - 1.5f) continue;
    const float latitude = float(std::abs(std::atan2(here.surface.sinLat, here.surface.cosLat)) * 180 / kPi);
    const float treeline = 3700.f - 2500.f * earthSmooth(22.f, 62.f, latitude);
    if (here.ground > treeline) continue;
    if (nearRunway(here.point)) continue;
    int which = -1;
    const float town = builtUp(here.point, here.px, here.py, &which);
    if (town > .15f) {
      // Gardens and parks keep a few; streets and buildings none.
      if (unit(mix32(h3 ^ 0x5bd1e995u)) < town * 1.4f) continue;
      // Not in the street.
      const EarthPlace& place = places[std::size_t(which)];
      const Vec3 d = here.point - place.centre;
      const float x = float(d.dot(place.east)), y = float(d.dot(place.south));
      if (place.kind == 0) {
        if (std::min(std::abs(x), std::abs(y)) < 6) continue;
      } else {
        const EarthDistrict district = earthDistrict(x, y);
        if (earthStreetLot(int(std::floor(district.x / kEarthLot)), int(std::floor(district.y / kEarthLot)))) continue;
      }
    }
    if (steep > .24f) continue;  // rock (earth_terrain_fs.glsl)
    const std::uint32_t h4 = mix32(h3 ^ 0x2545f491u), h5 = mix32(h4 ^ 0x9e3779b9u);
    const Vec3 p = local(here.point);
    TreeInstance tree;
    tree.east = float(p.x);
    tree.up = float(p.y);
    tree.south = float(p.z);
    // Conifers take over with height and toward the poles.
    const float coniferShare = std::min(1.f, .12f + .78f * earthSmooth(treeline - 1500, treeline - 650, here.ground) +
                                                 .7f * earthSmooth(48.f, 58.f, latitude));
    tree.conifer = !hedgerow && unit(h4) < coniferShare ? 1.f : 0.f;
    const float vigour = 1 - .45f * earthSmooth(treeline - 650, treeline, here.ground);
    tree.height = (tree.conifer > .5f ? 13.f + 13.f * unit(h5) : 10.f + 11.f * unit(h5)) * vigour;
    tree.spread = .82f + .36f * unit(mix32(h5 ^ 0x1b873593u));
    tree.yaw = unit(mix32(h5 ^ 0xcc9e2d51u)) * 6.2831853f;
    tree.tint = unit(mix32(h4 ^ 0xe6546b64u));
    (tree.conifer > .5f ? conifers : tile.trees).push_back(tree);
  }
  tile.broadleaf = std::uint32_t(tile.trees.size());
  tile.trees.insert(tile.trees.end(), conifers.begin(), conifers.end());

  // ---- Towns, airports and masts ------------------------------------------
  for (std::size_t i = 0; i < places.size(); ++i) townBuildings(places[i], i, places, runways, tile);
  for (const auto r : runways)
    if (layouts_[r].primary && tileOf(terrain_.runways()[r].up) == node) airportBuildings(r, tile);
  mast(tile);

  // ---- Extent --------------------------------------------------------------
  float radius = float(halfDiagonal) + 30, low = 1e9f, high = -1e9f;
  for (const auto& tree : tile.trees) {
    low = std::min(low, tree.up);
    high = std::max(high, tree.up + tree.height);
  }
  for (const auto& part : tile.parts)
    for (const auto& v : part) {
      radius = std::max(radius, std::hypot(v.x, v.z));
      low = std::min(low, v.y);
      high = std::max(high, v.y);
    }
  tile.radius = radius;
  tile.low = low > high ? 0 : low;
  tile.high = low > high ? 0 : high;
  return tile;
}

// ---------------------------------------------------------------------------
// Buildings
// ---------------------------------------------------------------------------

namespace {

// Geometry built in its own axes (x across, z along, y up from its ground),
// moved into the tile's frame: `origin` and the axes in the tile's frame.
// Each piece placed is also kept as a box to collide with.
void placeInto(std::vector<SurfaceVertex>& out, const std::vector<SurfaceVertex>& in, const Vec3& origin, const Vec3& x, const Vec3& y,
           const Vec3& z, std::vector<SceneryBox>& boxes) {
  if (!in.empty()) boxes.push_back(sceneryBox(origin, x, y, z, in));
  for (SurfaceVertex v : in) {
    const Vec3 p = origin + x * double(v.x) + y * double(v.y) + z * double(v.z);
    const Vec3 n = x * double(v.nx) + y * double(v.ny) + z * double(v.nz);
    v.x = float(p.x), v.y = float(p.y), v.z = float(p.z);
    v.nx = float(n.x), v.ny = float(n.y), v.nz = float(n.z);
    out.push_back(v);
  }
}

using Parts = std::array<std::vector<SurfaceVertex>, kEarthParts>;
std::vector<SurfaceVertex>& part(Parts& parts, EarthPart p) { return parts[std::size_t(p)]; }

}  // namespace

void EarthObjects::townBuildings(const EarthPlace& place, std::size_t index, const std::vector<EarthPlace>& places,
                                 const std::vector<std::uint32_t>& runways, EarthObjectTile& tile) const {
  const auto& node = tile.node;
  const double side = double(kTilesPerFace), s0 = node.x / side, t0 = node.y / side, step = 1 / side;
  // The tile's corners in the town's axes give the lots to look at.
  double x0 = 1e18, x1 = -1e18, y0 = 1e18, y1 = -1e18;
  for (const double cs : {s0, s0 + step})
    for (const double ct : {t0, t0 + step}) {
      const Vec3 d = earthSeaLevel(earthSurfaceOf(earthFaceDirection(node.face, cs, ct))) - place.centre;
      x0 = std::min(x0, d.dot(place.east)), x1 = std::max(x1, d.dot(place.east));
      y0 = std::min(y0, d.dot(place.south)), y1 = std::max(y1, d.dot(place.south));
    }
  const double reach = place.radius * 1.25;
  x0 = std::max(x0, -reach), x1 = std::min(x1, reach), y0 = std::max(y0, -reach), y1 = std::min(y1, reach);
  if (x0 >= x1 || y0 >= y1) return;
  const Vec3 placeUp = place.east.cross(place.south).normalized() * -1.;
  const Vec3 up = placeUp.dot(tile.up) < 0 ? placeUp * -1. : placeUp;
  // The town's axes in the tile's frame.
  const auto inTile = [&](const Vec3& v) { return Vec3{v.dot(tile.east), v.dot(tile.up), v.dot(tile.south)}; };
  const Vec3 ax = inTile(place.east), ay = inTile(up), az = inTile(place.south);
  const auto local = [&](const Vec3& p) { return inTile(p - tile.centre); };
  // The lots to look at, each with its centre in the town's axes and the
  // angle of its street grid.
  struct Lot {
    int i{}, j{};
    double x{}, y{};
    float c{1}, s{};
    std::uint32_t district{};
  };
  std::vector<Lot> lots;
  const auto inBox = [&](double x, double y) { return x >= x0 - kEarthLot && x <= x1 + kEarthLot && y >= y0 - kEarthLot && y <= y1 + kEarthLot; };
  if (place.kind == 0) {
    // Houses either side of the street and of the lane across it.
    for (int i = int(std::floor(x0 / kEarthLot)) - 1; i <= int(std::floor(x1 / kEarthLot)) + 1; ++i)
      for (const int j : {-1, 0})
        if (inBox((i + .5) * kEarthLot, (j + .5) * kEarthLot)) lots.push_back({i, j, (i + .5) * kEarthLot, (j + .5) * kEarthLot});
    for (int j = int(std::floor(y0 / kEarthLot)) - 1; j <= int(std::floor(y1 / kEarthLot)) + 1; ++j)
      for (const int i : {-1, 0})
        if (j != -1 && j != 0 && inBox((i + .5) * kEarthLot, (j + .5) * kEarthLot))
          lots.push_back({i, j, (i + .5) * kEarthLot, (j + .5) * kEarthLot});
  } else {
    for (int dy = int(std::floor(y0 / kEarthDistrict)); dy <= int(std::floor(y1 / kEarthDistrict)); ++dy)
      for (int dx = int(std::floor(x0 / kEarthDistrict)); dx <= int(std::floor(x1 / kEarthDistrict)); ++dx) {
        const EarthDistrict district{float(dx), float(dy), 0, 0};
        const float angle = (earthHash(float(dx) + 71.f, float(dy) + 13.f) - .5f) * .9f;
        const float c = std::cos(angle), sn = std::sin(angle);
        // The part of the district within the tile's box, in the district's turned axes.
        const double ax0 = std::max(x0, double(dx) * kEarthDistrict), ax1 = std::min(x1, double(dx + 1) * kEarthDistrict);
        const double ay0 = std::max(y0, double(dy) * kEarthDistrict), ay1 = std::min(y1, double(dy + 1) * kEarthDistrict);
        if (ax0 >= ax1 || ay0 >= ay1) continue;
        double l0 = 1e18, l1 = -1e18, m0 = 1e18, m1 = -1e18;
        for (const double x : {ax0, ax1})
          for (const double y : {ay0, ay1}) {
            const double lx = x * c - y * sn, ly = x * sn + y * c;
            l0 = std::min(l0, lx), l1 = std::max(l1, lx), m0 = std::min(m0, ly), m1 = std::max(m1, ly);
          }
        const std::uint32_t code = mix32(std::uint32_t(dx) * 0x2545f491u ^ std::uint32_t(dy) * 0x9e3779b9u);
        for (int j = int(std::floor(m0 / kEarthLot)); j <= int(std::floor(m1 / kEarthLot)); ++j)
          for (int i = int(std::floor(l0 / kEarthLot)); i <= int(std::floor(l1 / kEarthLot)); ++i) {
            const double lx = (i + .5) * kEarthLot, ly = (j + .5) * kEarthLot;
            const double x = lx * c + ly * sn, y = -lx * sn + ly * c;
            if (!inBox(x, y) || earthLotOpen(district, i, j)) continue;
            lots.push_back({i, j, x, y, c, sn, code});
          }
      }
  }
  // A lot's ground: a point at sea level under the town's plane, then the
  // ground there.
  struct Ground {
    Vec3 point;
    float altitude{}, water{};
    double s{}, t{};
    int face{};
  };
  const auto groundAt = [&](double x, double y) {
    const Vec3 flat = place.centre + place.east * x + place.south * y;
    Surface surface = surfaceFromEcef(flat);
    surface.altitude = 0;
    const Vec3 normal = axesOf(surface).up;
    Ground g;
    earthFaceOf(normal, g.face, g.s, g.t);
    const Vec3 sea = earthSeaLevel(surface);
    g.altitude = float(terrain_.groundAltitude(sea, surface));
    const double water = terrain_.waterAltitude(surface);
    g.water = water > kNoWater + 1 ? float(water) : -10000.f;
    g.point = sea + normal * double(g.altitude);
    return g;
  };
  Parts& parts = tile.parts;
  const std::uint32_t seed = place.seed;
  // Slate in the hills and the north, clay tiles elsewhere.
  const Surface townSurface = surfaceFromEcef(place.centre);
  const float latitude = float(std::abs(std::atan2(townSurface.sinLat, townSurface.cosLat)) * 180 / kPi);
  for (const Lot& lot : lots) {
      const int i = lot.i, j = lot.j;
      const std::uint32_t h = mix32(seed ^ lot.district ^ mix32(std::uint32_t(i) * 0x9e3779b1u ^ std::uint32_t(j) * 0x85ebca77u));
      const double cx = lot.x, cy = lot.y;
      // The lot's own axes: its district's turn of the town's.
      const Vec3 bx = ax * double(lot.c) - az * double(lot.s), bz = ax * double(lot.s) + az * double(lot.c);
      const float r = float(std::hypot(cx, cy));
      if (r > reach) continue;
      // A quick look before the ground is sampled: most lots stay empty.
      if (unit(h) > 1 - earthSmooth(.2f * place.radius, place.radius * 1.22f, r) + .02f) continue;
      const Ground g = groundAt(cx, cy);
      // The lot is this tile's only if its centre lies in it.
      if (g.face != node.face || g.s < s0 || g.s >= s0 + step || g.t < t0 || g.t >= t0 + step) continue;
      const float px = earthPattern(g.s * kEarthFaceMetres), py = earthPattern(g.t * kEarthFaceMetres);
      const float density = earthTownDensity(r, place.radius, px, py);
      if (unit(h) > density * (place.kind == 0 ? .75f : .9f)) continue;
      // Only the place that builds the ground up most builds on it.
      bool owned = true;
      for (std::size_t k = 0; k < places.size() && owned; ++k) {
        if (k == index) continue;
        const Vec3 d = g.point - places[k].centre;
        if (earthTownDensity(float(std::hypot(d.dot(places[k].east), d.dot(places[k].south))), places[k].radius, px, py) > density)
          owned = false;
      }
      if (!owned) continue;
      if (g.water > g.altitude - 2) continue;
      bool clear = true;
      for (const auto rw : runways)
        if (cleared(g.point, rw)) clear = false;
      if (!clear) continue;
      // Level enough to build on.
      const Ground gx = groundAt(cx + 8, cy), gy = groundAt(cx, cy + 8);
      if (std::max(std::abs(gx.altitude - g.altitude), std::abs(gy.altitude - g.altitude)) > 3.2f) continue;

      const Vec3 origin = local(g.point);
      const float h1 = unit(mix32(h ^ 0x1b873593u)), h2 = unit(mix32(h ^ 0xcc9e2d51u)), h3 = unit(mix32(h ^ 0xe6546b64u));
      // The ground under the lot in its own axes, metres up from its centre.
      const auto groundY = [&](float x, float z) {
        const Ground at = groundAt(cx + x * lot.c + z * lot.s, cy - x * lot.s + z * lot.c);
        return float((at.point - g.point).dot(up));
      };
      std::vector<SurfaceVertex> walls, roofs;
      Mesher wallMesh{walls};
      EarthPart wallPart = EarthPart::HouseWalls, roofPart = latitude > 52 || g.altitude > 700 || h3 < .2f ? EarthPart::RoofSlate
                                                                                                         : EarthPart::RoofTiles;
      const bool city = place.kind == 2, town = place.kind >= 1;
      // Factories: one side of a larger town's outskirts.
      const float bearing = std::atan2(float(cy), float(cx));
      const bool industrial = town && r > place.radius * .45f && std::cos(bearing - unit(seed) * 6.2831853f) > .55f;
      if (industrial) {
        wallPart = EarthPart::Cladding;
        roofPart = EarthPart::FlatRoof;
        const float w = 18 + 26 * h1, d = 14 + 30 * h2, height = 7 + 6 * h3;
        float low = 1e9f;
        for (const float u : {-.5f, .5f})
          for (const float v : {-.5f, .5f}) low = std::min(low, groundY(u * w, v * d));
        wallMesh.box(0, 0, w, d, low - .5f, height - low + .5f);
        Mesher{roofs}.box(0, 0, w + .6f, d + .6f, height, .5f);
        if (unit(mix32(h ^ 0x7f4a7c15u)) < .008f) {
          // A works chimney, banded at the top.
          const float stack = 45 + 70 * unit(mix32(h ^ 0x2c1b3c6du));
          std::vector<SurfaceVertex> brick, red, white;
          Mesher{brick}.cylinder(w * .5f + 6, 0, low - .5f, stack, 3.2f, 1.9f, 12);
          Mesher{red}.cylinder(w * .5f + 6, 0, stack - 6, stack, 2.06f, 1.9f, 12, false);
          Mesher{white}.cylinder(w * .5f + 6, 0, stack - 12, stack - 6, 2.22f, 2.06f, 12, false);
          placeInto(part(parts, EarthPart::Concrete), brick, origin, bx, ay, bz, tile.boxes);
          placeInto(part(parts, EarthPart::Red), red, origin, bx, ay, bz, tile.boxes);
          placeInto(part(parts, EarthPart::White), white, origin, bx, ay, bz, tile.boxes);
        }
      } else if (city && density > .8f && h2 < .5f) {
        // The centre of a city: offices, and in the largest towers, the
        // taller the nearer the middle.
        const float size = earthSmooth(4000.f, 9000.f, place.radius);
        wallPart = h3 < .15f + .3f * size ? EarthPart::Glass : EarthPart::Concrete;
        roofPart = EarthPart::FlatRoof;
        const float height = 15 + 10 * h1 + (130 * h1 * h1 * h1) * earthSmooth(.88f, 1.f, density) * size;
        const float w = 16 + 6 * h2, d = 16 + 6 * h3;
        float low = 1e9f;
        for (const float u : {-.5f, .5f})
          for (const float v : {-.5f, .5f}) low = std::min(low, groundY(u * w, v * d));
        wallMesh.box(0, 0, w, d, low - .5f, height - low + .5f);
        Mesher{roofs}.box(0, 0, w + .4f, d + .4f, height, .6f);
        if (wallPart == EarthPart::Glass) {
          // A frame between the panes.
          std::vector<SurfaceVertex> frame;
          Mesher f{frame};
          for (float y = 3.5f; y < height; y += 3.5f) f.box(0, 0, w + .25f, d + .25f, y, .35f);
          placeInto(part(parts, EarthPart::Concrete), frame, origin, bx, ay, bz, tile.boxes);
        }
      } else if (town && density > (city ? .45f : .62f) && h2 < (city ? .6f : .4f)) {
        // Blocks of flats, and shops under them.
        wallPart = EarthPart::Concrete;
        roofPart = h3 < .5f ? EarthPart::FlatRoof : roofPart;
        const float floors = float(3 + int(h1 * (city ? 6 : 3)));
        const float w = 12 + 4 * h2, d = 18 + 4 * h3, height = floors * 3.1f;
        float low = 1e9f;
        for (const float u : {-.5f, .5f})
          for (const float v : {-.5f, .5f}) low = std::min(low, groundY(u * w, v * d));
        if (roofPart == EarthPart::FlatRoof) {
          wallMesh.box(0, 0, w, d, low - .5f, height - low + .5f);
          Mesher{roofs}.box(0, 0, w + .4f, d + .4f, height, .5f);
        } else {
          house(walls, roofs, groundY, 0, 0, w, d, height, 3.5f, 0, false);
        }
      } else {
        // A house, turned a little off the street.
        const float w = 7.5f + 3.5f * h1, d = 9.f + 6.f * h2;
        house(walls, roofs, groundY, 0, 0, w, d, 4.6f + 2.4f * h3 + (town ? 2.6f * unit(mix32(h ^ 0x94d049bbu)) : 0.f),
              2.6f + 1.6f * unit(mix32(h ^ 0xbf58476du)), (unit(mix32(h ^ 0x3c6ef372u)) - .5f) * .18f, false);
        // The village church, by the middle of the place.
        if (place.kind == 0 && i == 0 && j == 0) {
          house(walls, roofs, groundY, 0, -22, 6.5f, 6.5f, 21, 11, 0, true);
        }
      }
      placeInto(part(parts, wallPart), walls, origin, bx, ay, bz, tile.boxes);
      placeInto(part(parts, roofPart), roofs, origin, bx, ay, bz, tile.boxes);
      ++tile.buildings;
    }
}

void EarthObjects::airportBuildings(std::uint32_t r, EarthObjectTile& tile) const {
  const EarthRunway& runway = terrain_.runways()[r];
  const EarthAirportLayout& l = layouts_[r];
  const Vec3 across = runway.along.cross(runway.up).normalized() * double(l.side);
  // The buildings stand in a row behind the apron, facing the runway.
  const double back = l.taxiOffset + l.taxiHalfWidth + l.apronDepth + (l.kind == 1 ? runway.width * .5 + 50 : 0);
  const auto groundAt = [&](const Vec3& flat) {
    Surface surface = surfaceFromEcef(flat);
    surface.altitude = 0;
    const Vec3 sea = earthSeaLevel(surface);
    return sea + axesOf(surface).up * terrain_.groundAltitude(sea, surface);
  };
  const auto inTile = [&](const Vec3& v) { return Vec3{v.dot(tile.east), v.dot(tile.up), v.dot(tile.south)}; };
  // x away from the runway, z along it, y up.
  const Vec3 ax = inTile(across), ay = inTile(runway.up), az = inTile(runway.along);
  Parts& parts = tile.parts;
  const std::uint32_t seed = mix32(r * 0x9e3779b1u ^ 0x7a3c19d1u);
  // A building on its own ground: `build` makes it in its axes, standing on y = 0
  // at its front, `width` along the runway and `depth` away from it.
  const auto building = [&](double along, double width, double depth, auto build) {
    const Vec3 front = runway.centre + runway.along * along + across * back;
    float low = 1e9f, high = -1e9f;
    const Vec3 origin = groundAt(front);
    for (const double u : {0., depth})
      for (const double v : {-width * .5, width * .5}) {
        const float y = float((groundAt(front + across * u + runway.along * v) - origin).dot(runway.up));
        low = std::min(low, y), high = std::max(high, y);
      }
    Parts made;
    build(made, low - .5f, high);
    for (std::size_t k = 0; k < kEarthParts; ++k) placeInto(parts[k], made[k], inTile(origin - tile.centre), ax, ay, az, tile.boxes);
    ++tile.buildings;
  };
  const double half = runway.length * .5;
  double cursor = l.apronAlong - l.apronHalfLength;
  const int hangars = l.kind == 3 ? 5 : l.kind == 2 ? 3 : 1 + int(unit(seed) * 2);
  if (l.kind >= 2) {
    // The terminal faces the middle of the apron, glass to the airside.
    const double width = l.kind == 3 ? 360 : 130, depth = l.kind == 3 ? 70 : 34, height = l.kind == 3 ? 19 : 11;
    building(l.apronAlong + l.apronHalfLength * .25, width, depth, [&](Parts& p, float low, float high) {
      Mesher walls{part(p, EarthPart::Concrete)}, glass{part(p, EarthPart::Glass)}, roof{part(p, EarthPart::FlatRoof)};
      walls.box(float(depth * .5), 0, float(depth), float(width), low, float(height) + high - low);
      glass.box(-.2f, 0, .4f, float(width) - 10, high + 3, float(height) - 6);
      roof.box(float(depth * .5), 0, float(depth) + 2, float(width) + 2, float(height) + high, 1);
      if (l.kind == 3)
        for (int k = 0; k < 4; ++k) {
          // Jet bridges reaching out toward the stands.
          const float z = float(-width * .35 + width * .7 * k / 3);
          walls.box(-15, z, 30, 3.4f, high + 4.2f, 3, true);
          glass.box(-15, z - 1.75f, 26, .1f, high + 5, 1.5f);
        }
    });
    // The control tower stands off the terminal's end.
    const double towerHeight = l.kind == 3 ? 62 : 30;
    building(l.apronAlong + l.apronHalfLength * .25 + (width * .5 + 50) * (unit(mix32(seed)) < .5 ? -1 : 1), 24, 24,
             [&](Parts& p, float low, float high) {
               Mesher walls{part(p, EarthPart::Concrete)}, glass{part(p, EarthPart::Glass)}, roof{part(p, EarthPart::FlatRoof)};
               Mesher red{part(p, EarthPart::Red)}, lamp{part(p, EarthPart::LampRed)}, steel{part(p, EarthPart::Steel)};
               const float top = float(towerHeight) + high;
               walls.box(12, 0, 22, 20, low, high - low + 6.5f);
               walls.cylinder(12, 0, high + 6.5f, top - 6, 4.6f, 3.9f, 10);
               red.cylinder(12, 0, top - 11, top - 9, 4.0f, 3.95f, 10, false);
               walls.cylinder(12, 0, top - 6, top - 4.6f, 4.4f, 8.8f, 10, false);
               glass.cylinder(12, 0, top - 4.6f, top - .6f, 7.6f, 8.6f, 10, false);
               roof.cylinder(12, 0, top - .6f, top + .3f, 9.3f, 9.3f, 10);
               steel.beam({12, top + .3f, 0}, {12, top + 8, 0}, .22f);
               lamp.box(12, 0, .5f, .5f, top + 8, .5f);
             });
    cursor = l.apronAlong + l.apronHalfLength * .25 - width * .5 - 30;
  }
  // Hangars stand along the apron toward its far end.
  for (int k = 0; k < hangars; ++k) {
    const std::uint32_t h = mix32(seed + std::uint32_t(k) * 0x27d4eb2fu);
    const double width = l.kind == 3 ? 90 + 40 * unit(h) : l.kind == 2 ? 50 + 25 * unit(h) : 18 + 10 * unit(h);
    const double depth = width * (.7 + .2 * unit(mix32(h)));
    const double height = l.kind == 1 ? 6 : width * .24;
    cursor -= width * .5;
    if (cursor < -half + 100) break;
    building(cursor, width, depth, [&](Parts& p, float low, float high) {
      Mesher cladding{part(p, EarthPart::Cladding)}, dark{part(p, EarthPart::Dark)}, roof{part(p, EarthPart::FlatRoof)};
      Mesher concrete{part(p, EarthPart::Concrete)};
      const float d = float(depth), w = float(width), y = high - .2f;
      if (l.kind == 1) {
        cladding.box(d * .5f, 0, d, w, low, y + float(height) - low);
        roof.box(d * .5f, 0, d + .6f, w + .6f, y + float(height), .4f);
        dark.box(-.1f, 0, .2f, w * .7f, y, float(height) * .75f);
        return;
      }
      // An arched shed, its doors open to the apron.
      concrete.box(d * .5f, 0, d, w, low, y - low + .2f);
      std::vector<SurfaceVertex> shed;
      Mesher vault{shed};
      vault.vault(d * .5f, 0, 0, d, w, float(height));
      vault.vaultEnd(d * .5f, 0, 0, d * .5f, w, float(height), 0, 0, 1);
      vault.vaultEnd(d * .5f, 0, 0, -d * .5f, w, float(height), .78f, .60f, -1);
      for (auto& v : shed) v.y += y;
      part(p, EarthPart::Cladding).insert(part(p, EarthPart::Cladding).end(), shed.begin(), shed.end());
      dark.quad({1.2f, y, -w * .39f}, {1.2f, y, w * .39f}, {1.2f, y + float(height) * .6f, w * .39f},
                {1.2f, y + float(height) * .6f, -w * .39f}, {-1, 0, 0});
    });
    cursor -= width * .5 + 25;
  }
}

void EarthObjects::mast(EarthObjectTile& tile) const {
  const auto& node = tile.node;
  const std::uint32_t h = mix32(std::uint32_t(node.face) * 0x3c6ef372u ^ node.x * 0x27d4eb2fu ^ node.y * 0x165667b1u ^ 0xa54ff53au);
  if (unit(h) > .035f) return;
  const double side = double(kTilesPerFace), s0 = node.x / side, t0 = node.y / side, step = 1 / side;
  // On the highest of a few spots in the tile.
  Spot best;
  best.ground = -1e9f;
  for (int k = 0; k < 7; ++k) {
    const std::uint32_t hk = mix32(h + std::uint32_t(k) * 0x9e3779b9u);
    const Spot at = spot(node.face, s0 + step * (.1 + .8 * unit(hk)), t0 + step * (.1 + .8 * unit(mix32(hk))));
    if (at.ground > best.ground) best = at;
  }
  if (best.water > best.ground - 2 || best.ground < 1) return;
  const auto runways = runwaysNear(best.point, 4000);
  for (const auto r : runways)
    if ((terrain_.runways()[r].centre - best.point).norm() < terrain_.runways()[r].length * .5 + 3000) return;
  const Vec3 origin{(best.point - tile.centre).dot(tile.east), (best.point - tile.centre).dot(tile.up), (best.point - tile.centre).dot(tile.south)};
  const Vec3 ax = Vec3{1, 0, 0}, ay = Vec3{best.up.dot(tile.east), best.up.dot(tile.up), best.up.dot(tile.south)}, az = Vec3{0, 0, 1};
  const float height = 70 + 140 * unit(mix32(h ^ 0x1f83d9abu));
  Parts made;
  Mesher red{part(made, EarthPart::Red)}, white{part(made, EarthPart::White)}, concrete{part(made, EarthPart::Concrete)};
  Mesher lamp{part(made, EarthPart::LampRed)}, steel{part(made, EarthPart::Steel)};
  concrete.box(0, 0, 9, 7, -1, 4.5f);
  // A lattice mast, painted in bands, held up by stays.
  const float base = 1.8f, top = .9f;
  const int bands = 7;
  for (int b = 0; b < bands; ++b) {
    const float y0 = height * float(b) / bands, y1 = height * float(b + 1) / bands;
    const float r0 = base + (top - base) * float(b) / bands, r1 = base + (top - base) * float(b + 1) / bands;
    Mesher& paint = b % 2 ? white : red;
    for (int leg = 0; leg < 3; ++leg) {
      const float a = 2.0943951f * float(leg);
      paint.beam({r0 * std::cos(a), y0, r0 * std::sin(a)}, {r1 * std::cos(a), y1, r1 * std::sin(a)}, .35f);
      const float c = 2.0943951f * float(leg + 1);
      paint.beam({r0 * std::cos(a), y0, r0 * std::sin(a)}, {r1 * std::cos(c), y1, r1 * std::sin(c)}, .12f);
    }
  }
  for (int leg = 0; leg < 3; ++leg) {
    const float a = 2.0943951f * float(leg) + .5f, reach = height * .45f;
    steel.beam({reach * std::cos(a), 0, reach * std::sin(a)}, {0, height * .8f, 0}, .06f);
  }
  lamp.box(0, 0, .6f, .6f, height, .6f);
  // The stays are wires: the mast is hit as its tower alone.
  std::vector<SceneryBox> stays;
  for (std::size_t k = 0; k < kEarthParts; ++k)
    placeInto(tile.parts[k], made[k], origin, ax, ay, az, EarthPart(k) == EarthPart::Steel ? stays : tile.boxes);
  ++tile.buildings;
}

// ---------------------------------------------------------------------------
// The builder
// ---------------------------------------------------------------------------

EarthObjectBuilder::EarthObjectBuilder(const EarthObjects& objects) : objects_(objects) {
  const unsigned count = std::clamp(std::thread::hardware_concurrency() / 6, 1u, 2u);
  for (unsigned i = 0; i < count; ++i) workers_.emplace_back([this] { work(); });
}

EarthObjectBuilder::~EarthObjectBuilder() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
  }
  wake_.notify_all();
  for (auto& worker : workers_) worker.join();
}

void EarthObjectBuilder::request(const EarthNode& node, double priority, int treeDensity) {
  {
    std::lock_guard lock(mutex_);
    const auto key = node.key();
    if (pending_.contains(key)) return;
    pending_[key] = {node, treeDensity};
    queue_.insert({priority, key});
  }
  wake_.notify_one();
}

void EarthObjectBuilder::clearQueue() {
  std::lock_guard lock(mutex_);
  for (const auto& [priority, key] : queue_) pending_.erase(key);
  queue_.clear();
}

std::vector<EarthObjectTile> EarthObjectBuilder::finished(std::size_t limit) {
  std::lock_guard lock(mutex_);
  std::vector<EarthObjectTile> out;
  const std::size_t count = std::min(limit, done_.size());
  for (std::size_t i = 0; i < count; ++i) {
    pending_.erase(done_[i].node.key());
    out.push_back(std::move(done_[i]));
  }
  done_.erase(done_.begin(), done_.begin() + std::ptrdiff_t(count));
  return out;
}

EarthPartLook earthPartLook(EarthPart part) {
  switch (part) {
    case EarthPart::HouseWalls: return {{.60f, .56f, .47f}, 0, .9f, 1, 0, 0};
    case EarthPart::RoofTiles: return {{.30f, .115f, .07f}, 0, .8f, 1, 0, 0};
    case EarthPart::RoofSlate: return {{.13f, .135f, .15f}, 0, .75f, 1, 0, 0};
    case EarthPart::Concrete: return {{.47f, .47f, .45f}, 0, .86f, 2, 0, 0};
    case EarthPart::FlatRoof: return {{.16f, .16f, .17f}, 0, .85f, 1, 0, 0};
    case EarthPart::Glass: return {{.03f, .045f, .06f}, 0, .08f, 0, 0, 1};
    case EarthPart::Cladding: return {{.36f, .40f, .44f}, .25f, .55f, 2, 0, 0};
    case EarthPart::Red: return {{.62f, .07f, .045f}, 0, .6f, 1, 0, 0};
    case EarthPart::White: return {{.80f, .80f, .77f}, 0, .6f, 1, 0, 0};
    case EarthPart::Steel: return {{.38f, .40f, .42f}, .6f, .5f, 1, 0, 0};
    case EarthPart::Dark: return {{.025f, .026f, .03f}, 0, .9f, 0, 0, 0};
    case EarthPart::LampRed: return {{.9f, .15f, .1f}, 0, .5f, 0, 2.1f, 0};
    case EarthPart::Count: break;
  }
  return {{.5f, .5f, .5f}, 0, .8f, 0, 0, 0};
}

bool earthPartCastsShadow(EarthPart part) {
  return part != EarthPart::Glass && part != EarthPart::Dark && part != EarthPart::LampRed;
}

void mergeEarthParts(EarthObjectTile& tile) {
  const auto byte = [](float value) { return std::uint8_t(std::lround(std::clamp(value, 0.f, 1.f) * 255)); };
  std::size_t total = 0;
  for (const auto& part : tile.parts) total += part.size();
  tile.merged.clear();
  tile.merged.reserve(total);
  for (const bool casting : {true, false}) {
    for (std::size_t k = 0; k < kEarthParts; ++k) {
      if (earthPartCastsShadow(EarthPart(k)) != casting) continue;
      const EarthPartLook look = earthPartLook(EarthPart(k));
      EarthBuildingVertex out{};
      for (int c = 0; c < 3; ++c) out.color[c] = byte(std::sqrt(look.color[c]));
      out.color[3] = byte(look.glass);
      out.look[0] = byte(look.metallic);
      out.look[1] = byte(look.roughness);
      out.look[2] = byte(look.detail / 2);
      out.look[3] = byte(look.emissive / 4);
      for (const SurfaceVertex& v : tile.parts[k]) {
        out.x = v.x, out.y = v.y, out.z = v.z;
        out.nx = v.nx, out.ny = v.ny, out.nz = v.nz;
        out.u = v.u, out.v = v.v;
        tile.merged.push_back(out);
      }
      std::vector<SurfaceVertex>().swap(tile.parts[k]);
    }
    if (casting) tile.casters = std::uint32_t(tile.merged.size());
  }
}

void EarthObjectBuilder::work() {
  for (;;) {
    std::pair<EarthNode, int> job;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [&] { return stop_ || !queue_.empty(); });
      if (stop_) return;
      const auto first = queue_.begin();
      job = pending_.at(first->second);
      queue_.erase(first);
    }
    const auto start = std::chrono::steady_clock::now();
    EarthObjectTile tile = objects_.build(job.first, job.second);
    mergeEarthParts(tile);
    buildSeconds_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    ++built_;
    std::lock_guard lock(mutex_);
    done_.push_back(std::move(tile));
  }
}

}  // namespace ofs::client
