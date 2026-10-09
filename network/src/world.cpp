#include "ofs/net/world.hpp"
#include "ofs/net/bot_ai.hpp"
#include "ofs/ground_service.hpp"
#include "ofs/trim.hpp"
#include "ofs/weapons.hpp"
#include "ofs/terrain.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>
namespace ofs::net {
World::World(bool airborne, std::optional<GunConfig> gun)
    : combat_(gun.value_or(GunConfig{})), airborne_(airborne), gunOverride_(gun) {
  for (const auto& definition : aircraftDefinitions()) {
    auto& spawn = spawns_[definition.type];
    if (airborne_) {
    auto trim = solveTrim(definition.flight);
    if (!trim.converged)
      throw std::runtime_error("network spawn trim failed");
    spawn.state = trim.state;
    spawn.controls = trim.controls;
  } else {
    spawn.state.pos_ned.z = -(definition.flight.gear_nose.z - .15);
    spawn.controls.brake01 = 1;
  }
  }
}
const GunConfig& World::gunFor(AircraftType type) const {
  return gunOverride_ ? *gunOverride_ : aircraftDefinition(type).gun.value();
}
namespace {
// Guns and launchers of the base defences. Gameplay values.
GunConfig flakGun() {
  GunConfig gun;
  gun.rpm = 480;
  gun.muzzleVelocity = 880;
  gun.dispersion = .011;
  gun.damage = 9;
  gun.lifetime = 4;
  gun.range = 3400;
  gun.muzzle = {};
  gun.ammo = 60000;
  return gun;
}
bool weaponStructure(StructureKind kind) {
  return kind == StructureKind::Flak || kind == StructureKind::Sam;
}
// Seconds between rounds of a team game.
constexpr Tick roundPause = 15 * 120;
} // namespace
void World::setTeams(unsigned scoreLimit) {
  teams_ = true;
  scoreLimit_ = std::clamp(scoreLimit, 50u, 5000u);
  sites_.assign(structures().size(), Site{});
}
Team World::smallerTeam() const {
  int red = 0, blue = 0;
  for (const auto &[id, p] : players_) {
    (void)id;
    red += p.team == Team::Red;
    blue += p.team == Team::Blue;
  }
  return blue < red ? Team::Blue : Team::Red;
}
bool World::setTeam(EntityId id, Team team) {
  const auto found = players_.find(id);
  if (!teams_ || found == players_.end() || team == Team::None ||
      found->second.team == team)
    return false;
  auto &p = found->second;
  p.team = team;
  combat_.removeOwner(id);
  missiles_.removeOwner(id, tick_);
  // A new life on the new side, so nothing aimed at the old one follows.
  ++p.life.generation;
  p.life.health = 100;
  p.life.ammo = aircraftDefinition(p.type).gun ? gunFor(p.type).ammo : 0;
  p.life.readyTick = tick_;
  p.life.respawnTick = 0;
  spawn(id, p);
  CombatEvent e;
  e.kind = CombatKind::Respawn;
  e.tick = tick_;
  e.owner = e.target = id;
  e.generation = p.life.generation;
  e.health = 100;
  e.position = p.sim.state().pos_ned;
  combat_.emit(e);
  return true;
}
TeamStatus World::teamStatus() const {
  TeamStatus status;
  status.teams = teams_;
  if (!teams_)
    return status;
  status.scoreLimit = std::uint16_t(scoreLimit_);
  status.score[0] = score_[0];
  status.score[1] = score_[1];
  status.winner = winner_;
  status.restartSeconds = winner_ != Team::None && restart_ > tick_
                              ? std::uint8_t(std::min<Tick>(255, (restart_ - tick_ + 119) / 120))
                              : 0;
  for (const auto &site : sites_)
    status.health.push_back(std::uint8_t(std::ceil(clamp(site.health, 0, 100))));
  return status;
}
std::vector<std::string> World::takeNotices() {
  auto notices = std::move(notices_);
  notices_.clear();
  return notices;
}
void World::score(Team team, int points) {
  if (!teams_ || team == Team::None || winner_ != Team::None || points <= 0)
    return;
  auto &total = score_[team == Team::Red ? 0 : 1];
  total += std::uint32_t(points);
  if (total >= scoreLimit_) {
    winner_ = team;
    restart_ = tick_ + roundPause;
    notices_.push_back(std::string(teamName(team)) + " wins the round, " +
                       std::to_string(score_[0]) + " to " + std::to_string(score_[1]) +
                       ". The next one starts in 15 seconds");
  }
}
void World::startRound() {
  score_[0] = score_[1] = 0;
  winner_ = Team::None;
  restart_ = 0;
  sites_.assign(structures().size(), Site{});
  combat_ = Combat(gunOverride_.value_or(GunConfig{}));
  missiles_ = MissileCombat();
  decoys_.clear();
  for (auto &[id, p] : players_) {
    ++p.life.generation;
    p.life.health = 100;
    p.life.ammo = aircraftDefinition(p.type).gun ? gunFor(p.type).ammo : 0;
    p.life.readyTick = tick_;
    p.life.respawnTick = 0;
    spawn(id, p);
    CombatEvent e;
    e.kind = CombatKind::Respawn;
    e.tick = tick_;
    e.owner = e.target = id;
    e.generation = p.life.generation;
    e.health = 100;
    e.position = p.sim.state().pos_ned;
    combat_.emit(e);
  }
  notices_.push_back("A new round has started");
}
void World::applyBlasts() {
  const auto blasts = missiles_.takeBlasts();
  if (!teams_ || winner_ != Team::None)
    return;
  const auto all = structures();
  for (const auto &blast : blasts) {
    const auto &bomb = weapons::bombDefinition(blast.type);
    for (std::size_t i = 0; i < all.size(); ++i) {
      auto &site = sites_[i];
      // A side's own bombs do its own ground no harm.
      if (site.health <= 0 || all[i].team == blast.team)
        continue;
      const Vec3 at{all[i].north, all[i].east, groundHeightNed(all[i].north, all[i].east)};
      const double distance = (at - blast.position).norm();
      if (distance >= bomb.structureRadius)
        continue;
      site.health -= bomb.structureDamage * std::pow(1 - distance / bomb.structureRadius, 2);
      if (site.health > 0)
        continue;
      site.health = 0;
      site.rebuilt = tick_ + Tick(kStructureRebuildSeconds / tickSeconds);
      const auto owner = players_.find(blast.owner.id);
      const std::string place = all[i].site ? "outpost " + std::string(1, char('A' + all[i].site - 1)) : "base";
      notices_.push_back((owner != players_.end() ? owner->second.name : std::string(teamName(blast.team))) +
                         " destroyed a " + structureName(all[i].kind) + " at " + teamName(all[i].team) + "'s " +
                         place + " (+" + std::to_string(all[i].points) + ")");
      score(blast.team, all[i].points);
    }
  }
}
void World::defend(std::span<CombatTarget> targets) {
  if (!teams_ || winner_ != Team::None)
    return;
  const auto all = structures();
  static const GunConfig gun = flakGun();
  for (std::size_t i = 0; i < all.size(); ++i) {
    auto &site = sites_[i];
    if (site.health <= 0) {
      if (tick_ >= site.rebuilt) {
        site = Site{};
        notices_.push_back(std::string(teamName(all[i].team)) + " rebuilt a " + structureName(all[i].kind));
      }
      continue;
    }
    if (!weaponStructure(all[i].kind) || !defences_)
      continue;
    const bool flak = all[i].kind == StructureKind::Flak;
    // Guns shoot in bursts of a second in every three, each gun in its turn.
    if (flak ? (tick_ / 120 + i) % 3 != 0 : tick_ < site.ready)
      continue;
    const double ground = groundHeightNed(all[i].north, all[i].east);
    const Vec3 muzzle{all[i].north, all[i].east, ground - (flak ? 3. : 8.)};
    const CombatTarget *nearest = nullptr;
    double range = flak ? kFlakRange : kSamRange;
    for (const auto &t : targets) {
      if (!t.life->alive() || t.team == all[i].team || t.team == Team::None)
        continue;
      const auto &at = t.current.pos_ned;
      const double distance = (at - muzzle).norm();
      if (distance >= range ||
          (!flak && groundHeightNed(at.x, at.y) - at.z < kSamFloor) ||
          !weapons::lineOfSight(muzzle, at))
        continue;
      range = distance;
      nearest = &t;
    }
    if (!nearest)
      continue;
    const auto &target = nearest->current;
    const EntityId entity = defenceEntityBase | i;
    State aim;
    aim.pos_ned = muzzle;
    if (flak) {
      // Lead the target by the shell's time of flight and lift for its drop.
      // The gunner's aim wanders, so a target that keeps turning is missed.
      Vec3 point = target.pos_ned;
      double flight = 0;
      for (int pass = 0; pass < 3; ++pass) {
        flight = (point - muzzle).norm() / gun.muzzleVelocity;
        point = target.pos_ned + target.vel_ned * flight;
      }
      point.z -= .5 * kG0 * flight * flight;
      const double phase = double(tick_) * .013 + double(i) * 1.7;
      const Vec3 wander{std::sin(phase), std::cos(phase * 1.31), std::sin(phase * .77)};
      const Vec3 direction = ((point - muzzle).normalized() + wander * .012).normalized();
      aim.att = quatFromEuler(0, std::asin(clamp(-direction.z, -1, 1)), std::atan2(direction.y, direction.x));
      site.gun.ammo = gun.ammo;
      combat_.fire(tick_, entity, aim, site.gun, gun, all[i].team);
    } else {
      // Launched steeply toward the target, with the seeker already looking.
      Vec3 direction = (target.pos_ned - muzzle).normalized();
      direction.z = std::min(direction.z, -.45);
      direction = direction.normalized();
      aim.att = quatFromEuler(0, std::asin(clamp(-direction.z, -1, 1)), std::atan2(direction.y, direction.x));
      aim.vel_ned = direction * 60;
      const double now = double(tick_) * tickSeconds;
      const weapons::Track track{{nearest->id, nearest->life->generation}, target.pos_ned, target.vel_ned, now, 1, now};
      if (missiles_.launch(tick_, {entity, 0}, aim, {}, WeaponType::ActiveRadar, track, all[i].team, true))
        site.ready = tick_ + Tick(kSamReloadSeconds / tickSeconds);
    }
  }
}
EntityId World::join(AircraftType type, std::string name, Team team, unsigned loadout) {
  if (!validAircraftType(type) || players_.size() >= maxPlayers)
    return 0;
  unsigned slot = 0;
  for (; slot < maxPlayers; ++slot) {
    bool used = false;
    for (const auto &[id, p] : players_) {
      (void)id;
      used = used || p.spawnSlot == slot;
    }
    if (!used)
      break;
  }
  const auto id = nextId_++;
  auto &p = players_[id];
  p.type = type;
  // A team game puts whoever did not choose on the side with fewer pilots.
  p.team = !teams_ ? Team::None : team != Team::None ? team : smallerTeam();
  p.loadout = std::uint8_t(weapons::bombLoadouts(type) ? loadout % weapons::bombLoadouts(type) : 0);
  p.name = name.empty() ? "Pilot " + std::to_string(id) : std::move(name);
  p.sim = Simulator(aircraftDefinition(type).flight);
  p.spawnSlot = slot;
  p.life.ammo = aircraftDefinition(type).gun ? gunFor(type).ammo : 0;
  spawn(id, p);
  return id;
}
EntityId World::joinBot(AircraftType type) {
  if (!validAircraftType(type) || !aircraftDefinition(type).gun || botCount() >= 8)
    return 0;
  const auto id = join(type);
  if (id) {
    auto &p = players_.at(id);
    p.bot = true;
    p.name = "Bandit " + std::to_string(id);
    spawn(id, p);
  }
  return id;
}
std::size_t World::botCount() const {
  return std::count_if(players_.begin(), players_.end(),
                       [](const auto &entry) { return entry.second.bot; });
}
void World::spawn(EntityId id, Player &p) {
  const auto& spawn = spawns_.at(p.type);
  auto s = spawn.state;
  s.pos_ned.x = -double(p.spawnSlot / 8) * 150;
  s.pos_ned.y = double(p.spawnSlot % 8) * 100;
  auto controls = spawn.controls;
  if (p.bot) {
    const auto trim = solveTrim(p.sim.config(), {1000, 160, 0, 0, 0});
    if (!trim.converged) throw std::runtime_error("bot spawn trim failed");
    s = trim.state;
    controls = trim.controls;
    const auto human = std::find_if(players_.begin(), players_.end(),
        [](const auto &entry) { return !entry.second.bot && entry.second.life.alive(); });
    if (human != players_.end() && !teams_) {
      const auto &target = human->second.sim.state();
      const double yaw = std::atan2(target.vel_ned.y, target.vel_ned.x);
      const auto heading = quatFromEuler(0, 0, yaw);
      const bool pursuing = p.spawnSlot % 2 != 0;
      s.pos_ned = target.pos_ned + heading.rotate(
          {pursuing ? -650. - 250. * (p.spawnSlot / 2) : 1500. + 350. * (p.spawnSlot / 2),
           pursuing ? -60. : 180., 0});
      s.pos_ned.z = std::min(s.pos_ned.z, groundHeightNed(s.pos_ned.x, s.pos_ned.y) - 1000.);
      const auto rotation = quatFromEuler(0, 0, yaw + (pursuing ? 0 : kPi));
      s.att = rotation * s.att;
      s.vel_ned = rotation.rotate(s.vel_ned);
    }
    p.botTarget = 0;
    p.evadeUntil = 0;
    p.missileReady = tick_ + 960;
    p.lastHealth = 100;
  }
  p.lastAttacker = 0;
  p.lastAttacked = 0;
  Vec3 back{-150, 0, 0};
  if (teams_ && p.team != Team::None) {
    // Each side starts at its own airfield, pointed down the runway toward
    // the other: in the air over it, or lined up nose to tail on the ground.
    const auto &field = teamAirfield(p.team);
    const double yaw = p.team == Team::Red ? kPi : 0;
    const auto heading = quatFromEuler(0, 0, yaw);
    const bool flying = airborne_ || p.bot;
    const double height = flying ? s.pos_ned.z : -(p.sim.config().gear_nose.z - .15);
    const double spacing = flying ? 250 : 95;
    s.att = heading * s.att;
    s.vel_ned = heading.rotate(s.vel_ned);
    // In the air later arrivals are put behind; on the runway, ahead.
    back = heading.rotate({flying ? -spacing : spacing, 0, 0});
    s.pos_ned = Vec3{field.north, field.east, height} +
                heading.rotate({flying ? 0. : -1230., 0, 0}) + back * double(p.spawnSlot % 12) +
                heading.rotate({0, flying ? 120. * double(p.spawnSlot / 12) : 0., 0});
  }
  // Existing slots supply the baseline; move back if a currently alive aircraft
  // occupies it. At most 64 exclusions, spaced candidates terminate in 65
  // tries.
  for (unsigned attempt = 0; attempt <= maxPlayers; ++attempt) {
    bool safe = true;
    for (const auto &[other, q] : players_)
      if (other != id && q.life.alive() &&
          (q.sim.state().pos_ned - s.pos_ned).norm() < 60)
        safe = false;
    if (safe)
      break;
    s.pos_ned += back;
  }
  p.weapons.inventory.reset(p.type, p.loadout);
  p.weapons.bombsReleased = 0;
  p.weapons.radar.reset();
  p.weapons.actions.clear();
  p.weapons.readyTick = tick_;
  p.weapons.acquisition = {};
  p.weapons.acquisitionTarget = {};
  p.weapons.lockProgress = 0;
  p.weapons.flares = p.weapons.chaff =
      std::uint8_t(weapons::decoyCapacity(p.type));
  p.weapons.decoysReleased = 0;
  p.weapons.decoyReady = tick_;
  p.standing = 0;
  p.botDecoyReady = tick_;
  p.weapons.inventory.applyPayload(p.sim.config(), s);
  s.time = double(tick_) * tickSeconds;
  if(airborne_ || p.bot) s.vel_ned+=weather_.wind_ned;
  p.sim.setWeather(weather_);
  p.sim.setState(s);
  p.sim.setControls(controls);
  p.lastInput = tick_;
}
void World::controlBot(EntityId id, Player &p) {
  // Human aircraft are opponents. Bots share the same damage/weapon rules,
  // but do not waste ammunition attacking each other.
  EntityId target = 0;
  double nearest = 30000.;
  for (const auto &[other, q] : players_)
    if (other != id && (teams_ ? q.team != p.team : !q.bot) && q.life.alive()) {
      const double distance = (q.sim.state().pos_ned - p.sim.state().pos_ned).norm();
      const double score = distance * (other == p.botTarget ? .8 : 1.);
      if (score < nearest) { nearest = score; target = other; }
    }
  p.botTarget = target;
  if (p.life.health < p.lastHealth) p.evadeUntil = tick_ + 180;
  p.lastHealth = p.life.health;
  const State *opponent = target ? &players_.at(target).sim.state() : nullptr;
  const auto decision = flyBot(p.sim, opponent, gunFor(p.type), tick_,
                               tick_ < p.evadeUntil, id % 2 ? -1 : 1);
  p.sim.setControls(decision.controls);
  p.lastInput = p.lastFireInput = tick_;
  p.firing = decision.firing && p.life.ammo > 0;
  auto &w = p.weapons;
  const weapons::EntityRef ref{target, target ? players_.at(target).life.generation : 0};
  if (target && w.radar.find(ref)) {
    const bool supported = w.radar.selected == ref && w.radar.locked == ref &&
        w.inventory.selected == WeaponType::ActiveRadar;
    w.radar.selected = ref;
    if (w.radar.locked != ref) {
      w.radar.locked = {};
      w.radar.toggleLock();
    }
    w.inventory.selected = WeaponType::ActiveRadar;
    // Spaced shots, with the same track, seeker, range and inventory checks
    // as player launches. No forced missile locks or scripted damage.
    const auto station = w.inventory.nextStation();
    if (supported && tick_ >= p.missileReady && tick_ >= p.evadeUntil && w.seekerReady &&
        station >= 0 && w.envelope.targetRange > 600 && w.envelope.targetRange < 8000) {
      WeaponAction action;
      action.sequence = w.lastSequence + 1;
      action.tick = tick_ + 1;
      action.generation = p.life.generation;
      action.kind = WeaponActionKind::Launch;
      action.station = static_cast<std::uint8_t>(station);
      if (enqueueWeapon(id, action)) p.missileReady = tick_ + 960;
    }
  } else {
    w.radar.selected = w.radar.locked = {};
  }
  // A bot answers a missile that is nearly on it with the matching decoy, one
  // every second or so. It is inattentive to every other missile, and like any
  // pilot it is only saved if its engines are cool enough or it is crossing.
  if (tick_ >= p.botDecoyReady)
    for (const auto &missile : missiles_.missiles()) {
      if (missile.target != EntityRef{id, p.life.generation} || missile.decoy ||
          (missile.id + id) % 2 ||
          (missile.state.position - p.sim.state().pos_ned).norm() > 1800)
        continue;
      if (releaseDecoy(id, missile.type == WeaponType::Infrared
                               ? weapons::DecoyType::Flare
                               : weapons::DecoyType::Chaff))
        p.botDecoyReady = tick_ + 110;
      break;
    }
}
bool World::releaseDecoy(EntityId id, weapons::DecoyType type) {
  const auto found = players_.find(id);
  if (found == players_.end() || !found->second.life.alive())
    return false;
  auto &p = found->second;
  auto &w = p.weapons;
  auto &stock = type == weapons::DecoyType::Flare ? w.flares : w.chaff;
  if (!stock || tick_ < w.decoyReady)
    return false;
  --stock;
  w.decoyReady =
      tick_ + Tick(std::ceil(weapons::decoyInterval / tickSeconds));
  auto decoy = weapons::releaseDecoy(type, {id, p.life.generation},
                                     p.sim.config(), p.sim.state(),
                                     w.decoysReleased++);
  decoy.id = nextDecoy_++;
  if (decoys_.size() >= maxDecoys)
    decoys_.erase(decoys_.begin());
  decoys_.push_back(decoy);
  CombatEvent event;
  event.kind = type == weapons::DecoyType::Flare ? CombatKind::Flare
                                                 : CombatKind::Chaff;
  event.tick = tick_;
  event.projectile = decoy.id;
  event.owner = event.target = id;
  event.generation = p.life.generation;
  event.position = decoy.position;
  event.velocity = decoy.velocity;
  combat_.emit(event);
  return true;
}
Tick World::serviceTicks() {
  return Tick(std::llround(serviceSeconds / tickSeconds));
}
bool World::needsService(const Player &p) const {
  const auto &w = p.weapons;
  const bool armed = aircraftDefinition(p.type).gun.has_value();
  weapons::Inventory full;
  full.reset(p.type, p.loadout);
  for (std::size_t i = 0; i < full.stations.size(); ++i)
    if (i >= w.inventory.stations.size() ||
        w.inventory.stations[i].mounted != full.stations[i].mounted)
      return true;
  if (w.inventory.bombs != full.bombs || w.inventory.bombType != full.bombType)
    return true;
  const auto decoys = weapons::decoyCapacity(p.type);
  return needsRepair(p.sim.config(), p.sim.state()) || p.life.health < 100 ||
         (armed && p.life.ammo < gunFor(p.type).ammo) || w.flares < decoys ||
         w.chaff < decoys;
}
void World::service(EntityId id, Player &p) {
  auto state = p.sim.state();
  repairAndRefuel(p.sim.config(), state);
  auto &w = p.weapons;
  const auto selected = w.inventory.selected;
  w.inventory.reset(p.type, p.loadout);
  if (!weapons::isBomb(w.inventory.bombType))
    w.inventory.selected = selected;
  w.inventory.applyPayload(p.sim.config(), state);
  w.flares = w.chaff = std::uint8_t(weapons::decoyCapacity(p.type));
  p.sim.setState(state);
  p.life.health = 100;
  p.life.ammo = aircraftDefinition(p.type).gun ? gunFor(p.type).ammo : 0;
  p.lastHealth = 100;
  p.lastAttacker = 0;
  CombatEvent event;
  event.kind = CombatKind::Serviced;
  event.tick = tick_;
  event.owner = event.target = id;
  event.generation = p.life.generation;
  event.health = 100;
  event.position = state.pos_ned;
  combat_.emit(event);
}
void World::setMissileReload(double seconds) {
  missileReload_ = seconds > 0 ? std::max<Tick>(1, Tick(std::llround(seconds / tickSeconds))) : 0;
}
void World::reload(Player &p) {
  if (!missileReload_)
    return;
  // Each reload time also puts back a quarter of the flares, the chaff and
  // the gun's rounds, so they fill at the pace of the four pylons.
  auto &w = p.weapons;
  const unsigned decoys = weapons::decoyCapacity(p.type);
  const unsigned rounds = aircraftDefinition(p.type).gun ? gunFor(p.type).ammo : 0;
  if (w.flares >= decoys && w.chaff >= decoys && p.life.ammo >= rounds)
    p.resupplying = 0;
  else if (++p.resupplying >= missileReload_) {
    p.resupplying = 0;
    const auto more = [](unsigned have, unsigned full) { return std::min(full, have + (full + 3) / 4); };
    w.flares = std::uint8_t(std::max<unsigned>(w.flares, more(w.flares, decoys)));
    w.chaff = std::uint8_t(std::max<unsigned>(w.chaff, more(w.chaff, decoys)));
    p.life.ammo = std::uint16_t(std::max<unsigned>(p.life.ammo, more(p.life.ammo, rounds)));
  }
  auto &stations = p.weapons.inventory.stations;
  weapons::Inventory full;
  full.reset(p.type);
  auto state = p.sim.state();
  // The first empty pylon that is still there to hang a missile on.
  std::size_t empty = stations.size();
  for (std::size_t i = stations.size(); i-- > 0;)
    if (i < full.stations.size() && stations[i].mounted == WeaponType::None &&
        full.stations[i].mounted != WeaponType::None &&
        !(stations[i].wing &&
          partDestroyed(state, stations[i].position.y < 0 ? DamagePart::LeftWing
                                                          : DamagePart::RightWing)))
      empty = i;
  if (empty == stations.size()) {
    p.reloading = 0;
    return;
  }
  if (++p.reloading < missileReload_)
    return;
  p.reloading = 0;
  stations[empty].mounted = full.stations[empty].mounted;
  p.weapons.inventory.applyPayload(p.sim.config(), state);
  p.sim.setState(state);
}
void World::loseStores(Player &p, State &state) {
  bool lost = false;
  for (auto &station : p.weapons.inventory.stations) {
    const auto wing = station.position.y < 0 ? DamagePart::LeftWing
                                             : DamagePart::RightWing;
    if (station.wing && station.mounted != WeaponType::None && partDestroyed(state, wing)) {
      station.mounted = WeaponType::None;
      lost = true;
    }
  }
  if (lost)
    p.weapons.inventory.applyPayload(p.sim.config(), state);
}
void World::destroy(EntityId id, Player &p, Vec3 position, Vec3 velocity) {
  p.life.health = 0;
  ++p.life.deaths;
  p.life.respawnTick = tick_ + combat_.gun().respawnDelay;
  CombatEvent event;
  event.kind = CombatKind::Destroyed;
  event.tick = tick_;
  event.owner = event.target = id;
  event.generation = p.life.generation;
  event.position = position;
  event.velocity = velocity;
  const auto attacker = players_.find(p.lastAttacker);
  if (attacker != players_.end() && p.lastAttacker != id &&
      tick_ - p.lastAttacked <= killCreditTicks) {
    ++attacker->second.life.kills;
    ++combat_.stats().kills;
    event.owner = p.lastAttacker;
  }
  combat_.emit(event);
}
void World::setWeather(const Weather& weather) {
  Simulator sanitizer;sanitizer.setWeather(weather);weather_=sanitizer.weather();
  for(auto& [id,p]:players_) { (void)id;p.sim.setWeather(weather_); }
}
void World::leave(EntityId id) {
  combat_.removeOwner(id);
  missiles_.removeOwner(id, tick_);
  players_.erase(id);
}
bool World::enqueue(EntityId id, const std::vector<Command> &commands,
                    std::uint32_t generation) {
  auto it = players_.find(id);
  if (it == players_.end() || commands.empty() || commands.size() > maxBatch) {
    ++stats_.rejected;
    return false;
  }
  auto &p = it->second;
  if (generation > p.life.generation) {
    ++stats_.rejected;
    return false;
  }
  // Delayed previous-life and in-flight dead input is retired without striking
  // an honest session. It can never mutate flight state.
  if (generation < p.life.generation || !p.life.alive()) {
    ++stats_.rejected;
    return true;
  }
  // Validate the whole batch before any mutation; old redundant commands are
  // harmless.
  std::uint64_t previousSequence = 0;
  Tick previousTick = 0;
  for (const auto &c : commands) {
    if (!validControls(c.controls) || !c.sequence ||
        (previousSequence &&
         (c.sequence <= previousSequence || c.tick <= previousTick)) ||
        c.tick > tick_ + 120 || c.sequence > p.highestSequence + 512) {
      ++stats_.rejected;
      return false;
    }
    if (c.sequence > p.acknowledged) {
      for (const auto &[queuedTick, queued] : p.inputs) {
        const bool sameSequence = c.sequence == queued.sequence;
        if ((sameSequence && (c.tick != queuedTick ||
                              !sameControls(c.controls, queued.controls))) ||
            (!sameSequence &&
             ((c.sequence < queued.sequence && c.tick >= queuedTick) ||
              (c.sequence > queued.sequence && c.tick <= queuedTick)))) {
          ++stats_.rejected;
          return false;
        }
      }
    }
    previousSequence = c.sequence;
    previousTick = c.tick;
  }
  for (const auto &c : commands) {
    if (c.sequence <= p.acknowledged)
      continue;
    if (c.tick <= tick_) {
      ++stats_.late;
      p.acknowledged = std::max(p.acknowledged, c.sequence);
      p.highestSequence = std::max(p.highestSequence, c.sequence);
      continue;
    }
    auto existing = p.inputs.find(c.tick);
    if (existing != p.inputs.end()) {
      if (existing->second.sequence != c.sequence) {
        ++stats_.rejected;
        return false;
      }
      continue;
    }
    if (p.inputs.size() >= 256) {
      ++stats_.rejected;
      return false;
    }
    p.inputs.emplace(c.tick, c);
    p.highestSequence = std::max(p.highestSequence, c.sequence);
  }
  stats_.maxQueue = std::max(stats_.maxQueue, p.inputs.size());
  return true;
}
bool World::enqueueFire(EntityId id, const FireCommand &c) {
  auto it = players_.find(id);
  auto reject = [&] {
    ++combat_.stats().rejectedFire;
    return false;
  };
  if (it == players_.end() || !c.sequence || c.weapon || c.tick > tick_ + 120 ||
      (c.tick < tick_ && tick_ - c.tick > 120))
    return reject();
  auto &p = it->second;
  if (!aircraftDefinition(p.type).gun) {
    ++combat_.stats().rejectedFire;
    return true; // Unarmed input ignored; no fire queue, ammo or projectile.
  }
  if (c.generation > p.life.generation || c.sequence > p.fireSequence + 512)
    return reject();
  if (c.generation < p.life.generation || !p.life.alive()) {
    ++combat_.stats().rejectedFire;
    return true;
  }
  if (c.sequence <= p.fireSequence)
    return true; // duplicate/reorder cannot refire
  if (c.tick < p.fireTick || p.fireInputs.size() >= 128)
    return reject();
  p.fireSequence = c.sequence;
  p.fireTick = c.tick;
  // Keep only the latest state for a single due tick, no allocations per round.
  p.fireInputs[std::max(tick_ + 1, c.tick)] = c;
  combat_.stats().peakFireQueue =
      std::max(combat_.stats().peakFireQueue, p.fireInputs.size());
  return true;
}
std::vector<Loadout> World::loadoutsNear(EntityId viewer) const {
  std::vector<std::pair<double, Loadout>> near;
  const auto self = players_.find(viewer);
  if (self == players_.end())
    return {};
  const auto eye = self->second.sim.state().pos_ned;
  for (const auto &[id, p] : players_) {
    if (id == viewer || !p.life.alive() || p.weapons.inventory.stations.empty())
      continue;
    const double distance = (p.sim.state().pos_ned - eye).norm2();
    if (distance <= 4000. * 4000.)
      near.push_back({distance,
                      {{id, p.life.generation}, mountedMask(p.weapons.inventory)}});
  }
  std::sort(near.begin(), near.end(),
            [](const auto &a, const auto &b) { return a.first < b.first; });
  std::vector<Loadout> result;
  for (std::size_t i = 0; i < near.size() && i < maxLoadouts; ++i)
    result.push_back(near[i].second);
  return result;
}
bool World::enqueueWeapon(EntityId id, const WeaponAction &action) {
  auto it = players_.find(id);
  auto reject = [&] {
    ++missiles_.stats().rejected;
    return false;
  };
  if (it == players_.end() || !action.sequence ||
      unsigned(action.kind) > unsigned(WeaponActionKind::Loadout) ||
      action.tick > tick_ + 120 ||
      (action.tick < tick_ && tick_ - action.tick > 120))
    return reject();
  auto &p = it->second;
  auto &w = p.weapons;
  if (action.generation > p.life.generation ||
      action.sequence > w.lastSequence + 512)
    return reject();
  if (action.generation < p.life.generation || !p.life.alive())
    return true;
  if (action.sequence <= w.lastSequence)
    return true;
  // Every aircraft has a seat to leave by; only armed ones have weapons.
  const auto &definition = aircraftDefinition(p.type);
  if (w.actions.size() >= 32 ||
      (!definition.gun && !definition.bomber && action.kind != WeaponActionKind::Eject))
    return reject();
  if (action.kind == WeaponActionKind::Launch && !definition.bomber &&
      action.station >= w.inventory.stations.size())
    return reject();
  w.lastSequence = action.sequence;
  // Reliable actions share a tick: retain order without overwriting a launch.
  Tick due = std::max(tick_ + 1, action.tick);
  if (!w.actions.empty())
    due = std::max(due, w.actions.rbegin()->first + 1);
  w.actions.emplace(due, action);
  return true;
}
void World::step() {
  auto start = std::chrono::steady_clock::now();
  ++tick_;
  if (teams_ && winner_ != Team::None && tick_ >= restart_)
    startRound();
  // What each side's sensors can be shown: everything in a free-for-all, and
  // in a team game only the other side.
  std::array<std::vector<weapons::SensorTarget>, 3> sensors;
  for (const auto &[id, p] : players_) {
    if (p.life.alive()) {
      const auto &s = p.sim.state();
      const weapons::SensorTarget target{{id, p.life.generation},
                                         s.pos_ned,
                                         s.vel_ned,
                                         s.att,
                                         p.type,
                                         heatPower(s),
                                         (s.afterburner[0] + s.afterburner[1]) * .5,
                                         true};
      for (unsigned side = 0; side < 3; ++side)
        if (side == 0 || side != unsigned(p.team))
          sensors[side].push_back(target);
    }
  }
  auto radarStart = std::chrono::steady_clock::now();
  for (auto &[id, p] : players_)
    if (p.life.alive() && aircraftDefinition(p.type).gun) {
      p.weapons.radar.update(p.sim.state(), sensors[unsigned(p.team)],
                             {id, p.life.generation},
                             double(tick_) * tickSeconds);
      if (p.weapons.radar.locked.id)
        p.weapons.radar.mode = weapons::RadarMode::Track;
    }
  missiles_.stats().radarUs = std::chrono::duration<double, std::micro>(
                                  std::chrono::steady_clock::now() - radarStart)
                                  .count();
  std::array<CombatTarget, maxPlayers> targets;
  std::size_t count = 0;
  for (auto &[id, p] : players_) {
    if (!p.life.alive()) {
      p.inputs.clear();
      p.fireInputs.clear();
      p.firing = false;
      p.acknowledged = std::max(p.acknowledged, p.highestSequence);
      combat_.removeOwner(id);
      p.weapons.actions.clear();
      p.weapons.radar.reset();
      p.weapons.seekerReady = false;
      p.weapons.acquisition = {};
      p.weapons.acquisitionTarget = {};
      p.weapons.lockProgress = 0;
      if (tick_ >= p.life.respawnTick) {
        ++p.life.generation;
        p.life.health = 100;
        p.life.ammo = aircraftDefinition(p.type).gun ? gunFor(p.type).ammo : 0;
        p.life.readyTick = tick_;
        p.life.respawnTick = 0;
        spawn(id, p);
        ++combat_.stats().respawns;
        CombatEvent e;
        e.kind = CombatKind::Respawn;
        e.tick = tick_;
        e.owner = e.target = id;
        e.generation = p.life.generation;
        e.health = 100;
        e.position = p.sim.state().pos_ned;
        combat_.emit(e);
      } else
        continue;
    }
    reload(p);
    const auto &sensorTargets = sensors[unsigned(p.team)];
    auto &w = p.weapons;
    w.seekerReady = false;
    w.envelope = {};
    const double now = double(tick_) * tickSeconds;
    if (w.inventory.selected == WeaponType::ActiveRadar ||
        w.inventory.nextStation() < 0) {
      w.acquisition = {};
      w.acquisitionTarget = {};
      w.lockProgress = 0;
    }
    if (w.inventory.nextStation() >= 0) {
      const auto &definition = weapons::missileDefinition(w.inventory.selected);
      const auto &own = p.sim.state();
      if (w.inventory.selected == WeaponType::ActiveRadar) {
        const auto *selected = w.radar.find(w.radar.selected);
        if (selected) {
          w.envelope = weapons::estimateEnvelope(
              definition, own, {selected->position, selected->velocity, true});
          w.seekerReady = w.radar.locked == selected->entity &&
                          now - selected->lastDetection <= .5;
          w.lockProgress = w.seekerReady ? 1 : 0;
        }
      } else {
        const auto station =
            w.inventory.stations[unsigned(w.inventory.nextStation())].position -
            loadedCg(p.sim.config(), own);
        auto seeker = weapons::launchState(definition, own, station, {});
        const auto nose = own.att.rotate({1, 0, 0});
        const auto sensorOf = [&](EntityRef ref) {
          return std::find_if(sensorTargets.begin(), sensorTargets.end(),
                              [&](const auto &t) { return t.entity == ref; });
        };
        auto sensor = sensorOf(w.acquisitionTarget);
        if (sensor == sensorTargets.end()) {
          // Uncaged search: take the detectable target closest to the nose.
          w.acquisition = {};
          w.acquisitionTarget = {};
          // A target just broken away from is retaken only when it is the
          // sole one in view.
          double best = -2;
          bool bestFresh = false;
          for (auto t = sensorTargets.begin(); t != sensorTargets.end(); ++t) {
            if (t->entity.id == id)
              continue;
            const bool fresh = !(t->entity == w.seekerRejected &&
                                 tick_ < w.seekerRejectedUntil);
            const double cosine =
                nose.dot((t->position - seeker.position).normalized());
            if ((fresh && !bestFresh) || (fresh == bestFresh && cosine > best)) {
              if (!weapons::seekerDetects(definition, seeker.position, nose,
                                          nose, *t))
                continue;
              best = cosine;
              bestFresh = fresh;
              sensor = t;
            }
          }
          if (sensor != sensorTargets.end()) {
            w.acquisitionTarget = sensor->entity;
            w.acquisition.boresight = nose;
          }
        }
        if (sensor != sensorTargets.end()) {
          seeker.seeker = w.acquisition;
          weapons::updateSeeker(definition, seeker, &*sensor, tickSeconds);
          w.acquisition = seeker.seeker;
          if (w.acquisition.phase == weapons::SeekerPhase::Lost) {
            w.acquisition = {};
            w.acquisitionTarget = {};
          } else {
            w.envelope = weapons::estimateEnvelope(
                definition, own, {sensor->position, sensor->velocity, true});
            w.lockProgress = clamp(
                w.acquisition.acquisition / definition.seeker.lockTime, 0, 1);
            w.seekerReady =
                w.acquisition.phase == weapons::SeekerPhase::Tracking &&
                w.lockProgress >= 1;
          }
        }
        if (!w.acquisitionTarget.id)
          w.lockProgress = 0;
      }
    }
    bool ejecting = false;
    while (!w.actions.empty() && w.actions.begin()->first <= tick_) {
      const auto action = w.actions.begin()->second;
      w.actions.erase(w.actions.begin());
      const bool heatSeeker = w.inventory.selected == WeaponType::Infrared;
      if (heatSeeker && w.acquisitionTarget.id &&
          (action.kind == WeaponActionKind::Unlock ||
           action.kind == WeaponActionKind::NextTarget ||
           action.kind == WeaponActionKind::PreviousTarget)) {
        // Break lock: the seeker returns to the nose and looks again.
        w.seekerRejected = w.acquisitionTarget;
        w.seekerRejectedUntil = tick_ + 180;
        w.acquisition = {};
        w.acquisitionTarget = {};
        w.lockProgress = 0;
        w.seekerReady = false;
      }
      switch (action.kind) {
      case WeaponActionKind::NextTarget:
      case WeaponActionKind::PreviousTarget: {
        // Stepping through targets with a lock held carries the lock along.
        const bool locked = w.radar.locked.id != 0;
        w.radar.cycle(action.kind == WeaponActionKind::NextTarget ? 1 : -1);
        if (locked && w.radar.locked != w.radar.selected) {
          w.radar.locked = {};
          w.radar.mode = weapons::RadarMode::Search;
          w.radar.toggleLock();
        }
        break;
      }
      case WeaponActionKind::Lock:
        // A target cycled to by hand is honoured; otherwise the radar takes
        // whatever the nose is pointed at.
        if (w.radar.locked.id || w.radar.find(w.radar.selected))
          w.radar.toggleLock();
        else
          w.radar.lockNearest(p.sim.state());
        break;
      case WeaponActionKind::Unlock:
        w.radar.selected = w.radar.locked = {};
        w.radar.mode = weapons::RadarMode::Search;
        break;
      case WeaponActionKind::SelectIR:
        w.inventory.selected = WeaponType::Infrared;
        break;
      case WeaponActionKind::SelectRadar:
        w.inventory.selected = WeaponType::ActiveRadar;
        break;
      case WeaponActionKind::Flare:
        releaseDecoy(id, weapons::DecoyType::Flare);
        break;
      case WeaponActionKind::Chaff:
        releaseDecoy(id, weapons::DecoyType::Chaff);
        break;
      case WeaponActionKind::Eject:
        ejecting = true;
        break;
      case WeaponActionKind::Loadout:
        if (const auto loads = weapons::bombLoadouts(p.type))
          p.loadout = std::uint8_t(action.station % loads);
        break;
      case WeaponActionKind::Launch: {
        if (weapons::isBomb(w.inventory.bombType)) {
          // A bomb is let go only with room to fall clear of the aircraft.
          const auto &own = p.sim.state();
          if (!w.inventory.bombs || tick_ < w.readyTick ||
              groundHeightNed(own.pos_ned.x, own.pos_ned.y) - own.pos_ned.z < 40) {
            ++missiles_.stats().rejected;
            break;
          }
          const auto &bomb = weapons::bombDefinition(w.inventory.bombType);
          if (missiles_.release(tick_, {id, p.life.generation}, own,
                                w.inventory.bay - loadedCg(p.sim.config(), own),
                                w.inventory.bombType, w.bombsReleased, p.team)) {
            ++w.bombsReleased;
            --w.inventory.bombs;
            w.readyTick = tick_ + Tick(std::ceil(bomb.releaseInterval / tickSeconds));
            auto state = own;
            w.inventory.applyPayload(p.sim.config(), state);
            p.sim.setState(state);
          }
          break;
        }
        if (action.station >= w.inventory.stations.size())
          break;
        auto &station = w.inventory.stations[action.station];
        weapons::Track target;
        bool tracked = false;
        if (w.inventory.selected == WeaponType::ActiveRadar) {
          if (const auto *track = w.radar.find(w.radar.selected)) {
            target = *track;
            tracked = true;
          }
        } else {
          const auto sensor = std::find_if(
              sensorTargets.begin(), sensorTargets.end(), [&](const auto &t) {
                return t.entity == w.acquisitionTarget;
              });
          if (sensor != sensorTargets.end()) {
            target = {sensor->entity, sensor->position, sensor->velocity,
                      now, 1, now};
            tracked = true;
          }
        }
        if (!tracked || !w.seekerReady ||
            station.mounted != w.inventory.selected ||
            w.envelope.targetRange <
                weapons::missileDefinition(w.inventory.selected).minimumRange ||
            tick_ < w.readyTick) {
          ++missiles_.stats().rejected;
          break;
        }
        const auto offset =
            station.position - loadedCg(p.sim.config(), p.sim.state());
        if (missiles_.launch(tick_, {id, p.life.generation}, p.sim.state(),
                             offset, station.mounted, target, p.team)) {
          w.inventory.consume(action.station, station.mounted);
          w.readyTick = tick_ + 60;
          auto state = p.sim.state();
          w.inventory.applyPayload(p.sim.config(), state);
          p.sim.setState(state);
        }
        break;
      }
      }
    }
    // A bot with both engines shot out has nothing left to fight with.
    if (p.bot && p.sim.state().engine_health[0] <= 0 &&
        p.sim.state().engine_health[1] <= 0)
      ejecting = true;
    if (ejecting) {
      // The seat goes and the aircraft is lost with it: a death for its
      // pilot, and a kill for whoever hit it last.
      const auto &abandoned = p.sim.state();
      CombatEvent event;
      event.kind = CombatKind::Ejected;
      event.tick = tick_;
      event.owner = event.target = id;
      event.generation = p.life.generation;
      event.position = abandoned.pos_ned;
      event.velocity = abandoned.vel_ned;
      combat_.emit(event);
      destroy(id, p, abandoned.pos_ned, abandoned.vel_ned);
      continue;
    }
    const auto previous = p.sim.state();
    while (!p.inputs.empty() && p.inputs.begin()->first <= tick_) {
      auto c = p.inputs.begin()->second;
      p.inputs.erase(p.inputs.begin());
      p.sim.setControls(c.controls);
      p.acknowledged = std::max(p.acknowledged, c.sequence);
      p.lastInput = tick_;
      ++stats_.applied;
    }
    if (tick_ - p.lastInput > 120) {
      auto c = p.sim.controls();
      c.elevator_stick = c.aileron_stick = c.rudder_pedal = c.steering = 0;
      p.sim.setControls(c);
    }
    while (!p.fireInputs.empty() && p.fireInputs.begin()->first <= tick_) {
      const auto c = p.fireInputs.begin()->second;
      p.fireInputs.erase(p.fireInputs.begin());
      p.firing = c.held;
      p.lastFireInput = tick_;
    }
    if (tick_ - p.lastFireInput > 120)
      p.firing = false;
    if (p.bot) controlBot(id, p);
    // Shot spawns at the start of this tick; target sweep uses the same
    // interval.
    if (p.firing && aircraftDefinition(p.type).gun) {
      auto gun=gunFor(p.type);gun.muzzle=gun.muzzle-loadedCg(p.sim.config(),previous);
      combat_.fire(tick_, id, previous, p.life, gun, p.team);
    }
    p.sim.step(tickSeconds);
    const auto& impact=p.sim.groundImpact();
    if(impact.damage>0) {
      p.life.health=std::max(0.,p.life.health-impact.damage*100.);
      if(aircraftCrashed(p.sim.state())) p.life.health=0;
      if(impact.damage>.02 || !p.life.alive()) {
        CombatEvent event;
        event.kind=CombatKind::Hit;event.tick=tick_;event.owner=event.target=id;
        event.generation=p.life.generation;event.health=p.life.health;
        event.position=impact.position;event.velocity=impact.velocity;
        combat_.emit(event);
        if(!p.life.alive()) destroy(id,p,impact.position,impact.velocity);
      }
    }
    // A wing that has been shot up snaps if it is then loaded hard. Sound
    // wings are never evaluated, so undamaged flight costs nothing here.
    if (p.life.alive() &&
        std::min(p.sim.state().surface_health[0],
                 p.sim.state().surface_health[1]) < weakenedWingHealth) {
      auto state = p.sim.state();
      if (applyOverstress(state, p.sim.normalLoad())) {
        loseStores(p, state);
        p.sim.setState(state);
        if (wingless(state))
          destroy(id, p, state.pos_ned, state.vel_ned);
      }
    }
    // Landing and standing still is a turn-round. The count runs whenever the
    // aircraft stands, so one that needs nothing is served the moment it does.
    // In a team game the stop has to be made at the side's own airfield.
    if (p.life.alive() && standingOnGround(p.sim.config(), p.sim.state()) &&
        (!teams_ || airfieldOwner(p.sim.state().pos_ned.x, p.sim.state().pos_ned.y) == p.team)) {
      if (++p.standing >= serviceTicks() && needsService(p)) {
        service(id, p);
        p.standing = 0;
      }
    } else
      p.standing = 0;
    targets[count] = {id, previous, p.sim.state(), &p.life, p.type};
    targets[count++].team = p.team;
  }
  defend(std::span(targets).first(count));
  for (auto &decoy : decoys_)
    weapons::advanceDecoy(decoy, weather_, tickSeconds);
  std::erase_if(decoys_, [](const auto &decoy) {
    return decoy.age >= weapons::decoyDefinition(decoy.type).lifetime ||
           decoy.position.z >= groundHeightNed(decoy.position.x, decoy.position.y);
  });
  combat_.step(tick_, std::span(targets).first(count));
  std::map<EntityId, AircraftWeapons *> controllers;
  for (auto &[id, p] : players_)
    if (p.life.alive())
      controllers[id] = &p.weapons;
  missiles_.step(tick_, std::span(targets).first(count), controllers, weather_,
                 combat_, decoys_);
  applyBlasts();
  for (std::size_t i = 0; i < count; ++i) {
    const auto &target = targets[i];
    if (!target.damaged)
      continue;
    auto &p = players_.at(target.id);
    // Combat changed nothing but the condition of the parts it struck.
    auto state = p.sim.state();
    for (unsigned e = 0; e < 2; ++e)
      state.engine_health[e] = target.current.engine_health[e];
    state.surface_health = target.current.surface_health;
    state.surface_drag = target.current.surface_drag;
    loseStores(p, state);
    p.sim.setState(state);
    if (target.attacker && target.attacker != target.id) {
      p.lastAttacker = target.attacker;
      p.lastAttacked = tick_;
    }
  }
  for (auto &[id, p] : players_)
    if (!p.life.alive()) {
      p.inputs.clear();
      p.fireInputs.clear();
      p.firing = false;
      p.acknowledged = std::max(p.acknowledged, p.highestSequence);
      combat_.removeOwner(id);
      p.weapons.actions.clear();
      p.weapons.radar.reset();
      p.weapons.seekerReady = false;
      p.weapons.acquisition = {};
      p.weapons.acquisitionTarget = {};
      p.weapons.lockProgress = 0;
    }
  if (teams_) {
    // Whatever was shot down this tick counts for the side that did it.
    for (auto &[id, p] : players_) {
      (void)id;
      if (p.life.kills > p.countedKills)
        score(p.team, int(p.life.kills - p.countedKills) * kKillPoints);
      p.countedKills = p.life.kills;
    }
  }
  stats_.lastTickUs = std::chrono::duration<double, std::micro>(
                          std::chrono::steady_clock::now() - start)
                          .count();
  stats_.maxTickUs = std::max(stats_.maxTickUs, stats_.lastTickUs);
}
Aircraft World::aircraft(EntityId id) const {
  const auto &p = players_.at(id);
  return {id, p.acknowledged, p.sim.state(), p.sim.controls(), p.life, p.type};
}
Message World::snapshot() const {
  Message m;
  m.type = Type::Snapshot;
  m.weather=weather_;
  m.tick = tick_;
  for (const auto &[id, p] : players_) {
    (void)p;
    m.aircrafts.push_back(aircraft(id));
  }
  return m;
}
} // namespace ofs::net
