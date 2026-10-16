#pragma once
// The patterns laid over the round Earth's land (docs/WHOLE_EARTH_PLAN.md):
// fields, hedges, lanes, woods and the streets of towns. The terrain shader
// (shaders/earth_terrain_fs.glsl) paints them and earth_objects.cpp stands
// trees and buildings on them, so both work from the same functions, written
// once here in C++ and once there in GLSL. Change one, change the other.
//
// Patterns are laid out in metres along a face of the terrain pack's cube
// (`faceMetres`: the face's fraction times its width at the centre), wrapped
// every kEarthPatternWrap so that single precision is enough on a GPU. The
// wrap leaves a seam every thousand kilometres or so, which nobody will find.

#include <algorithm>
#include <cmath>

namespace ofs::client {

inline constexpr double kEarthFaceMetres = 2 * 10001966.;  // a face's width at its centre
inline constexpr double kEarthPatternWrap = 1048576.;
// Texture tiles on the Earth are powers of two in metres no larger than this,
// so the coordinates a chunk carries can start from a multiple of it.
inline constexpr double kEarthTextureBase = 16384.;
inline constexpr float kEarthFieldSize = 512.f, kEarthLaneSize = 2048.f, kEarthFieldWander = 55.f;
// Villages stand one to a square of this size at most.
inline constexpr float kEarthVillageCell = 2400.f;

// Face metres to pattern coordinates.
inline float earthPattern(double faceMetres) { return float(faceMetres - kEarthPatternWrap * std::floor(faceMetres / kEarthPatternWrap)); }

inline float earthFract(float x) { return x - std::floor(x); }
inline float earthSmooth(float a, float b, float x) {
  const float t = std::clamp((x - a) / (b - a), 0.f, 1.f);
  return t * t * (3 - 2 * t);
}

// Sine-free hash (Hoskins), as cellHash in the shaders.
inline float earthHash(float x, float y) {
  float a = earthFract(x * .1031f), b = earthFract(y * .1031f), c = earthFract(x * .1031f);
  const float d = a * (b + 33.33f) + b * (c + 33.33f) + c * (a + 33.33f);
  a += d;
  b += d;
  c += d;
  return earthFract((a + b) * c);
}

// Smooth value noise, 0..1, one value per unit square.
inline float earthNoise(float x, float y) {
  const float ix = std::floor(x), iy = std::floor(y), fx = x - ix, fy = y - iy;
  const float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
  const float a = earthHash(ix, iy), b = earthHash(ix + 1, iy), c = earthHash(ix, iy + 1), d = earthHash(ix + 1, iy + 1);
  return (a + (b - a) * ux) + ((c + (d - c) * ux) - (a + (b - a) * ux)) * uy;
}

// What Natural Earth II's colour (sRGB, 0..1) says covers the land.
struct EarthCoverClass {
  float plants{}, ice{}, arid{}, wood{}, fields{};
};
inline EarthCoverClass earthCoverClass(float r, float g, float b) {
  EarthCoverClass c;
  const float light = r * .30f + g * .59f + b * .11f;
  c.plants = earthSmooth(.05f, .11f, g - .5f * (r + b));
  c.ice = (1 - c.plants) * earthSmooth(-.02f, .04f, b - r) * earthSmooth(.84f, .9f, light);
  c.arid = (1 - c.plants) * (1 - c.ice);
  c.wood = c.plants * earthSmooth(.775f, .72f, light);
  c.fields = c.plants - c.wood;
  return c;
}

// Steep ground that the cover calls farmland is not farmed: it is wooded.
// `steep` is one less the cosine of the slope.
inline void earthSteepWoods(EarthCoverClass& c, float steep) {
  const float woods = earthSmooth(.05f, .10f, steep) * c.fields;
  c.wood += woods;
  c.fields -= woods;
}

// Where land the cover calls wooded is actually under trees: most of it,
// with clearings and ragged edges.
inline float earthWoodland(float px, float py, float wood) {
  const float n = earthNoise(px / 1100.f, py / 1100.f) * .6f + earthNoise(px / 330.f + 17.f, py / 330.f + 17.f) * .4f;
  return wood * earthSmooth(.30f, .46f, n);
}

// Field boundaries and lanes wander a little instead of running dead straight.
inline void earthFieldWander(float& x, float& y) {
  const float ox = x, oy = y;
  x = ox + kEarthFieldWander * (std::sin(oy / 340.f) + .5f * std::sin(ox / 190.f + 1.7f) + .3f * std::sin(oy / 95.f + 4.1f));
  y = oy + kEarthFieldWander * (std::sin(ox / 410.f + .6f) + .5f * std::sin(oy / 230.f + 2.1f) + .3f * std::sin(ox / 83.f + .9f));
}

// Farmland is a scatter of points, one to a square: the ground nearest each
// point is one field (fieldEdge in the shader). Returns how far (qx, qy), in
// squares, is from the edge of its field, with the field's own square and
// its neighbour's across that edge, and the direction to that edge.
struct EarthFieldEdge {
  float gap{};
  float ownX{}, ownY{}, nextX{}, nextY{};
  float towardX{}, towardY{};
};
inline EarthFieldEdge earthFieldEdge(float qx, float qy) {
  const float hx = std::floor(qx), hy = std::floor(qy);
  float nearest = 1e9f, second = 1e9f;
  float ownX = hx, ownY = hy, nextX = hx, nextY = hy, siteX = hx, siteY = hy, otherX = 0, otherY = 0;
  for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i) {
      const float cx = hx + float(i), cy = hy + float(j);
      const float sx = cx + .15f + .7f * earthHash(cx + 5.3f, cy + 5.3f);
      const float sy = cy + .15f + .7f * earthHash(cx + 91.7f, cy + 91.7f);
      const float away = std::hypot(qx - sx, qy - sy);
      if (away < nearest) {
        second = nearest;
        nextX = ownX, nextY = ownY, otherX = siteX, otherY = siteY;
        nearest = away;
        ownX = cx, ownY = cy, siteX = sx, siteY = sy;
      } else if (away < second) {
        second = away;
        nextX = cx, nextY = cy, otherX = sx, otherY = sy;
      }
    }
  float bx = otherX - siteX, by = otherY - siteY;
  const float length = std::max(std::hypot(bx, by), 1e-4f);
  bx /= length;
  by /= length;
  EarthFieldEdge e;
  e.gap = ((siteX + otherX) * .5f - qx) * bx + ((siteY + otherY) * .5f - qy) * by;
  e.ownX = ownX, e.ownY = ownY, e.nextX = nextX, e.nextY = nextY;
  e.towardX = bx, e.towardY = by;
  return e;
}

