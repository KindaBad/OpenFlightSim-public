#include "ofs/net/combat.hpp"
#include "ofs/ballistics.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace ofs::net {
const std::array<HitSphere, 17> &aircraftHitboxes() {
  // FRD meters, approximating the existing transport placeholder, independent
  // of visual mesh. Sphere chains preserve explicit major-region identity.
  static const std::array<HitSphere, 17> boxes{
      {{{-14, 0, 0}, 2.2, HitRegion::Fuselage},
       {{-10, 0, 0}, 2.2, HitRegion::Fuselage},
       {{-6, 0, 0}, 2.2, HitRegion::Fuselage},
       {{-2, 0, 0}, 2.2, HitRegion::Fuselage},
       {{2, 0, 0}, 2.2, HitRegion::Fuselage},
       {{6, 0, 0}, 2.2, HitRegion::Fuselage},
       {{10, 0, 0}, 2.2, HitRegion::Fuselage},
       {{14, 0, 0}, 2.2, HitRegion::Fuselage},
       {{-2, -4, 0}, 2.5, HitRegion::LeftWing},
       {{-4, -8, 0}, 2.5, HitRegion::LeftWing},
       {{-6, -12, 0}, 2.5, HitRegion::LeftWing},
       {{-8, -16, 0}, 2.5, HitRegion::LeftWing},
       {{-2, 4, 0}, 2.5, HitRegion::RightWing},
       {{-4, 8, 0}, 2.5, HitRegion::RightWing},
       {{-6, 12, 0}, 2.5, HitRegion::RightWing},
       {{-8, 16, 0}, 2.5, HitRegion::RightWing},
       {{-18, 0, -2}, 3.0, HitRegion::Tail}}};
  return boxes;
}
namespace {
HitSphere bodyHitbox(AircraftType type,std::size_t index) {
  const auto& definition=aircraftDefinition(type);
  const auto& base=aircraftHitboxes()[index];
  if (definition.collision[0].radius>0)
    return {definition.collision[index].center,definition.collision[index].radius,base.region};
  const auto s=definition.hitboxScale;
  return {{base.center.x*s.x,base.center.y*s.y,base.center.z*s.z},
          base.radius*std::max({s.x,s.y,s.z}),base.region};
}
}
double sweptSphere(Vec3 start, Vec3 end, Vec3 center, double radius) {
  const auto offset = start - center, delta = end - start;
  const double c = offset.norm2() - radius * radius;
  if (c <= 0)
    return 0;
  const double a = delta.norm2(), b = offset.dot(delta);
  if (a < 1e-18 || b > 0)
    return std::numeric_limits<double>::infinity();
  const double discriminant = b * b - a * c;
  if (discriminant < 0)
    return std::numeric_limits<double>::infinity();
  const double t = (-b - std::sqrt(discriminant)) / a;
  return t >= 0 && t <= 1 ? t : std::numeric_limits<double>::infinity();
}
void advanceProjectile(Projectile &p, double dt) {
  const Vec3 delta = ballisticDisplacement(p.velocity, dt);
  p.position += delta;
  p.velocity.z += kG0 * dt;
  p.age += dt;
  p.distance += delta.norm();
}
Combat::Combat(GunConfig config) : gun_(config) {
  for (double v :
       {gun_.rpm, gun_.muzzleVelocity, gun_.dispersion, gun_.damage,
        gun_.lifetime, gun_.range, gun_.muzzle.norm2(), gun_.direction.norm2()})
    if (!std::isfinite(v))
      throw std::invalid_argument("nonfinite gun");
  if (gun_.rpm < 60 || gun_.rpm > 3600 || gun_.muzzleVelocity <= 0 ||
      gun_.muzzleVelocity > 5000 || gun_.dispersion < 0 ||
      gun_.dispersion > .1 || gun_.damage <= 0 || gun_.damage > 100 ||
      gun_.lifetime <= 0 || gun_.lifetime > 10 || gun_.range <= 0 ||
      gun_.range > 10000 || gun_.direction.norm() < .99 ||
      gun_.direction.norm() > 1.01 || !gun_.ammo || gun_.ammo > 600 ||
      !gun_.respawnDelay)
    throw std::invalid_argument("gun configuration bounds");
  rounds_.reserve(capacity);
  events_.reserve(eventCapacity);
}
void Combat::emit(CombatEvent e) {
  e.id = nextEvent_++;
  if (events_.size() == eventCapacity) {
    ++stats_.droppedEvents;
    return;
  }
  events_.push_back(e);
  stats_.peakEvents = std::max(stats_.peakEvents, events_.size());
}
std::vector<CombatEvent> Combat::takeEvents() {
  auto result = events_;
  events_.clear();
  return result;
}
bool Combat::fire(Tick tick, EntityId owner, const State &s, Life &life) {
  return fire(tick, owner, s, life, gun_);
}
bool Combat::fire(Tick tick, EntityId owner, const State &s, Life &life, const GunConfig& gun) {
  if (!life.alive() || !life.ammo || tick < life.readyTick) {
    ++stats_.cooldownBlocks;
    return false;
  }
  if (rounds_.size() == capacity) {
    ++stats_.poolFull;
    return false;
  }
  if (!finiteState(s)) {
    ++stats_.rejectedFire;
    return false;
  }
  // Deterministic server-owned dispersion, never client-selected.
  const auto direction = gunShotDirection(gun, nextProjectile_);
  Projectile p;
  p.id = nextProjectile_++;
  p.owner = owner;
  p.generation = life.generation;
  p.born = tick;
  p.position = s.pos_ned + s.att.rotate(gun.muzzle);
  p.velocity = s.vel_ned + s.att.rotate(direction) * gun.muzzleVelocity;
  p.damage = gun.damage; p.lifetime = gun.lifetime; p.range = gun.range;
  p.respawnDelay = gun.respawnDelay;
  rounds_.push_back(p);
  --life.ammo;
  life.readyTick = tick + static_cast<Tick>(std::ceil(7200 / gun.rpm));
  ++stats_.shots;
  stats_.peakProjectiles = std::max(stats_.peakProjectiles, rounds_.size());
  CombatEvent e;
  e.kind = CombatKind::Shot;
  e.tick = tick;
  e.projectile = p.id;
  e.owner = owner;
  e.generation = life.generation;
  e.position = p.position;
  e.velocity = p.velocity;
  e.lifetime = std::min(gun.lifetime,gun.range/std::max(1.,p.velocity.norm()));
  emit(e);
  return true;
}
void Combat::removeOwner(EntityId id) {
  std::erase_if(rounds_, [&](const Projectile &p) { return p.owner == id; });
}
void Combat::step(Tick tick, std::span<CombatTarget> targets) {
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  // Cache world hitbox centers once per tick, rather than rotate 17 spheres for
  // every projectile/aircraft pair. Broad sphere bounds eliminate most pairs.
  struct Boxes {
    std::array<Vec3, 17> previous, current;
  };
  std::array<Boxes, maxPlayers> cache;
  if (targets.size() > maxPlayers)
    throw std::invalid_argument("combat target bound");
  for (std::size_t i = 0; i < targets.size(); ++i) {
    const auto& cfg=aircraftDefinition(targets[i].type).flight;
    const auto previousCg=loadedCg(cfg,targets[i].previous),currentCg=loadedCg(cfg,targets[i].current);
    for (std::size_t h = 0; h < aircraftHitboxes().size(); ++h) {
      const Vec3 c=bodyHitbox(targets[i].type,h).center;
      cache[i].previous[h] =
          targets[i].previous.pos_ned + targets[i].previous.att.rotate(c-previousCg);
      cache[i].current[h] =
          targets[i].current.pos_ned + targets[i].current.att.rotate(c-currentCg);
    }
  }
  // Motion and collision separately measured (motion sweep starts retained).
  std::array<Vec3, capacity> previous;
  for (std::size_t i = 0; i < rounds_.size(); ++i) {
    previous[i] = rounds_[i].position;
    advanceProjectile(rounds_[i], tickSeconds);
  }
  const auto motionEnd = Clock::now();
  std::size_t write = 0;
  for (std::size_t i = 0; i < rounds_.size(); ++i) {
    auto &p = rounds_[i];
    double nearest = std::numeric_limits<double>::infinity();
    CombatTarget *hit = nullptr;
    HitRegion region{};
    for (std::size_t j = 0; j < targets.size(); ++j) {
      auto &t = targets[j];
      if (!t.life->alive() ||
          (t.id == p.owner && (p.age <= .1 || p.distance <= 40)))
        continue;
      if (!std::isfinite(sweptSphere(previous[i] - t.previous.pos_ned,
                                     p.position - t.current.pos_ned, {}, 24)))
        continue;
      for (std::size_t h = 0; h < aircraftHitboxes().size(); ++h) {
        const auto box=bodyHitbox(t.type,h);
        const auto fraction = sweptSphere(previous[i] - cache[j].previous[h],
                                          p.position - cache[j].current[h], {},
                                          box.radius);
        if (fraction < nearest) {
          nearest = fraction;
          hit = &t;
          region = box.region;
        }
      }
    }
    // Clip collision to finite lifetime/range, including the terminal partial
    // tick.
    const double ageBefore = p.age - tickSeconds;
    const double stepDistance = (p.position - previous[i]).norm();
    const double distanceBefore = p.distance - stepDistance;
    const double validFraction = std::min(
        {1.0, (p.lifetime - ageBefore) / tickSeconds,
         stepDistance > 0 ? (p.range - distanceBefore) / stepDistance
                          : 1.0});
    const double terrain = bulletTerrainFraction(previous[i],
        previous[i]+(p.position-previous[i])*std::clamp(validFraction,0.0,1.0));
    if (std::isfinite(terrain) && (!hit || terrain*validFraction <= nearest))
      continue; // Terrain occludes aircraft behind the hill and consumes the round.
    if (hit && nearest <= validFraction) {
      auto &life = *hit->life;
      life.health = std::max(0.0, life.health - p.damage);
      ++stats_.hits;
      CombatEvent e;
      e.kind = CombatKind::Hit;
      e.tick = tick;
      e.projectile = p.id;
      e.owner = p.owner;
      e.target = hit->id;
      e.generation = life.generation;
      e.region = region;
      e.health = life.health;
      e.position = previous[i] + (p.position - previous[i]) * nearest;
      emit(e);
      if (!life.alive()) {
        ++life.deaths;
        ++stats_.kills;
        life.respawnTick = tick + p.respawnDelay;
        for (auto &t : targets)
          if (t.id == p.owner && t.id != hit->id)
            ++t.life->kills;
        e.kind = CombatKind::Destroyed;
        emit(e);
      }
      continue; // Round consumed: one damage application maximum.
    }
    if (p.age >= p.lifetime || p.distance >= p.range ||
        !std::isfinite(p.position.norm2()) ||
        !std::isfinite(p.velocity.norm2()))
      continue;
    if (write != i)
      rounds_[write] = p;
    ++write;
  }
  rounds_.resize(write);
  stats_.motionUs =
      std::chrono::duration<double, std::micro>(motionEnd - start).count();
  stats_.collisionUs =
      std::chrono::duration<double, std::micro>(Clock::now() - motionEnd)
          .count();
}
} // namespace ofs::net
