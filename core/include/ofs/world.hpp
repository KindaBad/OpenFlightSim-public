#pragma once
// The shape of the world a game is played on.
//
// Positions, velocities and attitudes live in the game frame: Cartesian
// metres in doubles. In the valley (FlatWorld) that frame is north-east-down
// at the airfield, as it always was. On a round planet it is the planet seen
// from the game's anchor, so "down", "altitude" and "level" depend on where a
// point is. Code that needs any of those asks the world instead of reading the
// z axis. Differences between positions need no world at all.
//
// FlatWorld returns exactly the expressions the code used before, so moving
// code onto WorldShape changes no result. See docs/WHOLE_EARTH_PLAN.md.
#include "math.hpp"

#include <algorithm>

namespace ofs {

// The ground under a point.
struct Ground {
  double altitude{};        // metres above sea level
  Vec3 normal{0, 0, -1};    // outward unit normal, game frame
};

class WorldShape {
 public:
  virtual ~WorldShape() = default;
  // Metres above sea level.
  virtual double altitude(const Vec3& p) const = 0;
  // Unit vector toward the centre of the planet at p.
  virtual Vec3 down(const Vec3& p) const = 0;
  // Unit vector away from it.
  virtual Vec3 up(const Vec3& p) const = 0;
  // The upward part of a game-frame vector at p, e.g. the climb rate.
  virtual double upward(const Vec3& p, const Vec3& v) const = 0;
  // The point above or below p at the given altitude.
  virtual Vec3 atAltitude(const Vec3& p, double altitude) const = 0;
  // A game-frame vector or body attitude in the local north-east-down axes
  // at p, and back.
  virtual Vec3 toLocal(const Vec3& p, const Vec3& v) const = 0;
  virtual Vec3 fromLocal(const Vec3& p, const Vec3& v) const = 0;
  virtual Quat toLocal(const Vec3& p, const Quat& att) const = 0;
  virtual Quat fromLocal(const Vec3& p, const Quat& att) const = 0;
  // The terrain under p.
  virtual Ground terrain(const Vec3& p) const = 0;
  // A level plain at sea level under p, for runs without terrain.
  virtual Ground seaLevel(const Vec3& p) const = 0;
  // Altitude of standing water over p, or kNoWater where the ground is dry.
  virtual double waterAltitude(const Vec3& p) const = 0;

  // Metres above the terrain; negative below it.
  double clearance(const Vec3& p) const { return altitude(p) - terrain(p).altitude; }
  // The terrain's surface directly under or over p.
  Vec3 onTerrain(const Vec3& p) const { return atAltitude(p, terrain(p).altitude); }
  // p, lifted if need be to stand at least margin metres above the terrain.
  Vec3 aboveTerrain(const Vec3& p, double margin) const {
    return atAltitude(p, std::max(altitude(p), terrain(p).altitude + margin));
  }
};

// The valley: north-east-down at the airfield, the procedural terrain and lakes.
class FlatWorld final : public WorldShape {
 public:
  double altitude(const Vec3& p) const override { return -p.z; }
  Vec3 down(const Vec3&) const override { return {0, 0, 1}; }
  Vec3 up(const Vec3&) const override { return {0, 0, -1}; }
  double upward(const Vec3&, const Vec3& v) const override { return -v.z; }
  Vec3 atAltitude(const Vec3& p, double altitude) const override { return {p.x, p.y, -altitude}; }
  Vec3 toLocal(const Vec3&, const Vec3& v) const override { return v; }
  Vec3 fromLocal(const Vec3&, const Vec3& v) const override { return v; }
  Quat toLocal(const Vec3&, const Quat& att) const override { return att; }
  Quat fromLocal(const Vec3&, const Quat& att) const override { return att; }
  Ground terrain(const Vec3& p) const override;
  // As terrain() over level ground: the altitude is -heightNed, so -0.
  Ground seaLevel(const Vec3&) const override { return {-0., {0, 0, -1}}; }
  double waterAltitude(const Vec3& p) const override;
};

// The world this process plays on. The valley unless another is installed;
// install one before any simulation starts and keep it alive.
const WorldShape& world();
void useWorld(const WorldShape& world);

}  // namespace ofs