// The field pattern at a point, in metres (pattern coordinates).
struct EarthField {
  float edge{};          // metres to the field's edge
  float reach{};         // how far either side of the edge its hedge or belt reaches
  bool hedged{}, wooded{};
  float laneGap{};       // metres to the nearest lane
  float hedgeX{}, hedgeY{};  // the nearest point of the edge, wandered back
};
inline EarthField earthField(float px, float py) {
  float wx = px, wy = py;
  earthFieldWander(wx, wy);
  const EarthFieldEdge field = earthFieldEdge(wx / kEarthFieldSize, wy / kEarthFieldSize);
  const EarthFieldEdge lane = earthFieldEdge(wx / kEarthLaneSize + 37.5f, wy / kEarthLaneSize + 37.5f);
  EarthField f;
  f.edge = field.gap * kEarthFieldSize;
  f.laneGap = lane.gap * kEarthLaneSize;
  const float boundary = earthHash(field.ownX + field.nextX + 40.f, field.ownY + field.nextY + 40.f);
  f.hedged = boundary >= .30f;
  f.reach = boundary < .75f ? 4.f : 11.f;
  f.wooded = earthHash(field.ownX + 61.f, field.ownY + 61.f) <= .085f;
  // The nearest point of the edge, taken back out of the wandering.
  const float tx = wx + field.towardX * f.edge, ty = wy + field.towardY * f.edge;
  float x = tx, y = ty;
  for (int round = 0; round < 10; ++round) {
    float mx = x, my = y;
    earthFieldWander(mx, my);
    x += tx - mx;
    y += ty - my;
  }
  f.hedgeX = x;
  f.hedgeY = y;
  return f;
}

