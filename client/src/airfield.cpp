#include "airfield.hpp"

#include "ofs/bases.hpp"
#include "ofs/terrain.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <tuple>
#include <utility>
#include <vector>

namespace ofs::client {
namespace {

struct P {
  float x, y, z;
};
P operator+(P a, P b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
P operator-(P a, P b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
P operator*(P a, float s) { return {a.x * s, a.y * s, a.z * s}; }
P cross(P a, P b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(P a, P b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
P unit(P a) {
  const float length = std::sqrt(dot(a, a));
  return length > 1e-9f ? a * (1 / length) : P{0, 1, 0};
}

// A rectangle of ground, east-west by north-south.
struct Rect {
  float x0, z0, x1, z1;
};

// ---- Layout ---------------------------------------------------------------
// One table of where things are, used both to build the airfield and to answer
// what the ground at a point is used for.

constexpr float kTaxiway = -105;   // centreline of the parallel taxiway
constexpr float kTaxiHalf = 12;
constexpr float kApronWest = -400, kApronEast = kTaxiway - kTaxiHalf, kApronNorth = -1000, kApronSouth = -150;
constexpr std::array<float, 5> kConnectors{-1288, -700, 0, 700, 1288};
constexpr std::array<float, 4> kHangars{-900, -770, -640, -510};
// The dispersal: a loop of taxiway west of the field with a shelter on each stub.
constexpr float kLoopNorth = 400, kLoopSouth = 1000, kLoopWest = -422.5f, kLoopHalf = 7.5f;
constexpr std::array<float, 3> kWestShelters{520, 700, 880}, kSouthShelters{-360, -260, -160};

constexpr Rect kRunway{-kRunwayHalfWidth, -kRunwayHalfLength, kRunwayHalfWidth, kRunwayHalfLength};
constexpr Rect kTaxiwayA{kTaxiway - kTaxiHalf, -kRunwayHalfLength, kTaxiway + kTaxiHalf, kRunwayHalfLength};
constexpr Rect kApron{kApronWest, kApronNorth, kApronEast, kApronSouth};
constexpr Rect kFirePad{-175, -126, kApronEast, -100};
constexpr Rect kCarPark{-590, -420, -492, -220};
constexpr Rect kFuelYard{-648, -905, -532, -765};

std::vector<Rect> asphaltTaxiways() {
  std::vector<Rect> all{kTaxiwayA,
                        {-kRunwayHalfWidth, kRunwayHalfLength, kRunwayHalfWidth, kRunwayHalfLength + 100},     // stopways
                        {-kRunwayHalfWidth, -kRunwayHalfLength - 100, kRunwayHalfWidth, -kRunwayHalfLength},
                        {kLoopWest - kLoopHalf, kLoopNorth - kLoopHalf, kApronEast, kLoopNorth + kLoopHalf},    // dispersal loop
                        {kLoopWest - kLoopHalf, kLoopNorth - kLoopHalf, kLoopWest + kLoopHalf, kLoopSouth + kLoopHalf},
                        {kLoopWest - kLoopHalf, kLoopSouth - kLoopHalf, kApronEast, kLoopSouth + kLoopHalf}};
  for (const float z : kConnectors) all.push_back({kTaxiway + kTaxiHalf, z - 12, -kRunwayHalfWidth, z + 12});
  return all;
}
std::vector<Rect> concreteAreas() {
  std::vector<Rect> all{kApron,
                        kFirePad,
                        {-30, -kRunwayHalfLength, -kRunwayHalfWidth, kRunwayHalfLength},  // runway shoulders
                        {kRunwayHalfWidth, -kRunwayHalfLength, 30, kRunwayHalfLength},
                        kFuelYard};
  // The hardstand in front of each shelter.
  for (const float z : kWestShelters) all.push_back({kLoopWest - kLoopHalf - 22, z - 13, kLoopWest - kLoopHalf, z + 13});
  for (const float x : kSouthShelters) all.push_back({x - 13, kLoopSouth + kLoopHalf, x + 13, kLoopSouth + kLoopHalf + 22});
  return all;
}
std::vector<Rect> roadStrips() {
  return {{-704, -1724, -696, 1724},      // security road inside the fence
          {316, -1724, 324, 1724},
          {-704, -1724, 324, -1716},
          {-704, 1716, 324, 1724},
          {-2400, -326, -590, -314},      // the way in from the west
          kCarPark,
          {-648, -760, -640, -326},       // fuel yard and warehouses
          {-640, -746, -400, -738},
          {324, 300, 462, 313},           // east gate to the country road
          {455, -2800, 469, 2800}};
}

bool inside(const Rect& r, float x, float z) { return x >= r.x0 && x <= r.x1 && z >= r.z0 && z <= r.z1; }

// ---- Geometry ---------------------------------------------------------------

struct Mesher {
  std::vector<SurfaceVertex>& out;

  void vertex(P p, P n, float u, float v) { out.push_back({p.x, p.y, p.z, n.x, n.y, n.z, u, v}); }
  // A triangle wound to face the way its normals point.
  void tri(P a, P b, P c, P na, P nb, P nc) {
    const P face = cross(b - a, c - a);
    if (dot(face, face) < 1e-12f) return;
    if (dot(face, na + nb + nc) < 0) {
      std::swap(b, c);
      std::swap(nb, nc);
    }
    vertex(a, na, 0, 0);
    vertex(b, nb, 1, 0);
    vertex(c, nc, 1, 1);
  }
  void quad(P a, P b, P c, P d, P n) {
    tri(a, b, c, n, n, n);
    tri(a, c, d, n, n, n);
  }
  void quad(P a, P b, P c, P d, P na, P nb, P nc, P nd) {
    tri(a, b, c, na, nb, nc);
    tri(a, c, d, na, nc, nd);
  }
  // A horizontal rectangle at height y, facing up.
  void slab(Rect r, float y) { quad({r.x0, y, r.z0}, {r.x0, y, r.z1}, {r.x1, y, r.z1}, {r.x1, y, r.z0}, {0, 1, 0}); }
  // A box standing on height y: centre, size east-west and north-south, height.
  void box(float x, float z, float w, float d, float y, float h, bool base = false) {
    const float l = x - w / 2, r = x + w / 2, f = z - d / 2, b = z + d / 2, t = y + h;
    quad({l, t, f}, {l, t, b}, {r, t, b}, {r, t, f}, {0, 1, 0});
    quad({l, y, f}, {r, y, f}, {r, t, f}, {l, t, f}, {0, 0, -1});
    quad({r, y, b}, {l, y, b}, {l, t, b}, {r, t, b}, {0, 0, 1});
    quad({l, y, b}, {l, y, f}, {l, t, f}, {l, t, b}, {-1, 0, 0});
    quad({r, y, f}, {r, y, b}, {r, t, b}, {r, t, f}, {1, 0, 0});
    if (base) quad({l, y, f}, {r, y, f}, {r, y, b}, {l, y, b}, {0, -1, 0});
  }
  void box(Rect r, float y, float h) { box((r.x0 + r.x1) / 2, (r.z0 + r.z1) / 2, r.x1 - r.x0, r.z1 - r.z0, y, h); }
  // A box between two points, square in section: masts, rails and booms.
  void beam(P a, P b, float thickness) {
    const P along = unit(b - a);
    const P side = unit(std::abs(along.y) > .9f ? cross(along, {1, 0, 0}) : cross(along, {0, 1, 0}));
    const P up = cross(side, along);
    const P s = side * (thickness / 2), u = up * (thickness / 2);
    const P corners[4]{s + u, u - s, P{0, 0, 0} - s - u, s - u};
    const P normals[4]{unit(s + u), unit(u - s), unit(P{0, 0, 0} - s - u), unit(s - u)};
    for (int i = 0; i < 4; ++i) {
      const int j = (i + 1) % 4;
      const P n = unit(normals[i] + normals[j]);
      quad(a + corners[i], a + corners[j], b + corners[j], b + corners[i], n);
    }
    quad(b + corners[0], b + corners[1], b + corners[2], b + corners[3], along);
  }
  // An upright cylinder or cone frustum, closed on top.
  void cylinder(float x, float z, float y0, float y1, float r0, float r1, int sides = 20, bool cap = true) {
    const float slope = (r0 - r1) / std::max(y1 - y0, 1e-3f);
    for (int i = 0; i < sides; ++i) {
      const float a = 6.2831853f * float(i) / float(sides), b = 6.2831853f * float(i + 1) / float(sides);
      const P na = unit({std::cos(a), slope, std::sin(a)}), nb = unit({std::cos(b), slope, std::sin(b)});
      const P a0{x + std::cos(a) * r0, y0, z + std::sin(a) * r0}, b0{x + std::cos(b) * r0, y0, z + std::sin(b) * r0};
      const P a1{x + std::cos(a) * r1, y1, z + std::sin(a) * r1}, b1{x + std::cos(b) * r1, y1, z + std::sin(b) * r1};
      quad(a0, b0, b1, a1, na, nb, nb, na);
      if (cap && r1 > 1e-3f) tri({x, y1, z}, a1, b1, {0, 1, 0}, {0, 1, 0}, {0, 1, 0});
    }
  }
  // The upper part of a sphere, down to `from` radians below the pole.
  void dome(float x, float y, float z, float radius, float from = 1.5708f, int sides = 20, int rings = 8) {
    const auto point = [&](int ring, int side) {
      const float polar = from * float(ring) / float(rings), around = 6.2831853f * float(side) / float(sides);
      const P n{std::sin(polar) * std::cos(around), std::cos(polar), std::sin(polar) * std::sin(around)};
      return std::pair{P{x, y, z} + n * radius, n};
    };
    for (int ring = 0; ring < rings; ++ring)
      for (int side = 0; side < sides; ++side) {
        const auto [a, na] = point(ring, side);
        const auto [b, nb] = point(ring, side + 1);
        const auto [c, nc] = point(ring + 1, side + 1);
        const auto [d, nd] = point(ring + 1, side);
        quad(a, b, c, d, na, nb, nc, nd);
      }
  }
  // A barrel vault standing on the ground, centred on (cx, cz): `length` along
  // `axis` (0 = east-west, 1 = north-south), `width` across and `height` at the
  // crown. The end walls are separate; see vaultEnd.
  void vault(float cx, float cz, int axis, float length, float width, float height, int segments = 14) {
    const auto point = [&](float along, int segment) {
      const float angle = 3.14159265f * float(segment) / float(segments);
      const float across = -std::cos(angle) * width / 2, up = std::sin(angle) * height;
      // The normal of an ellipse: its gradient, not the direction from the centre.
      const float flat = -std::cos(angle) / (width / 2);
      const P normal = axis == 0 ? unit({0, std::sin(angle) / height, flat}) : unit({flat, std::sin(angle) / height, 0});
      return std::pair{axis == 0 ? P{cx + along, up, cz + across} : P{cx + across, up, cz + along}, normal};
    };
    for (int segment = 0; segment < segments; ++segment) {
      const auto [a, na] = point(-length / 2, segment);
      const auto [b, nb] = point(-length / 2, segment + 1);
      const auto [c, nc] = point(length / 2, segment + 1);
      const auto [d, nd] = point(length / 2, segment);
      quad(a, b, c, d, na, nb, nc, nd);
    }
  }
  // The wall that closes one end of a vault, with an opening left in it:
  // `open` of the width and `clear` of the height, from the ground up.
  void vaultEnd(float cx, float cz, int axis, float at, float width, float height, float open, float clear, float facing,
                int segments = 14) {
    const P normal = axis == 0 ? P{facing, 0, 0} : P{0, 0, facing};
    const auto place = [&](float across, float up) { return axis == 0 ? P{cx + at, up, cz + across} : P{cx + across, up, cz + at}; };
    const float half = width / 2 * open, top = height * clear;
    for (int segment = 0; segment < segments; ++segment) {
      const float a0 = 3.14159265f * float(segment) / float(segments), a1 = 3.14159265f * float(segment + 1) / float(segments);
      const float x0 = -std::cos(a0) * width / 2, x1 = -std::cos(a1) * width / 2;
      const float y0 = std::sin(a0) * height, y1 = std::sin(a1) * height;
      // Fill from the opening's edge up to the vault over this slice.
      const auto lintel = [&](float x) { return std::abs(x) < half ? top : 0.f; };
      const float f0 = std::min(lintel(x0), y0), f1 = std::min(lintel(x1), y1);
      quad(place(x0, f0), place(x1, f1), place(x1, y1), place(x0, y0), normal);
    }
  }
};

// Seven-segment digits for the runway numbers and the stands, drawn as paint.
// `forward` is the way a reader faces: -1 reads flying north, +1 flying south.
void digit(Mesher& paint, int value, float x, float z, float width, float height, float stroke, float forward, float y) {
  // Segments: 0 top, 1 upper right, 2 lower right, 3 bottom, 4 lower left, 5 upper left, 6 middle.
  static const std::uint8_t kSegments[10]{0b0111111, 0b0000110, 0b1011011, 0b1001111, 0b1100110,
                                          0b1101101, 0b1111101, 0b0000111, 0b1111111, 0b1101111};
  // In the reader's frame: +u to the right, +v away from the reader (up the digit).
  const auto rect = [&](float u0, float v0, float u1, float v1) {
    // Facing north (-Z), the reader's right is east; facing south it is west.
    const float sx = forward < 0 ? 1.f : -1.f;
    const float xa = x + sx * u0, xb = x + sx * u1, za = z + forward * v0, zb = z + forward * v1;
    paint.slab({std::min(xa, xb), std::min(za, zb), std::max(xa, xb), std::max(za, zb)}, y);
  };
  const float w = width / 2, h = height / 2, s = stroke;
  const std::uint8_t on = kSegments[value % 10];
  if (on & 1) rect(-w, h - s, w, h);
  if (on & 2) rect(w - s, 0, w, h);
  if (on & 4) rect(w - s, -h, w, 0);
  if (on & 8) rect(-w, -h, w, -h + s);
  if (on & 16) rect(-w, -h, -w + s, 0);
  if (on & 32) rect(-w, 0, -w + s, h);
  if (on & 64) rect(-w, -s / 2, w, s / 2);
}

}  // namespace

AirfieldUse airfieldUse(double north, double east) {
  // Measured from whichever airfield is nearest.
  const AirfieldSite* nearest = &kAirfieldSites[0];
  for (const auto& site : kAirfieldSites)
    if (std::hypot(north - site.north, east - site.east) < std::hypot(north - nearest->north, east - nearest->east)) nearest = &site;
  const float x = float(east - nearest->east), z = float(-(north - nearest->north));
  static const std::vector<Rect> paved = [] {
    auto all = asphaltTaxiways();
    const auto concrete = concreteAreas(), roads = roadStrips();
    all.push_back(kRunway);
    all.insert(all.end(), concrete.begin(), concrete.end());
    all.insert(all.end(), roads.begin(), roads.end());
    return all;
  }();
  for (const auto& rect : paved)
    if (inside(rect, x, z)) return AirfieldUse::Paved;
  return x > kFenceWest && x < kFenceEast && z > kFenceNorth && z < kFenceSouth ? AirfieldUse::Grass : AirfieldUse::Outside;
}

Airfield buildAirfield() {
  Airfield field;
  for (std::size_t i = 0; i < field.parts.size(); ++i) field.parts[i].material = AirfieldMaterial(i);
  const auto part = [&](AirfieldMaterial material) { return Mesher{field.parts[std::size_t(material)].vertices}; };
  using M = AirfieldMaterial;
  Mesher runway{field.ground.runway}, taxi{field.ground.taxiways}, concrete{field.ground.concrete}, road{field.ground.roads};
  Mesher white{field.ground.whitePaint}, yellow{field.ground.yellowPaint};
  Mesher walls = part(M::Concrete), cladding = part(M::Cladding), roof = part(M::Roof), shelter = part(M::Shelter);
  Mesher glass = part(M::Glass), dark = part(M::Dark), tank = part(M::White), red = part(M::Red), steel = part(M::Steel);
  Mesher olive = part(M::Olive), gse = part(M::Yellow), carLight = part(M::CarLight), carDark = part(M::CarDark);
  Mesher lampWhite = part(M::LightWhite), lampAmber = part(M::LightAmber), lampRed = part(M::LightRed);
  Mesher lampGreen = part(M::LightGreen), lampBlue = part(M::LightBlue);
  const auto building = [&](Rect r, const char* name) { field.buildings.push_back({r.x0, r.z0, r.x1, r.z1, name}); };
  const auto movement = [&](Rect r, const char* name) { field.movement.push_back({r.x0, r.z0, r.x1, r.z1, name}); };

  // Paving and paint are coplanar with the ground; the terrain shader orders
  // them by layer. The small lifts only keep them apart for the shadow pass.
  constexpr float kPaving = .05f, kPaint = .10f;

  // ---- Paving ---------------------------------------------------------------
  runway.slab(kRunway, kPaving);
  movement(kRunway, "runway");
  for (const auto& rect : asphaltTaxiways()) {
    taxi.slab(rect, kPaving);
    movement(rect, "taxiway");
  }
  for (const auto& rect : concreteAreas()) concrete.slab(rect, kPaving - .01f);
  movement(kApron, "apron");
  for (const auto& rect : roadStrips()) road.slab(rect, kPaving - .02f);

  // ---- Runway markings ------------------------------------------------------
  const float half = kRunwayHalfLength, edge = kRunwayHalfWidth;
  for (const float side : {-1.f, 1.f}) white.slab({side * (edge - 1.6f) - .45f, -half + 6, side * (edge - 1.6f) + .45f, half - 6}, kPaint);
  // `end` is +1 at the southern threshold, which is landed over flying north.
  for (const float end : {-1.f, 1.f}) {
    const auto along = [&](float from, float to) {  // metres in from the threshold
      const float a = end * (half - from), b = end * (half - to);
      return std::pair{std::min(a, b), std::max(a, b)};
    };
    // Threshold bar and the piano keys behind it.
    auto [z0, z1] = along(1.5f, 3.3f);
    white.slab({-edge + 1.6f, z0, edge - 1.6f, z1}, kPaint);
    std::tie(z0, z1) = along(6, 36);
    for (int i = 0; i < 6; ++i)
      for (const float side : {-1.f, 1.f}) {
        const float x = side * (2.7f + 3.3f * float(i));
        white.slab({x - .9f, z0, x + .9f, z1}, kPaint);
      }
    // The runway's number: 36 heading north, 18 heading south.
    const int number = end > 0 ? 36 : 18;
    const float numberZ = end * (half - 54);
    const float forward = -end;  // the reader flies away from this end
    const float sx = forward < 0 ? 1.f : -1.f;
    digit(white, number / 10, -3.6f * sx, numberZ, 4.2f, 15, 1.1f, forward, kPaint);
    digit(white, number % 10, 3.6f * sx, numberZ, 4.2f, 15, 1.1f, forward, kPaint);
    // Aiming point, and touchdown zone bars either side of the centreline.
    std::tie(z0, z1) = along(400, 450);
    for (const float side : {-1.f, 1.f}) white.slab({side * 9 - 3, z0, side * 9 + 3, z1}, kPaint);
    const std::pair<float, int> zones[]{{150, 3}, {300, 2}, {550, 2}, {700, 1}, {850, 1}};
    for (const auto& [distance, bars] : zones) {
      std::tie(z0, z1) = along(distance, distance + 22.5f);
      for (int bar = 0; bar < bars; ++bar)
        for (const float side : {-1.f, 1.f}) {
          const float x = side * (9.9f + 3.3f * float(bar));
          white.slab({x - .9f, z0, x + .9f, z1}, kPaint);
        }
    }
    // Stopway chevrons, pointing at the threshold.
    for (int i = 0; i < 4; ++i) {
      const float tip = end * (half + 8 + 24 * float(i)), wing = tip + end * 20;
      for (const float side : {-1.f, 1.f}) {
        const P a{0, kPaint, tip}, b{side * 20, kPaint, wing}, c{side * 20, kPaint, wing + end * 2.2f}, d{0, kPaint, tip + end * 2.2f};
        yellow.quad(a, b, c, d, {0, 1, 0});
      }
    }
  }
  // Centreline dashes between the numbers.
  for (float z = -half + 78; z < half - 100; z += 50) white.slab({-.45f, z, .45f, z + 30}, kPaint);

  // ---- Taxiway markings -----------------------------------------------------
  const auto line = [&](Mesher& paint, P a, P b, float width) {
    const P along = unit(b - a), across = unit(cross({0, 1, 0}, along)) * (width / 2);
    paint.quad(a - across, a + across, b + across, b - across, {0, 1, 0});
  };
  line(yellow, {kTaxiway, kPaint, -half + 12}, {kTaxiway, kPaint, half - 12}, .3f);
  for (const float side : {-1.f, 1.f})
    line(yellow, {kTaxiway + side * (kTaxiHalf - .8f), kPaint, -half}, {kTaxiway + side * (kTaxiHalf - .8f), kPaint, half}, .15f);
  for (const float z : kConnectors) {
    line(yellow, {kTaxiway, kPaint, z}, {-kRunwayHalfWidth - 1, kPaint, z}, .3f);
    // Holding position: two solid lines and two broken ones, 60 m from the runway.
    for (int i = 0; i < 4; ++i) {
      const float x = -60 - .45f * float(i) * 2;
      if (i < 2) line(yellow, {x, kPaint, z - 11}, {x, kPaint, z + 11}, .3f);
      else for (float dash = -11; dash < 11; dash += 1.8f) line(yellow, {x, kPaint, z + dash}, {x, kPaint, z + dash + .9f}, .3f);
    }
  }
  // The dispersal loop, and the way into each shelter.
  line(yellow, {kLoopWest, kPaint, kLoopNorth}, {kTaxiway, kPaint, kLoopNorth}, .3f);
  line(yellow, {kLoopWest, kPaint, kLoopNorth}, {kLoopWest, kPaint, kLoopSouth}, .3f);
  line(yellow, {kLoopWest, kPaint, kLoopSouth}, {kTaxiway, kPaint, kLoopSouth}, .3f);
  for (const float z : kWestShelters) line(yellow, {kLoopWest, kPaint, z}, {kLoopWest - kLoopHalf - 20, kPaint, z}, .3f);
  for (const float x : kSouthShelters) line(yellow, {x, kPaint, kLoopSouth}, {x, kPaint, kLoopSouth + kLoopHalf + 20}, .3f);
  // Apron: a taxilane along its east side, lead-in lines to the hangars and
  // numbered stands in front of the terminal.
  const float lane = kApronEast - 40;
  line(yellow, {lane, kPaint, kApronNorth + 20}, {lane, kPaint, kApronSouth - 20}, .3f);
  for (const float z : {kApronNorth + 60, -640.f, kApronSouth - 60}) line(yellow, {lane, kPaint, z}, {kTaxiway, kPaint, z}, .3f);
  for (const float z : kHangars) line(yellow, {lane, kPaint, z}, {kApronWest + 6, kPaint, z}, .3f);
  int stand = 1;
  for (const float z : {-385.f, -330.f, -275.f, -220.f}) {
    line(yellow, {lane, kPaint, z}, {kApronWest + 42, kPaint, z}, .3f);
    white.slab({kApronWest + 40.5f, z - 9, kApronWest + 41.5f, z + 9}, kPaint);   // stop bar
    digit(white, stand++, kApronWest + 50, z + 15, 2.4f, 5, .6f, 1, kPaint);
    for (const float side : {-1.f, 1.f})  // the edges of the stand
      white.slab({kApronWest + 42, z + side * 26 - .15f, lane - 12, z + side * 26 + .15f}, kPaint);
  }
  // Car park bays.
  for (float z = kCarPark.z0 + 6; z < kCarPark.z1 - 4; z += 2.8f)
    for (const float x : {kCarPark.x0 + 6, kCarPark.x0 + 34, kCarPark.x0 + 62}) white.slab({x, z - .06f, x + 22, z + .06f}, kPaint);

  // ---- Hangars --------------------------------------------------------------
  // Arched sheds whose doors stand open onto the apron.
  for (const float z : kHangars) {
    constexpr float depth = 75, width = 106, height = 24;
    const float cx = kApronWest - 4 - depth / 2;
    cladding.vault(cx, z, 0, depth, width, height);
    cladding.vaultEnd(cx, z, 0, -depth / 2, width, height, 0, 0, -1);
    cladding.vaultEnd(cx, z, 0, depth / 2, width, height, .78f, .60f, 1);
    // What shows through the doorway, and the doors run back either side.
    dark.quad({cx + depth / 2 - 1.2f, 0, z - width * .39f}, {cx + depth / 2 - 1.2f, 0, z + width * .39f},
              {cx + depth / 2 - 1.2f, height * .6f, z + width * .39f}, {cx + depth / 2 - 1.2f, height * .6f, z - width * .39f}, {1, 0, 0});
    for (const float side : {-1.f, 1.f}) {
      steel.box(cx + depth / 2 + .6f, z + side * (width * .39f + 9), .5f, 18, 0, height * .58f);
      red.box(cx + depth / 2 + .9f, z + side * (width * .39f + 9), .12f, 18, height * .52f, 1.1f);
    }
    roof.box(cx, z, depth * .8f, 3, height - .4f, 1.2f);  // ridge vent
    building({cx - depth / 2, z - width / 2, cx + depth / 2, z + width / 2}, "hangar");
  }

  // ---- Terminal ---------------------------------------------------------------
  {
    const Rect body{-474, -420, kApronWest - 4, -200};
    walls.box(body, 0, 13);
    roof.box({body.x0 - 2, body.z0 - 2, body.x1 + 5, body.z1 + 2}, 13, .9f);
    // Airside glazing, and a band of windows landside.
    glass.box(body.x1 + .15f, (body.z0 + body.z1) / 2, .3f, body.z1 - body.z0 - 12, 3.2f, 8.4f);
    glass.box(body.x0 - .15f, (body.z0 + body.z1) / 2, .3f, body.z1 - body.z0 - 30, 6.5f, 3.4f);
    for (float z = body.z0 + 10; z < body.z1 - 6; z += 12.5f) steel.box(body.x1 + .4f, z, .35f, .5f, 3.2f, 8.4f);
    // Entrance canopy and the plant on the roof.
    roof.box(body.x0 - 8, (body.z0 + body.z1) / 2, 12, 70, 5.2f, .5f, true);
    for (const float z : {-340.f, -280.f}) steel.box(body.x0 - 13, z, .5f, .5f, 0, 5.2f);
    walls.box(body.x0 + 22, body.z0 + 40, 14, 22, 13.9f, 3.2f);
    walls.box(body.x0 + 26, body.z1 - 46, 10, 30, 13.9f, 2.4f);
    // A walkway out to each of the first two stands.
    for (const float z : {-357.f, -302.f}) {
      walls.box(body.x1 + 15, z, 30, 3.4f, 4.2f, 3, true);
      glass.box(body.x1 + 15, z - 1.75f, 26, .1f, 5, 1.5f);
      steel.cylinder(body.x1 + 30, z, 0, 7.4f, 2.6f, 2.6f, 12);
      steel.box(body.x1 + 12, z, .7f, .7f, 0, 4.2f);
    }
    building(body, "terminal");
  }

  // ---- Control tower ----------------------------------------------------------
  {
    constexpr float x = -300, z = -100;
    walls.box(x, z, 26, 20, 0, 6.5f);
    roof.box(x, z, 27, 21, 6.5f, .5f);
    glass.box(x, z - 10.1f, 18, .2f, 2.2f, 2.6f);
    walls.cylinder(x, z, 6.5f, 34, 4.6f, 3.9f, 8);
    red.cylinder(x, z, 29.5f, 31.5f, 4.15f, 4.08f, 8, false);     // obstruction band
    walls.cylinder(x, z, 34, 35.4f, 4.4f, 8.8f, 8, false);        // the flare under the cab
    glass.cylinder(x, z, 35.4f, 39.4f, 7.6f, 8.6f, 8, false);     // outward-leaning glazing
    for (int i = 0; i < 8; ++i) {
      const float a = 6.2831853f * float(i) / 8;
      steel.beam({x + std::cos(a) * 7.6f, 35.4f, z + std::sin(a) * 7.6f}, {x + std::cos(a) * 8.6f, 39.4f, z + std::sin(a) * 8.6f}, .3f);
    }
    roof.cylinder(x, z, 39.4f, 40.3f, 9.3f, 9.3f, 8);
    walls.cylinder(x, z, 40.3f, 42, 2.4f, 2.0f, 8);
    steel.beam({x, 42, z}, {x, 50, z}, .22f);
    steel.beam({x - 1.5f, 46, z}, {x + 1.5f, 46, z}, .12f);
    lampRed.box(x, z, .5f, .5f, 50, .5f);
    building({x - 13, z - 10, x + 13, z + 10}, "control tower");
  }

  // ---- Fire station -----------------------------------------------------------
  {
    const Rect body{-215, -128, -175, -98};
    walls.box(body, 0, 7);
    roof.box({body.x0 - 1, body.z0 - 1, body.x1 + 1, body.z1 + 1}, 7, .6f);
    for (const float z : {-121.f, -113.f, -105.f}) red.box(body.x1 + .12f, z, .25f, 6.2f, 0, 5.2f);
    walls.box(body.x0 + 5, body.z0 + 5, 6, 6, 7.6f, 7);  // hose tower
    // Two tenders stand ready outside.
    for (const float z : {-121.f, -105.f}) {
      red.box(-160, z, 9.5f, 3, .9f, 2.6f, true);
      dark.box(-156.8f, z, 2.2f, 3.05f, 2.1f, 1.1f);
      for (const float wheel : {-3.2f, 0.f, 3.2f}) dark.box(-160 + wheel, z, 1.1f, 3.1f, 0, 1.1f);
    }
    building(body, "fire station");
  }

  // ---- Fuel farm and warehouses -----------------------------------------------
  for (const float x : {-616.f, -566.f})
    for (const float z : {-868.f, -802.f}) {
      tank.cylinder(x, z, 0, 15, 17, 17, 28, false);
      tank.cylinder(x, z, 15, 18.5f, 17, 0.f, 28, false);
      steel.cylinder(x, z, 14.6f, 15.2f, 17.25f, 17.25f, 28, false);
      steel.beam({x + 17.3f, 0, z}, {x + 17.3f, 16, z}, .25f);   // ladder
      building({x - 17, z - 17, x + 17, z + 17}, "fuel tank");
    }
  for (const float z : {kFuelYard.z0, kFuelYard.z1}) walls.box((kFuelYard.x0 + kFuelYard.x1) / 2, z, kFuelYard.x1 - kFuelYard.x0, .5f, 0, 1.4f);
  for (const float x : {kFuelYard.x0, kFuelYard.x1}) walls.box(x, (kFuelYard.z0 + kFuelYard.z1) / 2, .5f, kFuelYard.z1 - kFuelYard.z0, 0, 1.4f);
  steel.beam({kFuelYard.x1, .7f, -835}, {kApronWest - 82, .7f, -835}, .35f);  // the pipe to the apron
  for (const float z : {-690.f, -610.f}) {
    // Portal-framed sheds with a pitched roof.
    const Rect body{-632, z - 28, -548, z + 28};
    cladding.box(body, 0, 9);
    const P ridgeA{body.x0 - 1, 14, z}, ridgeB{body.x1 + 1, 14, z};
    for (const float side : {-1.f, 1.f}) {
      const P eaveA{body.x0 - 1, 8.8f, z + side * 29.5f}, eaveB{body.x1 + 1, 8.8f, z + side * 29.5f};
      roof.quad(eaveA, eaveB, ridgeB, ridgeA, unit({0, 29.5f, side * 5.2f}));
    }
    for (const float x : {body.x0, body.x1})
      cladding.tri({x, 9, z - 28}, {x, 9, z + 28}, {x, 14, z}, {x < -590 ? -1.f : 1.f, 0, 0}, {x < -590 ? -1.f : 1.f, 0, 0},
                   {x < -590 ? -1.f : 1.f, 0, 0});
    for (const float door : {-14.f, 14.f}) dark.box(body.x1 + .1f, z + door, .2f, 12, 0, 6.5f);
    building(body, "warehouse");
  }

  // ---- Radar ------------------------------------------------------------------
  {
    constexpr float x = -610, z = -80;
    walls.box(x, z, 12, 12, 0, 4);
    for (const float sx : {-1.f, 1.f})
      for (const float sz : {-1.f, 1.f}) {
        steel.beam({x + sx * 4, 4, z + sz * 4}, {x + sx * 2.2f, 22, z + sz * 2.2f}, .35f);
        steel.beam({x + sx * 4, 4, z + sz * 4}, {x - sx * 3.1f, 13, z + sz * 3.1f}, .16f);
        steel.beam({x + sx * 3.1f, 13, z + sz * 3.1f}, {x + sx * 2.2f, 22, z - sz * 2.2f}, .16f);
      }
    steel.box(x, z, 8, 8, 22, .4f, true);
    tank.dome(x, 26.5f, z, 6.5f, 2.35f);
    lampRed.box(x, z, .4f, .4f, 33, .4f);
    building({x - 6, z - 6, x + 6, z + 6}, "radar");
  }

  // ---- Hardened shelters --------------------------------------------------------
  // Thick concrete arches, one for each fighter, their doors rolled half open.
  const auto shelterAt = [&](float cx, float cz, int axis, float mouth) {
    constexpr float depth = 34, width = 27, height = 9.5f;
    shelter.vault(cx, cz, axis, depth, width, height, 10);
    shelter.vaultEnd(cx, cz, axis, -mouth * depth / 2, width, height, 0, 0, -mouth, 10);
    shelter.vaultEnd(cx, cz, axis, mouth * depth / 2, width, height, .74f, .72f, mouth, 10);
    const float at = mouth * (depth / 2 - .8f);
    if (axis == 0) {
      dark.quad({cx + at, 0, cz - width * .37f}, {cx + at, 0, cz + width * .37f}, {cx + at, height * .72f, cz + width * .37f},
                {cx + at, height * .72f, cz - width * .37f}, {mouth, 0, 0});
      shelter.box(cx + mouth * (depth / 2 + .5f), cz - width * .37f + 3.6f, .7f, 7.2f, 0, height * .7f);
      building({cx - depth / 2, cz - width / 2, cx + depth / 2, cz + width / 2}, "shelter");
    } else {
      dark.quad({cx - width * .37f, 0, cz + at}, {cx + width * .37f, 0, cz + at}, {cx + width * .37f, height * .72f, cz + at},
                {cx - width * .37f, height * .72f, cz + at}, {0, 0, mouth});
      shelter.box(cx - width * .37f + 3.6f, cz + mouth * (depth / 2 + .5f), 7.2f, .7f, 0, height * .7f);
      building({cx - width / 2, cz - depth / 2, cx + width / 2, cz + depth / 2}, "shelter");
    }
  };
  for (const float z : kWestShelters) shelterAt(kLoopWest - kLoopHalf - 22 - 17, z, 0, 1);
  for (const float x : kSouthShelters) shelterAt(x, kLoopSouth + kLoopHalf + 22 + 17, 1, -1);

  // ---- Lighting ---------------------------------------------------------------
  const auto lamp = [&](Mesher& colour, float x, float z, float height = .28f, float size = .36f) {
    colour.box(x, z, size, size, height, size * .8f);
  };
  for (float z = -half; z <= half + 1; z += 50) {
    // White along the runway, amber over the last 600 m of each direction.
    const bool caution = half - std::abs(z) < 600;
    for (const float x : {-24.5f, 24.5f}) lamp(caution ? lampAmber : lampWhite, x, z);
  }
  for (const float end : {-1.f, 1.f}) {
    for (float x = -21; x <= 21; x += 3) {
      lamp(lampGreen, x, end * (half + 1.2f));   // the threshold, seen on approach
      lamp(lampRed, x, end * (half - 1.2f));     // the end, seen from the runway
    }
    // Approach lights: a centreline of bars out to 390 m with a crossbar at 300 m.
    for (float distance = 30; distance <= 390; distance += 30) {
      const float z = end * (half + distance), mast = distance > 100 ? 1.6f : .25f;
      for (float x = -4; x <= 4; x += 2) lamp(lampWhite, x, z, mast, .42f);
      if (mast > 1) steel.box(0, z, 9, .12f, mast - .12f, .12f);
      if (mast > 1) for (const float x : {-4.f, 4.f}) steel.box(x, z, .12f, .12f, 0, mast);
      if (distance == 300)
        for (float x = -15; x <= 15; x += 2.5f)
          if (std::abs(x) > 5) lamp(lampWhite, x, z, mast, .42f);
    }
    // PAPI: four units left of the runway at the aiming point. Two white and
    // two red is the picture of an aircraft on the glide path.
    const float papiZ = end * (half - 420), left = end > 0 ? -1.f : 1.f;
    for (int i = 0; i < 4; ++i) {
      const float x = left * (36 + 9 * float(i));
      steel.box(x, papiZ, 1.6f, 1.1f, .25f, .7f, true);
      lamp(i < 2 ? lampRed : lampWhite, x, papiZ - end * .7f, .45f, .5f);
    }
  }
  for (float z = -half; z <= half + 1; z += 40)
    for (const float x : {kTaxiway - kTaxiHalf - .8f, kTaxiway + kTaxiHalf + .8f}) lamp(lampBlue, x, z, .25f, .3f);
  for (float x = kLoopWest; x < kApronEast; x += 40)
    for (const float z : {kLoopNorth - kLoopHalf - .8f, kLoopSouth + kLoopHalf + .8f}) lamp(lampBlue, x, z, .25f, .3f);
  // Floodlight masts along the buildings' side of the apron.
  std::vector<std::pair<float, float>> masts{{-388, -150}, {-290, kApronSouth + 4}, {-190, kApronSouth + 4}, {-200, kApronNorth - 4},
                                             {-320, kApronNorth - 4}};
  for (std::size_t i = 0; i + 1 < kHangars.size(); ++i) masts.push_back({kApronWest - 1, (kHangars[i] + kHangars[i + 1]) / 2});
  masts.push_back({kApronWest - 1, -437});
  masts.push_back({kApronWest - 1, kApronNorth + 10});
  for (const auto& [x, z] : masts) {
    steel.cylinder(x, z, 0, 26, .45f, .22f, 8);
    steel.box(x, z, 4.2f, 1.2f, 26, .5f, true);
    lampWhite.box(x + .8f, z, 3.6f, 1, 25.75f, .26f, true);
  }

  // ---- Windsock and signals -----------------------------------------------------
  for (const float z : {-150.f, 760.f}) {
    constexpr float x = 62;
    steel.cylinder(x, z, 0, 7, .14f, .1f, 8);
    // The sock stands out to the south-east in the prevailing wind, banded red and white.
    const P root{x, 6.7f, z};
    const P along = unit({.75f, -.12f, .65f});
    for (int band = 0; band < 5; ++band) {
      Mesher& colour = band % 2 ? tank : red;
      const float r0 = .55f - .08f * float(band), r1 = .55f - .08f * float(band + 1);
      const P a = root + along * (.75f * float(band)), b = root + along * (.75f * float(band + 1));
      const P side = unit(cross(along, {0, 1, 0})), up = cross(side, along);
      for (int i = 0; i < 8; ++i) {
        const float p = 6.2831853f * float(i) / 8, q = 6.2831853f * float(i + 1) / 8;
        const P np = side * std::cos(p) + up * std::sin(p), nq = side * std::cos(q) + up * std::sin(q);
        colour.quad(a + np * r0, a + nq * r0, b + nq * r1, b + np * r1, np, nq, nq, np);
      }
    }
    // The white circle a windsock stands in.
    for (int i = 0; i < 24; ++i) {
      const float p = 6.2831853f * float(i) / 24, q = 6.2831853f * float(i + 1) / 24;
      white.quad({x + std::cos(p) * 7, kPaint, z + std::sin(p) * 7}, {x + std::cos(q) * 7, kPaint, z + std::sin(q) * 7},
                 {x + std::cos(q) * 7.6f, kPaint, z + std::sin(q) * 7.6f}, {x + std::cos(p) * 7.6f, kPaint, z + std::sin(p) * 7.6f},
                 {0, 1, 0});
    }
  }

  // ---- Vehicles -----------------------------------------------------------------
  // Parked square to the apron, nose to the east or, with `south`, to the south.
  const auto vehicle = [&](Mesher& paint, float x, float z, float length, float width, float height, bool south, float cab) {
    const float w = south ? width : length, d = south ? length : width;
    paint.box(x, z, w, d, .45f, height, true);
    // Cab glass at the front and a dark underside with the wheels.
    const float nose = length / 2 - cab / 2;
    dark.box(south ? x : x + nose, south ? z + nose : z, (south ? width : cab) + .04f, (south ? cab : width) + .04f, height * .55f + .45f,
             height * .32f);
    for (const float axle : {-length * .32f, length * .32f})
      dark.box(south ? x : x + axle, south ? z + axle : z, south ? width + .1f : .9f, south ? .9f : width + .1f, 0, .9f);
  };
  // Fuel bowsers by the hangars, tugs and steps by the terminal stands.
  for (const float z : {-835.f, -705.f, -575.f}) {
    vehicle(olive, kApronWest + 26, z, 11, 2.7f, 2.9f, true, 2.4f);
    tank.cylinder(kApronWest + 26, z - 1.3f, 3.35f, 3.6f, 1.1f, 1.1f, 10);
  }
  for (const float z : {-398.f, -343.f, -288.f, -233.f}) {
    vehicle(gse, kApronWest + 16, z, 4.2f, 2, 1.5f, false, 1.2f);
    vehicle(gse, kApronWest + 24, z + 5, 6.5f, 2.2f, 1.2f, true, 1.5f);
    for (int cart = 0; cart < 3; ++cart) carLight.box(kApronWest + 12, z + 12 + 3.4f * float(cart), 2, 3, .5f, 1.5f, true);
  }
  vehicle(olive, -250, kLoopNorth - kLoopHalf - 6, 7.5f, 2.5f, 2.6f, false, 2);
  vehicle(olive, kLoopWest + kLoopHalf + 6, 610, 5, 2.1f, 1.9f, true, 1.6f);
  // The car park, about two thirds full: three aisles of bays, two cars deep.
  for (int row = 0; row < 3; ++row)
    for (int slot = 0; slot < 68; ++slot)
      for (int depth = 0; depth < 2; ++depth) {
        const float chance = sceneryRandom(std::uint32_t(row * 977 + slot * 131 + depth * 53 + 17));
        if (chance > .66f) continue;
        const float x = kCarPark.x0 + 11.5f + 28 * float(row) + 11 * float(depth), z = kCarPark.z0 + 7.4f + 2.8f * float(slot);
        Mesher& paint = chance < .22f ? carDark : chance < .5f ? carLight : chance < .58f ? red : chance < .62f ? olive : carLight;
        paint.box(x, z, 4.3f, 1.8f, .35f, .85f, true);
        dark.box(x - .2f, z, 2.2f, 1.7f, 1.2f, .55f);
      }

  // ---- Fence --------------------------------------------------------------------
  const auto fence = [&](P a, P b) {
    const float length = std::sqrt(dot(b - a, b - a));
    const int posts = std::max(1, int(length / 30));
    for (int i = 0; i <= posts; ++i) {
      const P at = a + (b - a) * (float(i) / float(posts));
      steel.box(at.x, at.z, .18f, .18f, 0, 2.6f);
    }
    for (const float y : {1.1f, 2.5f}) steel.beam({a.x, y, a.z}, {b.x, y, b.z}, .09f);
  };
  // Gates break the west side at the road in and the east side at the road out.
  fence({kFenceWest, 0, kFenceNorth}, {kFenceEast, 0, kFenceNorth});
  fence({kFenceWest, 0, kFenceSouth}, {kFenceEast, 0, kFenceSouth});
  fence({kFenceWest, 0, kFenceNorth}, {kFenceWest, 0, -332});
  fence({kFenceWest, 0, -308}, {kFenceWest, 0, kFenceSouth});
  fence({kFenceEast, 0, kFenceNorth}, {kFenceEast, 0, 294});
  fence({kFenceEast, 0, 319}, {kFenceEast, 0, kFenceSouth});
  // The gatehouse on the way in.
  walls.box(kFenceWest + 12, -338, 7, 6, 0, 3.4f);
  roof.box(kFenceWest + 12, -338, 9, 14, 3.4f, .35f, true);
  glass.box(kFenceWest + 12, -334.9f, 5, .15f, 1.3f, 1.4f);
  red.box(kFenceWest + 4, -320, .25f, 11, 1.1f, .25f, true);  // the barrier
  building({kFenceWest + 8.5f, -341, kFenceWest + 15.5f, -335}, "gatehouse");

  return field;
}

std::array<AirfieldPart, std::size_t(AirfieldMaterial::Count)> buildStructures(std::span<const std::uint8_t> health) {
  std::array<AirfieldPart, std::size_t(AirfieldMaterial::Count)> parts;
  for (std::size_t i = 0; i < parts.size(); ++i) parts[i].material = AirfieldMaterial(i);
  const auto part = [&](AirfieldMaterial material) { return Mesher{parts[std::size_t(material)].vertices}; };
  using M = AirfieldMaterial;
  Mesher walls = part(M::Concrete), cladding = part(M::Cladding), roof = part(M::Roof), shelter = part(M::Shelter);
  Mesher dark = part(M::Dark), white = part(M::White), steel = part(M::Steel), olive = part(M::Olive);
  const auto all = structures();
  for (std::size_t i = 0; i < all.size(); ++i) {
    const auto& structure = all[i];
    const float x = float(structure.east), z = float(-structure.north);
    const float y = float(-groundHeightNed(structure.north, structure.east));
    if (i < health.size() && health[i] == 0) {
      // Burnt ground, the stumps of walls and what fell from them.
      dark.box(x, z, 26, 20, y, .25f);
      dark.box(x - 7, z + 4, 9, 1.2f, y, 2.2f);
      dark.box(x + 6, z - 5, 1.2f, 8, y, 1.6f);
      walls.box(x + 2, z + 3, 5, 4, y, .9f);
      steel.beam({x - 4, y + .3f, z - 6}, {x + 5, y + 1.6f, z - 2}, .35f);
      continue;
    }
    // Each side flies its colour over what it holds.
    Mesher flag = part(structure.team == Team::Red ? M::Red : M::LightBlue);
    const auto colours = [&](float fx, float fz, float height) {
      steel.beam({fx, y, fz}, {fx, y + height, fz}, .14f);
      flag.box(fx + 1.3f, fz, 2.6f, .08f, y + height - 1.7f, 1.6f, true);
    };
    switch (structure.kind) {
      case StructureKind::Command:
        walls.box(x, z, 30, 20, y, 6.5f);
        walls.box(x - 6, z, 12, 12, y + 6.5f, 3.2f);
        dark.box(x + 15.05f, z, .2f, 5, y, 3);
        steel.beam({x + 9, y + 6.5f, z + 5}, {x + 9, y + 21, z + 5}, .3f);
        colours(x - 17, z - 12, 11);
        break;
      case StructureKind::Fuel:
        for (const float dx : {-11.f, 0.f, 11.f}) white.cylinder(x + dx, z, y, y + 8, 4.6f, 4.6f, 18);
        walls.box(x, z, 40, 16, y, .8f);  // the bund around the tanks
        steel.beam({x - 15, y + 8.2f, z}, {x + 15, y + 8.2f, z}, .3f);
        colours(x - 22, z - 10, 9);
        break;
      case StructureKind::Ammunition:
        shelter.vault(x, z, 0, 34, 16, 6.5f);
        shelter.vaultEnd(x, z, 0, 17, 16, 6.5f, .45f, .6f, 1);
        shelter.vaultEnd(x, z, 0, -17, 16, 6.5f, 0, 0, -1);
        dark.box(x + 17.1f, z, .2f, 6.6f, y, 3.7f);
        colours(x + 20, z - 10, 9);
        break;
      case StructureKind::Radar:
        walls.box(x, z, 9, 9, y, 4);
        steel.cylinder(x, z, y + 4, y + 13, 1.1f, .8f, 10);
        white.dome(x, y + 15.5f, z, 4.2f, 2.6f);
        colours(x - 7, z - 7, 8);
        break;
      case StructureKind::Depot:
        cladding.box(x, z, 34, 20, y, 7.5f);
        roof.box(x, z, 35.5f, 21.5f, y + 7.5f, .6f, true);
        dark.box(x, z + 10.05f, 8, .2f, y, 5);
        olive.box(x + 24, z + 4, 6.5f, 2.6f, y, 2.7f);  // a lorry waiting to load
        colours(x - 20, z - 12, 10);
        break;
      case StructureKind::Sam: {
        walls.box(x, z, 13, 13, y, .5f);
        olive.box(x, z, 5.5f, 3.2f, y + .5f, 1.9f);
        for (const float dz : {-1.1f, 1.1f}) {
          olive.beam({x - 2.6f, y + 2.6f, z + dz}, {x + 2.4f, y + 5.4f, z + dz}, .6f);
          white.beam({x - 2.9f, y + 3.25f, z + dz}, {x + 3.3f, y + 6.7f, z + dz}, .34f);
        }
        olive.box(x - 9, z + 6, 3, 3, y, 3);  // its radar cabin
        steel.beam({x - 9, y + 3, z + 6}, {x - 9, y + 7, z + 6}, .22f);
        colours(x + 8, z - 8, 7);
        break;
      }
      case StructureKind::Flak:
        // A ring of sandbags, the mount and two barrels at high elevation.
        for (int side = 0; side < 10; ++side) {
          const float a = 6.2831853f * side / 10;
          walls.box(x + 4.6f * std::cos(a), z + 4.6f * std::sin(a), 2.4f, 2.4f, y, 1.1f);
        }
        olive.box(x, z, 2.6f, 2.6f, y, 1.5f);
        for (const float dz : {-.35f, .35f}) steel.beam({x - .4f, y + 1.7f, z + dz}, {x + 2.6f, y + 5.2f, z + dz}, .18f);
        colours(x - 6, z - 6, 6);
        break;
    }
  }
  return parts;
}

}  // namespace ofs::client
