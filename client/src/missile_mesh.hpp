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

// `detail` 1 is the mesh seen up close; 0 halves the facets for distant stores.
inline StoreMesh buildStoreMesh(weapons::WeaponType type, int detail) {
  using namespace store_detail;
  if (weapons::isBomb(type)) {
    // A free-fall bomb: an ogive nose, a fat parallel body and a tapered tail
    // carrying four fins. The band is the nose marking.
    const auto& bomb = weapons::bombDefinition(type);
    const double r = bomb.diameter * .5, nose = bomb.length * .5, tail = -bomb.length * .5;
    const int sides = detail ? 16 : 8;
    StoreMesh mesh;
    auto& body = mesh.parts[std::size_t(StorePart::Body)];
    std::vector<std::pair<double, double>> profile;
    const double ogive = bomb.length * .30;
    for (int i = 0; i <= 6; ++i) {
      const double t = double(i) / 6;
      profile.push_back({nose - ogive * t, std::max(r * std::sqrt(1 - (1 - t) * (1 - t)), 1e-4)});
    }
    profile.push_back({tail + bomb.length * .34, r});
    profile.push_back({tail, r * .30});
    lathe(body, profile, sides);
    disc(body, tail, r * .30, -1, sides);
    lathe(mesh.parts[std::size_t(StorePart::Band)], {{nose - ogive * .55, r * .93}, {nose - ogive * .80, r * 1.0}}, sides);
    cruciform(body, r * .55, r * .95, tail, tail + bomb.length * .26, tail, tail + bomb.length * .14, .02);
    return mesh;
  }
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
