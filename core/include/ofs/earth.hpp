#pragma once
// The round Earth: the WGS84 ellipsoid.
//
// The game frame is the planet seen from the game's anchor: its origin is at
// sea level under the anchor, and there x points north, y east and z down.
// Away from the anchor those axes no longer line up with the local vertical,
// so altitude, "down" and the local north-east-down axes are worked out from
// latitude and longitude. The Earth does not turn.
//
// Without a terrain pack the planet is smooth: solid ground at sea level
// everywhere, and no water. With one (EarthTerrain) it has the real land, sea
// and lakes.
#include "world.hpp"

namespace ofs {

// WGS84.
inline constexpr double kEarthEquatorialRadius = 6378137.0;
inline constexpr double kEarthFlattening = 1 / 298.257223563;
inline constexpr double kEarthEccentricity2 = kEarthFlattening * (2 - kEarthFlattening);

class EarthTerrain;

struct Geodetic {
  double latitude{}, longitude{};  // radians, geodetic
  double altitude{};               // metres above the ellipsoid
};

// The same, as the sines and cosines of latitude and longitude.
struct Surface {
  double sinLat{}, cosLat{1}, sinLon{}, cosLon{1};
  double altitude{};
};

// Earth-centred, Earth-fixed metres: x toward 0N 0E, y toward 0N 90E, z toward 90N.
Vec3 ecefFromGeodetic(const Geodetic& g);
Geodetic geodeticFromEcef(const Vec3& ecef);
Surface surfaceFromEcef(const Vec3& ecef);

class EarthWorld final : public WorldShape {
 public:
  // Anchor latitude and longitude in radians. The terrain, if any, must
  // outlive the world.
  EarthWorld(double latitude, double longitude, const EarthTerrain* terrain = nullptr);

  Geodetic geodetic(const Vec3& p) const { return geodeticFromEcef(toEcef(p)); }
  Vec3 position(const Geodetic& g) const { return fromEcef(ecefFromGeodetic(g)); }
  Vec3 toEcef(const Vec3& p) const;
  Vec3 fromEcef(const Vec3& ecef) const;
  // A direction or displacement, from ECEF axes to the game frame.
  Vec3 directionFromEcef(const Vec3& v) const { return frame_.apply(v); }

  double altitude(const Vec3& p) const override;
  Vec3 down(const Vec3& p) const override;
  Vec3 up(const Vec3& p) const override { return -down(p); }
  double upward(const Vec3& p, const Vec3& v) const override { return v.dot(up(p)); }
  Vec3 atAltitude(const Vec3& p, double altitude) const override;
  Vec3 toLocal(const Vec3& p, const Vec3& v) const override;
  Vec3 fromLocal(const Vec3& p, const Vec3& v) const override;
  Quat toLocal(const Vec3& p, const Quat& att) const override;
  Quat fromLocal(const Vec3& p, const Quat& att) const override;
  Ground terrain(const Vec3& p) const override;
  Ground seaLevel(const Vec3& p) const override { return {0., up(p)}; }
  double waterAltitude(const Vec3& p) const override;

 private:
  // Rows are the local north, east and down axes, in the frame of the columns.
  struct Axes {
    Vec3 north, east, down;
    Vec3 apply(const Vec3& v) const { return {north.dot(v), east.dot(v), down.dot(v)}; }
    Vec3 applyTransposed(const Vec3& v) const { return north * v.x + east * v.y + down * v.z; }
  };
  struct Local {
    Surface surface;
    Axes axes;  // game -> local NED
  };
  static Axes ecefAxes(const Surface&);  // ECEF -> local NED
  const Local& local(const Vec3& p) const;
  const EarthTerrain* terrain_{};
  Vec3 anchor_;  // ECEF of the game origin
  Axes frame_;   // ECEF -> game
};

}  // namespace ofs