// A village's square, in pattern coordinates: whether it holds one, and if so
// where its centre is, how far it spreads and which way its street runs. The
// cover must also be farmland there, which the caller checks.
struct EarthVillage {
  bool present{};
  float x{}, y{}, radius{}, heading{};
};
inline EarthVillage earthVillage(float cellX, float cellY) {
  EarthVillage v;
  v.present = earthHash(cellX + 3.1f, cellY + 7.7f) < .42f;
  v.x = (cellX + .2f + .6f * earthHash(cellX + 11.f, cellY + 13.f)) * kEarthVillageCell;
  v.y = (cellY + .2f + .6f * earthHash(cellX + 17.f, cellY + 19.f)) * kEarthVillageCell;
  v.radius = 170.f + 260.f * earthHash(cellX + 23.f, cellY + 29.f);
  v.heading = earthHash(cellX + 31.f, cellY + 37.f) * 3.14159265f;
  return v;
}

// How built up a town makes ground `distance` metres from its centre: one in
// the middle, fading to nothing at its edge, which is ragged.
inline float earthTownDensity(float distance, float radius, float px, float py) {
  const float ragged = radius * (.78f + .44f * earthNoise(px / 640.f + 3.f, py / 640.f + 5.f));
  return 1 - earthSmooth(.25f * ragged, ragged, distance);
}

// A town of this many people spreads this far, in metres.
inline float earthTownRadius(float population) { return 350.f * std::pow(std::max(population, 300.f) / 1000.f, .42f); }

// Streets. A town is divided into districts, each with a grid of blocks at
// its own angle in the town's axes; a block is kEarthBlockLots lots square with
// a street along one side. A village has one street through its middle along
// its axis and one across it, with houses either side.
inline constexpr float kEarthLot = 24.f;
inline constexpr int kEarthBlockLots = 4;
inline constexpr float kEarthDistrict = 768.f;
inline bool earthStreetLot(int i, int j) {
  const auto m = [](int a) { return ((a % kEarthBlockLots) + kEarthBlockLots) % kEarthBlockLots; };
  return m(i) == kEarthBlockLots - 1 || m(j) == kEarthBlockLots - 1;
}
// A point in a town's axes, in its district's turned axes: the district's
// square, and the point's coordinates about the town's centre turned by the
// district's angle (shader: districtXY).
struct EarthDistrict {
  float cellX{}, cellY{};
  float x{}, y{};
};
inline EarthDistrict earthDistrict(float x, float y) {
  EarthDistrict d;
  d.cellX = std::floor(x / kEarthDistrict);
  d.cellY = std::floor(y / kEarthDistrict);
  const float angle = (earthHash(d.cellX + 71.f, d.cellY + 13.f) - .5f) * .9f;
  const float c = std::cos(angle), s = std::sin(angle);
  d.x = x * c - y * s;
  d.y = x * s + y * c;
  return d;
}
// Whether the lot (i, j) of a district holds no building: a street, or
// partly outside its district.
inline bool earthLotOpen(const EarthDistrict& district, int i, int j) {
  if (earthStreetLot(i, j)) return true;
  // The lot's corners, turned back, must lie in the district.
  const float angle = (earthHash(district.cellX + 71.f, district.cellY + 13.f) - .5f) * .9f;
  const float c = std::cos(angle), s = std::sin(angle);
  for (const float u : {0.f, 1.f})
    for (const float v : {0.f, 1.f}) {
      const float lx = (float(i) + u) * kEarthLot, ly = (float(j) + v) * kEarthLot;
      const float x = lx * c + ly * s, y = -lx * s + ly * c;
      if (std::floor(x / kEarthDistrict) != district.cellX || std::floor(y / kEarthDistrict) != district.cellY) return true;
    }
  return false;
}

}  // namespace ofs::client
