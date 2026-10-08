#pragma once
// Pilots who have left their aircraft.
//
// The seat fires up and out of the aircraft, the airstream stops it within a
// second or two, and a parachute opens and brings the pilot down. This is
// presentation only: the simulation already has the aircraft as lost.
//
// The figure and its parachute are built in the pilot's own axes (X forward,
// Y right, Z down), with the origin where the risers meet the harness.

#include "ofs/aircraft_definition.hpp"
#include "ofs/terrain.hpp"
#include "scenery.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ofs::client {

struct EjectedPilot {
  std::uint64_t entity{};  // whose pilot; the renderer's 0 is the player's own
  bool own{};
  Vec3 position{}, velocity{};
  Vec3 up{0, 0, -1}, forward{1, 0, 0};  // unit, world NED
  double age{};
  double canopy{};  // 0 packed .. 1 full
  double landed{};  // seconds on the ground; 0 while coming down
  bool opened{};    // set on the update in which the parachute opened
  bool touched{};   // set on the update in which the pilot reached the ground
};

class Ejections {
 public:
  static constexpr std::size_t kCapacity = 16;
  static constexpr double kRocketSeconds = .4, kOpenAt = 1.1, kOpenSeconds = .9, kLandedSeconds = 9, kLifetime = 900;

  // A pilot leaving `aircraft`: up through where the canopy was.
  void eject(std::uint64_t entity, bool own, const State& aircraft, AircraftType type) {
    const auto& definition = aircraftDefinition(type);
    add(entity, own, aircraft.pos_ned + aircraft.att.rotate(definition.visual.cockpit - loadedCg(definition.flight, aircraft)),
        aircraft.vel_ned, aircraft.att.rotate({0, 0, -1}), aircraft.att.rotate({1, 0, 0}));
  }
  // The same for an aircraft that is not being drawn: only where it was and
  // how it was moving are known.
  void eject(std::uint64_t entity, bool own, const Vec3& position, const Vec3& velocity) {
    const Vec3 level{velocity.x, velocity.y, 0};
    add(entity, own, position, velocity, {0, 0, -1}, level.norm() > 1 ? level.normalized() : Vec3{1, 0, 0});
  }

  void update(double dt, const Vec3& wind = {}) {
    if (!(dt > 0)) return;
    dt = std::min(dt, .1);
    for (EjectedPilot& pilot : pilots_) {
      pilot.age += dt;
      pilot.opened = pilot.touched = false;
      if (pilot.landed > 0) {
        // Down: the canopy spills its air and settles.
        pilot.landed += dt;
        pilot.canopy = std::max(0., pilot.canopy - dt * .45);
        continue;
      }
      if (pilot.age < kRocketSeconds) pilot.velocity += pilot.up * (65 * dt);
      const double before = pilot.canopy;
      pilot.canopy = std::clamp((pilot.age - kOpenAt) / kOpenSeconds, 0., 1.);
      pilot.opened = before == 0 && pilot.canopy > 0;
      // Drag: a seat and its occupant in the airstream, then the canopy,
      // which settles the descent at about six metres a second.
      const double drag = .0035 + (.26 - .0035) * pilot.canopy * pilot.canopy;
      Vec3 relative = pilot.velocity - wind;
      const double speed = relative.norm();
      relative = relative / (1 + drag * speed * dt);
      pilot.velocity = wind + relative;
      pilot.velocity.z += kG0 * dt;
      pilot.position += pilot.velocity * dt;
      // The canopy trails behind the motion through the air, so the pilot
      // swings under it until the descent is steady.
      const Vec3 through = pilot.velocity - wind;
      if (pilot.age > kRocketSeconds && through.norm() > 1) {
        const Vec3 trailing = -through.normalized();
        pilot.up = (pilot.up + (trailing - pilot.up) * std::min(1., dt * (1.5 + 4 * pilot.canopy))).normalized();
      }
      const double ground = groundHeightNed(pilot.position.x, pilot.position.y);
      if (pilot.position.z >= ground - 1.3) {
        pilot.position.z = ground - 1.3;
        pilot.velocity = {};
        pilot.up = {0, 0, -1};
        pilot.landed = 1e-6;
        pilot.touched = true;
      }
    }
    std::erase_if(pilots_, [](const EjectedPilot& pilot) {
      return pilot.landed > kLandedSeconds || pilot.age > kLifetime;
    });
  }

  // The player's own pilot, while they are under a parachute or just down.
  const EjectedPilot* own() const {
    for (const EjectedPilot& pilot : pilots_)
      if (pilot.own) return &pilot;
    return nullptr;
  }
  void clear() { pilots_.clear(); }
  void clearOwn() { std::erase_if(pilots_, [](const EjectedPilot& pilot) { return pilot.own; }); }
  const std::vector<EjectedPilot>& pilots() const { return pilots_; }

 private:
  void add(std::uint64_t entity, bool own, const Vec3& position, const Vec3& velocity, const Vec3& up, const Vec3& forward) {
    std::erase_if(pilots_, [&](const EjectedPilot& pilot) { return pilot.entity == entity && pilot.own == own; });
    if (pilots_.size() >= kCapacity) pilots_.erase(pilots_.begin());
    EjectedPilot pilot;
    pilot.entity = entity;
    pilot.own = own;
    pilot.up = up;
    pilot.forward = forward;
    pilot.position = position;
    pilot.velocity = velocity + up * 18;
    pilots_.push_back(pilot);
  }
  std::vector<EjectedPilot> pilots_;
};

