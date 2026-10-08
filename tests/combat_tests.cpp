#include "ofs/net/client.hpp"
#include "ofs/net/server.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
using namespace ofs;
using namespace ofs::net;
namespace {
void check(bool value, const char *why) {
  if (!value)
    throw std::runtime_error(why);
}
// Authoritative scenario fixture only. There is deliberately no network message
// or game-facing API for assigning client transforms or health.
Player &fixture(World &world, EntityId id) {
  return const_cast<Player &>(world.players().at(id));
}
void align(World &world, EntityId shooter, EntityId target, double lateral = 0) {
  auto &a = fixture(world, shooter), &b = fixture(world, target);
  auto s = a.sim.state();
  s.vel_ned = {110, 0, 0};
  s.omega_body = {};
  s.pos_ned = {double(world.tick()) * 110 / 120, 0, -1000};
  a.sim.setState(s);
  s.pos_ned += s.att.rotate(Vec3{180, lateral, 0});
  b.sim.setState(s);
}
std::size_t rss() {
#ifdef __linux__
  std::ifstream file("/proc/self/status");
  std::string line;
  while (std::getline(file, line))
    if (line.starts_with("VmRSS:"))
      return std::stoull(line.substr(6));
#endif
  return 0;
}
void unit() {
  GunConfig gun;
  gun.dispersion = 0;
  Combat combat(gun);
  State state;
  state.pos_ned.z = -1000;
  Life shooter, target;
  check(combat.fire(1, 1, state, shooter), "initial fire");
  for (Tick tick = 1; tick < 13; ++tick)
    check(!combat.fire(tick, 1, state, shooter), "rate bypass");
  check(combat.fire(13, 1, state, shooter), "rate readiness");
  shooter.health = 0;
  check(!combat.fire(100, 1, state, shooter), "dead fire");
  shooter.health = 100;
  shooter.ammo = 0;
  check(!combat.fire(100, 1, state, shooter), "empty gun");
  Projectile p;
  p.velocity = {850, 10, 0};
  p.position = {1, 2, 3};
  advanceProjectile(p, .1);
  check(std::abs(p.position.x - 86) < 1e-10 &&
            std::abs(p.position.z - (3 + .5 * kG0 * .01)) < 1e-10 &&
            std::abs(p.velocity.z - kG0 * .1) < 1e-10,
        "ballistic motion");
  check(std::abs(sweptSphere({-100, 0, 0}, {100, 0, 0}, {}, 2) - .49) < 1e-10,
        "high speed sweep");
  check(!std::isfinite(sweptSphere({-100, 2.01, 0}, {100, 2.01, 0}, {}, 2)),
        "near miss");
  check(sweptSphere({0, 0, 0}, {0, 0, 0}, {}, 2) == 0,
        "inside stationary segment");
  // Actual projectile crosses a fuselage sphere in one tick with both endpoints
  // outside it, using max supported muzzle speed (41.7 m/tick).
  gun.muzzleVelocity = 5000;
  Combat fast(gun);
  shooter = {};
  target = {};
  check(fast.fire(1, 1, state, shooter), "fast fire");
  State targetState = state;
  targetState.pos_ned.x = 50;
  CombatTarget t{2, targetState, targetState, &target};
  fast.step(1, std::span(&t, 1));
  // The round enters the fin, which absorbs half of it.
  check(target.health == 87.5 && fast.stats().hits == 1 &&
            fast.projectiles().empty() && t.damaged && t.attacker == 1 &&
            partHealth(t.current, DamagePart::Tail) == 1 - 25. / 80,
        "swept damage");
  fast.step(2, std::span(&t, 1));
  check(target.health == 87.5, "duplicate damage");
  auto events = fast.takeEvents();
  check(events.size() == 2 && events[1].region == HitRegion::Tail &&
            events[1].health == 87.5,
        "hit region/event");
  // Three more rounds finish the fin; after that it protects nothing.
  for (unsigned i = 0; i < 3; ++i) {
    fast.fire(13 + i * 12, 1, state, shooter);
    fast.step(13 + i * 12, std::span(&t, 1));
  }
  check(target.health == 50 && partDestroyed(t.current, DamagePart::Tail),
        "part destroyed before the aircraft");
  for (unsigned i = 3; i < 5; ++i) {
    fast.fire(13 + i * 12, 1, state, shooter);
    fast.step(13 + i * 12, std::span(&t, 1));
  }
  check(!target.alive() && target.deaths == 1 && fast.stats().kills == 1,
        "lethal transition");
  fast.fire(100, 1, state, shooter);
  fast.step(100, std::span(&t, 1));
  check(target.deaths == 1 && fast.stats().kills == 1, "duplicate destruction");
  // Crossing target: moving hitbox sweep intersects stationary round path.
  Combat crossing(gun);
  shooter = {};
  target = {};
  crossing.fire(1, 1, state, shooter);
  State before = targetState, after = targetState;
  before.pos_ned.y = -50;
  after.pos_ned.y = 50;
  CombatTarget moving{2, before, after, &target};
  crossing.step(1, std::span(&moving, 1));
  check(crossing.stats().hits == 1, "moving high-speed crossing");
  Combat miss(gun);
  shooter = {};
  target = {};
  miss.fire(1, 1, state, shooter);
  before = after = targetState;
  before.pos_ned.y = after.pos_ned.y = 30;
  moving = {2, before, after, &target};
  miss.step(1, std::span(&moving, 1));
  check(target.health == 100 && miss.stats().hits == 0, "actual miss");
  for (Tick tick = 2; tick < 500; ++tick)
    miss.step(tick, {});
  check(miss.projectiles().empty(), "expiry cleanup");
  // Expiry must not hit beyond range on its last partial tick.
  gun.range = 1;
  Combat shortRange(gun);
  shooter = {};
  target = {};
  shortRange.fire(1, 1, state, shooter);
  shortRange.step(1, std::span(&t, 1));
  check(target.health == 100 && shortRange.projectiles().empty(),
        "terminal range clipping");
  // Terrain consumes rounds before they can hit aircraft beyond the ground.
  GunConfig downward=gun; downward.direction=Vec3{1,0,1}.normalized();
  downward.muzzle={0,0,0}; downward.muzzleVelocity=5000;
  Combat terrainCombat(downward); shooter={}; target={};
  State low; low.pos_ned={0,0,-1};
  State buried=low; buried.pos_ned={20,0,20};
  CombatTarget underground{2,buried,buried,&target,AircraftType::Su57};
  check(terrainCombat.fire(1,1,low,shooter),"terrain fixture fires");
  terrainCombat.step(1,std::span(&underground,1));
  check(terrainCombat.projectiles().empty() && target.health==100 && terrainCombat.stats().hits==0,
        "terrain blocks bullet before target");
  // Owner immunity is bounded: survives muzzle overlap then can hit owner
  // later.
  gun = GunConfig{};
  gun.dispersion = 0;
  gun.muzzle = {0, 0, 0};
  Combat self(gun);
  shooter = {};
  self.fire(1, 1, state, shooter);
  CombatTarget owner{1, state, state, &shooter};
  self.step(1, std::span(&owner, 1));
  check(shooter.health == 100, "owner muzzle grace");
  for (Tick tick = 2; tick < 20; ++tick)
    self.step(tick, {});
  auto intercept = state;
  intercept.pos_ned = self.projectiles().front().position;
  owner = {1, intercept, intercept, &shooter};
  self.step(20, std::span(&owner, 1));
  check(shooter.health == 75, "permanent owner immunity");
  // Multiple attackers can damage one target, but only one lethal transition.
  gun = GunConfig{};
  gun.dispersion = 0;
  gun.muzzleVelocity = 5000;
  gun.damage = 100;
  Combat multiple(gun);
  Life one, two;
  target = {};
  multiple.fire(1, 1, state, one);
  multiple.fire(1, 2, state, two);
  t = {9, targetState, targetState, &target};
  multiple.step(1, std::span(&t, 1));
  check(target.health == 0 && multiple.stats().hits == 2 &&
            multiple.stats().kills == 1 && target.deaths == 1 &&
            multiple.projectiles().empty(),
        "multiple shooters lethal once");
  // Rotation and aircraft velocity are derived on the server, with no wire
  // input.
  Combat transform;
  Life life;
  auto rotated = state;
  rotated.att = quatFromEuler(0, 0, kPi / 2);
  rotated.vel_ned = {100, 20, 0};
  transform.fire(1, 5, rotated, life);
  const auto &round = transform.projectiles().front();
  check(std::abs(round.position.y - 19) < 1e-9 && round.velocity.y > 869 &&
            std::abs(round.velocity.x - 100) < 2,
        "authoritative muzzle/velocity transform");
  // Finite guard and bounded buffers even if a caller does not drain events.
  Combat bounded;
  shooter = {};
  auto invalid = state;
  invalid.pos_ned.x = std::numeric_limits<double>::quiet_NaN();
  check(!bounded.fire(1, 1, invalid, shooter), "invalid projectile creation");
  for (unsigned i = 0; i < 5000; ++i) {
    Life fresh;
    bounded.fire(1, 1, state, fresh);
  }
  check(bounded.projectiles().size() == Combat::capacity &&
            bounded.stats().poolFull > 0 &&
            bounded.stats().peakEvents <= Combat::eventCapacity,
        "combat storage bound");
  std::puts("combat unit: rate/ammo/dead, motion, swept/crossing hit, miss, "
            "single damage/destruction, regions, finite/range/lifetime "
            "cleanup, bounded owner grace/pools PASS");
}
void lifecycle() {
  GunConfig gun;
  gun.dispersion = 0;
  gun.respawnDelay = 60;
  World world(true, gun);
  auto a = world.join(AircraftType::Typhoon), b = world.join(AircraftType::Typhoon);
  align(world, a, b);
  check(!world.enqueueFire(999, {1, 1, 0, 0, true}), "unknown ownership");
  check(!world.enqueueFire(a, {1, 1000, 0, 0, true}), "future fire");
  check(!world.enqueueFire(a, {1, 1, 0, 1, true}), "invalid gun");
  check(!world.enqueueFire(a, {0, 1, 0, 0, true}), "zero sequence");
  check(!world.enqueueFire(a, {10000, 1, 0, 0, true}), "sequence window");
  check(!world.enqueueFire(a, {1, 1, 10, 0, true}), "future generation");
  check(world.enqueueFire(a, {1, 1, 0, 0, true}), "queued fire");
  check(world.enqueueFire(a, {1, 1, 0, 0, true}), "safe duplicate");
  for (unsigned i = 0; i < 100 && world.aircraft(b).life.alive(); ++i) {
    world.step();
    world.combat().takeEvents();
  }
  check(!world.aircraft(b).life.alive(), "world kill");
  const auto shots = world.combat().stats().shots;
  check(world.enqueueFire(b, {1, world.tick() + 1, 0, 0, true}),
        "dead in-flight retirement");
  auto due = world.aircraft(b).life.respawnTick;
  auto deadState = world.aircraft(b).state;
  world.step();
  check((deadState.pos_ned - world.aircraft(b).state.pos_ned).norm() == 0,
        "dead flight advanced");
  check(world.combat().stats().shots <= shots + 1, "dead player shot");
  while (world.tick() + 1 < due) {
    world.step();
    world.combat().takeEvents();
  }
  check(!world.aircraft(b).life.alive(), "early respawn");
  auto occupied = fixture(world, a).sim.state();
  occupied.pos_ned = {0, 100, -1000};
  fixture(world, a).sim.setState(occupied);
  world.step();
  auto reborn = world.aircraft(b);
  check(reborn.state.pos_ned.x < -140,
        "occupied spawn candidate was not skipped");
  check(reborn.life.alive() && reborn.life.health == 100 &&
            reborn.life.ammo == gun.ammo && reborn.life.generation == 1 &&
            world.players().size() == 2 && world.combat().stats().respawns == 1,
        "respawn state");
  check((reborn.state.pos_ned - world.aircraft(a).state.pos_ned).norm() > 60,
        "unsafe respawn");
  check(fixture(world, b).inputs.empty() &&
            fixture(world, b).fireInputs.empty() && !fixture(world, b).firing,
        "stale queue");
  check(world.enqueueFire(b, {2, world.tick() + 1, 0, 0, true}),
        "old life fire retirement");
  auto old = reborn.controls;
  old.aileron_stick = 1;
  check(world.enqueue(b, {{1, world.tick() + 1, old}}, 0),
        "old flight retirement");
  world.step();
  check(fixture(world, b).sim.controls().aileron_stick == 0,
        "old life flight changed controls");
  Prediction prediction;
  Aircraft baseline = world.aircraft(a);
  prediction.initialize(world.tick(), baseline, 0);
  for (unsigned i = 0; i < 30; ++i)
    prediction.advance(baseline.controls);
  auto changed = baseline;
  ++changed.life.generation;
  changed.state.pos_ned = {-100, 200, -1000};
  prediction.reconcile(world.tick(), changed);
  check(prediction.pending().empty() && prediction.stats().reconciliations == 0,
        "respawn pending history survived");
  changed.life.health = 0;
  changed.life.respawnTick = world.tick() + 60;
  prediction.reconcile(world.tick(), changed);
  check(prediction.pending().empty() && prediction.tick() == world.tick() &&
            (prediction.simulator().state().pos_ned - changed.state.pos_ned)
                    .norm() == 0,
        "dead prediction");
  RemoteTrack track;
  track.push(1, baseline);
  track.push(2, reborn);
  check(track.size() == 1, "remote respawn interpolation history");
  track.push(3, baseline);
  check(track.size() == 1, "old generation remote accepted");
  world.leave(a);
  world.leave(b);
  check(world.players().empty() && world.combat().projectiles().empty(),
        "leave combat leak");
  std::puts("combat lifecycle: queued fire validation/duplicates, dead freeze, "
            "timed safe same-ID respawn, old-life retirement, "
            "prediction/interpolation reset, leave cleanup PASS");
}
void codec() {
  for (auto type : {Type::Fire, Type::Combat}) {
    Message m;
    m.type = type;
    m.entity = 1;
    m.tick = 10;
    m.sequence = 1;
    m.fire = {1, 10, 0, 0, true};
    for (unsigned kind = 1; kind <= 4; ++kind) {
      CombatEvent e;
      e.id = kind;
      e.tick = 10;
      e.kind = CombatKind(kind);
      e.owner = 1;
      e.target = 2;
      e.projectile = 3;
      e.lifetime = 3;
      e.health = 75;
      e.position = {1e9, 1, -1000};
      e.velocity = {850, 0, 0};
      m.events.push_back(e);
    }
    auto bytes = encode(m);
    Message out;
    std::string reason;
    check(decode(bytes, out, reason), "combat wire roundtrip");
    for (std::size_t i = 0; i < bytes.size(); ++i)
      check(!decode(std::span(bytes).first(i), out, reason),
            "combat truncated");
    bytes.push_back(0);
    check(!decode(bytes, out, reason), "combat trailing data");
    if (type == Type::Fire) {
      for (unsigned field : {52u, 53u}) {
        bytes = encode(m);
        bytes[field] = 2;
        check(!decode(bytes, out, reason), "invalid fire value");
      }
      m.sequence = 2;
      check(!decode(encode(m), out, reason), "fire header order mismatch");
    } else {
      m.events[0].velocity.x = std::numeric_limits<double>::infinity();
      check(!decode(encode(m), out, reason), "nonfinite event");
    }
  }
  // Random decoder inputs and mutated valid combat packets.
  std::mt19937 rng(234);
  Message m;
  m.type = Type::Fire;
  m.entity = 1;
  m.tick = 1;
  m.sequence = 1;
  m.fire = {1, 1, 0, 0, true};
  for (unsigned i = 0; i < 10000; ++i) {
    auto bytes = encode(m);
    bytes[rng() % bytes.size()] = rng() % 256;
    Message out;
    std::string why;
    (void)decode(bytes, out, why);
  }
  std::puts("combat codec: v2 fire/event batches, truncation, trailing, "
            "enum/boolean/header/nonfinite rejection, 10000 mutations PASS");
}
void network(const std::string &preset, unsigned seconds,
             unsigned clients = 2) {
  ServerConfig config;
  config.bind = "127.0.0.1";
  config.port = 0;
  config.maxClients = clients;
  // Exercise the reusable lifecycle/network contract with its original gun
  // fixture. Production per-aircraft gun parameters have separate coverage.
  config.gun=GunConfig{};
  Server server(config);
  Transport::conditions(preset);
  std::vector<std::unique_ptr<Client>> bots;
  for (unsigned i = 0; i < clients; ++i) {
    bots.push_back(std::make_unique<Client>("combat" + std::to_string(i), AircraftType::Typhoon));
    bots.back()->connect("127.0.0.1", server.port());
  }
  std::vector<std::uint32_t> generations(
      clients, std::numeric_limits<std::uint32_t>::max());
  std::vector<std::uint64_t> lastHit(clients);
  double maxLatency = 0;
  Tick firstRequest{}, firstShot{};
  auto start = std::chrono::steady_clock::now(), last = start, next = start;
  double accumulator = 0;
  bool started = false;
  std::size_t warmRss = 0, maxRss = 0;
  const auto period =
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(tickSeconds));
  while (true) {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start).count();
    if (!started && elapsed >= 5)throw std::runtime_error("combat handshake timeout");
    if (started && elapsed >= seconds)
      break;
    const double dt = std::chrono::duration<double>(now - last).count();
    last = now;
    server.poll();
    const bool ready = std::all_of(bots.begin(), bots.end(),
                                   [](const auto &b) { return b->ready(); });
    if (started)
      check(ready, "combat client disconnected");
    if (ready) {
      if(!started) {
        // The impairment/lifecycle observation window starts at the actual
        // handshake barrier. Connection setup must not consume respawn time.
        start=now;next=now;accumulator=0;elapsed=0;
      }
      started = true;
      for (unsigned i = 0; i < clients; i += 2) {
        const auto a = bots[i]->entity(), b = bots[i + 1]->entity();
        const auto generation = server.world().aircraft(b).life.generation;
        const auto generationA = server.world().aircraft(a).life.generation;
        if ((generations[i + 1] != generation ||
             generations[i] != generationA) &&
            server.world().aircraft(a).life.alive() &&
            server.world().aircraft(b).life.alive()) {
          // Alternate the aimed role after each respawn. A head-on duel under
          // asymmetric loss need not destroy both pilots; this scenario
          // verifies both clients' life resets without assuming simultaneous
          // lethal hits.
          if ((generation + generationA) % 2 == 0)
            align(server.world(), a, b);
          else
            align(server.world(), b, a);
          // Separate pairs in space while preserving deterministic flight.
          auto &pa = fixture(server.world(), a),
               &pb = fixture(server.world(), b);
          auto sa = pa.sim.state(), sb = pb.sim.state();
          sa.pos_ned.y = sb.pos_ned.y = double(i) * 200;
          pa.sim.setState(sa);
          pb.sim.setState(sb);
          generations[i + 1] = generation;
          generations[i] = generationA;
        }
      }
    }
    unsigned catchup = 0;
    while (now >= next && catchup++ < 16) {
      server.step();
      next += period;
      if (!firstShot && server.world().combat().stats().shots)
        firstShot = server.world().tick();
    }
    for (auto &bot : bots)
      bot->poll(dt);
    if (ready) {
      accumulator += dt;
      while (accumulator >= tickSeconds) {
        for (auto &bot : bots) {
          if (!firstRequest)
            firstRequest = server.world().tick();
          bot->setFiring(true);
          auto controls = bot->prediction().simulator().controls();
          // Deterministic trim trajectory; separate M2 suites exercise
          // maneuvers.
          controls.aileron_stick = 0;
          bot->predict(controls);
        }
        accumulator -= tickSeconds;
      }
      for (const auto &[id, p] : server.world().players()) {
        (void)id;
        check(finiteState(p.sim.state()) && p.inputs.size() <= 256 &&
                  p.fireInputs.size() <= 128,
              "soak state/queue");
      }
      for (unsigned index = 0; index < bots.size(); ++index) {
        const auto &bot = bots[index];
        check(bot->prediction().pending().size() <= 512 &&
                  bot->stats().historySamples <= 32 * (clients - 1) &&
                  bot->visualCount() <= Combat::capacity,
              "client combat bound");
        if (bot->latestHit().id > lastHit[index]) {
          lastHit[index] = bot->latestHit().id;
          maxLatency = std::max(maxLatency, (double(server.world().tick()) -
                                             double(bot->latestHit().tick)) *
                                                tickSeconds);
        }
      }
    }
    if (elapsed > (seconds >= 120 ? 30 : 5) && !warmRss)
      warmRss = rss();
    if (warmRss && unsigned(elapsed * 10) % 10 == 0)
      maxRss = std::max(maxRss, rss());
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto combat = server.world().combat().stats();
  std::printf("authority shots=%llu hits=%llu kills=%llu respawns=%llu\n",
              (unsigned long long)combat.shots, (unsigned long long)combat.hits,
              (unsigned long long)combat.kills,
              (unsigned long long)combat.respawns);
  check(started && combat.shots >= 32 && combat.hits >= 16 &&
            combat.kills >= 4 && combat.respawns >= 4,
        "repeat combat loop");
  check(combat.droppedEvents == 0 && combat.poolFull == 0 &&
            server.stats().sendFailures == 0,
        "combat overflow/send failure");
  for (unsigned i = 0; i < clients; ++i) {
    std::printf("client=%u shots=%llu received=%llu resets=%llu respawns=%llu "
                "generation=%u kills=%u deaths=%u\n",
                i, (unsigned long long)bots[i]->stats().shots,
                (unsigned long long)bots[i]->stats().hitsReceived,
                (unsigned long long)bots[i]->stats().predictionResets,
                (unsigned long long)bots[i]->stats().respawns,
                bots[i]->life().generation, bots[i]->life().kills,
                bots[i]->life().deaths);
    check(bots[i]->stats().hitsReceived >= 4 && bots[i]->stats().respawns > 0 &&
              bots[i]->stats().predictionResets >= 2,
          "client damage/lifecycle/reset delivery");
    check(bots[i]->life().generation > 0, "client respawn baseline");
  }
  const auto bytes = server.stats().combatBytes;
  if (seconds >= 120 && warmRss && maxRss)
    check(maxRss < warmRss + 16384, "soak RSS growth >16MiB");
  for (auto &b : bots)
    b->disconnect();
  for (unsigned i = 0; i < 300 && !server.world().players().empty(); ++i) {
    server.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  check(server.world().players().empty() &&
            server.world().combat().projectiles().empty(),
        "network combat leak");
  Transport::conditions("local");
  std::printf("combat GNS preset=%s seconds=%u clients=%u shots=%llu hits=%llu "
              "kills=%llu respawns=%llu peakRounds=%zu peakEvents=%zu "
              "peakFireQueue=%zu combatFanoutBps=%.1f maxHitDeliveryDelay=%.3f "
              "firstFireAdmission=%.3f rssWarmKiB=%zu rssPeakKiB=%zu "
              "remainingEntities=0 remainingRounds=0\n",
              preset.c_str(), seconds, clients,
              (unsigned long long)combat.shots, (unsigned long long)combat.hits,
              (unsigned long long)combat.kills,
              (unsigned long long)combat.respawns, combat.peakProjectiles,
              combat.peakEvents, combat.peakFireQueue, double(bytes) / seconds,
              maxLatency, double(firstShot - firstRequest) * tickSeconds,
              warmRss, maxRss);
}
void abuse() {
  ServerConfig config;
  config.bind = "127.0.0.1";
  config.port = 0;
  Server server(config);
  Client a("abuse", AircraftType::Typhoon), b("target", AircraftType::Typhoon);
  a.connect("127.0.0.1", server.port());
  b.connect("127.0.0.1", server.port());
  auto pump = [&](unsigned ticks) {
    for (unsigned i = 0; i < ticks; ++i) {
      server.poll();
      server.step();
      a.poll(tickSeconds);
      b.poll(tickSeconds);
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
  };
  pump(60);
  check(a.ready() && b.ready(), "abuse handshake");
  Message m;
  m.type = Type::Fire;
  m.entity = a.entity();
  m.tick = server.world().tick() + 1;
  m.sequence = 1;
  m.fire = {1, m.tick, 0, 0, true};
  // Duplicates and reordering through actual GNS cannot increase firing rate.
  for (unsigned i = 0; i < 30; ++i)
    a.sendTestPacket(encode(m));
  pump(30);
  check(server.world().combat().stats().shots <= 31u / static_cast<unsigned>(std::max(1, int(std::ceil(7200/aircraftDefinition(AircraftType::Typhoon).gun->rpm)))) + 1u,
        "duplicate firing network");
  const auto id = a.entity();
  m.entity = b.entity();
  a.sendTestPacket(encode(m)); // spoof ownership
  m.entity = id;
  m.fire.weapon = 2;
  a.sendTestPacket(encode(m));
  m.fire.weapon = 0;
  m.fire.tick = m.tick = server.world().tick() + 10000;
  a.sendTestPacket(encode(m));
  m.fire.tick = m.tick = server.world().tick();
  m.fire.sequence = m.sequence = 100000;
  a.sendTestPacket(encode(m));
  m.fire.sequence = m.sequence = 2;
  m.fire.generation = 99;
  a.sendTestPacket(encode(m));
  auto bytes = encode(m);
  bytes[53] = 255;
  a.sendTestPacket(bytes);
  a.sendTestPacket(std::vector<std::uint8_t>{0, 1, 2});
  bytes = encode(m);
  bytes.push_back(0);
  a.sendTestPacket(bytes);
  pump(60);
  check(!a.ready() && b.ready() && server.stats().invalid >= 8 &&
            !server.world().players().contains(id),
        "abuse strikes/session removal");
  // Fresh session hits dedicated fire-message limiter before generic ingress
  // cap.
  a.connect("127.0.0.1", server.port());
  pump(60);
  check(a.ready(), "spam reconnect");
  m.entity = a.entity();
  m.fire.generation = 0;
  m.fire.weapon = 0;
  for (unsigned i = 1; i <= 80; ++i) {
    m.sequence = m.fire.sequence = i;
    m.tick = m.fire.tick = server.world().tick() + 1;
    a.sendTestPacket(encode(m));
  }
  pump(60);
  check(!a.ready() && b.ready(), "fire flood disconnect");
  std::puts(
      "combat GNS abuse: duplicate/reordered rate, spoofed aircraft, invalid "
      "weapon/bool/generation, future tick/sequence, malformed/trailing, fire "
      "flood, isolated offender disconnect PASS");
}
void soak() { network("moderate", 180, 4); }
void graphicalServer() {
  ServerConfig config;
  config.bind = "127.0.0.1";
  config.port = 27021;
  Server server(config);
  std::map<EntityId, std::uint32_t> generation;
  auto start = std::chrono::steady_clock::now(), next = start;
  const auto period =
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(tickSeconds));
  while (std::chrono::steady_clock::now() - start < std::chrono::seconds(24)) {
    server.poll();
    const auto now = std::chrono::steady_clock::now();
    if (server.world().players().size() == 2) {
      auto it = server.world().players().begin();
      const auto a = it++->first, b = it->first;
      const auto aa = server.world().aircraft(a),
                 bb = server.world().aircraft(b);
      if (aa.life.alive() && bb.life.alive() &&
          (!generation.contains(a) || generation[a] != aa.life.generation ||
           generation[b] != bb.life.generation)) {
        if ((aa.life.generation+bb.life.generation)%2==0)
          align(server.world(), a, b);
        else
          align(server.world(), b, a);
        generation[a] = aa.life.generation;
        generation[b] = bb.life.generation;
      }
    }
    while (now >= next) {
      server.step();
      next += period;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto &s = server.world().combat().stats();
  check(s.hits >= 8 && s.kills >= 2 && s.respawns >= 2,
        "graphical authoritative combat");
  std::printf("graphical server shots=%llu hits=%llu kills=%llu respawns=%llu "
              "peakRounds=%zu PASS\n",
              (unsigned long long)s.shots, (unsigned long long)s.hits,
              (unsigned long long)s.kills, (unsigned long long)s.respawns,
              s.peakProjectiles);
}
void profile() {
  std::puts("players,shots,hits,kills,respawns,peakRounds,tickMeanUs,"
            "motionMeanUs,collisionMeanUs,eventEncodeUsPerTick,"
            "eventBytesPerSecondPerClient,snapshotBytesPerSecondPerClient");
  for (unsigned n : {2, 8, 16}) {
    World world;
    std::vector<EntityId> ids;
    for (unsigned i = 0; i < n; ++i)
      ids.push_back(world.join(AircraftType::Typhoon));
    double total = 0, motion = 0, collision = 0, encoding = 0;
    std::uint64_t bytes = 0;
    // 60 simulated seconds; all fire parallel, deliberately miss to retain
    // rounds.
    for (Tick t = 0; t < 7200; ++t) {
      for (auto id : ids)
        if (t % 12 == 0) {
          const auto a = world.aircraft(id);
          world.enqueueFire(
              id, {t / 12 + 1, world.tick() + 1, a.life.generation, 0, true});
        }
      world.step();
      total += world.stats().lastTickUs;
      motion += world.combat().stats().motionUs;
      collision += world.combat().stats().collisionUs;
      auto events = world.combat().takeEvents();
      if (!events.empty()) {
        Message m;
        m.type = Type::Combat;
        m.tick = world.tick();
        auto begin = std::chrono::steady_clock::now();
        for(std::size_t offset=0;offset<events.size();offset+=maxCombatEvents) {
          const auto end=std::min(events.size(),offset+maxCombatEvents);
          m.events.assign(events.begin()+offset,events.begin()+end);
          auto wire=encode(m);check(wire.size()<=snapshotPayload,"combat MTU");bytes+=wire.size();
        }
        encoding += std::chrono::duration<double, std::micro>(
                        std::chrono::steady_clock::now() - begin).count();
      }
    }
    const auto &s = world.combat().stats();
    std::printf("%u,%llu,%llu,%llu,%llu,%zu,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f\n", n,
                (unsigned long long)s.shots, (unsigned long long)s.hits,
                (unsigned long long)s.kills, (unsigned long long)s.respawns,
                s.peakProjectiles, total / 7200, motion / 7200,
                collision / 7200, encoding / 7200, double(bytes) / 60,
                double(encode(world.snapshot()).size()) * 24);
    world.enqueueFire(ids[0], {601, world.tick() + 1, 0, 0, false});
    for (auto id : ids)
      world.leave(id);
    check(world.combat().projectiles().empty(), "profile leak");
  }
}
} // namespace
int main(int argc, char **argv) {
  try {
    const std::string suite = argc > 1 ? argv[1] : "unit";
    if (suite == "unit")
      unit();
    else if (suite == "lifecycle")
      lifecycle();
    else if (suite == "protocol")
      codec();
    else if (suite == "abuse")
      abuse();
    else if (suite == "soak")
      soak();
    else if (suite == "profile")
      profile();
    else if (suite == "graphical-server")
      graphicalServer();
    else if (suite == "local" || suite == "good" || suite == "moderate" ||
             suite == "bad")
      network(suite, 20);
    else
      throw std::runtime_error("unknown combat suite");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "COMBAT FAIL: %s\n", e.what());
    return 1;
  }
}
