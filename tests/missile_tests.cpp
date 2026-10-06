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
              motorThrust(d.motor, 0) == d.motor.boostThrust &&
              motorThrust(d.motor, 100) == 0,
          "motor stages");
    auto s = launchState(d, launchAircraft(), {}, {});
    const double initial = s.mass;
    double burnoutSpeed = 0;
    for (unsigned i = 0; i < 120 * 12; ++i) {
      advanceMissile(d, s, nullptr, nullptr, {}, tickSeconds);
      if (i + 1 == unsigned((d.motor.boostTime + d.motor.sustainTime) * 120))
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
  auto bytes = encodeWeapon(m);
  WeaponMessage decoded;
  check(bytes.size() == 592 && decodeWeapon(bytes, decoded),
        "maximum radar packet");
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
  std::printf("maxRadar=592 tracks=16 stations=8\n");
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
  check(decoded.missiles[0].target.id == 0, "target identity owner only");
  receiver.receive(decoded, 4);
  receiver.expire(245);
  check(receiver.missiles().empty(), "watchdog expiry");
}
} // namespace
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
    else if (suite == "protocol")
      protocol();
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
    else
      throw std::runtime_error("unknown suite");
    std::printf("m4.%s PASS\n", suite.c_str());
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "M4 FAIL: %s\n", e.what());
    return 1;
  }
}
