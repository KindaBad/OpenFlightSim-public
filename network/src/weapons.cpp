#include "ofs/net/weapons.hpp"
#include "ofs/terrain.hpp"
#include <algorithm>
#include <chrono>
#include <limits>
namespace ofs::net {
MissileNetState projectMissile(const Missile &m, EntityId viewer) {
  // Target identity is useful only to the launching aircraft, never a warning.
  return {m.id,
          m.owner,
          viewer == m.owner.id ? m.target : EntityRef{},
          m.type,
          m.state.position,
          m.state.velocity,
          m.state.attitude,
          m.state.motor,
          m.state.seeker.phase,
          m.state.age};
}
bool missileInterest(const Missile &m, EntityId viewer, Vec3 position) {
  return m.owner.id == viewer || m.target.id == viewer ||
         (m.state.position - position).norm2() <= 15000. * 15000.;
}
RadarNetState radarProjection(const AircraftWeapons &w,
                              std::uint32_t generation) {
  RadarNetState n;
  n.generation = generation;
  n.mode = w.radar.mode;
  n.selected = w.radar.selected;
  n.locked = w.radar.locked;
  n.tracks = w.radar.tracks();
  n.weapon = w.inventory.selected;
  n.seekerReady = w.seekerReady;
  n.seekerTarget = w.inventory.selected == WeaponType::Infrared
                       ? w.acquisitionTarget
                       : w.radar.locked;
  n.lockProgress = w.lockProgress;
  n.envelope = w.envelope;
  for (const auto &s : w.inventory.stations)
    n.stations.push_back(s.mounted);
  return n;
}
std::uint8_t mountedMask(const weapons::Inventory &inventory) {
  std::uint8_t mask = 0;
  for (std::size_t i = 0; i < inventory.stations.size() && i < 8; ++i)
    if (inventory.stations[i].mounted != WeaponType::None)
      mask |= std::uint8_t(1u << i);
  return mask;
}
MissileCombat::MissileCombat() {
  missiles_.reserve(capacity);
  events_.reserve(capacity * 2);
}
void MissileCombat::emit(MissileEvent event) {
  if (events_.size() < capacity * 2)
    events_.push_back(event);
  else
    ++stats_.droppedEvents;
}
bool MissileCombat::launch(Tick tick, EntityRef owner, const State &aircraft,
                           Vec3 station, WeaponType type,
                           const weapons::Track &target) {
  if (missiles_.size() >= capacity || !finiteState(aircraft) || !owner.id ||
      !target.entity.id || target.entity == owner ||
      (type != WeaponType::Infrared && type != WeaponType::ActiveRadar)) {
    ++stats_.rejected;
    return false;
  }
  Missile m;
  m.id = nextId_++;
  m.owner = owner;
  m.target = target.entity;
  m.type = type;
  m.born = tick;
  m.state =
      weapons::launchState(weapons::missileDefinition(type), aircraft, station,
                           {target.position, target.velocity, true});
  missiles_.push_back(m);
  ++stats_.launches;
  stats_.peakMissiles = std::max(stats_.peakMissiles, missiles_.size());
  return true;
}
void MissileCombat::removeOwner(EntityId id, Tick tick) {
  std::erase_if(missiles_, [&](const auto &m) {
    if (m.owner.id != id)
      return false;
    emit({projectMissile(m, m.owner.id), tick, false});
    return true;
  });
}
std::vector<MissileEvent> MissileCombat::takeEvents() {
  auto e = std::move(events_);
  events_.clear();
  return e;
}
void MissileCombat::step(
    Tick tick, std::span<CombatTarget> targets,
    const std::map<EntityId, AircraftWeapons *> &controllers,
    const Weather &weather, Combat &combat) {
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  stats_.fuseUs = 0;
  std::size_t write = 0;
  for (auto &missile : missiles_) {
    auto &s = missile.state;
    const auto previous = s.position;
    const auto &d = weapons::missileDefinition(missile.type);
    const CombatTarget *target = nullptr;
    for (const auto &t : targets)
      if (EntityRef{t.id, t.life->generation} == missile.target &&
          t.life->alive())
        target = &t;
    weapons::SensorTarget sensor;
    if (target) {
      sensor = {
          missile.target,
          target->current.pos_ned,
          target->current.vel_ned,
          target->current.att,
          target->type,
          (target->current.n1[0] + target->current.n1[1]) * .5,
          (target->current.afterburner[0] + target->current.afterburner[1]) *
              .5,
          true};
    }
    weapons::Measurement support;
    const auto controller = controllers.find(missile.owner.id);
    const bool currentOwner =
        std::any_of(targets.begin(), targets.end(), [&](const auto &t) {
          return EntityRef{t.id, t.life->generation} == missile.owner &&
                 t.life->alive();
        });
    // IR and autonomous terminal seekers need no launch-radar support.
    if (missile.type == WeaponType::ActiveRadar && !s.autonomous && target &&
        currentOwner && controller != controllers.end() &&
        controller->second->radar.locked == missile.target) {
      const auto *track = controller->second->radar.find(missile.target);
      if (track && double(tick) * tickSeconds - track->lastDetection <= .5) {
        support = {track->position, track->velocity, true};
        controller->second->radar.mode = weapons::RadarMode::MissileSupport;
      }
    }
    weapons::advanceMissile(d, s, target ? &sensor : nullptr,
                            support.valid ? &support : nullptr, weather,
                            tickSeconds);
    const auto fuseStart = Clock::now();
    double nearest = std::numeric_limits<double>::infinity();
    // Proximity and direct contact sweep against actual aircraft sphere chains.
    // Both bodies move over the tick; no endpoint-only hit or entity teleport.
    if (s.age >= d.armTime && s.distance >= d.minimumRange * .25) {
      for (const auto &t : targets) {
        if (!t.life->alive() || t.id == missile.owner.id)
          continue;
        const auto &def = aircraftDefinition(t.type);
        for (std::size_t h = 0; h < aircraftHitboxes().size(); ++h) {
          const auto &base = aircraftHitboxes()[h];
          const auto scale = def.hitboxScale;
          const auto center =
              def.collision[0].radius > 0
                  ? def.collision[h].center
                  : Vec3{base.center.x * scale.x, base.center.y * scale.y,
                         base.center.z * scale.z};
          const double radius =
              def.collision[0].radius > 0
                  ? def.collision[h].radius
                  : base.radius * std::max({scale.x, scale.y, scale.z});
          const auto a =
              t.previous.pos_ned +
              t.previous.att.rotate(center - loadedCg(def.flight, t.previous));
          const auto b =
              t.current.pos_ned +
              t.current.att.rotate(center - loadedCg(def.flight, t.current));
          const auto before = previous - a, after = s.position - b;
          const bool closing = before.dot(after - before) < 0;
          const double impact =
              sweptSphere(before, after, {}, radius + d.diameter * .5);
          const double prox =
              closing ? sweptSphere(before, after, {}, radius + d.fuseRadius)
                      : std::numeric_limits<double>::infinity();
          nearest = std::min({nearest, impact, prox});
        }
      }
    }
    const double valid =
        std::min(1., (d.lifetime - (s.age - tickSeconds)) / tickSeconds);
    bool detonation = nearest <= valid;
    if (detonation) {
      s.position = previous + (s.position - previous) * nearest;
      ++stats_.detonations;
      for (auto &t : targets) {
        if (!t.life->alive() || t.id == missile.owner.id)
          continue;
        const auto position =
            t.previous.pos_ned +
            (t.current.pos_ned - t.previous.pos_ned) * nearest;
        double distance = std::numeric_limits<double>::infinity();
        const auto &definition = aircraftDefinition(t.type);
        const auto cg = loadedCg(definition.flight, t.current);
        for (std::size_t h = 0; h < aircraftHitboxes().size(); ++h) {
          const auto &base = aircraftHitboxes()[h];
          const auto scale = definition.hitboxScale;
          const auto center =
              definition.collision[0].radius > 0
                  ? definition.collision[h].center
                  : Vec3{base.center.x * scale.x, base.center.y * scale.y,
                         base.center.z * scale.z};
          const double radius =
              definition.collision[0].radius > 0
                  ? definition.collision[h].radius
                  : base.radius * std::max({scale.x, scale.y, scale.z});
          distance = std::min(
              distance,
              std::max(0., (position + t.current.att.rotate(center - cg) -
                            s.position)
                                   .norm() -
                               radius));
        }
        if (distance > d.damageRadius)
          continue;
        const double damage =
            d.damage * std::pow(1 - distance / d.damageRadius, 2);
        if (damage <= 0)
          continue;
        auto &life = *t.life;
        life.health = std::max(0., life.health - damage);
        ++stats_.hits;
        CombatEvent e;
        e.kind = CombatKind::Hit;
        e.tick = tick;
        e.projectile = missile.id;
        e.owner = missile.owner.id;
        e.target = t.id;
        e.generation = life.generation;
        e.health = life.health;
        e.position = s.position;
        combat.emit(e);
        if (!life.alive()) {
          ++life.deaths;
          life.respawnTick = tick + combat.gun().respawnDelay;
          ++combat.stats().kills;
          for (auto &owner : targets)
            if (EntityRef{owner.id, owner.life->generation} == missile.owner)
              ++owner.life->kills;
          e.kind = CombatKind::Destroyed;
          combat.emit(e);
        }
      }
    }
    const bool expired =
        s.age >= d.lifetime ||
        s.position.z >= groundHeightNed(s.position.x, s.position.y) ||
        !std::isfinite(s.position.norm2()) ||
        !std::isfinite(s.velocity.norm2());
    if (detonation || expired) {
      emit({projectMissile(missile, missile.owner.id), tick, detonation});
      if (!detonation)
        ++stats_.expired;
    } else {
      if (&missile != &missiles_[write])
        missiles_[write] = missile;
      ++write;
    }
    stats_.fuseUs +=
        std::chrono::duration<double, std::micro>(Clock::now() - fuseStart)
            .count();
  }
  missiles_.resize(write);
  stats_.missileUs =
      std::chrono::duration<double, std::micro>(Clock::now() - start).count() -
      stats_.fuseUs;
}
} // namespace ofs::net
