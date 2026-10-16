#include "ofs/earth.hpp"

#include "ofs/earth_terrain.hpp"
#include "ofs/water.hpp"

#include <cmath>

namespace ofs {
namespace {

// The rotation that a matrix with these rows applies, as a quaternion.
Quat quatFromRows(const Vec3& r0, const Vec3& r1, const Vec3& r2) {
  const double trace = r0.x + r1.y + r2.z;
  Quat q;
  if (trace > 0) {
    const double s = std::sqrt(trace + 1) * 2;
    q = {s / 4, (r2.y - r1.z) / s, (r0.z - r2.x) / s, (r1.x - r0.y) / s};
  } else if (r0.x > r1.y && r0.x > r2.z) {
    const double s = std::sqrt(1 + r0.x - r1.y - r2.z) * 2;
    q = {(r2.y - r1.z) / s, s / 4, (r0.y + r1.x) / s, (r0.z + r2.x) / s};
  } else if (r1.y > r2.z) {
    const double s = std::sqrt(1 + r1.y - r0.x - r2.z) * 2;
    q = {(r0.z - r2.x) / s, (r0.y + r1.x) / s, s / 4, (r1.z + r2.y) / s};
  } else {
    const double s = std::sqrt(1 + r2.z - r0.x - r1.y) * 2;
    q = {(r1.x - r0.y) / s, (r0.z + r2.x) / s, (r1.z + r2.y) / s, s / 4};
  }
  return q.normalized();
}

}  // namespace

Vec3 ecefFromGeodetic(const Geodetic& g) {
  const double sinLat = std::sin(g.latitude), cosLat = std::cos(g.latitude);
  const double n = kEarthEquatorialRadius / std::sqrt(1 - kEarthEccentricity2 * sinLat * sinLat);
  return {(n + g.altitude) * cosLat * std::cos(g.longitude), (n + g.altitude) * cosLat * std::sin(g.longitude),
          (n * (1 - kEarthEccentricity2) + g.altitude) * sinLat};
}

Geodetic geodeticFromEcef(const Vec3& e) {
  const Surface s = surfaceFromEcef(e);
  return {std::atan2(s.sinLat, s.cosLat), std::atan2(s.sinLon, s.cosLon), s.altitude};
}

Surface surfaceFromEcef(const Vec3& e) {
  // Bowring's method, twice: the error after two passes is far below a
  // micrometre from the deepest ocean to orbit. It yields the sine and cosine
  // of the latitude directly, so the local axes need no further trigonometry.
  constexpr double a = kEarthEquatorialRadius, b = a * (1 - kEarthFlattening);
  constexpr double e2 = kEarthEccentricity2, ep2 = (a * a - b * b) / (b * b);
  const double p = std::hypot(e.x, e.y);
  Surface out;
  if (p > 0) {
    out.sinLon = e.y / p;
    out.cosLon = e.x / p;
  }
  // Parametric latitude from the point, then the geodetic latitude from it.
  double sinT = e.z * a, cosT = p * b;
  double norm = std::hypot(sinT, cosT);
  sinT /= norm, cosT /= norm;
  double sinLat = 0, cosLat = 1;
  for (int pass = 0; pass < 2; ++pass) {
    const double y = e.z + ep2 * b * sinT * sinT * sinT, x = p - e2 * a * cosT * cosT * cosT;
    norm = std::hypot(y, x);
    sinLat = y / norm, cosLat = x / norm;
    sinT = (1 - kEarthFlattening) * sinLat, cosT = cosLat;
    norm = std::hypot(sinT, cosT);
    sinT /= norm, cosT /= norm;
  }
  out.sinLat = sinLat;
  out.cosLat = cosLat;
  out.altitude = p * cosLat + e.z * sinLat - a * std::sqrt(1 - e2 * sinLat * sinLat);
  return out;
}

EarthWorld::Axes EarthWorld::ecefAxes(const Surface& s) {
  return {{-s.sinLat * s.cosLon, -s.sinLat * s.sinLon, s.cosLat},
          {-s.sinLon, s.cosLon, 0},
          {-s.cosLat * s.cosLon, -s.cosLat * s.sinLon, -s.sinLat}};
}

EarthWorld::EarthWorld(double latitude, double longitude, const EarthTerrain* terrain)
    : terrain_(terrain),
      anchor_(ecefFromGeodetic({latitude, longitude, 0})),
      frame_(ecefAxes({std::sin(latitude), std::cos(latitude), std::sin(longitude), std::cos(longitude), 0})) {}

Vec3 EarthWorld::toEcef(const Vec3& p) const { return anchor_ + frame_.applyTransposed(p); }
Vec3 EarthWorld::fromEcef(const Vec3& ecef) const { return frame_.apply(ecef - anchor_); }

// Most questions in a simulation step are about the same few points, the
// aircraft's position above all, so the last answer is kept.
const EarthWorld::Local& EarthWorld::local(const Vec3& p) const {
  thread_local const EarthWorld* owner = nullptr;
  thread_local Vec3 point{};
  thread_local Local cached{};
  if (owner == this && point.x == p.x && point.y == p.y && point.z == p.z) return cached;
  const Surface s = surfaceFromEcef(toEcef(p));
  const Axes axes = ecefAxes(s);
  cached = {s, {frame_.apply(axes.north), frame_.apply(axes.east), frame_.apply(axes.down)}};
  owner = this;
  point = p;
  return cached;
}

double EarthWorld::altitude(const Vec3& p) const { return local(p).surface.altitude; }
Vec3 EarthWorld::down(const Vec3& p) const { return local(p).axes.down; }

Vec3 EarthWorld::atAltitude(const Vec3& p, double altitude) const {
  // Along the normal through p, which leaves latitude and longitude unchanged.
  const Local& l = local(p);
  return p + l.axes.down * (l.surface.altitude - altitude);
}

Vec3 EarthWorld::toLocal(const Vec3& p, const Vec3& v) const { return local(p).axes.apply(v); }
Vec3 EarthWorld::fromLocal(const Vec3& p, const Vec3& v) const { return local(p).axes.applyTransposed(v); }

Quat EarthWorld::toLocal(const Vec3& p, const Quat& att) const {
  const Axes& a = local(p).axes;
  return (quatFromRows(a.north, a.east, a.down) * att).normalized();
}
Quat EarthWorld::fromLocal(const Vec3& p, const Quat& att) const {
  const Axes& a = local(p).axes;
  return (quatFromRows(a.north, a.east, a.down).conj() * att).normalized();
}

Ground EarthWorld::terrain(const Vec3& p) const {
  if (!terrain_) return seaLevel(p);
  const Local here = local(p);
  const Vec3 ecef = toEcef(p);
  const double ground = terrain_->groundAltitude(ecef, here.surface);
  // The slope only matters to what touches the ground; well above it, the
  // vertical stands in and saves two samples.
  if (here.surface.altitude - ground > 50) return {ground, here.axes.applyTransposed({0, 0, -1})};
  // The slope from two more samples a few metres north and east.
  constexpr double step = 2;
  const auto at = [&](const Vec3& gameAxis) {
    const Vec3 offset = ecef + frame_.applyTransposed(gameAxis) * step;
    return terrain_->groundAltitude(offset, surfaceFromEcef(offset));
  };
  const Vec3 normal = Vec3{-(at(here.axes.north) - ground) / step, -(at(here.axes.east) - ground) / step, -1}.normalized();
  return {ground, here.axes.applyTransposed(normal)};
}

double EarthWorld::waterAltitude(const Vec3& p) const {
  return terrain_ ? terrain_->waterAltitude(local(p).surface) : kNoWater;
}

}  // namespace ofs
