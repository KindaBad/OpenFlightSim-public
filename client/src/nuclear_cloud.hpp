#pragma once
// The cloud of a nuclear burst.
//
// Soft particles alone cannot hold the shape of a mushroom cloud from across
// the map, so its body is four solid, lumpy surfaces drawn with the same
// shader as everything else and lit by the same sun: the fireball, the stem,
// the cap that rolls out at its top and the ring of dust that runs out along
// the ground. Each is built once at unit size and placed every frame from the
// age of the burst. Particles add the fire inside it and the haze around it.
// Sized for the eye, to stand over the ground this weapon flattens, and not
// from weapon effects data.

#include "scenery.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ofs::client {

// Where the parts of the cloud are `age` seconds after the burst, in metres.
struct CloudShape {
  double height;        // of the middle of the cap above the ground
  double capRadius, capThickness, stemRadius;
  double climb;         // how fast the cap is rising, m/s
};
inline CloudShape cloudShape(double age) {
  const double rise = 1 - std::exp(-age / 26.);
  CloudShape shape;
  shape.height = 350 + 5600 * rise;
  shape.climb = 5600 / 26. * std::exp(-age / 26.);
  shape.capRadius = 520 * std::min(1., age / 2.5) + 2700 * (1 - std::exp(-std::max(0., age - 4) / 30.));
  shape.capThickness = 420 + 700 * rise;
  shape.stemRadius = 260 + 330 * rise;
  return shape;
}
// Seconds a cloud is drawn for, the last of them fading.
inline constexpr double kCloudSeconds = 130, kCloudFadeSeconds = 28;

enum class CloudPart : std::uint8_t { Fireball, Cap, Stem, Skirt, Count };

namespace cloud_detail {
inline double lattice(int x, int y, int z) {
  std::uint32_t h = std::uint32_t(x) * 0x8da6b343u ^ std::uint32_t(y) * 0xd8163841u ^ std::uint32_t(z) * 0xcb1ab31fu;
  h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
  return double(h) * (1. / 4294967296.);
}
inline double noise(Vec3 p) {
  const double fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
  const int x = int(fx), y = int(fy), z = int(fz);
  const auto ease = [](double t) { return t * t * (3 - 2 * t); };
  const double u = ease(p.x - fx), v = ease(p.y - fy), w = ease(p.z - fz);
  const auto mix = [](double a, double b, double t) { return a + (b - a) * t; };
  return mix(mix(mix(lattice(x, y, z), lattice(x + 1, y, z), u), mix(lattice(x, y + 1, z), lattice(x + 1, y + 1, z), u), v),
             mix(mix(lattice(x, y, z + 1), lattice(x + 1, y, z + 1), u),
                 mix(lattice(x, y + 1, z + 1), lattice(x + 1, y + 1, z + 1), u), v), w);
}
// Billowing: rounded lumps with sharp creases between them, as cumulus has.
inline double billow(Vec3 p, int octaves) {
  double sum = 0, weight = .5, total = 0;
  for (int i = 0; i < octaves; ++i) {
    sum += weight * (1 - std::abs(2 * noise(p) - 1));
    total += weight;
    weight *= .5;
    p = Vec3{p.x * 2.03 + 11.7, p.y * 2.03 + 5.3, p.z * 2.03 + 8.1};
  }
  return sum / total;
}
}  // namespace cloud_detail

