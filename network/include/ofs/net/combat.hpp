#pragma once
#include "ofs/net/protocol.hpp"
#include <array>
#include <span>
namespace ofs::net {
using GunConfig = ofs::GunConfig;
struct HitSphere {
  Vec3 center;
  double radius;
  HitRegion region;
};
const std::array<HitSphere, 17> &aircraftHitboxes();
// Fraction of earliest segment/sphere entry; infinity means no intersection.
double sweptSphere(Vec3 start, Vec3 end, Vec3 center, double radius);
struct Projectile {
  std::uint64_t id{};
  EntityId owner{};
  std::uint32_t generation{};
  Tick born{};
  Vec3 position, velocity;
  double age{}, distance{};
  double damage{25}, lifetime{3}, range{2400};
  Tick respawnDelay{480};
};
void advanceProjectile(Projectile &, double dt);
struct CombatTarget {
  EntityId id{};
  State previous, current;
  Life *life{};
  AircraftType type{AircraftType::A320};
};
struct CombatStats {
  std::uint64_t shots{}, hits{}, kills{}, respawns{}, rejectedFire{},
      cooldownBlocks{}, poolFull{}, droppedEvents{};
  std::size_t peakProjectiles{}, peakEvents{}, peakFireQueue{};
  double motionUs{}, collisionUs{};
};
class Combat {
public:
  static constexpr std::size_t capacity = 4096, eventCapacity = 256;
  explicit Combat(GunConfig config = {});
  const GunConfig &gun() const { return gun_; }
  bool fire(Tick, EntityId, const State &, Life &);
  bool fire(Tick, EntityId, const State &, Life &, const GunConfig &);
  void step(Tick, std::span<CombatTarget>);
  void removeOwner(EntityId);
  void emit(CombatEvent);
  std::vector<CombatEvent> takeEvents();
  const std::vector<Projectile> &projectiles() const { return rounds_; }
  CombatStats &stats() { return stats_; }
  const CombatStats &stats() const { return stats_; }

private:
  GunConfig gun_;
  std::uint64_t nextProjectile_{1}, nextEvent_{1};
  std::vector<Projectile> rounds_;
  std::vector<CombatEvent> events_;
  CombatStats stats_;
};
} // namespace ofs::net
