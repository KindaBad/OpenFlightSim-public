#pragma once
// Procedural missile and pylon geometry.
//
// Stores are authored in the missile's own body axes (X forward, Y right, Z
// down, origin at mid-length), the same axes the simulation flies them in, so
// one body-to-render matrix places a store on a pylon or in flight. Each part
// is a separate vertex list because the surface shader takes one material per
// draw.

#include "ofs/weapons.hpp"
#include "scenery.hpp"

#include <array>
#include <cmath>
#include <vector>

namespace ofs::client {

enum class StorePart : std::uint8_t { Body, Seeker, Band, Nozzle, Count };

struct StoreMesh {
  std::array<std::vector<SurfaceVertex>, std::size_t(StorePart::Count)> parts;
};

namespace store_detail {

// Surface of revolution about the X axis through (x, radius) stations.
inline void lathe(std::vector<SurfaceVertex>& out, const std::vector<std::pair<double, double>>& profile,
                  int sides) {
  const auto ring = [&](std::size_t i, int side) {
    const double angle = 2 * kPi * side / sides;
    return Vec3{profile[i].first, profile[i].second * std::cos(angle), profile[i].second * std::sin(angle)};
  };
  const auto normal = [&](std::size_t i, int side) {
    const std::size_t a = i ? i - 1 : 0, b = std::min(i + 1, profile.size() - 1);
    // Perpendicular to the profile slope, turned about the axis.
    const double along = profile[b].first - profile[a].first, out_ = profile[b].second - profile[a].second;
    const double angle = 2 * kPi * side / sides;
    return Vec3{-out_, along * std::cos(angle), along * std::sin(angle)}.normalized() * (along < 0 ? -1. : 1.);
  };
  for (std::size_t i = 0; i + 1 < profile.size(); ++i)
    for (int side = 0; side < sides; ++side) {
      const Vec3 a = ring(i, side), b = ring(i, side + 1), c = ring(i + 1, side + 1), d = ring(i + 1, side);
      const Vec3 na = normal(i, side), nb = normal(i, side + 1), nc = normal(i + 1, side + 1),
                 nd = normal(i + 1, side);
      sceneryTriangle(out, a, b, c, na, nb, nc, 0);
      sceneryTriangle(out, a, c, d, na, nc, nd, 0);
    }
}

// Flat cap closing a revolved body at `x`, facing along `direction` (+1 or -1).
inline void disc(std::vector<SurfaceVertex>& out, double x, double radius, double direction, int sides) {
  const Vec3 normal{direction, 0, 0}, centre{x, 0, 0};
  for (int side = 0; side < sides; ++side) {
    const double a = 2 * kPi * side / sides, b = 2 * kPi * (side + 1) / sides;
    sceneryTriangle(out, centre, {x, radius * std::cos(a), radius * std::sin(a)},
                    {x, radius * std::cos(b), radius * std::sin(b)}, normal, normal, normal, 0);
  }
}

// One thin fin standing on the body at `roll` radians about the axis. The
// root runs from rootAft to rootFore on the skin; the tip is `span` further
// out and runs from tipAft to tipFore.
inline void fin(std::vector<SurfaceVertex>& out, double roll, double radius, double span, double rootAft,
                double rootFore, double tipAft, double tipFore, double thickness) {
  const Vec3 outward{0, std::cos(roll), std::sin(roll)}, side{0, -std::sin(roll), std::cos(roll)};
  const auto point = [&](double x, double height, double offset) {
    return Vec3{x, 0, 0} + outward * (radius * .92 + height) + side * offset;
  };
  const double half = thickness * .5, edge = thickness * .12;
  for (const double sign : {-1., 1.}) {
    const Vec3 n = side * sign;
    const Vec3 a = point(rootAft, 0, sign * half), b = point(rootFore, 0, sign * half),
               c = point(tipFore, span, sign * edge), d = point(tipAft, span, sign * edge);
    sceneryTriangle(out, a, b, c, n, n, n, 0);
    sceneryTriangle(out, a, c, d, n, n, n, 0);
  }
  // Leading, trailing and tip edges close the plate.
  const auto strip = [&](double x0, double h0, double x1, double h1, double t0, double t1, Vec3 n) {
    const Vec3 a = point(x0, h0, -t0), b = point(x0, h0, t0), c = point(x1, h1, t1), d = point(x1, h1, -t1);
    sceneryTriangle(out, a, b, c, n, n, n, 0);
    sceneryTriangle(out, a, c, d, n, n, n, 0);
  };
  strip(rootFore, 0, tipFore, span, half, edge, (Vec3{span, 0, 0} + outward * (rootFore - tipFore)).normalized());
  strip(rootAft, 0, tipAft, span, half, edge, (Vec3{-span, 0, 0} + outward * (tipAft - rootAft)).normalized());
  strip(tipAft, span, tipFore, span, edge, edge, outward);
}

inline void cruciform(std::vector<SurfaceVertex>& out, double radius, double span, double rootAft,
                      double rootFore, double tipAft, double tipFore, double thickness) {
  // Stores hang in the X arrangement, so no fin points straight at the pylon.
  for (int i = 0; i < 4; ++i)
    fin(out, kPi * .25 + kPi * .5 * i, radius, span, rootAft, rootFore, tipAft, tipFore, thickness);
}

}  // namespace store_detail

namespace store_detail {

// A small block, for a suspension lug: centred on (x, 0, z), in body axes.
inline void block(std::vector<SurfaceVertex>& out, Vec3 centre, Vec3 half) {
  const auto face = [&](Vec3 normal, Vec3 u, Vec3 v) {
    const Vec3 c = centre + Vec3{normal.x * half.x, normal.y * half.y, normal.z * half.z};
    const Vec3 du{u.x * half.x, u.y * half.y, u.z * half.z}, dv{v.x * half.x, v.y * half.y, v.z * half.z};
    Vec3 a = c - du - dv, b = c + du - dv, cc = c + du + dv, d = c - du + dv;
    if ((b - a).cross(cc - a).dot(normal) < 0) std::swap(b, d);
    sceneryTriangle(out, a, b, cc, normal, normal, normal, 0);
    sceneryTriangle(out, a, cc, d, normal, normal, normal, 0);
  };
  face({1, 0, 0}, {0, 1, 0}, {0, 0, 1});
  face({-1, 0, 0}, {0, 1, 0}, {0, 0, 1});
  face({0, 1, 0}, {1, 0, 0}, {0, 0, 1});
  face({0, -1, 0}, {1, 0, 0}, {0, 0, 1});
  face({0, 0, 1}, {1, 0, 0}, {0, 1, 0});
  face({0, 0, -1}, {1, 0, 0}, {0, 1, 0});
}

}  // namespace store_detail

// Free-fall bombs. Body is the painted case and its fins, Seeker the bare
// steel (nose fuze, suspension lugs), Band the colour markings and Nozzle the
// dark fittings at the tail.
inline StoreMesh buildBombMesh(weapons::WeaponType type, int detail) {
  using namespace store_detail;
  const auto& bomb = weapons::bombDefinition(type);
  const double r = bomb.diameter * .5, length = bomb.length, nose = length * .5, tail = -length * .5;
  const int sides = detail ? 28 : 10;
  // Stations are given from the nose as fractions of the length.
  const auto at = [&](double fraction) { return nose - length * fraction; };
  StoreMesh mesh;
  auto& body = mesh.parts[std::size_t(StorePart::Body)];
  auto& steel = mesh.parts[std::size_t(StorePart::Seeker)];
  auto& band = mesh.parts[std::size_t(StorePart::Band)];
  auto& dark = mesh.parts[std::size_t(StorePart::Nozzle)];
  std::vector<std::pair<double, double>> profile;
  if (type == weapons::WeaponType::Nuclear) {
    // A B83: a long plain cylinder with a blunt, flat-fronted nose that is
    // made to crush, and a short tail of four swept fins round the can that
    // holds its parachute.
    const double flat = r * .52;
    disc(steel, at(0), flat, 1, sides);
    profile.clear();
    for (int i = 0; i <= 8; ++i) {
      const double a = kPi * .5 * i / 8;
      profile.push_back({at(.075) + length * .075 * std::cos(a), flat + (r - flat) * std::sin(a)});
    }
    lathe(steel, profile, sides);
    lathe(body, {{at(.075), r}, {at(.80), r}, {at(.86), r * .93}, {at(1), r * .90}}, sides);
    // Red bands behind the nose and ahead of the tail, and the joint between them.
    lathe(band, {{at(.105), r * 1.012}, {at(.125), r * 1.012}}, sides);
    lathe(band, {{at(.74), r * 1.012}, {at(.755), r * 1.012}}, sides);
    lathe(dark, {{at(.43), r * 1.008}, {at(.436), r * 1.008}}, sides);
    // The parachute can's lid, set a little into the tail.
    lathe(dark, {{tail, r * .90}, {tail + .03, r * .78}}, sides);
    disc(dark, tail + .03, r * .78, -1, sides);
    cruciform(body, r * .93, r * .95, tail + .02, at(.79), tail + .02, at(.90), .024);
    for (const double station : {.36, .57}) block(steel, {at(station), 0, -r - .012}, {.045, .018, .022});
    return mesh;
  }
  // A Mk 80 series low-drag bomb: a long ogive to its widest at a third of its
  // length, a body that tapers away aft, and a conical fin assembly bolted to
  // it carrying four fins. The same shape at two sizes.
  const double fuze = .028, widest = .36, taper = .52, join = .705;
  lathe(steel, {{at(0), r * .10}, {at(fuze * .45), r * .15}, {at(fuze), r * .17}}, sides);
  disc(steel, at(0), r * .10, 1, sides);
  // The ogive is an arc that leaves the fuze at an angle and arrives at the
  // widest station parallel to the axis.
  for (int i = 0; i <= 12; ++i) {
    const double t = double(i) / 12, a = kPi * .5 * t;
    profile.push_back({at(fuze + (widest - fuze) * (1 - std::cos(a))), r * (.17 + .83 * std::sin(a))});
  }
  profile.push_back({at(taper), r});
  for (int i = 1; i <= 5; ++i) {
    const double t = double(i) / 5;
    profile.push_back({at(taper + (join - taper) * t), r * (1 - .36 * t * t)});
  }
  lathe(body, profile, sides);
  // The fin assembly: a cone a shade wider than the body it is clamped to.
  lathe(body, {{at(join), r * .66}, {at(join + .012), r * .66}, {at(1), r * .30}}, sides);
  disc(dark, tail, r * .30, -1, sides);
  lathe(dark, {{at(join - .004), r * .655}, {at(join), r * .67}}, sides);
  const double tip = r * 1.40 - r * .30 * .92;
  cruciform(body, r * .30, tip, tail, at(join + .02), tail, at(join + .17), .014 + r * .03);
  // One yellow band round the nose: high explosive.
  lathe(band, {{at(.085), r * .648}, {at(.108), r * .722}}, sides);
  const double spacing = type == weapons::WeaponType::Bomb2000 ? .762 : .356;
  for (const double sign : {-1., 1.})
    block(steel, {at(.40) + sign * spacing * .5 - length * .0, 0, -r - .010}, {.038, .016, .020});
  return mesh;
}

// `detail` 1 is the mesh seen up close; 0 halves the facets for distant stores.
inline StoreMesh buildStoreMesh(weapons::WeaponType type, int detail) {
  using namespace store_detail;
  if (weapons::isBomb(type)) return buildBombMesh(type, detail);
  const auto& d = weapons::missileDefinition(type);
  const double r = d.diameter * .5, nose = d.length * .5, tail = -d.length * .5;
  const int sides = detail ? 18 : 8;
  StoreMesh mesh;
  auto& body = mesh.parts[std::size_t(StorePart::Body)];
  auto& seeker = mesh.parts[std::size_t(StorePart::Seeker)];
  auto& band = mesh.parts[std::size_t(StorePart::Band)];
  auto& nozzle = mesh.parts[std::size_t(StorePart::Nozzle)];
  const double bandRadius = r * 1.03;
  if (type == weapons::WeaponType::Infrared) {
    // Short-range heat seeker: glass dome, nose canards, clipped tail fins.
    const double dome = r * .82;
    std::vector<std::pair<double, double>> glass;
    for (int i = 0; i <= 5; ++i) {
      const double a = kPi * .5 * i / 5;
      glass.push_back({nose - dome + dome * std::cos(a), std::max(dome * std::sin(a), 1e-4)});
    }
    lathe(seeker, glass, sides);
    lathe(body, {{nose - dome, dome}, {nose - dome - .30, r}, {tail + .10, r}, {tail, r * .86}}, sides);
    lathe(band, {{nose - .62, bandRadius}, {nose - .70, bandRadius}}, sides);
    lathe(band, {{-.18, bandRadius}, {-.25, bandRadius}}, sides);
    cruciform(body, r, .14, nose - .78, nose - .52, nose - .76, nose - .70, .012);
    cruciform(body, r, .24, tail + .04, tail + .52, tail + .04, tail + .22, .016);
  } else {
    // Medium-range radar missile: ogive radome, mid-body wings, tail controls.
    std::vector<std::pair<double, double>> radome;
    const double ogive = .62;
    for (int i = 0; i <= 7; ++i) {
      const double t = double(i) / 7;
      radome.push_back({nose - ogive * t, std::max(r * std::sqrt(1 - (1 - t) * (1 - t)), 1e-4)});
    }
    lathe(seeker, radome, sides);
    lathe(body, {{nose - ogive, r}, {tail + .12, r}, {tail, r * .84}}, sides);
    lathe(band, {{nose - ogive - .30, bandRadius}, {nose - ogive - .39, bandRadius}}, sides);
    lathe(band, {{-.55, bandRadius}, {-.63, bandRadius}}, sides);
    cruciform(body, r, .22, -.36, .34, -.34, -.12, .018);
    cruciform(body, r, .23, tail + .05, tail + .46, tail + .05, tail + .26, .018);
  }
  // Recessed motor nozzle: a dark throat inside the boat-tail.
  const double exit = r * (type == weapons::WeaponType::Infrared ? .86 : .84);
  lathe(nozzle, {{tail, exit}, {tail + .07, exit * .45}}, sides);
  disc(nozzle, tail + .07, exit * .45, -1, sides);
  return mesh;
}

// Pylon of unit size: X spans -0.5..0.5 along the store, Y -0.5..0.5 across
// it, and Z runs from 0 at the store up to -1 at the wing skin. The wing end is
// longer than the store end, with a swept leading edge.
inline std::vector<SurfaceVertex> buildPylonMesh() {
  std::vector<SurfaceVertex> out;
  const double top[2] = {-.5, .5}, bottom[2] = {-.42, .34};
  const auto corner = [&](int fore, int right, int low) {
    return Vec3{low ? bottom[fore] : top[fore], right ? .5 : -.5, low ? 0. : -1.};
  };
  const auto face = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 outward) {
    Vec3 n = (b - a).cross(c - a).normalized();
    if (n.dot(outward) < 0) n = n * -1.;
    sceneryTriangle(out, a, b, c, n, n, n, 0);
    sceneryTriangle(out, a, c, d, n, n, n, 0);
  };
  face(corner(0, 1, 0), corner(1, 1, 0), corner(1, 1, 1), corner(0, 1, 1), {0, 1, 0});
  face(corner(1, 0, 0), corner(0, 0, 0), corner(0, 0, 1), corner(1, 0, 1), {0, -1, 0});
  face(corner(1, 1, 0), corner(1, 0, 0), corner(1, 0, 1), corner(1, 1, 1), {1, 0, 0});
  face(corner(0, 0, 0), corner(0, 1, 0), corner(0, 1, 1), corner(0, 0, 1), {-1, 0, 0});
  face(corner(0, 1, 1), corner(1, 1, 1), corner(1, 0, 1), corner(0, 0, 1), {0, 0, 1});
  return out;
}

}  // namespace ofs::client