// A surface of revolution about the vertical through `profile` (radius,
// height), pushed out along its own normals by billowing noise of `amplitude`.
// `stretch` scales the noise's sample point, so a part that is drawn much
// taller than it is wide still gets round lumps.
inline std::vector<SurfaceVertex> buildCloudSurface(const std::vector<std::pair<double, double>>& outline, int sides,
                                                    double amplitude, Vec3 stretch, int octaves, double seed,
                                                    bool sideways = false) {
  // Resample the outline evenly along its length, so the lumps are not
  // stretched where its points are sparse.
  std::vector<std::pair<double, double>> profile;
  for (std::size_t i = 0; i + 1 < outline.size(); ++i) {
    const double length = std::hypot(outline[i + 1].first - outline[i].first, outline[i + 1].second - outline[i].second);
    const int steps = std::max(1, int(std::ceil(length / .035)));
    for (int s = 0; s < steps; ++s) {
      const double t = double(s) / steps;
      profile.push_back({outline[i].first + (outline[i + 1].first - outline[i].first) * t,
                         outline[i].second + (outline[i + 1].second - outline[i].second) * t});
    }
  }
  profile.push_back(outline.back());
  const int rings = int(profile.size());
  std::vector<Vec3> points(std::size_t(rings) * sides);
  for (int ring = 0; ring < rings; ++ring) {
    const int before = std::max(0, ring - 1), after = std::min(rings - 1, ring + 1);
    // Outward normal of the undisplaced surface: the outline's own, turned.
    double nr = profile[after].second - profile[before].second, ny = -(profile[after].first - profile[before].first);
    const double length = std::max(1e-9, std::hypot(nr, ny));
    nr /= length; ny /= length;
    for (int side = 0; side < sides; ++side) {
      const double angle = 2 * kPi * side / sides, c = std::cos(angle), s = std::sin(angle);
      const Vec3 base{profile[ring].first * c, profile[ring].second, profile[ring].first * s};
      // A part drawn far taller than wide is pushed sideways only, or its
      // lumps would be drawn out into spikes.
      const Vec3 normal = sideways ? Vec3{c, 0, s} : Vec3{nr * c, ny, nr * s};
      const Vec3 sample{base.x * stretch.x + seed, base.y * stretch.y + seed * 1.7, base.z * stretch.z - seed};
      // Nothing is pushed where the surface meets the axis, or it would tear.
      const double pinned = std::min(1., profile[ring].first / .08);
      points[std::size_t(ring) * sides + side] = base + normal * (amplitude * pinned * (cloud_detail::billow(sample, octaves) - .35));
    }
  }
  const auto at = [&](int ring, int side) -> const Vec3& {
    return points[std::size_t(std::clamp(ring, 0, rings - 1)) * sides + std::size_t((side % sides + sides) % sides)];
  };
  const auto normalAt = [&](int ring, int side) {
    Vec3 n = (at(ring + 1, side) - at(ring - 1, side)).cross(at(ring, side + 1) - at(ring, side - 1));
    if (n.norm2() < 1e-16) return Vec3{0, 1, 0};
    n = n.normalized();
    // Outward: away from the axis, or upward where the surface lies on it.
    const Vec3 p = at(ring, side);
    const Vec3 out{p.x, 0, p.z};
    return n.dot(out) < 0 || (out.norm2() < 1e-6 && n.y < 0) ? n * -1. : n;
  };
  std::vector<SurfaceVertex> mesh;
  mesh.reserve(std::size_t(rings - 1) * sides * 6);
  for (int ring = 0; ring + 1 < rings; ++ring)
    for (int side = 0; side < sides; ++side) {
      const Vec3 a = at(ring, side), b = at(ring, side + 1), c = at(ring + 1, side + 1), d = at(ring + 1, side);
      const Vec3 na = normalAt(ring, side), nb = normalAt(ring, side + 1), nc = normalAt(ring + 1, side + 1),
                 nd = normalAt(ring + 1, side);
      sceneryTriangle(mesh, a, b, c, na, nb, nc, 0);
      sceneryTriangle(mesh, a, c, d, na, nc, nd, 0);
    }
  return mesh;
}

// The parts at unit size, in renderer axes (Y up), standing on or about the origin.
inline std::vector<SurfaceVertex> buildCloudPart(CloudPart part) {
  std::vector<std::pair<double, double>> outline;
  switch (part) {
    case CloudPart::Fireball:
      for (int i = 0; i <= 24; ++i) {
        const double a = kPi * (.5 - double(i) / 24);
        outline.push_back({std::max(0., std::cos(a)), std::sin(a)});
      }
      return buildCloudSurface(outline, 56, .16, {2.6, 2.6, 2.6}, 4, 3.1);
    case CloudPart::Cap:
      // A dome whose rim rolls over and under, back in to where the stem joins.
      outline = {{0, .50}, {.16, .495}, {.32, .475}, {.48, .44}};
      for (int i = 0; i <= 26; ++i) {
        const double a = (78 - 236. * i / 26) * kPi / 180;
        outline.push_back({.60 + .40 * std::cos(a), .04 + .41 * std::sin(a)});
      }
      outline.push_back({.24, -.20});
      outline.push_back({.16, -.06});
      return buildCloudSurface(outline, 112, .13, {3.4, 3.8, 3.4}, 5, 7.9);
    case CloudPart::Stem:
      // Flared where it leaves the ground and again where it enters the cap.
      outline = {{1.7, 0}, {1.3, .03}, {1.08, .08}, {.94, .17}, {.82, .34}, {.78, .55}, {.84, .74}, {1.02, .88}, {1.35, .97}, {1.6, 1.0}};
      return buildCloudSurface(outline, 72, .26, {2.2, 7.5, 2.2}, 4, 12.4, true);
    case CloudPart::Skirt:
      // The base surge: a low rolling ring.
      for (int i = 0; i <= 18; ++i) {
        const double a = kPi * double(i) / 18;
        outline.push_back({1 + .20 * std::cos(a), .16 * std::sin(a)});
      }
      return buildCloudSurface(outline, 144, .07, {9, 9, 9}, 4, 20.2);
    case CloudPart::Count: break;
  }
  return {};
}

