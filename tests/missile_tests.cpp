#include "ofs/net/client.hpp"
#include "ofs/net/server.hpp"
#include "ofs/weapons.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <random>
#include <stdexcept>
#include <thread>
using namespace ofs;
using namespace ofs::net;
using namespace ofs::weapons;
namespace {
void check(bool value, const char *reason) {
  if (!value)
    throw std::runtime_error(reason);
}
Player &fixture(World &w, EntityId id) {
  return const_cast<Player &>(w.players().at(id));
}
State launchAircraft(double altitude = 3000) {
  State s;
  s.pos_ned = {0, 0, -altitude};
  s.vel_ned = {250, 0, 0};
  return s;
}
SensorTarget targetAt(Vec3 position, Vec3 velocity = {}) {
  return {{2, 0}, position, velocity, {}, AircraftType::Typhoon, 1, 0, true};
}
void propulsion() {
  for (auto type : {WeaponType::Infrared, WeaponType::ActiveRadar}) {
    const auto &d = missileDefinition(type);
    check(motorThrust(d.motor, -1) == 0 &&
              motorThrust(d.motor, d.motor.ignitionDelay * .5) ==
                  (d.motor.ignitionDelay > 0 ? 0 : d.motor.boostThrust) &&
              motorThrust(d.motor, d.motor.ignitionDelay) ==
                  d.motor.boostThrust &&
              motorThrust(d.motor, 100) == 0,
          "motor stages");
    auto s = launchState(d, launchAircraft(), {}, {});
    const double initial = s.mass;
    double burnoutSpeed = 0;
    for (unsigned i = 0; i < 120 * 12; ++i) {
      advanceMissile(d, s, nullptr, nullptr, {}, tickSeconds);
      if (i + 1 == unsigned((d.motor.ignitionDelay + d.motor.boostTime +
                             d.motor.sustainTime) *
                            120))
        burnoutSpeed = s.velocity.norm();
    }
    check(s.propellant < 1e-5 &&
              std::abs(s.mass - (initial - d.motor.propellant)) < 1e-5 &&
              s.motor == MotorPhase::Burnout,
          "finite propellant mass burnout");
    check(s.velocity.x > 250 && std::isfinite(s.attitude.w) &&
              std::isfinite(s.position.norm2()),
          "rigid straight launch");
    const double speed = s.velocity.norm();
    for (unsigned i = 0; i < 120 * 5; ++i)
      advanceMissile(d, s, nullptr, nullptr, {}, tickSeconds);
    check(s.velocity.norm() < speed, "coast loses energy");
    std::printf("motor=%u burnoutMass=%.3f burnoutSpeed=%.2f coast12=%.2f "
                "coast17=%.2f distance=%.1f\n",
                unsigned(type), s.mass, burnoutSpeed, speed, s.velocity.norm(),
                s.distance);
  }
}
void aero() {
  const auto &d = missileDefinition(WeaponType::Infrared);
  check(dragCoefficient(d.aero, 1.05, 0) > dragCoefficient(d.aero, .4, 0),
        "transonic rise");
  check(dragCoefficient(d.aero, 2, 3) > dragCoefficient(d.aero, 2, 0),
        "induced drag");
  auto low = launchState(d, launchAircraft(1000), {}, {}),
       high = launchState(d, launchAircraft(12000), {}, {});
  for (unsigned i = 0; i < 600; ++i) {
    advanceMissile(d, low, nullptr, nullptr, {}, tickSeconds);
    advanceMissile(d, high, nullptr, nullptr, {}, tickSeconds);
  }
  check(high.velocity.norm() > low.velocity.norm() + 20, "density drag effect");
  std::printf("low1km_v5=%.2f high12km_v5=%.2f\n", low.velocity.norm(),
              high.velocity.norm());
}
void pn() {
  check(proportionalNavigation({}, {}, {200, 0, 0}, 3.5, 200).norm() == 0,
        "zero range PN");
  check(proportionalNavigation({1000, 0, 0}, {0, 0, 0}, {200, 0, 0}, 3.5, 200)
                .norm() == 0,
        "zero relative velocity PN");
  const auto command = proportionalNavigation({1000, 0, 0}, {-200, 100, 0},
                                              {200, 0, 0}, 3.5, 200);
  check(std::abs(command.y - 70) < 1e-9 && command.x == 0 && command.z == 0,
        "analytic crossing PN");
  check(proportionalNavigation({1, 0, 0}, {-1000, 10000, 0}, {200, 0, 0}, 3.5,
                               200)
                .norm() <= 200.00001,
        "excessive LOS saturation");
  check(
      proportionalNavigation({1000, 0, 0}, {100, 100, 0}, {200, 0, 0}, 3.5, 200)
              .norm() == 0,
      "receding PN no closure");
}
void guidance() {
  const auto &d = missileDefinition(WeaponType::Infrared);
  for (unsigned scenario = 0; scenario < 6; ++scenario) {
    auto own = launchAircraft(6000);
    auto target =
        targetAt({1800, scenario == 1 ? 100. : 0, -6000},
                 {scenario == 2 ? 1100. : 0, scenario == 1 ? 100. : 0, 0});
    if (scenario == 5)
      target.position.y = 8000;
    auto s = launchState(d, own, {}, {target.position, target.velocity, true});
    double miss = 1e9, maxG = 0, burnSpeed = 0, finalSpeed = 0;
    for (unsigned i = 0; i < 120 * 25; ++i) {
      if (scenario == 3)
        target.velocity.y = 130 * std::sin(i * tickSeconds * .7);
      if (scenario == 4 && i > 360)
        target.velocity.y = 200;
      target.position += target.velocity * tickSeconds;
      advanceMissile(d, s, &target, nullptr, {}, tickSeconds);
      const double distance = (target.position - s.position).norm();
      miss = std::min(miss, distance);
      maxG = std::max(maxG, s.telemetry.achievedG);
      if (i == 359)
        burnSpeed = s.velocity.norm();
      check(std::isfinite(s.position.norm2()) &&
                std::isfinite(s.omega.norm2()) &&
                std::abs(s.attitude.w * s.attitude.w +
                         s.attitude.x * s.attitude.x +
                         s.attitude.y * s.attitude.y +
                         s.attitude.z * s.attitude.z - 1) < 1e-10,
            "finite normalized missile trajectory");
      check(s.telemetry.commandedG <= d.maxG + 1e-8 &&
                s.telemetry.achievedG <= d.maxG + 1e-8,
            "physical G limits");
      if (distance < 10) {
        finalSpeed = s.velocity.norm();
        break;
      }
    }
    if (scenario == 0 || scenario == 1)
      check(miss < 15, "stationary/crossing intercept");
    if (scenario == 2 || scenario == 5)
      check(miss > 100, "impossible/receding miss");
    if (scenario == 4)
      check(s.velocity.norm() < burnSpeed, "burnout maneuver energy loss");
    std::printf("scenario=%u miss=%.3f maxG=%.3f burnout=%.2f terminal=%.2f "
                "final=%.2f\n",
                scenario, miss, maxG, burnSpeed, finalSpeed, s.velocity.norm());
  }
}
void seekers() {
  for (auto type : {WeaponType::Infrared, WeaponType::ActiveRadar}) {
    const auto &d = missileDefinition(type);
    auto target = targetAt({3000, 0, -3000});
    auto s = launchState(d, launchAircraft(), {}, {});
    for (unsigned i = 0; i < 30; ++i)
      updateSeeker(d, s, &target, tickSeconds);
    check(s.seeker.phase == SeekerPhase::Tracking, "inside FOV acquisition");
    target.position = {0, 4000, -3000};
    for (unsigned i = 0; i < 120; ++i)
      updateSeeker(d, s, &target, tickSeconds);
    check(s.seeker.phase == SeekerPhase::Lost && !s.seeker.measurement.valid,
          "gimbal/FOV loss");
    target.position = {3000, 2000, -3000};
    s = launchState(d, launchAircraft(), {}, {});
    updateSeeker(d, s, &target, 1);
    check(!s.seeker.measurement.valid,
          "outside acquisition FOV no omniscience");
    target.position = {3000, 0, -3000};
    s = launchState(d, launchAircraft(), {}, {});
    for (unsigned i = 0; i < 30; ++i)
      updateSeeker(d, s, &target, tickSeconds);
    target.alive = false;
    for (unsigned i = 0; i < 120; ++i)
      updateSeeker(d, s, &target, tickSeconds);
    check(!s.seeker.measurement.valid, "dead target loss");
    target.alive = true;
    for (unsigned i = 0; i < 30; ++i)
      updateSeeker(d, s, &target, tickSeconds);
    check(s.seeker.measurement.valid, "reacquisition");
  }
  auto t = targetAt({5000, 0, -3000});
  const auto rear = infraredSignal(t, {});
  t.attitude = quatFromEuler(0, 0, kPi);
  const auto front = infraredSignal(t, {});
  t.afterburner = 1;
  check(rear > front * 2 && infraredSignal(t, {}) > front * 3,
        "IR aspect/afterburner");
  const auto &d = missileDefinition(WeaponType::ActiveRadar);
  auto s = launchState(d, launchAircraft(), {}, {{30000, 0, -3000}, {}, true});
  auto distant = targetAt({1000, 9000, -3000});
  advanceMissile(d, s, &distant, nullptr, {}, tickSeconds);
  check(s.seeker.phase == SeekerPhase::Midcourse && !s.autonomous,
        "midcourse estimate independent of truth");
  auto support = Measurement{{5000, 0, -3000}, {}, true};
  auto close = targetAt(support.position);
  for (unsigned i = 0; i < 60; ++i)
    advanceMissile(d, s, &close, &support, {}, tickSeconds);
  check(s.autonomous && s.seeker.phase == SeekerPhase::Tracking,
        "active transition");
}
void radar() {
  State own = launchAircraft();
  Radar radar;
  auto target = targetAt({10000, 0, -3000});
  check(radar.signal(own, target) > 1, "scan SNR");
  auto side = target;
  side.position = {1000, 10000, -3000};
  check(radar.signal(own, side) == 0, "outside azimuth");
  side.position = {1000, 0, -15000};
  check(radar.signal(own, side) == 0, "outside elevation");
  side.position = {200000, 0, -3000};
  check(radar.signal(own, side) == 0, "outside range");
  side = target;
  side.type = AircraftType::Su57;
  check(radar.signal(own, side) < radar.signal(own, target), "RCS differences");
  side.attitude = quatFromEuler(0, 0, kPi / 2);
  check(targetRcs(side, own.pos_ned) >
            targetRcs(targetAt({10000, 0, -3000}), own.pos_ned) * .1,
        "aspect RCS");
  for (unsigned i = 0; i < 300; ++i)
    radar.update(own, std::span(&target, 1), {1, 0}, i * tickSeconds);
  check(radar.tracks().size() == 1, "track formation");
  radar.cycle(1);
  check(radar.selected == target.entity && radar.toggleLock(),
        "selection lock");
  target.position.y = 50000;
  radar.update(own, std::span(&target, 1), {1, 0}, 2.6);
  check(radar.tracks().size() == 1, "coasting track");
  radar.update(own, std::span(&target, 1), {1, 0}, 6);
  check(radar.tracks().empty() && !radar.locked.id && !radar.selected.id,
        "expiry lock reset");
  radar.reset();
  target.position.y = 0;
  for (unsigned i = 0; i < 300; ++i)
    radar.update(own, std::span(&target, 1), {1, 0}, i * tickSeconds);
  target.entity.generation = 1;
  radar.update(own, std::span(&target, 1), {1, 0}, 3);
  check(!radar.find({2, 0}), "generation retires stale track");
}
void inventory() {
  for (auto type : {AircraftType::A320, AircraftType::SR71,
                    AircraftType::Typhoon, AircraftType::Su57}) {
    Inventory inventory;
    inventory.reset(type);
    const bool armed =
        type == AircraftType::Typhoon || type == AircraftType::Su57;
    check(inventory.stations.size() == (armed ? 4 : 0), "appropriate stations");
    if (!armed)
      continue;
    Simulator sim(aircraftDefinition(type).flight);
    auto s = launchAircraft();
    inventory.applyPayload(sim.config(), s);
    sim.setState(s);
    const auto before = sim.massProperties();
    check(inventory.remaining(WeaponType::Infrared) == 2 &&
              inventory.remaining(WeaponType::ActiveRadar) == 2,
          "test loadout");
    check(!inventory.consume(9, WeaponType::Infrared) &&
              !inventory.consume(0, WeaponType::ActiveRadar),
          "station compatibility");
    check(inventory.consume(0, WeaponType::Infrared) &&
              !inventory.consume(0, WeaponType::Infrared),
          "ammo exactly once");
    inventory.applyPayload(sim.config(), s);
    sim.setState(s);
    const auto after = sim.massProperties();
    check(std::abs(before.mass - after.mass - 90) < 1e-8 &&
              after.cg.y > before.cg.y &&
              (before.inertia - after.inertia).norm() > 0 &&
              after.tensor().positiveDefinite(),
          "payload mass CG inertia asymmetry");
  }
}
void security() {
  World world;
  auto a = world.join(AircraftType::Typhoon),
       b = world.join(AircraftType::Su57);
  auto &p = fixture(world, a);
  check(!world.enqueueWeapon(999, {1, 0, 0, WeaponActionKind::Launch, 0}),
        "unknown owner");
  check(!world.enqueueWeapon(a, {1, 0, 99, WeaponActionKind::Launch, 0}),
        "future generation");
  check(!world.enqueueWeapon(a, {1, 0, 0, WeaponActionKind::Launch, 8}),
        "invalid hardpoint");
  check(!world.enqueueWeapon(a, {900, 0, 0, WeaponActionKind::Launch, 0}),
        "sequence jump");
  check(world.enqueueWeapon(a, {1, 0, 0, WeaponActionKind::Launch, 0}),
        "valid request admission");
  world.step();
  check(world.missiles().missiles().empty(), "launch no selected target");
  p.life.health = 0;
  check(
      world.enqueueWeapon(a, {2, world.tick(), 0, WeaponActionKind::Launch, 0}),
      "dead request retired");
  world.step();
  check(world.missiles().missiles().empty(), "dead cannot launch");
  p.life.health = 100;
  auto s = launchAircraft();
  p.sim.setState(s);
  s.pos_ned.x = 2000;
  fixture(world, b).sim.setState(s);
  for (unsigned i = 0; i < 300; ++i)
    world.step();
  check(!p.weapons.radar.tracks().empty(), "world radar formation");
  p.weapons.radar.cycle(1);
  p.weapons.radar.toggleLock();
  for (unsigned i = 0; i < 20; ++i)
    world.step();
  auto action = WeaponAction{3, world.tick(), p.life.generation,
                             WeaponActionKind::Launch, 0};
  check(world.enqueueWeapon(a, action), "launch authorized");
  check(world.enqueueWeapon(a, action), "duplicate harmless");
  world.step();
  std::printf("launches=%llu ready=%d envelope=%d range=%.1f max=%.1f "
              "selected=%llu tracks=%zu rejected=%llu\n",
              (unsigned long long)world.missiles().stats().launches,
              p.weapons.seekerReady, p.weapons.envelope.inside,
              p.weapons.envelope.targetRange, p.weapons.envelope.kinematicRange,
              (unsigned long long)p.weapons.radar.selected.id,
              p.weapons.radar.tracks().size(),
              (unsigned long long)world.missiles().stats().rejected);
  check(world.missiles().stats().launches == 1 &&
            p.weapons.inventory.remaining(WeaponType::Infrared) == 1,
        "unique authoritative launch");
  check(p.weapons.radar.mode == RadarMode::Track,
        "IR launch does not claim active-radar support");
  check(world.enqueueWeapon(a, {4, world.tick(), p.life.generation,
                                WeaponActionKind::Launch, 0}),
        "empty station request");
  world.step();
  check(world.missiles().stats().launches == 1, "empty/cooldown no launch");
  world.leave(a);
  check(world.missiles().missiles().empty(), "disconnect missile cleanup");
}
void acquisition() {
  // The mounted seeker finds, holds and releases its own target; the radar
  // locks whatever the nose points at. Neither needs a target cycled to.
  World world;
  const auto a = world.join(AircraftType::Typhoon),
             b = world.join(AircraftType::Su57),
             c = world.join(AircraftType::Typhoon);
  auto &p = fixture(world, a);
  const auto place = [&] {
    auto s = launchAircraft();
    p.sim.setState(s);
    s.pos_ned = {2500, 60, -3000};
    fixture(world, b).sim.setState(s);
    s.pos_ned = {2500, 350, -3000};
    fixture(world, c).sim.setState(s);
  };
  const auto &w = p.weapons;
  const auto &lock = missileDefinition(WeaponType::Infrared).seeker;
  place();
  world.step();
  check(w.acquisitionTarget.id == b && !w.seekerReady && w.lockProgress < 1 &&
            !w.radar.selected.id,
        "seeker takes the target nearest the nose without the radar");
  unsigned ticks = 1;
  while (!w.seekerReady && ticks < 600) {
    place();
    world.step();
    ++ticks;
  }
  check(ticks >= unsigned(lock.lockTime * 120) && ticks < 600 &&
            w.lockProgress == 1,
        "lock builds over the seeker lock time");
  std::uint64_t sequence = 0;
  const auto act = [&](WeaponActionKind kind, unsigned station = 0) {
    check(world.enqueueWeapon(a, {++sequence, world.tick(), p.life.generation,
                                  kind, std::uint8_t(station)}),
          "action admitted");
    place();
    world.step();
  };
  act(WeaponActionKind::Unlock);
  place();
  world.step();
  check(w.acquisitionTarget.id == c && !w.seekerReady,
        "breaking lock moves the seeker to the next target");
  while (!w.seekerReady && ticks < 1200) {
    place();
    world.step();
    ++ticks;
  }
  act(WeaponActionKind::Launch, 0);
  check(world.missiles().missiles().size() == 1 &&
            world.missiles().missiles()[0].target.id == c &&
            world.missiles().missiles()[0].type == WeaponType::Infrared,
        "heat seeker launches at its own target");
  for (unsigned i = 0; i < 300; ++i) {
    place();
    world.step();
  }
  check(w.radar.tracks().size() == 2, "radar formation");
  act(WeaponActionKind::SelectRadar);
  place();
  world.step();
  check(!w.acquisitionTarget.id && w.lockProgress == 0,
        "seeker caged with the radar missile selected");
  act(WeaponActionKind::Lock);
  for (unsigned i = 0; i < 15; ++i) { // one radar revisit of the new lock
    place();
    world.step();
  }
  check(w.radar.locked.id == b && w.radar.selected.id == b && w.seekerReady,
        "lock takes the track nearest the nose");
  const auto projected = radarProjection(w, p.life.generation);
  check(projected.seekerTarget.id == b && projected.lockProgress == 1,
        "projected lock target");
  act(WeaponActionKind::Unlock);
  check(!w.radar.locked.id && !w.radar.selected.id, "unlock clears selection");
  const auto loadouts = world.loadoutsNear(b);
  check(loadouts.size() == 2 && loadouts[0].entity.id == c &&
            loadouts[0].mounted == 0b1111 && loadouts[1].entity.id == a &&
            loadouts[1].mounted == 0b1110,
        "nearby stores projected nearest first");
  auto &motor = missileDefinition(WeaponType::ActiveRadar);
  auto s = launchState(motor, launchAircraft(), {}, {});
  advanceMissile(motor, s, nullptr, nullptr, {}, tickSeconds);
  check(s.motor == MotorPhase::Ignition && s.telemetry.thrust == 0 &&
            s.velocity.z > 5,
        "ejected store falls clear before ignition");
  for (unsigned i = 0; i < 60; ++i)
    advanceMissile(motor, s, nullptr, nullptr, {}, tickSeconds);
  check(s.motor == MotorPhase::Boost && s.position.z > -2999,
        "motor lights below the aircraft");
}
void fuse() {
  MissileCombat missiles;
  Combat combat;
  Life owner, life;
  State a = launchAircraft();
  Track track{{2, 0}, {1000, 0, -3000}, {}, 0, 1, 0};
  check(missiles.launch(0, {1, 0}, a, {}, WeaponType::Infrared, track),
        "fuse missile launch");
  auto &m = const_cast<Missile &>(missiles.missiles()[0]);
  m.state.age = 1;
  m.state.distance = 400;
  m.state.position = {-100, 0, -3000};
  m.state.velocity = {30000, 0, 0};
  m.state.propellant = 0;
  State target = a;
  target.pos_ned.x = 0;
  CombatTarget t{2, target, target, &life, AircraftType::Typhoon};
  missiles.step(120, std::span(&t, 1), {}, {}, combat);
  check(missiles.stats().detonations == 1 && missiles.missiles().empty() &&
            life.health < 100,
        "swept proximity prevents tunnelling");
  const double health = life.health;
  missiles.step(121, std::span(&t, 1), {}, {}, combat);
  check(life.health == health, "damage exactly once");
  check(missiles.takeEvents().size() == 1 && missiles.takeEvents().empty(),
        "detonation once");
  check(!std::isfinite(sweptSphere({-100, 10, 0}, {100, 10, 0}, {}, 9)),
        "fuse near miss");
}
void energy() {
  for (auto type : {WeaponType::Infrared, WeaponType::ActiveRadar}) {
    const auto &d = missileDefinition(type);
    double burnout[2]{};
    for (unsigned altitude = 0; altitude < 2; ++altitude) {
      auto state =
          launchState(d, launchAircraft(altitude ? 12000 : 1000), {}, {});
      const auto burnTicks =
          unsigned((d.motor.boostTime + d.motor.sustainTime) * 120);
      for (unsigned tick = 0; tick < burnTicks; ++tick)
        advanceMissile(d, state, nullptr, nullptr, {}, tickSeconds);
      burnout[altitude] = state.velocity.norm();
      check(state.telemetry.kineticEnergy > 0 &&
                std::abs(state.telemetry.kineticEnergy -
                         .5 * state.mass * state.telemetry.speed *
                             state.telemetry.speed) < 1e-5,
            "energy telemetry");
    }
    check(burnout[1] > burnout[0], "thin-air burnout advantage");
    std::printf("altitude motor=%u burnout1000m=%.2f burnout12000m=%.2f\n",
                unsigned(type), burnout[0], burnout[1]);
  }
  auto d = missileDefinition(WeaponType::Infrared);
  d.maxG = 5;
  auto own = launchAircraft(6000);
  auto state = launchState(d, own, {}, {});
  auto target = targetAt({2000, 100, -6000}, {0, 200, 0});
  double achieved = 0;
  for (unsigned i = 0; i < 600; ++i) {
    target.position += target.velocity * tickSeconds;
    advanceMissile(d, state, &target, nullptr, {}, tickSeconds);
    achieved = std::max(achieved, state.telemetry.achievedG);
    check(state.telemetry.achievedG <= 5 + 1e-8 &&
              state.telemetry.commandedG <= 5 + 1e-8,
          "structural demand/force bound");
  }
  check(achieved > 1, "bounded turn physically generated");
}
void radarProtocol() {
  WeaponMessage m;
  m.type = Type::RadarState;
  m.tick = 120;
  m.sequence = 1;
  m.radar.generation = 2;
  m.radar.mode = RadarMode::Track;
  m.radar.selected = m.radar.locked = {2, 3};
  m.radar.weapon = WeaponType::Infrared;
  m.radar.stations = std::vector<WeaponType>(8, WeaponType::Infrared);
  m.radar.seekerReady = true;
  m.radar.envelope = {350, 12000, 1234, 480, true};
  for (unsigned i = 0; i < 16; ++i)
    m.radar.tracks.push_back({{i + 2, 3},
                              {double(i) * 100 + .11, 200, -6000},
                              {240.11, 1.1, 0},
                              .8,
                              .75,
                              1});
  m.radar.seekerTarget = {5, 3};
  m.radar.lockProgress = .6;
  for (unsigned i = 0; i < maxLoadouts; ++i)
    m.radar.loadouts.push_back({{i + 2, 3}, std::uint8_t(i)});
  m.radar.flares = 13;
  m.radar.chaff = 255;
  auto bytes = encodeWeapon(m);
  WeaponMessage decoded;
  check(bytes.size() == 816 && decodeWeapon(bytes, decoded),
        "maximum radar packet");
  check(decoded.radar.flares == 13 && decoded.radar.chaff == 255,
        "countermeasure stock round trip");
  check(decoded.radar.seekerTarget == m.radar.seekerTarget &&
            std::abs(decoded.radar.lockProgress - .6) < .005 &&
            decoded.radar.loadouts.size() == maxLoadouts &&
            decoded.radar.loadouts[9].entity == EntityRef{11, 3} &&
            decoded.radar.loadouts[9].mounted == 9,
        "seeker and stores round trip");
  check(decoded.radar.locked == m.radar.locked &&
            decoded.radar.generation == 2 &&
            (decoded.radar.tracks[0].position - m.radar.tracks[0].position)
                    .norm() < .44,
        "radar round trip");
  for (unsigned i = 0; i < bytes.size(); ++i)
    check(!decodeWeapon(std::span(bytes).first(i), decoded),
          "radar truncation");
  WeaponReplicationReceiver receiver;
  receiver.receive(m, 120);
  auto stale = m;
  stale.tick = 119;
  stale.radar.tracks.clear();
  receiver.receive(stale, 120);
  check(receiver.radar.tracks.size() == 16, "radar reordered state");
  stale.tick = 121;
  stale.radar.generation = 1;
  receiver.receive(stale, 121);
  check(receiver.radar.generation == 2, "radar stale life");
  m.radar.tracks.push_back(m.radar.tracks[0]);
  bool bounded = false;
  try {
    encodeWeapon(m);
  } catch (const std::length_error &) {
    bounded = true;
  }
  check(bounded, "radar track cap");
  std::printf("maxRadar=816 tracks=16 stations=8 loadouts=16\n");
}
void presentation() {
  WeaponReplicationReceiver receiver;
  WeaponMessage m;
  m.type = Type::MissileState;
  m.tick = 100;
  m.sequence = 1;
  m.missiles.push_back({1,
                        {1, 0},
                        {2, 0},
                        WeaponType::Infrared,
                        {0, 0, -6000},
                        {120, 0, 0},
                        {},
                        MotorPhase::Boost,
                        SeekerPhase::Tracking,
                        1});
  receiver.receive(m, 100);
  m.tick = 106;
  m.sequence = 2;
  m.missiles[0].position.x = 6;
  m.missiles[0].attitude = {-1, 0, 0, 0};
  receiver.receive(m, 106);
  auto state = receiver.sample(103);
  check(state.size() == 1 && std::abs(state[0].position.x - 3) < 1e-8 &&
            std::abs(state[0].attitude.w - 1) < 1e-8,
        "position and hemisphere interpolation");
  state = receiver.sample(1000);
  check(std::abs(state[0].position.x - 12) < 1e-8,
        "extrapolation bounded to50ms");
  receiver.expire(347);
  check(receiver.missiles().empty() && receiver.sample(347).empty(),
        "presentation watchdog cleanup");
}
void robustness() {
  for (auto type : {WeaponType::Infrared, WeaponType::ActiveRadar})
    for (double altitude : {0., 20000., 90000.})
      for (double speed : {0., 10., 3000., 10000.}) {
        const auto &d = missileDefinition(type);
        auto own = launchAircraft(altitude);
        own.vel_ned = {speed, 0, 0};
        own.omega_body = {2, 1, -1};
        auto a = launchState(d, own, {}, {}), b = a;
        auto target = targetAt(a.position);
        for (unsigned i = 0; i < 120; ++i) {
          advanceMissile(d, a, &target, nullptr, {}, tickSeconds);
          advanceMissile(d, b, &target, nullptr, {}, tickSeconds);
        }
        check(std::isfinite(a.position.norm2()) &&
                  std::isfinite(a.velocity.norm2()) &&
                  std::isfinite(a.omega.norm2()),
              "extreme missile finite");
        check(a.position.x == b.position.x && a.position.y == b.position.y &&
                  a.attitude.w == b.attitude.w,
              "fixed-step deterministic repeat");
      }
}
void respawn() {
  World world;
  const auto a = world.join(AircraftType::Typhoon),
             b = world.join(AircraftType::Su57);
  auto &owner = fixture(world, a);
  auto &target = fixture(world, b);
  auto own = launchAircraft(6000);
  owner.sim.setState(own);
  target.sim.setState(own);
  Track track{
      {b, target.life.generation}, {30000, 0, -6000}, {250, 0, 0}, 0, 1, 0};
  check(world.missiles().launch(0, {a, owner.life.generation}, own, {},
                                WeaponType::ActiveRadar, track),
        "old-life missile");
  const auto missileId = world.missiles().missiles()[0].id;
  target.life.health = 0;
  target.life.respawnTick = world.tick() + 1;
  world.step();
  check(target.life.generation == 1 &&
            target.weapons.inventory.remaining(WeaponType::Infrared) == 2 &&
            target.weapons.radar.tracks().empty(),
        "respawn inventory/radar reset");
  check(world.missiles().missiles()[0].target.generation == 0 &&
            world.missiles().missiles()[0].id == missileId &&
            !world.missiles().missiles()[0].state.autonomous,
        "old target generation cannot reacquire new life");
  owner.life.health = 0;
  owner.life.respawnTick = world.tick() + 1;
  world.step();
  check(world.missiles().missiles().size() == 1 &&
            world.missiles().missiles()[0].owner.generation == 0,
        "in-flight missile survives death without attaching to respawn");
  world.leave(a);
  check(world.missiles().missiles().empty(), "disconnect retires missiles");
}
void protocol() {
  WeaponMessage m;
  m.tick = 10;
  m.sequence = 1;
  m.entity = 1;
  m.action = {1, 10, 0, WeaponActionKind::Launch, 0};
  WeaponMessage out;
  check(decodeWeapon(encodeWeapon(m), out) && out.action.kind == m.action.kind,
        "action round trip");
  m.type = Type::MissileState;
  for (unsigned i = 0; i < 16; ++i)
    m.missiles.push_back({i + 1,
                          {1, 2},
                          {2, 3},
                          WeaponType::Infrared,
                          {1000000.02, -40000.12, -1234.5},
                          {1234.02, -23.03, 0},
                          quatFromEuler(.4, .3, -.2),
                          MotorPhase::Boost,
                          SeekerPhase::Tracking,
                          1.23});
  auto bytes = encodeWeapon(m);
  check(bytes.size() == 1033 && decodeWeapon(bytes, out), "max packet budget");
  check((out.missiles[0].position - m.missiles[0].position).norm() <= .11 &&
            (out.missiles[0].velocity - m.missiles[0].velocity).norm() <= .22,
        "quantization precision");
  for (unsigned i = 0; i < bytes.size(); ++i)
    check(!decodeWeapon(std::span(bytes).first(i), out), "truncation rejected");
  bytes.push_back(0);
  check(!decodeWeapon(bytes, out), "trailing bytes");
  m.missiles.push_back(m.missiles[0]);
  bool threw = false;
  try {
    encodeWeapon(m);
  } catch (const std::length_error &) {
    threw = true;
  }
  check(threw, "record count bound");
  m.missiles.pop_back();
  m.missiles[0].position.x = std::numeric_limits<double>::quiet_NaN();
  threw = false;
  try {
    encodeWeapon(m);
  } catch (const std::invalid_argument &) {
    threw = true;
  }
  check(threw, "nonfinite cast rejection");
  std::mt19937 rng(4);
  m.missiles[0].position.x = 1000;
  bytes = encodeWeapon(m);
  for (unsigned i = 0; i < 10000; ++i) {
    auto fuzz = bytes;
    fuzz[rng() % fuzz.size()] = rng() % 256;
    (void)decodeWeapon(fuzz, out);
  }
  std::printf("missileRecord=63 maxState=1033 action=38 mutations=10000\n");
}
// A heat seeker fired from two and a half kilometres astern, and what the
// aircraft in front does about it. Returns the hit points it has left.
double chased(unsigned flares, bool reheat, bool breakAway,
              double *miss = nullptr, double flareRange = 1300) {
  World world;
  const auto a = world.join(AircraftType::Typhoon),
             b = world.join(AircraftType::Typhoon);
  auto &owner = fixture(world, a);
  auto &target = fixture(world, b);
  auto own = owner.sim.state();
  own.pos_ned = {0, 0, -3000};
  own.vel_ned = {250, 0, 0};
  owner.sim.setState(own);
  auto ahead = target.sim.state();
  ahead.pos_ned = {2500, 0, -3000};
  ahead.vel_ned = {250, 0, 0};
  target.sim.setState(ahead);
  auto controls = target.sim.controls();
  controls.throttle[0] = controls.throttle[1] = reheat ? 1 : .8;
  // Engines settle at the chosen power before the shot.
  for (unsigned i = 0; i < 360; ++i) {
    owner.lastInput = target.lastInput = world.tick() + 1;
    target.sim.setControls(controls);
    world.step();
  }
  const auto &flown = target.sim.state();
  check(world.missiles().launch(
            world.tick(), {a, owner.life.generation}, owner.sim.state(), {},
            WeaponType::Infrared,
            {{b, target.life.generation}, flown.pos_ned, flown.vel_ned, 0, 1, 0}),
        "chase launch");
  double nearest = 1e9;
  Tick flareTick = 0;
  for (unsigned i = 0; i < 120 * 14 && !world.missiles().missiles().empty() &&
                       target.life.alive();
       ++i) {
    const auto &missile = world.missiles().missiles()[0];
    const double range =
        (missile.state.position - target.sim.state().pos_ned).norm();
    nearest = std::min(nearest, range);
    if (range < flareRange && flares && world.tick() >= flareTick) {
      world.releaseDecoy(b, DecoyType::Flare);
      flareTick = world.tick() + 48;
      --flares;
    }
    if (range < 1300) {
      if (breakAway) {
        controls.throttle[0] = controls.throttle[1] = reheat ? 1 : .1;
        const double bank = target.sim.instruments().roll_deg;
        controls.aileron_stick = bank < 75 ? 1 : 0;
        controls.elevator_stick = bank > 55 ? 1 : 0;
      }
    }
    owner.lastInput = target.lastInput = world.tick() + 1;
    target.sim.setControls(controls);
    world.step();
  }
  if (miss)
    *miss = nearest;
  return target.life.health;
}
void countermeasures() {
  for (auto type : {DecoyType::Flare, DecoyType::Chaff}) {
    const auto &d = decoyDefinition(type);
    check(decoyStrength(type, 0) == 0 && decoyStrength(type, -1) == 0 &&
              decoyStrength(type, d.rise) == d.peak &&
              decoyStrength(type, d.hold) == d.peak &&
              decoyStrength(type, (d.hold + d.lifetime) / 2) < d.peak &&
              decoyStrength(type, (d.hold + d.lifetime) / 2) > 0 &&
              decoyStrength(type, d.lifetime) == 0 &&
              decoyStrength(type, std::numeric_limits<double>::quiet_NaN()) == 0,
          "a decoy comes up, holds and fades to nothing");
  }
  check(decoyCapacity(AircraftType::Typhoon) == 16 &&
            decoyCapacity(AircraftType::Su57) == 16 &&
            decoyCapacity(AircraftType::A320) == 0 &&
            decoyCapacity(AircraftType::SR71) == 0,
        "armed aircraft carry decoys");
  // Release and flight: thrown down and out to alternate sides from behind the
  // centre of gravity, left behind by the aircraft and sinking.
  const auto &cfg = aircraftDefinition(AircraftType::Typhoon).flight;
  const auto aircraft = launchAircraft();
  auto flare = releaseDecoy(DecoyType::Flare, {1, 0}, cfg, aircraft, 0);
  const auto other = releaseDecoy(DecoyType::Flare, {1, 0}, cfg, aircraft, 1);
  check(flare.velocity.y < 0 && other.velocity.y > 0 && flare.velocity.z > 15 &&
            flare.position.x < -2 && flare.position.z > aircraft.pos_ned.z &&
            flare.owner.id == 1 && flare.age == 0,
        "decoys leave from under the tail to alternate sides");
  auto chaff = releaseDecoy(DecoyType::Chaff, {1, 0}, cfg, aircraft, 0);
  Weather wind;
  wind.wind_ned = {0, 12, 0};
  for (unsigned i = 0; i < 120; ++i) {
    advanceDecoy(flare, {}, tickSeconds);
    advanceDecoy(chaff, wind, tickSeconds);
  }
  check(std::abs(flare.age - 1) < 1e-9 && flare.position.x < 250 - 60 &&
            flare.position.x > 60 && flare.position.z > aircraft.pos_ned.z + 12 &&
            flare.velocity.norm() < 140,
        "a flare is left behind and falls");
  check((chaff.velocity - wind.wind_ned).norm() < 12 && chaff.velocity.z < 1.5,
        "chaff stops in the air and drifts with it");
  auto untouched = flare;
  advanceDecoy(untouched, {}, 0);
  advanceDecoy(untouched, {}, std::numeric_limits<double>::infinity());
  check(untouched.age == flare.age && untouched.position.x == flare.position.x,
        "a bad time step moves nothing");

  // ---- Heat seeker: the brightest source in view wins ----
  const auto &ir = missileDefinition(WeaponType::Infrared);
  auto s = launchState(ir, launchAircraft(), {}, {});
  auto target = targetAt({2000, 0, -3000}, {250, 0, 0}); // seen from astern
  for (unsigned i = 0; i < 30; ++i)
    updateSeeker(ir, s, &target, tickSeconds);
  check(s.seeker.phase == SeekerPhase::Tracking, "seeker on the target");
  const auto dropped = [&](DecoyType type, Vec3 position, double age) {
    Decoy decoy;
    decoy.id = 7;
    decoy.type = type;
    decoy.position = position;
    decoy.velocity = {120, 0, 10};
    decoy.age = age;
    return std::vector<Decoy>{decoy};
  };
  auto decoys = dropped(DecoyType::Flare, {1990, 0, -2998}, .3);
  check(seekerDecoy(ir, s, &target, decoys, 0) == &decoys[0],
        "a flare outshines an engine at military power");
  target.afterburner = 1;
  check(!seekerDecoy(ir, s, &target, decoys, 0),
        "a flare does not outshine reheat");
  check(!seekerDecoy(ir, s, &target, decoys, 7),
        "and lighting reheat beside a flare takes the seeker back");
  target.afterburner = 0;
  check(!seekerDecoy(ir, s, &target, dropped(DecoyType::Chaff, {1990, 0, -2998}, .5), 0),
        "chaff means nothing to a heat seeker");
  check(!seekerDecoy(ir, s, &target, dropped(DecoyType::Flare, {2000, 800, -3000}, .3), 0),
        "a flare outside the seeker's view is not seen");
  check(!seekerDecoy(ir, s, &target, dropped(DecoyType::Flare, {1990, 0, -2998}, 3.5), 0),
        "a spent flare fools nothing");
  check(!seekerDecoy(ir, s, nullptr, decoys, 0),
        "a seeker with nothing to follow does not go looking for flares");
  // Following a flare: it is kept while it burns, then given up for the
  // aircraft if that is still beside it.
  decoys = dropped(DecoyType::Flare, {1990, 0, -2998}, 2.0);
  check(seekerDecoy(ir, s, &target, decoys, 7) == &decoys[0],
        "a held flare is kept while it is nearly as bright");
  decoys = dropped(DecoyType::Flare, {1990, 0, -2998}, 3.0);
  check(!seekerDecoy(ir, s, &target, decoys, 7),
        "the aircraft is retaken as the flare fades");
  check(!seekerDecoy(ir, s, &target, {}, 7), "a flare that is gone is let go");
  auto second = dropped(DecoyType::Flare, {1990, 0, -2998}, 2.4);
  second.push_back(second[0]);
  second[1].id = 8;
  second[1].age = .4;
  check(seekerDecoy(ir, s, &target, second, 7) == &second[1],
        "a fresh flare takes over from a fading one");

  // ---- Radar seeker: chaff only hides an aircraft crossing the beam ----
  const auto &ar = missileDefinition(WeaponType::ActiveRadar);
  auto r = launchState(ar, launchAircraft(), {}, {{5000, 0, -3000}, {}, true});
  auto fleeing = targetAt({5000, 0, -3000}, {250, 0, 0});
  auto cloud = dropped(DecoyType::Chaff, {4990, 0, -2998}, 1.);
  check(!seekerDecoy(ar, r, &fleeing, cloud, 0),
        "chaff does not hide an aircraft flying away");
  auto crossing = targetAt({5000, 0, -3000}, {0, 250, 0});
  crossing.attitude = quatFromEuler(0, 0, kPi / 2);
  check(seekerDecoy(ar, r, &crossing, cloud, 0) == &cloud[0],
        "chaff hides an aircraft crossing the line of sight");
  auto slanting = targetAt({5000, 0, -3000}, {177, 177, 0});
  slanting.attitude = quatFromEuler(0, 0, kPi / 4);
  check(!seekerDecoy(ar, r, &slanting, cloud, 0),
        "half a turn is not enough");
  check(!seekerDecoy(ar, r, &crossing, dropped(DecoyType::Flare, {4990, 0, -2998}, .3), 0),
        "flares mean nothing to a radar seeker");
  check(seekerDecoy(ar, r, &fleeing, cloud, 7) == &cloud[0],
        "a seeker on the cloud stays there when the aircraft runs");
  auto guided = launchState(ar, launchAircraft(), {}, {{30000, 0, -3000}, {}, true});
  auto far = targetAt({30000, 0, -3000}, {0, 250, 0});
  far.attitude = crossing.attitude;
  check(!seekerDecoy(ar, guided, &far, dropped(DecoyType::Chaff, {29990, 0, -2998}, 1.), 0),
        "a missile still on guidance from its launch aircraft is not listening");

  // ---- The whole thing: a shot from astern ----
  // ---- In a game: released by request, counted, paced and announced ----
  {
    World world;
    const auto a = world.join(AircraftType::Typhoon),
               liner = world.join(AircraftType::A320);
    auto &p = fixture(world, a);
    std::uint64_t sequence = 0;
    const auto ask = [&](EntityId id, WeaponActionKind kind) {
      return world.enqueueWeapon(id, {++sequence, world.tick(),
                                      fixture(world, id).life.generation, kind, 0});
    };
    const auto released = [&](unsigned ticks) {
      std::vector<CombatEvent> events;
      for (unsigned i = 0; i < ticks; ++i) {
        world.step();
        for (const auto &event : world.combat().takeEvents())
          if (event.kind == CombatKind::Flare || event.kind == CombatKind::Chaff)
            events.push_back(event);
      }
      return events;
    };
    check(p.weapons.flares == 16 && p.weapons.chaff == 16 &&
              fixture(world, liner).weapons.flares == 0,
          "dispensers are full at spawn");
    check(ask(a, WeaponActionKind::Flare), "flare request admitted");
    auto events = released(2);
    check(events.size() == 1 && events[0].kind == CombatKind::Flare &&
              events[0].owner == a && events[0].target == a &&
              events[0].projectile == world.decoys()[0].id &&
              world.decoys().size() == 1 && p.weapons.flares == 15 &&
              p.weapons.chaff == 16,
          "a flare is released, counted and announced");
    check((events[0].position - p.sim.state().pos_ned).norm() < 30 &&
              (events[0].velocity - p.sim.state().vel_ned).norm() > 15,
          "it leaves the aircraft that asked");
    // The dispenser cycles: a request made before it is ready is lost,
    // whichever kind of decoy it asks for.
    check(ask(a, WeaponActionKind::Chaff), "chaff request too soon");
    check(released(4).empty() && p.weapons.chaff == 16,
          "the dispenser is still cycling");
    released(unsigned(decoyInterval * 120));
    check(ask(a, WeaponActionKind::Chaff) && ask(a, WeaponActionKind::Chaff),
          "two chaff requests");
    events = released(4);
    check(events.size() == 1 && events[0].kind == CombatKind::Chaff &&
              p.weapons.chaff == 15,
          "the dispenser releases one at a time");
    released(unsigned(decoyInterval * 120) + 2);
    // An empty dispenser releases nothing; an unarmed aircraft has none.
    p.weapons.flares = 0;
    check(ask(a, WeaponActionKind::Flare), "request with nothing left");
    check(released(4).empty() && p.weapons.flares == 0, "nothing left to release");
    check(!ask(liner, WeaponActionKind::Flare),
          "an unarmed aircraft cannot ask for countermeasures");
    check(!world.releaseDecoy(liner, DecoyType::Flare) &&
              !world.releaseDecoy(99, DecoyType::Flare),
          "nor release them");
    // Decoys burn out and are forgotten; a new life starts with full dispensers.
    released(unsigned(decoyDefinition(DecoyType::Chaff).lifetime * 120) + 4);
    check(world.decoys().empty(), "spent decoys are removed");
    p.life.health = 0;
    p.life.respawnTick = world.tick() + 1;
    world.step();
    check(p.weapons.flares == 16 && p.weapons.chaff == 16,
          "a new life has full dispensers");
    // The sky never holds more than its share.
    for (unsigned i = 0; i < maxDecoys + 40; ++i) {
      p.weapons.chaff = 16;
      p.weapons.decoyReady = 0;
      check(world.releaseDecoy(a, DecoyType::Chaff), "release for the bound");
    }
    check(world.decoys().size() == maxDecoys, "decoys in the air are bounded");
    // The new requests and announcements cross the wire.
    WeaponMessage m, out;
    m.tick = 10;
    m.sequence = 1;
    m.entity = 1;
    for (const auto kind : {WeaponActionKind::Flare, WeaponActionKind::Chaff}) {
      m.action = {1, 10, 0, kind, 0};
      check(decodeWeapon(encodeWeapon(m), out) && out.action.kind == kind,
            "countermeasure request round trip");
    }
    auto bytes = encodeWeapon(m);
    bytes[36] = std::uint8_t(unsigned(WeaponActionKind::Eject) + 1);
    check(!decodeWeapon(bytes, out), "unknown request rejected");
    Message combat;
    combat.type = Type::Combat;
    combat.tick = 50;
    combat.sequence = 3;
    std::uint64_t id = 0;
    for (const auto kind :
         {CombatKind::Flare, CombatKind::Chaff, CombatKind::Serviced}) {
      CombatEvent event;
      event.id = ++id;
      event.tick = 40;
      event.kind = kind;
      event.projectile = 77;
      event.owner = event.target = 5;
      event.health = kind == CombatKind::Serviced ? 100 : 0;
      event.position = {1000, -200, -3000};
      event.velocity = {240, 3, 20};
      combat.events.push_back(event);
    }
    Message received;
    std::string reason;
    check(decode(encode(combat), received, reason) &&
              received.events.size() == 3 &&
              received.events[0].kind == CombatKind::Flare &&
              received.events[1].kind == CombatKind::Chaff &&
              received.events[2].kind == CombatKind::Serviced &&
              received.events[0].projectile == 77 &&
              (received.events[1].velocity - Vec3{240, 3, 20}).norm() < .1,
          "countermeasure and turn-round events round trip");
    auto packet = encode(combat);
    combat.events[0].kind = CombatKind(unsigned(CombatKind::Ejected) + 1);
    check(!decode(encode(combat), received, reason), "unknown event rejected");
  }
  // Ejecting: the pilot leaves, the aircraft is lost with them, whoever hit it
  // last has the kill, and a new life follows as after any other loss.
  {
    World world;
    const auto liner = world.join(AircraftType::A320),
               fighter = world.join(AircraftType::Typhoon);
    std::uint64_t sequence = 0;
    const auto ask = [&](EntityId id) {
      return world.enqueueWeapon(id, {++sequence, world.tick(),
                                      fixture(world, id).life.generation,
                                      WeaponActionKind::Eject, 0});
    };
    auto &left = fixture(world, liner);
    left.lastAttacker = fighter;
    left.lastAttacked = world.tick();
    const auto generation = left.life.generation;
    const Vec3 where = left.sim.state().pos_ned;
    check(ask(liner), "an unarmed aircraft can still be left");
    bool ejected = false, destroyed = false, ordered = false, credited = false;
    for (unsigned i = 0; i < 3; ++i) {
      world.step();
      for (const auto &event : world.combat().takeEvents()) {
        if (event.target != liner)
          continue;
        if (event.kind == CombatKind::Ejected) {
          ejected = true;
          ordered = !destroyed && (event.position - where).norm() < 20;
        }
        if (event.kind == CombatKind::Destroyed) {
          destroyed = true;
          credited = event.owner == fighter;
        }
      }
    }
    check(ejected && destroyed && ordered,
          "the seat is announced where the aircraft was, then its loss");
    check(!left.life.alive() && left.life.deaths == 1 && credited &&
              fixture(world, fighter).life.kills == 1,
          "ejecting is a death, and a kill for whoever hit the aircraft last");
    check(ask(liner), "a request from a pilot who has already gone");
    unsigned again = 0;
    for (unsigned i = 0; i < 2000 && !left.life.alive(); ++i) {
      world.step();
      for (const auto &event : world.combat().takeEvents())
        again += event.kind == CombatKind::Ejected;
    }
    check(left.life.alive() && left.life.generation == generation + 1 && !again,
          "a new life follows, and nobody ejects twice from one aircraft");
    // Nobody hit the fighter: leaving it is nobody's kill.
    check(ask(fighter), "an armed aircraft ejects too");
    bool own = false;
    for (unsigned i = 0; i < 3; ++i) {
      world.step();
      for (const auto &event : world.combat().takeEvents())
        own = own || (event.kind == CombatKind::Destroyed &&
                      event.target == fighter && event.owner == fighter);
    }
    check(own && fixture(world, liner).life.kills == 0,
          "an undamaged aircraft left by its pilot is nobody's kill");
    // The request and the announcement cross the wire.
    WeaponMessage m, out;
    m.tick = 10;
    m.sequence = 1;
    m.entity = 1;
    m.action = {1, 10, 0, WeaponActionKind::Eject, 0};
    check(decodeWeapon(encodeWeapon(m), out) &&
              out.action.kind == WeaponActionKind::Eject,
          "eject request round trip");
    Message combat, received;
    combat.type = Type::Combat;
    combat.tick = 50;
    combat.sequence = 3;
    CombatEvent event;
    event.id = 1;
    event.tick = 40;
    event.kind = CombatKind::Ejected;
    event.owner = event.target = 5;
    event.position = {1000, -200, -3000};
    event.velocity = {240, 3, 20};
    combat.events.push_back(event);
    std::string reason;
    check(decode(encode(combat), received, reason) &&
              received.events.size() == 1 &&
              received.events[0].kind == CombatKind::Ejected &&
              received.events[0].target == 5 &&
              (received.events[0].position - event.position).norm() < 1,
          "ejection event round trip");
  }
  // A bot answers a missile that is nearly on it with the matching decoy,
  // and is inattentive to every other missile.
  {
    World world;
    const auto human = world.join(AircraftType::Typhoon);
    const auto bot = world.joinBot(AircraftType::Typhoon);
    check(human && bot, "pilot and bot");
    auto &b = fixture(world, bot);
    unsigned flares = 0, chaff = 0;
    for (unsigned shot = 0; shot < 4; ++shot) {
      const auto own = fixture(world, human).sim.state();
      auto near = b.sim.state();
      near.pos_ned = own.pos_ned + own.att.rotate({900, 0, 0});
      b.sim.setState(near);
      b.botDecoyReady = 0;
      const auto type =
          shot < 2 ? WeaponType::Infrared : WeaponType::ActiveRadar;
      check(world.missiles().launch(
                world.tick(), {human, fixture(world, human).life.generation},
                own, {}, type,
                {{bot, b.life.generation}, near.pos_ned, near.vel_ned, 0, 1, 0}),
            "shot at the bot");
      const auto before = std::pair{b.weapons.flares, b.weapons.chaff};
      for (unsigned i = 0; i < 12; ++i)
        world.step();
      flares += before.first - b.weapons.flares;
      chaff += before.second - b.weapons.chaff;
      world.missiles().removeOwner(human, world.tick());
      check(world.missiles().missiles().empty(), "shot withdrawn");
    }
    check(flares == 1 && chaff == 1,
          "a bot answers every other missile, each with the right decoy");
  }
  // Flares are an answer only to a pilot who uses them well: out of reheat,
  // with the missile close, and breaking away while it looks at the flare.
  double miss = 0;
  const double unanswered = chased(0, false, false);
  const double lit = chased(16, true, true);
  const double early = chased(2, false, false, nullptr, 2600);
  const double timed = chased(2, false, true, &miss);
  std::printf("countermeasures: unanswered=%.0f in reheat=%.0f too early=%.0f "
              "timed=%.0f miss=%.0f m\n",
              unanswered, lit, early, timed, miss);
  check(unanswered < 100, "an unanswered shot from astern hits");
  check(lit < 100, "no number of flares saves an aircraft that stays in reheat");
  check(early < 100, "flares dropped too early have burnt out when it arrives");
  check(timed == 100 && miss > 30,
        "two flares, cold engines and a break turn defeat the shot");
}
void lifecycle() {
  WeaponReplicationSender sender;
  WeaponReplicationReceiver receiver;
  RadarNetState radar;
  std::vector<Missile> missiles;
  Missile m;
  m.id = 1;
  m.owner = {1, 0};
  m.target = {2, 0};
  m.state.position = {1000, 0, -3000};
  m.state.attitude = {};
  missiles.push_back(m);
  auto packets = sender.build(1, 1, {}, radar, missiles, {});
  check(packets.size() == 1 && packets[0].reliable, "reliable spawn");
  WeaponMessage decoded;
  check(decodeWeapon(packets[0].bytes, decoded) && receiver.receive(decoded, 1),
        "spawn delivery");
  const auto spawn = decoded;
  check(receiver.receive(decoded, 1) && receiver.missiles().size() == 1,
        "duplicate spawn");
  MissileEvent event{projectMissile(m, 1), 2, true};
  missiles.clear();
  packets = sender.build(2, 1, {}, radar, missiles, std::span(&event, 1));
  check(packets.size() == 1 && packets[0].reliable &&
            decodeWeapon(packets[0].bytes, decoded) &&
            receiver.receive(decoded, 2),
        "reliable retirement");
  receiver.receive(decoded, 2);
  receiver.receive(spawn, 2);
  check(receiver.missiles().empty() && receiver.takeDetonations().size() == 1,
        "duplicate detonation stale spawn");
  m.id = 2;
  missiles.push_back(m);
  packets = sender.build(3, 3, {100000, 0, 0}, radar, missiles, {});
  check(packets.empty(), "distant unrelated missile hidden");
  packets = sender.build(4, 2, {100000, 0, 0}, radar, missiles, {});
  check(!packets.empty(), "target priority outside AOI");
  decodeWeapon(packets[0].bytes, decoded);
  check(decoded.missiles[0].target.id == 2, "the target is warned of its missile");
  WeaponReplicationSender bystander;
  packets = bystander.build(4, 3, {1000, 0, -3000}, radar, missiles, {});
  check(!packets.empty() && decodeWeapon(packets[0].bytes, decoded) &&
            decoded.missiles[0].target.id == 0,
        "nobody else learns who a missile is meant for");
  missiles[0].decoy = 9;
  check(projectMissile(missiles[0], 2).seeker == SeekerPhase::Decoyed &&
            missiles[0].state.seeker.phase != SeekerPhase::Decoyed,
        "a decoyed missile is reported as such");
  missiles[0].decoy = 0;
  receiver.receive(decoded, 4);
  receiver.expire(245);
  check(receiver.missiles().empty(), "watchdog expiry");
}
} // namespace
// A game set to hand missiles out gives one back per interval, to an empty
// pylon, until the aircraft is full; a game that is not leaves them empty.
void reload() {
  for (const double seconds : {0.0, 2.0}) {
    World world;
    world.setMissileReload(seconds);
    const auto a = world.join(AircraftType::Typhoon);
    auto &p = fixture(world, a);
    auto &stations = p.weapons.inventory.stations;
    const auto mounted = [&] {
      return p.weapons.inventory.remaining(WeaponType::Infrared) +
             p.weapons.inventory.remaining(WeaponType::ActiveRadar);
    };
    check(mounted() == 4, "pylons are full at spawn");
    const auto first = stations[0].mounted;
    stations[0].mounted = stations[3].mounted = WeaponType::None;
    const auto rounds = p.life.ammo;
    check(rounds >= 4 && p.weapons.flares == 16, "gun and dispensers are full at spawn");
    p.weapons.flares = 2;
    p.weapons.chaff = 14;
    p.life.ammo = 0;
    const auto run = [&](double time) {
      for (int i = 0; i < int(std::lround(time / tickSeconds)); ++i)
        world.step();
    };
    run(1.9);
    check(mounted() == 2, "nothing comes back before the reload time");
    run(.2);
    if (seconds == 0) {
      run(30);
      check(mounted() == 2 && p.weapons.flares == 2 && p.life.ammo == 0,
            "without a reload time nothing comes back in flight");
      continue;
    }
    check(mounted() == 3 && stations[0].mounted == first,
          "one missile comes back, of the kind its pylon carries");
    check(p.weapons.flares == 6 && p.weapons.chaff == 16 &&
              p.life.ammo == (rounds + 3) / 4,
          "a quarter of the flares, chaff and rounds come back with it");
    run(2);
    check(mounted() == 4, "the next follows a reload time later");
    run(5);
    check(mounted() == 4 && p.reloading == 0, "a full aircraft is left alone");
    run(4);
    check(p.weapons.flares == 16 && p.weapons.chaff == 16 &&
              p.life.ammo == rounds && p.resupplying == 0,
          "gun and dispensers fill and stop there");
  }
}
int main(int argc, char **argv) {
  try {
    const std::string suite = argc > 1 ? argv[1] : "guidance";
    if (suite == "propulsion")
      propulsion();
    else if (suite == "aero")
      aero();
    else if (suite == "pn")
      pn();
    else if (suite == "guidance")
      guidance();
    else if (suite == "seekers")
      seekers();
    else if (suite == "radar")
      radar();
    else if (suite == "inventory")
      inventory();
    else if (suite == "security")
      security();
    else if (suite == "fuse")
      fuse();
    else if (suite == "acquisition")
      acquisition();
    else if (suite == "protocol")
      protocol();
    else if (suite == "countermeasures")
      countermeasures();
    else if (suite == "lifecycle")
      lifecycle();
    else if (suite == "energy")
      energy();
    else if (suite == "radar_protocol")
      radarProtocol();
    else if (suite == "presentation")
      presentation();
    else if (suite == "robustness")
      robustness();
    else if (suite == "respawn")
      respawn();
    else if (suite == "reload")
      reload();
    else
      throw std::runtime_error("unknown suite");
    std::printf("m4.%s PASS\n", suite.c_str());
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "M4 FAIL: %s\n", e.what());
    return 1;
  }
}