// The pilot's axes in world NED: columns forward, right and down. The figure
// hangs along `up`, facing as nearly `forward` as that allows.
inline std::array<Vec3, 3> pilotAxes(const EjectedPilot& pilot) {
  const Vec3 down = -pilot.up;
  Vec3 right = down.cross(pilot.forward);
  if (right.norm2() < 1e-6) right = down.cross({0, 1, 0});
  right = right.normalized();
  return {right.cross(down).normalized(), right, down};
}

enum class ChutePart : std::uint8_t { Figure, Helmet, Seat, Panels, Stripes, Lines, Count };

struct ChuteMesh {
  std::array<std::vector<SurfaceVertex>, std::size_t(ChutePart::Count)> parts;
};

inline constexpr double kChuteRadius = 3.7, kChuteHeight = 7.6;

inline ChuteMesh buildChuteMesh() {
  ChuteMesh mesh;
  const auto part = [&](ChutePart p) -> std::vector<SurfaceVertex>& { return mesh.parts[std::size_t(p)]; };
  const auto box = [](std::vector<SurfaceVertex>& out, Vec3 centre, Vec3 half) {
    for (int axis = 0; axis < 3; ++axis)
      for (double sign : {-1., 1.}) {
        Vec3 normal{}, u{}, v{};
        (axis == 0 ? normal.x : axis == 1 ? normal.y : normal.z) = sign;
        (axis == 0 ? u.y : u.x) = 1;
        (axis == 2 ? v.y : v.z) = 1;
        const Vec3 face = centre + Vec3{normal.x * half.x, normal.y * half.y, normal.z * half.z};
        const Vec3 du{u.x * half.x, u.y * half.y, u.z * half.z}, dv{v.x * half.x, v.y * half.y, v.z * half.z};
        sceneryTriangle(out, face - du - dv, face + du - dv, face + du + dv, normal, normal, normal, 0);
        sceneryTriangle(out, face - du - dv, face + du + dv, face - du + dv, normal, normal, normal, 0);
      }
  };
  // The pilot: body, legs and the arms up on the risers.
  box(part(ChutePart::Figure), {0, 0, .38}, {.13, .2, .33});
  for (double side : {-1., 1.}) {
    box(part(ChutePart::Figure), {.04, side * .1, 1.08}, {.085, .08, .4});
    box(part(ChutePart::Figure), {0, side * .25, -.12}, {.055, .055, .3});
  }
  box(part(ChutePart::Helmet), {.01, 0, -.13}, {.125, .115, .13});
  // The seat falls away once the parachute is out; until then it is what is seen.
  box(part(ChutePart::Seat), {-.2, 0, .3}, {.07, .26, .62});
  box(part(ChutePart::Seat), {.05, 0, .82}, {.24, .24, .06});

  // The canopy: gores of alternating colour, each bulging between its seams,
  // with a vent at the crown.
  constexpr int kGores = 16, kAround = 3, kUp = 7;
  const auto shell = [&](double turn, double rise) {
    // `rise` 0 at the skirt .. 1 at the crown; `turn` in gores.
    const double angle = 2 * kPi * turn / kGores, latitude = (.06 + .94 * rise) * kPi * .5;
    const double seam = std::abs(turn - std::floor(turn) - .5) * 2;  // 1 at a seam, 0 mid-gore
    const double bulge = 1 + .07 * (1 - seam * seam) * (1 - rise);
    const double radius = kChuteRadius * std::cos(latitude) * bulge * (1 - .1 * (1 - rise) * (1 - rise));
    return Vec3{radius * std::cos(angle), radius * std::sin(angle), -(kChuteHeight + 2.3 * std::sin(latitude))};
  };
  for (int gore = 0; gore < kGores; ++gore) {
    auto& out = part(gore % 2 ? ChutePart::Stripes : ChutePart::Panels);
    for (int i = 0; i < kAround; ++i)
      for (int j = 0; j < kUp; ++j) {
        const double t0 = gore + double(i) / kAround, t1 = gore + double(i + 1) / kAround;
        const double r0 = .93 * j / kUp, r1 = .93 * (j + 1) / kUp;
        const Vec3 a = shell(t0, r0), b = shell(t1, r0), c = shell(t1, r1), d = shell(t0, r1);
        const Vec3 centre{0, 0, -(kChuteHeight - 1.2)};
        const auto normal = [&](const Vec3& p) { return (p - centre).normalized(); };
        sceneryTriangle(out, a, b, c, normal(a), normal(b), normal(c), 0);
        sceneryTriangle(out, a, c, d, normal(a), normal(c), normal(d), 0);
      }
  }
  // Rigging lines from each seam of the skirt down to the two risers.
  for (int gore = 0; gore < kGores; ++gore) {
    const Vec3 skirt = shell(gore, 0);
    const Vec3 riser{0, skirt.y > 0 ? .25 : -.25, -.42};
    const Vec3 along = (skirt - riser).normalized();
    for (const Vec3& across : {along.cross({0, 0, 1}).normalized(), along.cross({1, .3, 0}).normalized()}) {
      const Vec3 half = across * .017, normal = along.cross(across).normalized();
      sceneryTriangle(part(ChutePart::Lines), riser - half, riser + half, skirt + half, normal, normal, normal, 0);
      sceneryTriangle(part(ChutePart::Lines), riser - half, skirt + half, skirt - half, normal, normal, normal, 0);
    }
  }
  return mesh;
}

}  // namespace ofs::client
