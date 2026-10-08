// Regional battle damage: the rules, what they do to flight, and how the
// server applies, credits and replicates them.
#include "ofs/damage.hpp"
#include "ofs/net/replication.hpp"
#include "ofs/net/world.hpp"
#include "ofs/trim.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
using namespace ofs;
using namespace ofs::net;
namespace {
void check(bool value, const char *why) {
  if (!value)
    throw std::runtime_error(why);
}
// Authoritative scenario fixture only, as in the combat tests.
Player &fixture(World &world, EntityId id) {
  return const_cast<Player &>(world.players().at(id));
}
Simulator trimmed(AircraftType type) {
  const auto &config = aircraftDefinition(type).flight;
  const auto trim = solveTrim(config);
  check(trim.converged, "trim");
  Simulator sim(config);
  sim.setState(trim.state);
  sim.setControls(trim.controls);
  return sim;
}
void parts() {
  const auto &typhoon = aircraftDefinition(AircraftType::Typhoon);
  const auto &airliner = aircraftDefinition(AircraftType::A320);
  State state;
  // A wing soaks up half of each hit and is gone after its own strength.
  check(applyPartDamage(typhoon.flight, state, DamagePart::LeftWing, 34) == 17 &&
            std::abs(partHealth(state, DamagePart::LeftWing) - .66) < 1e-12 &&
            partHealth(state, DamagePart::RightWing) == 1 &&
            state.surface_drag[0] > 1 && state.surface_drag[1] == 1,
        "a wing hit damages that wing alone");
  applyPartDamage(typhoon.flight, state, DamagePart::LeftWing, 34);
  applyPartDamage(typhoon.flight, state, DamagePart::LeftWing, 34);
  check(partDestroyed(state, DamagePart::LeftWing) && !wingless(state),
        "three cannon hits take a wing off");
  check(applyPartDamage(typhoon.flight, state, DamagePart::LeftWing, 34) == 34,
        "a destroyed part protects nothing");
  applyPartDamage(typhoon.flight, state, DamagePart::RightWing, 100);
  check(wingless(state), "both wings gone");
  // Engines keep running hurt, then flame out for good.
  state = {};
  check(applyPartDamage(typhoon.flight, state, DamagePart::RightEngine, 34) ==
                34 * .6 &&
            state.engine_health[1] > engineFailureHealth &&
            state.engine_health[1] < 1 && state.engine_health[0] == 1,
        "a damaged engine still runs");
  applyPartDamage(typhoon.flight, state, DamagePart::RightEngine, 34);
  check(state.engine_health[1] == 0 &&
            partDestroyed(state, DamagePart::RightEngine),
        "a second hit stops the engine");
  // The fuselage takes hits in full and only gains drag.
  state = {};
  check(applyPartDamage(typhoon.flight, state, DamagePart::Fuselage, 34) == 34 &&
            partHealth(state, DamagePart::Fuselage) == 1 &&
            state.surface_drag[5] > 1,
        "fuselage hits cost full hit points");
  for (unsigned i = 0; i < 20; ++i)
    applyPartDamage(typhoon.flight, state, DamagePart::Fuselage, 34);
  check(state.surface_drag[5] <= 10, "drag stays inside the replicated range");
  // A canard aircraft keeps its foreplanes when the fin is shot away; a
  // conventional tail loses most, but not all, of its tailplanes.
  state = {};
  applyPartDamage(typhoon.flight, state, DamagePart::Tail, 200);
  check(partDestroyed(state, DamagePart::Tail) && state.surface_health[2] == 1 &&
            state.surface_health[3] == 1,
        "canards survive a fin hit");
  state = {};
  applyPartDamage(airliner.flight, state, DamagePart::Tail, 200);
  check(partDestroyed(state, DamagePart::Tail) &&
            state.surface_health[2] == .35 && state.surface_health[3] == .35,
        "tailplanes keep some authority");
  check(applyPartDamage(typhoon.flight, state, DamagePart::Fuselage, 0) == 0 &&
            applyPartDamage(typhoon.flight, state, DamagePart::Fuselage, -5) ==
                0 &&
            applyPartDamage(typhoon.flight, state, DamagePart::Fuselage,
                            std::nan("")) == 0,
        "invalid damage is ignored");
  // A sound wing has no load limit; a holed one snaps when pulled too hard.
  state = {};
  check(!applyOverstress(state, 12) && !applyOverstress(state, -12),
        "sound wings are not limited");
  applyPartDamage(typhoon.flight, state, DamagePart::LeftWing, 70);
  const double limit = wingLoadLimit(partHealth(state, DamagePart::LeftWing));
  check(limit > 3 && limit < 9 &&
            wingLoadLimit(.2) < wingLoadLimit(.4) &&
            !applyOverstress(state, limit - .1) &&
            partHealth(state, DamagePart::LeftWing) > 0,
        "a damaged wing holds up to its limit");
  check(!applyOverstress(state, std::nan("")) &&
            applyOverstress(state, -(limit + .1)) &&
            partDestroyed(state, DamagePart::LeftWing) &&
            partHealth(state, DamagePart::RightWing) == 1 &&
            !applyOverstress(state, 20),
        "overloading a damaged wing snaps it off, in either direction");
  // Engines have no collision spheres: a hit beside one is the engine's.
  for (const auto &definition : aircraftDefinitions())
    for (unsigned e = 0; e < definition.flight.engine_count; ++e) {
      const auto bay = engineBay(definition, e);
      const auto engine = e == 0 ? DamagePart::LeftEngine : DamagePart::RightEngine;
      check(classifyHit(definition, DamagePart::Fuselage,
                        (bay.aft + bay.fore) * .5) == engine &&
                classifyHit(definition, DamagePart::Tail, bay.aft) == engine,
            "hit beside an engine");
      check(classifyHit(definition, DamagePart::LeftWing,
                        definition.visual.wingtip[0]) == DamagePart::LeftWing &&
                classifyHit(definition, DamagePart::RightWing,
                            definition.visual.wingtip[1]) ==
                    DamagePart::RightWing,
            "wingtip hit stays a wing hit");
      check(classifyHit(definition, DamagePart::Fuselage,
                        definition.visual.cockpit) == DamagePart::Fuselage,
            "cockpit hit stays a fuselage hit");
    }
  std::puts("damage parts: shares, strengths, destruction, engine flame-out, "
            "tail layouts, hit classification PASS");
}
void flight() {
  for (const auto &definition : aircraftDefinitions()) {
    const std::string name(definition.key);
    // A damaged left wing lifts less, so the aircraft rolls toward it.
    auto sim = trimmed(definition.type);
    const auto level = sim.evalAero();
    auto state = sim.state();
    applyPartDamage(definition.flight, state, DamagePart::LeftWing, 60);
    check(sim.setState(state), "damaged state accepted");
    const auto holed = sim.evalAero();
    check(holed.lift_body.norm() < level.lift_body.norm() &&
              holed.moment_body.x < level.moment_body.x - 1000,
          "a holed left wing rolls the aircraft left");
    for (unsigned i = 0; i < 120 * 10; ++i)
      sim.step(tickSeconds);
    check(finiteState(sim.state()), "holed wing flight stays finite");
    const double holedRoll = sim.instruments().roll_deg;
    // The mirror image rolls the other way.
    auto mirror = trimmed(definition.type);
    state = mirror.state();
    applyPartDamage(definition.flight, state, DamagePart::RightWing, 60);
    mirror.setState(state);
    check(mirror.evalAero().moment_body.x > level.moment_body.x + 1000,
          "a holed right wing rolls the aircraft right");
    // Both engines shot out: no thrust, and the aircraft slows.
    auto glider = trimmed(definition.type);
    const double speed = glider.instruments().tas;
    state = glider.state();
    for (const auto engine : {DamagePart::LeftEngine, DamagePart::RightEngine})
      applyPartDamage(definition.flight, state, engine, 100);
    glider.setState(state);
    for (unsigned i = 0; i < 120 * 15; ++i)
      glider.step(tickSeconds);
    const auto thrust = glider.evalThrust();
    check(thrust.each[0] == 0 && thrust.each[1] == 0 &&
              glider.state().afterburner[0] == 0 && finiteState(glider.state()),
          "shot-out engines produce nothing");
    // One engine out leaves the other pushing off the centreline.
    auto single = trimmed(definition.type);
    state = single.state();
    applyPartDamage(definition.flight, state, DamagePart::LeftEngine, 100);
    single.setState(state);
    for (unsigned i = 0; i < 120 * 4; ++i)
      single.step(tickSeconds);
    const auto asymmetric = single.evalThrust();
    if (definition.flight.engine_count == 1)
      check(asymmetric.each[0] == 0 && asymmetric.force_body.norm() == 0,
            "a single-engined aircraft is left with no thrust");
    else
      check(asymmetric.each[0] == 0 && asymmetric.each[1] > 0 &&
                asymmetric.moment_body.z < 0,
            "one engine out yaws toward the dead engine");
    // Every part wrecked at once must still integrate.
    auto wreck = trimmed(definition.type);
    state = wreck.state();
    for (unsigned part = 0; part < damagePartCount; ++part)
      applyPartDamage(definition.flight, state, DamagePart(part), 500);
    check(wreck.setState(state), "wrecked state accepted");
    for (unsigned i = 0; i < 120 * 20; ++i)
      wreck.step(tickSeconds);
    check(finiteState(wreck.state()), "a wreck falls without a numerical fault");
    std::printf("damage flight %s holedWingRoll=%.1f deg glideSpeed %.0f -> "
                "%.0f m/s\n",
                name.c_str(), holedRoll, speed, glider.instruments().tas);
  }
  std::puts("damage flight: wing asymmetry, engine loss, total wreck stay "
            "finite and act in the expected direction PASS");
}
// A shooter in formation with `target`, offset by `from` in the target's body
// axes and pointed at `aim` on it. It shares the target's velocity, so its
// rounds do not fall behind an aircraft in flight.
State aimed(const State &target, Vec3 from, Vec3 aim) {
  State shooter = target;
  shooter.pos_ned = target.pos_ned + target.att.rotate(from);
  const Vec3 line = target.att.rotate(aim - from).normalized();
  shooter.att = quatFromEuler(0, std::asin(-line.z), std::atan2(line.y, line.x));
  return shooter;
}
void combat() {
  GunConfig gun;
  gun.dispersion = 0;
  gun.muzzle = {};
  gun.muzzleVelocity = 1000;
  gun.damage = 34;
  for (const auto type : {AircraftType::Typhoon, AircraftType::Su57}) {
    if (!validAircraftType(type))
      continue;
    const auto &definition = aircraftDefinition(type);
    State level;
    level.pos_ned = {0, 0, -3000};
    const auto cg = loadedCg(definition.flight, level);
    struct Shot {
      Vec3 from, aim;
      HitRegion expected;
      const char *why;
    };
    const Vec3 leftTip = definition.visual.wingtip[0] - cg,
               rightTip = definition.visual.wingtip[1] - cg;
    const auto leftBay = engineBay(definition, 0),
               rightBay = engineBay(definition, 1);
    const Shot shots[]{
        // From abeam and above, into each outer wing.
        {leftTip * .8 + Vec3{0, -60, -25}, leftTip * .8, HitRegion::LeftWing,
         "left wing"},
        {rightTip * .8 + Vec3{0, 60, -25}, rightTip * .8, HitRegion::RightWing,
         "right wing"},
        // From dead astern, up each jet pipe.
        {leftBay.aft - cg + Vec3{-80, 0, 0}, leftBay.aft - cg,
         HitRegion::LeftEngine, "left engine"},
        {rightBay.aft - cg + Vec3{-80, 0, 0}, rightBay.aft - cg,
         HitRegion::RightEngine, "right engine"},
        // From ahead and above, into the cockpit.
        {definition.visual.cockpit - cg + Vec3{60, 0, -30},
         definition.visual.cockpit - cg, HitRegion::Fuselage, "fuselage"},
    };
    for (const auto &shot : shots) {
      Combat authority(gun);
      Life shooter, life;
      CombatTarget target{2, level, level, &life, type};
      check(authority.fire(1, 1, aimed(level, shot.from, shot.aim), shooter),
            "fixture fires");
      HitRegion region{};
      bool hit = false;
      for (Tick tick = 1; tick < 40 && !hit; ++tick) {
        authority.step(tick, std::span(&target, 1));
        for (const auto &event : authority.takeEvents())
          if (event.kind == CombatKind::Hit) {
            hit = true;
            region = event.region;
          }
      }
      if (!hit || region != shot.expected)
        std::printf("damage combat %s %s: hit=%d region=%u\n",
                    definition.key.data(), shot.why, hit, unsigned(region));
      check(hit && region == shot.expected, shot.why);
      check(target.damaged && target.attacker == 1 &&
                life.health == 100 - 34 * partHitPointShare(shot.expected),
            "hit points follow the part");
      for (unsigned part = 0; part < damagePartCount; ++part)
        check((partHealth(target.current, DamagePart(part)) < 1) ==
                  (DamagePart(part) == shot.expected &&
                   shot.expected != DamagePart::Fuselage),
              "only the struck part is damaged");
    }
  }
  std::puts("damage combat: rounds into each wing, each engine and the "
            "fuselage damage that part PASS");
}
void world() {
  GunConfig gun;
  gun.dispersion = 0;
  gun.muzzle = {};
  gun.muzzleVelocity = 1000;
  gun.damage = 34;
  gun.respawnDelay = 60;
  World authority(true, gun);
  const auto attacker = authority.join(AircraftType::Typhoon, "Hunter"),
             victim = authority.join(AircraftType::Typhoon, "Quarry");
  check(authority.players().at(attacker).name == "Hunter" &&
            authority.join(AircraftType::Typhoon) != 0,
        "pilot names are kept");
  const auto &definition = aircraftDefinition(AircraftType::Typhoon);
  auto &quarry = fixture(authority, victim);
  // A round into the right wing reaches the simulated aircraft, not only the
  // hit points.
  const auto strike = [&](DamagePart part) {
    const auto state = quarry.sim.state();
    const auto cg = loadedCg(definition.flight, state);
    const bool left = part == DamagePart::LeftWing;
    const Vec3 tip = definition.visual.wingtip[left ? 0 : 1] - cg;
    Life firing;
    check(authority.combat().fire(
              authority.tick() + 1, attacker,
              aimed(state, tip * .8 + Vec3{0, left ? -40. : 40., -15}, tip * .8),
              firing, gun),
          "fixture fires");
    const double before = quarry.life.health;
    for (unsigned i = 0; i < 20 && quarry.life.health == before; ++i)
      authority.step();
  };
  strike(DamagePart::RightWing);
  check(partHealth(quarry.sim.state(), DamagePart::RightWing) < 1 &&
            partHealth(quarry.sim.state(), DamagePart::LeftWing) == 1 &&
            quarry.life.health == 83 && quarry.lastAttacker == attacker,
        "wing damage reaches the simulated aircraft");
  // The damage is in the state every viewer receives.
  const auto seen = authority.aircraft(victim);
  Aircraft remote;
  check(expandAircraft(victim,
                       projectAircraft(seen, false, Tier::Near, authority.tick(),
                                       seen.state.pos_ned),
                       seen.state.pos_ned, remote) &&
            std::abs(partHealth(remote.state, DamagePart::RightWing) -
                     partHealth(seen.state, DamagePart::RightWing)) <= 1. / 510 &&
            partHealth(remote.state, DamagePart::LeftWing) == 1,
        "other players see the damaged wing");
  auto burning = seen;
  burning.state.engine_health[0] = 0;
  burning.state.surface_health[4] = .25;
  check(expandAircraft(victim,
                       projectAircraft(burning, false, Tier::Far, 1, {}), {},
                       remote) &&
            remote.state.engine_health[0] == 0 &&
            remote.state.engine_health[1] == 1 &&
            std::abs(remote.state.surface_health[4] - .25) <= 1. / 510,
        "other players see a dead engine and a torn fin");
  // Both wings shot away ends the life and counts for the attacker.
  for (unsigned i = 0; i < 6 && quarry.life.alive(); ++i)
    strike(i % 2 ? DamagePart::RightWing : DamagePart::LeftWing);
  check(!quarry.life.alive() && quarry.life.deaths == 1 &&
            authority.players().at(attacker).life.kills == 1,
        "an aircraft without wings is destroyed");
  while (!quarry.life.alive())
    authority.step();
  for (unsigned part = 0; part < damagePartCount; ++part)
    check(partHealth(quarry.sim.state(), DamagePart(part)) == 1,
          "respawn restores every part");
  check(quarry.lastAttacker == 0, "respawn forgets the last attacker");
  // Two hits leave a wing that carries its stores but not a hard turn.
  authority.combat().takeEvents();
  strike(DamagePart::LeftWing);
  strike(DamagePart::LeftWing);
  const auto mounted = [&] {
    unsigned left = 0, right = 0;
    for (const auto &station : quarry.weapons.inventory.stations)
      if (station.mounted != WeaponType::None)
        ++(station.position.y < 0 ? left : right);
    return std::pair{left, right};
  };
  check(quarry.life.alive() && mounted() == std::pair{3u, 3u} &&
            partHealth(quarry.sim.state(), DamagePart::LeftWing) <
                weakenedWingHealth,
        "a weakened wing still carries its stores");
  // Fast enough that the wings, not the stall, limit the turn.
  auto fast = quarry.sim.state();
  fast.vel_ned = fast.vel_ned.normalized() * 260;
  quarry.sim.setState(fast);
  auto pulling = quarry.sim.controls();
  pulling.elevator_stick = 1;
  pulling.throttle[0] = pulling.throttle[1] = 1;
  bool snapped = false;
  for (unsigned i = 0; i < 120 * 12 && quarry.life.alive() && !snapped; ++i) {
    quarry.sim.setControls(pulling);
    quarry.lastInput = authority.tick();
    authority.step();
    snapped = partDestroyed(quarry.sim.state(), DamagePart::LeftWing);
  }
  // The Typhoon's outer-pylon missile goes with the wing; the two half sunk
  // into that side of the fuselage stay.
  check(snapped && quarry.life.alive() && mounted() == std::pair{2u, 3u},
        "pulling hard on a weakened wing snaps it off with its stores");
  // The aircraft cannot stay up on one wing; when it goes in, the pilot who
  // shot the wing up is credited.
  bool credit = false;
  for (unsigned i = 0; i < 120 * 90 && quarry.life.alive(); ++i) {
    authority.step();
    for (const auto &event : authority.combat().takeEvents())
      credit = credit || (event.kind == CombatKind::Destroyed &&
                          event.owner == attacker && event.target == victim);
  }
  check(!quarry.life.alive() && credit, "a snapped wing is the attacker's kill");
  while (!quarry.life.alive())
    authority.step();
  // A hit followed by a crash is the attacker's kill; a crash alone is not.
  authority.combat().takeEvents();
  strike(DamagePart::LeftWing);
  check(quarry.life.alive() && quarry.lastAttacker == attacker, "wounded");
  auto diving = quarry.sim.state();
  diving.pos_ned.z = groundHeightNed(diving.pos_ned.x, diving.pos_ned.y) - 30;
  diving.vel_ned = {150, 0, 80};
  quarry.sim.setState(diving);
  bool credited = false;
  for (unsigned i = 0; i < 240 && quarry.life.alive(); ++i) {
    authority.step();
    for (const auto &event : authority.combat().takeEvents())
      credited = credited || (event.kind == CombatKind::Destroyed &&
                              event.owner == attacker && event.target == victim);
  }
  check(!quarry.life.alive() && credited &&
            authority.players().at(attacker).life.kills == 3,
        "a crash after a hit is credited to the attacker");
  while (!quarry.life.alive())
    authority.step();
  authority.combat().takeEvents();
  diving = quarry.sim.state();
  diving.pos_ned.z = groundHeightNed(diving.pos_ned.x, diving.pos_ned.y) - 30;
  diving.vel_ned = {150, 0, 80};
  quarry.sim.setState(diving);
  bool own = false;
  for (unsigned i = 0; i < 240 && quarry.life.alive(); ++i) {
    authority.step();
    for (const auto &event : authority.combat().takeEvents())
      own = own || (event.kind == CombatKind::Destroyed &&
                    event.owner == victim && event.target == victim);
  }
  check(!quarry.life.alive() && own &&
            authority.players().at(attacker).life.kills == 3,
        "an unprovoked crash is nobody's kill");
  std::puts("damage world: authoritative part damage, replication to "
            "viewers, wingless destruction, crash credit, respawn repair PASS");
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 2, "suite required");
    const std::string suite = argv[1];
    if (suite == "parts")
      parts();
    else if (suite == "flight")
      flight();
    else if (suite == "combat")
      combat();
    else if (suite == "world")
      world();
    else
      throw std::invalid_argument("suite");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "DAMAGE FAIL: %s\n", error.what());
    return 1;
  }
}
