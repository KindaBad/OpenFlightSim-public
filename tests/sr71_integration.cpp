#include "animation.hpp"
#include "gltf.hpp"
#include "mesh.hpp"
#include "ofs/net/client.hpp"
#include "ofs/net/server.hpp"
#include "scenario.hpp"
#include <chrono>
#include <cstdio>
#include <limits>
#include <set>
#include <thread>
using namespace ofs;
using namespace ofs::net;
using namespace ofs::client;
using scenario::check;
void registry() {
  check(aircraftDefinitions().size() >= 3 &&
            aircraftTypeFromName("sr71") == AircraftType::SR71 &&
            static_cast<unsigned>(AircraftType::SR71) == 4,
        "stable reconnaissance aircraft");
  const auto &d = aircraftDefinition(AircraftType::SR71);
  check(!d.gun && d.flight.engine_count == 2 && d.flight.variable_inlets,
        "unarmed twin reconnaissance aircraft");
  check(d.flight.control_law == FlightControlLaw::Delta &&
            d.flight.flap_max_deg == 0 && d.flight.surfaces[2].position.x < 0,
        "dedicated trailing elevon configuration");
  for (const auto &b : d.collision)
    check(b.radius > 0 && b.center.norm() + b.radius < d.visual.radius,
          "bounded specific hitboxes");
  World w;
  std::uint64_t sr = 0;
  for (unsigned i = 0; i < maxPlayers; ++i) {
    const auto id =
        w.join(aircraftDefinitions()[i % aircraftDefinitions().size()].type);
    check(id != 0, "64 registry-driven spawns");
    if (w.aircraft(id).type == AircraftType::SR71)
      sr = id;
  }
  check(!w.join(AircraftType::SR71), "capacity rejects excess");
  const auto rejected = w.combat().stats().rejectedFire;
  check(w.aircraft(sr).life.ammo == 0 &&
            w.enqueueFire(sr, {1, w.tick() + 1, 0, 0, true}) &&
            w.combat().stats().rejectedFire == rejected + 1,
        "server ignores SR71 fire and records rejection like A320");
  w.step();
  check(w.combat().projectiles().empty(), "no unarmed projectile");
  Message m = w.snapshot(), decoded;
  std::string reason;
  for (auto &a : m.aircrafts)
    if (a.type == AircraftType::SR71) {
      a.state.inlet_spike[0] = .75;
      a.state.inlet_spike[1] = .25;
    }
  check(decode(encode(m), decoded, reason) &&
            decoded.aircrafts.size() == maxPlayers,
        "64 aircraft snapshot");
  for (std::size_t i = 0; i < m.aircrafts.size(); ++i) {
    check(m.aircrafts[i].type == decoded.aircrafts[i].type, "type identity");
    check(m.aircrafts[i].state.inlet_spike[0] ==
                  decoded.aircrafts[i].state.inlet_spike[0] &&
              m.aircrafts[i].state.inlet_spike[1] ==
                  decoded.aircrafts[i].state.inlet_spike[1],
          "independent inlet memory roundtrip");
  }
  m.aircrafts.resize(1);
  for (double bad : {-.01, 1.01, std::numeric_limits<double>::quiet_NaN(),
                     std::numeric_limits<double>::infinity()}) {
    m.aircrafts[0].state.inlet_spike[0] = bad;
    check(!decode(encode(m), decoded, reason), "invalid inlet memory rejected");
  }
  auto old = encode(w.snapshot());
  old[4] = 5;
  check(!decode(old, decoded, reason), "old wire version rejected cleanly");
  std::puts("SR71 stable ID4, 64 mixed spawns, unarmed fire rejection and "
            "independent inlet protocol PASS");
}
void assets(const std::string &root) {
  const auto &d = aircraftDefinition(AircraftType::SR71);
  if (scenario::skipMissingAsset(root, d.modelAsset))
    return;
  auto base = loadGltf(root + "/" + std::string(d.modelAsset));
  check(base.valid(), "SR71 GLB valid");
  std::set<std::string> channels, materials;
  for (const auto &n : base.nodes)
    if (!n.channel.empty())
      channels.insert(n.channel);
  for (const auto &m : base.materials)
    materials.insert(m.name);
  for (const auto *name :
       {"elevon_L", "elevon_R", "rudder", "gear_fold", "gear_door", "wheel",
        "nose_wheel", "steering", "inlet_L", "inlet_R", "nozzle_L", "nozzle_R"})
    check(channels.contains(name), "required rig channel");
  check(base.images.size() >= 12, "authored PBR images loaded");
  auto built = buildGpuMesh(base, nullptr, 1);
  auto previous = built.levels[0].triangleCount;
  std::printf(
      "SR71 LOD0 triangles=%llu primitives=%zu nodes=%zu textures=%zu\n",
      (unsigned long long)previous, base.primitives.size(), base.nodes.size(),
      base.images.size());
  for (const auto path : d.lodAssets) {
    auto low = loadGltf(root + "/" + std::string(path));
    check(low.valid() && low.images.empty(), "geometry-only LOD");
    for (const auto &m : low.materials)
      check(materials.contains(m.name), "shared material identity");
    for (const auto &n : low.nodes)
      if (!n.channel.empty())
        check(channels.contains(n.channel), "shared animation channel");
    auto b = buildGpuMesh(low, nullptr, 1);
    check(b.levels[0].triangleCount < previous, "strict LOD reduction");
    previous = b.levels[0].triangleCount;
    std::printf("SR71 reduced triangles=%llu primitives=%zu\n",
                (unsigned long long)previous, low.primitives.size());
  }
  check(previous < 8000, "cheap silhouette");
  AircraftPose pose;
  State s;
  Controls c;
  pose.update(s, c, d, 0);
  std::vector<AssetMatrix> a, b;
  evaluatePose(base.nodes, pose, a);
  s.inlet_spike[0] = 1;
  s.afterburner[0] = 1;
  pose.update(s, c, d, 1);
  evaluatePose(base.nodes, pose, b);
  unsigned spike = 0, left = 0, right = 0;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a[i] != b[i]) {
      if (base.nodes[i].channel == "inlet_L")
        ++spike;
      if (base.nodes[i].channel == "nozzle_L")
        ++left;
      if (base.nodes[i].channel == "nozzle_R")
        ++right;
    }
  check(spike == 1 && left == 24 && right == 0,
        "spike and 24 petals independently articulated");
}
void replay() {
  const auto &cfg = aircraftDefinition(AircraftType::SR71).flight;
  TrimRequest r;
  r.altitude = 25000;
  r.tas = 950;
  auto t = solveTrim(cfg, r);
  check(t.converged, "replay trim");
  Simulator a(cfg);
  a.setState(t.state);
  auto c = t.controls;
  c.throttle[0] = 1;
  c.throttle[1] = .7;
  a.setControls(c);
  scenario::advance(a, 80);
  Message m;
  m.type = Type::Snapshot;
  Aircraft record;
  record.id = 1;
  record.type = AircraftType::SR71;
  record.state = a.state();
  record.controls = c;
  m.aircrafts = {record};
  Message decoded;
  std::string reason;
  check(decode(encode(m), decoded, reason), "replay state decode");
  Simulator b(cfg);
  b.setState(decoded.aircrafts[0].state);
  b.setControls(decoded.aircrafts[0].controls);
  Simulator exact(cfg);
  exact.setState(a.state());
  exact.setControls(a.controls());
  scenario::advance(a, 1200);
  scenario::advance(b, 1200);
  scenario::advance(exact, 1200);
  const double packetError = (a.state().pos_ned - b.state().pos_ned).norm();
  const double memoryError = (a.state().pos_ned - exact.state().pos_ned).norm();
  check(memoryError < 1e-8 &&
            a.state().inlet_spike[0] == exact.state().inlet_spike[0] &&
            std::abs(a.state().fuel_mass - exact.state().fuel_mass) < 1e-8,
        "complete native memory replays");
  check(packetError < .01 &&
            (a.state().vel_ned - b.state().vel_ned).norm() < .001,
        "packet float rounding remains bounded");
  std::printf(
      "SR71 10s native replay positionError=%.9g packetError=%.9g PASS\n",
      memoryError, packetError);
}
void multiplayer() {
  ServerConfig config;
  config.bind = "127.0.0.1";
  config.port = 0;
  Server server(config);
  Client civil("A320", AircraftType::A320),
      su57("Su57", AircraftType::Su57),
      typhoon("Typhoon", AircraftType::Typhoon),
      sr71("SR71", AircraftType::SR71);
  const std::array<Client *, 4> clients{&civil, &su57, &typhoon, &sr71};
  for (auto *c : clients)
    c->connect("127.0.0.1", server.port());
  auto start = std::chrono::steady_clock::now(), next = start, last = start;
  double acc = 0;
  auto period = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(tickSeconds));
  while (std::chrono::steady_clock::now() - start < std::chrono::seconds(12)) {
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - last).count();
    last = now;
    server.poll();
    while (now >= next) {
      server.step();
      next += period;
    }
    bool ready = true;
    for (auto *c : clients) {
      c->poll(dt);
      ready &= c->ready();
    }
    if (ready) {
      acc += dt;
      for (auto *c : clients)
        c->setFiring(true);
      while (acc >= tickSeconds) {
        for (auto *c : clients) {
          auto control = c->prediction().simulator().controls();
          if (c == &sr71) {
            control.throttle[0] = 1;
            control.throttle[1] = .82;
          }
          c->predict(control);
        }
        acc -= tickSeconds;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  for (auto *c : clients)
    check(c->ready() && c->remotes().size() == 3, "four live clients");
  for (auto *c : {&civil, &su57, &typhoon}) {
    auto remote =
        c->remotes().at(sr71.entity()).sampleAircraft(c->stats().renderTick);
    check(remote.type == AircraftType::SR71 &&
              remote.state.afterburner[0] > .98 &&
              remote.state.afterburner[1] < .001,
          "remote independent SR71 engine state");
  }
  check(sr71.life().ammo == 0 && civil.life().ammo == 0 &&
            su57.life().ammo < aircraftDefinition(AircraftType::Su57).gun->ammo && typhoon.life().ammo < 150,
        "four-type authoritative capabilities");
  std::printf("SR71 live four-type GNS12s snapshots=%llu/%llu/%llu/%llu "
              "AB=%.6f/%.6f ammoSR71=%u shots=%llu\n",
              (unsigned long long)civil.stats().snapshots,
              (unsigned long long)su57.stats().snapshots,
              (unsigned long long)typhoon.stats().snapshots,
              (unsigned long long)sr71.stats().snapshots,
              server.world().aircraft(sr71.entity()).state.afterburner[0],
              server.world().aircraft(sr71.entity()).state.afterburner[1],
              sr71.life().ammo,
              (unsigned long long)server.world().combat().stats().shots);
  for (auto *c : clients)
    c->disconnect();
}
int main(int argc, char **argv) {
  try {
    check(argc >= 2, "expected suite");
    const std::string s = argv[1];
    if (s == "registry")
      registry();
    else if (s == "assets") {
      check(argc == 3, "expected root");
      assets(argv[2]);
    } else if (s == "replay")
      replay();
    else if (s == "multiplayer")
      multiplayer();
    else
      throw std::invalid_argument("unknown suite");
    return scenario::assetResult();
  } catch (const std::exception &e) {
    std::fprintf(stderr, "SR71 INTEGRATION FAIL: %s\n", e.what());
    return 1;
  }
}