// One part of a cloud as it is drawn: where, how large, what colour.
struct CloudDraw {
  CloudPart part{CloudPart::Cap};
  double up{};               // of its origin above the ground
  double radius{}, height{}; // the unit mesh is scaled by these across and up
  double turn{};             // about the vertical, radians
  float color[3]{}, emissive[3]{};
  float alpha{1};
};
// Every part of a cloud `age` seconds old. Empty once it has gone.
inline std::vector<CloudDraw> cloudParts(double age) {
  std::vector<CloudDraw> parts;
  if (!(age >= 0) || age >= kCloudSeconds) return parts;
  const auto smooth = [](double low, double high, double value) {
    const double t = std::clamp((value - low) / (high - low), 0., 1.);
    return t * t * (3 - 2 * t);
  };
  const auto shape = cloudShape(age);
  const float alpha = float(1 - smooth(kCloudSeconds - kCloudFadeSeconds, kCloudSeconds, age));
  const auto mix = [](float a, float b, double t) { return float(a + (b - a) * std::clamp(t, 0., 1.)); };
  // The heat inside shows through for the first half minute.
  const double glow = std::exp(-age / 9.);
  // The fireball swells in two seconds, lifts with the cap and is lost in it.
  if (age < 15) {
    CloudDraw fire;
    fire.part = CloudPart::Fireball;
    fire.radius = fire.height = 640 * std::pow(std::min(1., age / 2.2), .55) * (1 - smooth(8, 15, age));
    fire.up = shape.height;
    fire.turn = age * .05;
    const double heat = std::exp(-age / 3.2);
    fire.color[0] = 1; fire.color[1] = .45f; fire.color[2] = .12f;
    fire.emissive[0] = float(3 + 60 * heat); fire.emissive[1] = float(.9 + 46 * heat); fire.emissive[2] = float(.1 + 30 * heat);
    fire.alpha = alpha;
    parts.push_back(fire);
  }
  const double grown = smooth(1.2, 6, age);
  CloudDraw cap;
  cap.part = CloudPart::Cap;
  cap.radius = shape.capRadius * 1.12 * grown;
  // Nearly a ball at first, flattening as it spreads.
  cap.height = std::max(shape.capThickness * 2.0, cap.radius * (1.25 - .55 * smooth(4, 45, age))) * grown;
  cap.up = shape.height;
  cap.turn = age * .012;
  cap.color[0] = mix(.52f, .78f, (age - 3) / 34); cap.color[1] = mix(.30f, .76f, (age - 3) / 34); cap.color[2] = mix(.20f, .74f, (age - 3) / 34);
  // Hot through and through at first, then only a dull glow in a body the sun lights.
  const double hot = std::exp(-age / 4.);
  cap.emissive[0] = float(9 * hot + 1.1 * glow); cap.emissive[1] = float(4.5 * hot + .28 * glow); cap.emissive[2] = float(1.2 * hot + .03 * glow);
  cap.alpha = alpha;
  if (grown > 0) parts.push_back(cap);
  CloudDraw stem;
  stem.part = CloudPart::Stem;
  stem.radius = shape.stemRadius * smooth(.6, 4.5, age);
  stem.height = std::max(60., shape.height - cap.height * .12);
  stem.turn = -age * .02;
  stem.color[0] = mix(.26f, .52f, (age - 3) / 30); stem.color[1] = mix(.13f, .46f, (age - 3) / 30); stem.color[2] = mix(.08f, .42f, (age - 3) / 30);
  stem.emissive[0] = float(2.2 * glow * glow); stem.emissive[1] = float(.5 * glow * glow);
  stem.alpha = alpha;
  if (stem.radius > 1) parts.push_back(stem);
  CloudDraw skirt;
  skirt.part = CloudPart::Skirt;
  skirt.radius = 300 + 250 * age * std::exp(-age / 50.);
  skirt.height = 1500 * smooth(0, 7, age) * (1 - smooth(55, 105, age)) + 1;
  skirt.turn = age * .006;
  skirt.color[0] = .46f; skirt.color[1] = .39f; skirt.color[2] = .31f;
  // Dust thins as it spreads.
  skirt.alpha = alpha * float(.92 * (1 - smooth(14, 75, age)));
  if (skirt.alpha > .01f) parts.push_back(skirt);
  return parts;
}

}  // namespace ofs::client
