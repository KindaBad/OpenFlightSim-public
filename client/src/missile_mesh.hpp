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

enum class StorePart : std::uint8_t { Body, Seeker, Band, Nozzle, Metal, Throat, Count };

struct StoreMesh {
  std::array<std::vector<SurfaceVertex>, std::size_t(StorePart::Count)> parts;
};

namespace store_detail {

// Surface of revolution about the X axis through (x, radius) stations.
// `inward` turns the surface to face the axis: the inside of a nozzle.
inline void lathe(std::vector<SurfaceVertex>& out, const std::vector<std::pair<double, double>>& profile,
                  int sides, bool inward = false) {
  const auto ring = [&](std::size_t i, int side) {
    const double angle = 2 * kPi * side / sides;
    return Vec3{profile[i].first, profile[i].second * std::cos(angle), profile[i].second * std::sin(angle)};
  };
  const auto normal = [&](std::size_t i, int side) {
    const std::size_t a = i ? i - 1 : 0, b = std::min(i + 1, profile.size() - 1);
    // Perpendicular to the profile slope, turned about the axis.
    const double along = profile[b].first - profile[a].first, out_ = profile[b].second - profile[a].second;
    const double angle = 2 * kPi * side / sides;
    return Vec3{-out_, along * std::cos(angle), along * std::sin(angle)}.normalized() * ((along < 0) != inward ? -1. : 1.);
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

namespace store_detail {

// A box standing on the body at `roll`, `height` proud of the skin and `width`
// across, from xAft to xFore, its front face raked back by `rake`: a ramjet's
// intake duct.
inline void duct(std::vector<SurfaceVertex>& out, double roll, double radius, double height, double width,
                 double xAft, double xFore, double rake) {
  const Vec3 outward{0, std::cos(roll), std::sin(roll)}, side{0, -std::sin(roll), std::cos(roll)};
  const auto point = [&](double x, double h, double offset) {
    return Vec3{x, 0, 0} + outward * (radius * .8 + h) + side * offset;
  };
  const double half = width * .5, top = radius * .2 + height;
  const auto quad = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 facing) {
    Vec3 n = (b - a).cross(c - a).normalized();
    if (n.dot(facing) < 0) n = n * -1.;
    sceneryTriangle(out, a, b, c, n, n, n, 0);
    sceneryTriangle(out, a, c, d, n, n, n, 0);
  };
  quad(point(xAft, top, -half), point(xFore - rake, top, -half), point(xFore - rake, top, half), point(xAft, top, half), outward);
  for (const double sign : {-1., 1.})
    quad(point(xAft, 0, sign * half), point(xFore, 0, sign * half), point(xFore - rake, top, sign * half),
         point(xAft, top, sign * half), side * sign);
  quad(point(xFore, 0, -half), point(xFore, 0, half), point(xFore - rake, top, half), point(xFore - rake, top, -half),
       Vec3{1, 0, 0});
  quad(point(xAft, 0, -half), point(xAft, 0, half), point(xAft, top, half), point(xAft, top, -half), Vec3{-1, 0, 0});
}

}  // namespace store_detail

// A missile's airframe. Body is the painted tube and its fins, Seeker the
// dome or radome, Band the colour markings, Nozzle the dark fittings, Metal
// the bare steel (hangers, nozzle rim and jet vanes) and Throat the inside of
// the motor, which glows while it burns. Each is drawn to its own
// type's layout: where its fins are, how many sets it has and what its nose
// is, from published drawings and photographs, to the published length and
// diameter. `detail` 1 is the mesh seen up close; 0 is the bare shape, with
// fewer facets, for distant stores.
inline StoreMesh buildMissileMesh(weapons::MissileModel model, int detail) {
  using namespace store_detail;
  using weapons::MissileModel;
  const auto& d = weapons::missileDefinition(model);
  const bool infrared = d.type == weapons::WeaponType::Infrared;
  const double r = d.diameter * .5, nose = d.length * .5, tail = -d.length * .5;
  const int sides = detail ? 24 : 8, steps = detail ? 12 : 5;
  StoreMesh mesh;
  auto& body = mesh.parts[std::size_t(StorePart::Body)];
  auto& seeker = mesh.parts[std::size_t(StorePart::Seeker)];
  auto& band = mesh.parts[std::size_t(StorePart::Band)];
  auto& nozzle = mesh.parts[std::size_t(StorePart::Nozzle)];
  auto& metal = mesh.parts[std::size_t(StorePart::Metal)];
  auto& throat = mesh.parts[std::size_t(StorePart::Throat)];
  const double bandRadius = r * 1.03, proud = r * .08;
  const auto ring = [&](double fore, double width) {
    lathe(band, {{fore, bandRadius}, {fore - width, bandRadius}}, sides);
  };
  // A glass dome of `radius` at the nose, the steel collar it is set in and
  // the body's taper out to full width behind it.
  const auto dome = [&](double radius, double taper) {
    std::vector<std::pair<double, double>> glass;
    for (int i = 0; i <= steps; ++i) {
      const double a = kPi * .5 * i / steps;
      glass.push_back({nose - radius + radius * std::cos(a), std::max(radius * std::sin(a), 1e-4)});
    }
    lathe(seeker, glass, sides);
    const double collar = detail ? .035 : 0;
    if (detail) lathe(metal, {{nose - radius, radius}, {nose - radius - collar, radius + (r - radius) * collar / taper}}, sides);
    lathe(body, {{nose - radius - collar, radius + (r - radius) * collar / taper}, {nose - radius - taper, r}}, sides);
    return nose - radius - taper;
  };
  // A pointed radome `length` long: a tangent ogive, which meets the body
  // without a corner.
  const auto radome = [&](double length) {
    const double rho = (r * r + length * length) / (2 * r);
    std::vector<std::pair<double, double>> profile;
    for (int i = 0; i <= steps; ++i) {
      const double x = length * i / steps;  // back from the tip
      profile.push_back({nose - x, std::max(std::sqrt(rho * rho - (length - x) * (length - x)) + r - rho, 1e-4)});
    }
    // The tip is a little blunted, as a real radome's is.
    profile.front().second = r * .06;
    disc(seeker, nose, r * .06, 1, sides);
    lathe(seeker, profile, sides);
    return nose - length;
  };
  // What is only seen from close to. A joint between two sections of the body.
  const auto joint = [&](double x) {
    if (detail) lathe(nozzle, {{x + .005, r * 1.006}, {x - .005, r * 1.006}}, sides);
  };
  // The windows of the proximity fuze, round the body.
  const auto fuze = [&](double fore, double length) {
    if (!detail) return;
    for (int i = 0; i < 4; ++i) fin(nozzle, kPi * .5 * i, r, proud + .002, fore - length, fore, fore - length, fore, r * .42);
  };
  // A hanger on top, by which the missile sits on its rail or is held to its ejector.
  const auto hanger = [&](double x, double length) {
    if (detail) block(metal, {x, 0, -r - .011}, {length * .5, r * .28, .016});
  };
  // A fairing along the body for the wiring that passes the motor.
  const auto raceway = [&](double roll, double aft, double fore) {
    if (detail) fin(body, roll, r, proud + .011, aft, fore, aft + .03, fore - .05, r * .34);
  };
  double boatTail = .86;
  bool vanes = false;
  switch (model) {
    case MissileModel::IrisT: {
      // No canards: four long shallow strakes down the middle of the body and
      // four cropped fins at the tail, round a nozzle with vanes in it.
      const double shoulder = dome(r * .86, .22);
      lathe(body, {{shoulder, r}, {tail + .08, r}, {tail, r * .9}}, sides);
      cruciform(body, r, .075, -.62, .52, -.62, .30, .012);
      cruciform(body, r, .155, tail + .03, tail + .36, tail + .03, tail + .17, .014);
      ring(nose - .55, .06);
      ring(-.25, .06);
      joint(shoulder - .12);
      joint(.60);
      joint(tail + .44);
      fuze(shoulder - .16, .09);
      for (const double x : {.72, -.10, -.92}) hanger(x, .09);
      boatTail = .9;
      vanes = true;
      break;
    }
    case MissileModel::R74M2: {
      // The R-73's nose: small fixed vanes right behind the dome, then the
      // canards, and broad cropped wings at the tail.
      const double shoulder = dome(r * .78, .30);
      lathe(body, {{shoulder, r}, {tail + .10, r}, {tail, r * .88}}, sides);
      cruciform(body, r, .045, shoulder - .13, shoulder - .03, shoulder - .12, shoulder - .08, .010);
      cruciform(body, r, .115, shoulder - .40, shoulder - .19, shoulder - .39, shoulder - .31, .012);
      cruciform(body, r, .19, tail + .04, tail + .60, tail + .04, tail + .30, .016);
      ring(shoulder - .50, .05);
      ring(-.30, .05);
      joint(shoulder - .46);
      joint(.10);
      joint(tail + .66);
      fuze(shoulder - .58, .10);
      for (const double x : {.55, -.20, -.95}) hanger(x, .10);
      raceway(kPi * .5, tail + .66, .06);
      boatTail = .88;
      vanes = true;
      break;
    }
    case MissileModel::PL5EII: {
      // The Sidewinder pattern: double-delta canards on a slim nose and wide
      // wings at the tail with a rolleron at each tip.
      const double shoulder = dome(r * .80, .26);
      lathe(body, {{shoulder, r}, {tail + .06, r}, {tail, r * .9}}, sides);
      cruciform(body, r, .075, shoulder - .22, shoulder - .02, shoulder - .21, shoulder - .16, .010);
      cruciform(body, r, .15, shoulder - .44, shoulder - .20, shoulder - .43, shoulder - .37, .012);
      cruciform(body, r, .245, tail + .03, tail + .58, tail + .03, tail + .34, .016);
      // Rollerons: a block at each wing tip.
      for (int i = 0; i < 4; ++i)
        fin(nozzle, kPi * .25 + kPi * .5 * i, r + .225, .022, tail + .03, tail + .12, tail + .03, tail + .12, .05);
      ring(shoulder - .52, .06);
      ring(-.20, .06);
      joint(shoulder - .48);
      joint(.34);
      joint(tail + .62);
      fuze(shoulder - .62, .10);
      for (const double x : {.60, -.15, -.90}) hanger(x, .08);
      boatTail = .9;
      break;
    }
    case MissileModel::Meteor: {
      // No wings at all: two intake ducts along the lower quarters of the
      // body feeding the ramjet, and four fins at the tail.
      const double shoulder = radome(.66);
      lathe(body, {{shoulder, r}, {tail + .10, r}, {tail, r * .9}}, sides);
      for (const double roll : {kPi * .25, kPi * .75}) {
        duct(body, roll, r, .075, .11, tail + .52, .12, .16);
        // The dark mouth of each intake.
        duct(nozzle, roll, r, .060, .085, .10, .135, .16);
      }
      cruciform(body, r, .20, tail + .05, tail + .46, tail + .05, tail + .24, .018);
      ring(shoulder - .28, .08);
      joint(shoulder - .02);
      joint(shoulder - .62);
      joint(.14);
      fuze(shoulder - .40, .12);
      for (const double x : {.52, -.60}) hanger(x, .10);
      boatTail = .9;
      break;
    }
    case MissileModel::R77M: {
      // Long thin strakes down the body and plain cropped fins at the tail, in
      // place of the R-77's lattice fins, so that it fits the Su-57's bays.
      const double shoulder = radome(.78);
      lathe(body, {{shoulder, r}, {tail + .12, r}, {tail, r * .86}}, sides);
      cruciform(body, r, .07, -.95, .40, -.95, .10, .014);
      cruciform(body, r, .19, tail + .05, tail + .42, tail + .05, tail + .22, .018);
      ring(shoulder - .30, .07);
      ring(-1.15, .07);
      joint(shoulder - .02);
      joint(shoulder - .56);
      joint(tail + .50);
      fuze(shoulder - .42, .12);
      for (const double x : {.62, -.70}) hanger(x, .10);
      boatTail = .86;
      break;
    }
    case MissileModel::SD10A: {
      // Four cropped delta wings at the middle and four fins at the tail.
      const double shoulder = radome(.72);
      lathe(body, {{shoulder, r}, {tail + .12, r}, {tail, r * .84}}, sides);
      cruciform(body, r, .225, -.38, .30, -.36, -.14, .018);
      cruciform(body, r, .235, tail + .05, tail + .50, tail + .05, tail + .28, .018);
      ring(shoulder - .30, .09);
      ring(-.62, .08);
      joint(shoulder - .02);
      joint(shoulder - .60);
      joint(tail + .58);
      fuze(shoulder - .44, .12);
      for (const double x : {.70, -.55}) hanger(x, .10);
      raceway(kPi * .5, tail + .58, -.42);
      boatTail = .84;
      break;
    }
    default:
      if (infrared) {
        // Short-range heat seeker: glass dome, nose canards, clipped tail fins.
        const double shoulder = dome(r * .82, .30);
        lathe(body, {{shoulder, r}, {tail + .10, r}, {tail, r * .86}}, sides);
        ring(nose - .62, .08);
        ring(-.18, .07);
        cruciform(body, r, .14, nose - .78, nose - .52, nose - .76, nose - .70, .012);
        cruciform(body, r, .24, tail + .04, tail + .52, tail + .04, tail + .22, .016);
      } else {
        // Medium-range radar missile: ogive radome, mid-body wings, tail controls.
        const double shoulder = radome(.62);
        lathe(body, {{shoulder, r}, {tail + .12, r}, {tail, r * .84}}, sides);
        ring(shoulder - .30, .09);
        ring(-.55, .08);
        cruciform(body, r, .22, -.36, .34, -.34, -.12, .018);
        cruciform(body, r, .23, tail + .05, tail + .46, tail + .05, tail + .26, .018);
        boatTail = .84;
      }
      break;
  }
  // The motor's nozzle: the rim of the boat-tail, a steel cone inside it down
  // to the throat, and the dark of the motor beyond.
  const double exit = r * boatTail;
  lathe(metal, {{tail, exit}, {tail + .012, exit * .86}}, sides, true);
  lathe(throat, {{tail + .012, exit * .86}, {tail + .10, exit * .40}}, sides, true);
  disc(throat, tail + .10, exit * .40, -1, sides);
  // Jet vanes standing in the exhaust, which turn it.
  if (vanes && detail)
    for (int i = 0; i < 4; ++i)
      fin(metal, kPi * .5 * i, exit * .16, exit * .66, tail - .03, tail + .03, tail - .02, tail + .03, .008);
  return mesh;
}

inline StoreMesh buildStoreMesh(weapons::WeaponType type, int detail) {
  if (weapons::isBomb(type)) return buildBombMesh(type, detail);
  return buildMissileMesh(weapons::missileDefinition(type).model, detail);
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
