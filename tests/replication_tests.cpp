#include "ofs/net/client.hpp"
#include "ofs/net/replication.hpp"
#include "ofs/trim.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <stdexcept>
using namespace ofs;
using namespace ofs::net;
namespace {
void check(bool value, const char *text) {
  if (!value)
    throw std::runtime_error(text);
}
Aircraft aircraft(EntityId id = 1, AircraftType type = AircraftType::Su57) {
  auto t = solveTrim(aircraftDefinition(type).flight);
  check(t.converged, "trim");
  return {id,
          0,
          t.state,
          t.controls,
          {100, std::uint16_t(aircraftDefinition(type).gun ? 600 : 0)},
          type};
}
double angle(Quat a, Quat b) {
  return 2 *
         std::acos(std::clamp(
             std::abs(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z), 0., 1.));
}
void quantization() {
  std::mt19937_64 rng(371);
  std::uniform_real_distribution<double> unit(-1, 1);
  double pe = 0, qe = 0, ve = 0, we = 0, ce = 0, ne = 0, ee = 0, se = 0,
         massError = 0, offsetError = 0, cgError = 0, unitInputError = 0,
         healthError = 0, integrityError = 0;
  for (unsigned i = 0; i < 20000; ++i) {
    auto a = aircraft();
    a.state.pos_ned = {unit(rng) * 80000, unit(rng) * 80000, unit(rng) * 80000};
    a.state.att = Quat{unit(rng), unit(rng), unit(rng), unit(rng)}.normalized();
    a.state.vel_ned = {unit(rng) * 2000, unit(rng) * 2000, unit(rng) * 2000};
    a.state.omega_body = {unit(rng) * 15, unit(rng) * 15, unit(rng) * 15};
    a.controls.maneuver_mode = i%2;
    a.controls.elevator_stick = unit(rng);
    a.controls.throttle[0] = (unit(rng) + 1) / 2;
    a.state.nozzle_angle[0] =
        unit(rng) * aircraftDefinition(a.type).flight.engines[0].vector_limit;
    a.state.n1[0] = (unit(rng) + 1) / 2;
    a.state.elevator = unit(rng);
    a.state.fuel_mass = (unit(rng) + 1) * 15000;
    a.state.payload_mass = (unit(rng) + 1) * 20000;
    a.state.payload_offset = {unit(rng) * 5, unit(rng) * 5, unit(rng) * 5};
    a.life.health = (unit(rng) + 1) * 50;
    a.state.surface_health[5] = (unit(rng) + 1) / 2;
    auto s = projectAircraft(a, false, Tier::Near, 17, {});
    const auto &constState = s;
    check(constState.fields[0][0] == 0, "read-only relative position mode");
    Aircraft b;
    check(expandAircraft(a.id, s, {}, b), "quantized expansion");
    check(a.controls.maneuver_mode==b.controls.maneuver_mode,"remote maneuver presentation");
    massError =
        std::max({massError, std::abs(a.state.fuel_mass - b.state.fuel_mass),
                  std::abs(a.state.payload_mass - b.state.payload_mass)});
    offsetError = std::max(
        offsetError, (a.state.payload_offset - b.state.payload_offset).norm());
    const auto &cfg = aircraftDefinition(a.type).flight;
    cgError = std::max(
        cgError, (loadedCg(cfg, a.state) - loadedCg(cfg, b.state)).norm());
    pe = std::max(pe, (a.state.pos_ned - b.state.pos_ned).norm());
    qe = std::max(qe, angle(a.state.att, b.state.att));
    ve = std::max(ve, (a.state.vel_ned - b.state.vel_ned).norm());
    we = std::max(we, (a.state.omega_body - b.state.omega_body).norm());
    auto c = quantizeControls(a.controls);
    ce = std::max(ce, std::abs(c.elevator_stick - a.controls.elevator_stick));
    unitInputError = std::max(unitInputError,
                              std::abs(c.throttle[0] - a.controls.throttle[0]));
    healthError =
        std::max(healthError, std::abs(a.life.health - b.life.health));
    integrityError =
        std::max(integrityError, std::abs(airframeIntegrity(a.state) -
                                          airframeIntegrity(b.state)));
    ne = std::max(ne,
                  std::abs(a.state.nozzle_angle[0] - b.state.nozzle_angle[0]));
    ee = std::max(ee, std::abs(a.state.n1[0] - b.state.n1[0]));
    se = std::max(se, std::abs(a.state.elevator - b.state.elevator));
    check(finiteState(b.state), "quantization finite");
    if (i < 50) {
      auto exact = projectAircraft(a, true, Tier::Owner, 17, {});
      Aircraft restored;
      check(expandAircraft(a.id, exact, {}, restored), "owner exact expansion");
      check((a.state.pos_ned - restored.state.pos_ned).norm() == 0 &&
                (a.state.vel_ned - restored.state.vel_ned).norm() == 0 &&
                angle(a.state.att, restored.state.att) < 1e-7,
            "owner precision");
    }
  }
  check(pe <= std::sqrt(3.) * .005001 && qe < .0001 &&
            ve <= std::sqrt(3.) * .031251 &&
            we <= std::sqrt(3.) / 4096 + .000001 && ce <= .5 / 32767 + .0000001,
        "quantization bound");
  check(massError <= 1. / 32 &&
            offsetError <= std::sqrt(3.) * .0005 + .000001 && cgError < .001,
        "remote loading/CG precision");
  check(unitInputError <= .5 / 65534 + 1e-10 && healthError <= .005001 &&
            integrityError <= 1. / 510 + 1e-10,
        "control/life quantization bounds");
  std::printf("input_unit=%.9f health=%.9f integrity=%.9f\n", unitInputError,
              healthError, integrityError);
  std::printf(
      "loading mass_kg=%.9f payload_offset_norm_m=%.9f loaded_cg_norm_m=%.9f\n",
      massError, offsetError, cgError);
  auto a = aircraft();
  a.state.pos_ned = {1e9 + .125, -1e9 - .75, -1000};
  a.state.vel_ned = {1e6, -1e6, 0};
  a.state.omega_body = {1e3, -1e3, 0};
  Aircraft b;
  auto s = projectAircraft(a, false, Tier::Far, 7, {});
  check(s.fields[0].size() == 25 && expandAircraft(a.id, s, {}, b) &&
            b.state.pos_ned.x == a.state.pos_ned.x,
        "absolute fallback");
  check(b.state.vel_ned.x == 32767. / 16 && b.state.vel_ned.y == -32767. / 16 &&
            b.state.omega_body.x == 32767. / 2048,
        "saturation without wrap");
  for (double integrity : {0., .0001, .001, .0012, .1, 1.}) {
    a.state.surface_health[5] = integrity;
    check(expandAircraft(a.id, projectAircraft(a, false, Tier::Far, 7, {}), {},
                         b) &&
              aircraftCrashed(a.state) == aircraftCrashed(b.state) &&
              std::abs(airframeIntegrity(a.state) -
                       airframeIntegrity(b.state)) <= 1. / 510,
          "physical crash presentation classification");
  }
  for (double health : {0., .0001, .001, .0049, .005, .01, 1., 100.}) {
    a.life.health = health;
    check(expandAircraft(a.id, projectAircraft(a, false, Tier::Near, 7, {}), {},
                         b) &&
              a.life.alive() == b.life.alive() &&
              std::abs(a.life.health - b.life.health) <= .005001,
          "exact life classification at health quantization boundary");
  }
  a.state.payload_offset = {19.99999, .019, 0};
  auto edge = projectAircraft(a, false, Tier::Near, 7, {});
  check(expandAircraft(a.id, edge, {}, b) &&
            b.state.payload_offset.norm() <= 20 &&
            (a.state.payload_offset - b.state.payload_offset).norm() < .000867,
        "payload offset sphere boundary quantization");
  edge.fields[8][6] = 0x4e;
  edge.fields[8][7] = 0x2a; // Corrupt X to20.010 m.
  check(!expandAircraft(a.id, edge, {}, b),
        "payload offset outside quantization tolerance");
  for (auto quaternion : {Quat{1, 0, 0, 0}, Quat{0, 1, 0, 0}, Quat{0, 0, 1, 0},
                          Quat{0, 0, 0, -1}}) {
    a.state.att = quaternion;
    check(expandAircraft(a.id, projectAircraft(a, false, Tier::Far, 7, {}), {},
                         b) &&
              angle(quaternion, b.state.att) < 1e-7,
          "quaternion endpoints");
  }
  s.fields[1][0] = 4;
  check(!expandAircraft(a.id, s, {}, b), "invalid quaternion index");
  std::printf(
      "quantization samples=20000 position_norm_m=%.9f quaternion_rad=%.9f "
      "velocity_norm_mps=%.9f omega_norm_radps=%.9f input_axis=%.9f "
      "nozzle_rad=%.9f engine=%.9f surface=%.9f\n",
      pe, qe, ve, we, ce, ne, ee, se);
}
struct Fixture {
  std::vector<Aircraft> aircrafts;
  InterestGrid grid;
  ReplicationSender sender;
  ReplicationReceiver receiver;
  Fixture(unsigned count = 64) {
    for (unsigned i = 0; i < count; ++i) {
      auto a = aircraft(i + 1, (i % 4 == 0   ? AircraftType::A320
                                : i % 4 == 1 ? AircraftType::Typhoon
                                : i % 4 == 2 ? AircraftType::SR71
                                             : AircraftType::Su57));
      if (!validAircraftType(a.type))
        a = aircraft(i + 1);
      a.state.pos_ned = {double(i) * 50, 0, -3000};
      aircrafts.push_back(a);
    }
    grid.rebuild(aircrafts);
  }
  ReplicationOutput build(Tick t) {
    grid.rebuild(aircrafts);
    return sender.build(t, 1, aircrafts, grid);
  }
  void lifecycle(const ReplicationOutput &o) {
    for (const auto &e : o.lifecycle)
      if (e.type == Type::Spawn)
        receiver.spawn(e.aircraft, e.tick);
      else
        receiver.despawn(e.entity, e.tick);
  }
  std::optional<NetFrame> apply(const ReplicationOutput &o, Tick t) {
    lifecycle(o);
    std::optional<NetFrame> f;
    for (const auto &p : o.packets) {
      check(p.size() <= snapshotPayload, "MTU");
      auto value = receiver.receive(p, t);
      if (value)
        f = value;
    }
    if (f)
      sender.acknowledge(receiver.acknowledged());
    return f;
  }
};
void packetization() {
  Fixture f;
  auto o = f.build(5);
  check(o.packets.size() > 1, "cluster must chunk");
  f.lifecycle(o);
  check(!f.receiver.receive(o.packets.back(), 5), "partial applied");
  check(!f.receiver.receive(o.packets.back(), 5), "duplicate applied");
  std::optional<NetFrame> frame;
  for (std::size_t i = o.packets.size() - 1; i > 0; --i) {
    auto v = f.receiver.receive(o.packets[i - 1], 5);
    if (v)
      frame = v;
  }
  check(frame && frame->entities.size() == 64 &&
            f.receiver.stats().duplicates == 1,
        "reordered complete chunks");
  check(!f.receiver.receive(o.packets[0], 6), "stale frame applied");
  Fixture broken;
  auto bad = broken.build(5);
  broken.lifecycle(bad);
  auto bytes = bad.packets[0];
  bytes[32] = bytes[33];
  check(!broken.receiver.receive(bytes, 5) && broken.receiver.needsRecovery(),
        "invalid fragment index");
  bytes = bad.packets[0];
  bytes[33] = 255;
  check(!broken.receiver.receive(bytes, 5), "unbounded fragment count");
  for (std::size_t i = 0; i < bad.packets[0].size(); ++i)
    check(!broken.receiver.receive(std::span(bad.packets[0]).first(i), 5),
          "truncation");
  Fixture lost;
  auto first = lost.build(5);
  lost.lifecycle(first);
  lost.receiver.receive(first.packets[0], 5);
  check(lost.receiver.acknowledged() == 0, "lost chunk applied");
  check(lost.receiver.expire(130) && lost.receiver.assemblySize() == 0,
        "idle fragment expiry");
  auto refreshed = lost.build(130);
  check(lost.apply(refreshed, 130).has_value() &&
            lost.receiver.stats().expiredAssemblies > 0,
        "lost assembly expiry and refresh");
  Fixture fast(2);
  for (Tick tick = 2; tick <= 50; tick += 2) {
    auto frame = fast.apply(fast.build(tick), tick);
    check(frame && frame->entities.at(1).sampledTick == tick,
          "owner stays current above default publication frequency");
  }
  Fixture wrongOwner(2);
  auto impostor = wrongOwner.build(5);
  wrongOwner.lifecycle(impostor);
  check(!wrongOwner.receiver.receive(impostor.packets[0], 5, 2) &&
            wrongOwner.receiver.needsRecovery(),
        "compact/full owner identity mismatch");
  std::printf("packetization chunks=%zu maximum=%zu "
              "duplicate/reorder/lost/expiry/index/count/truncation PASS\n",
              o.packets.size(), f.sender.stats().maxChunk);
}
void recovery() {
  ReplicationReceiver limited;
  auto spawn = aircraft();
  for (EntityId id = 1; id <= maxPlayers; ++id) {
    spawn.id = id;
    check(limited.spawn(spawn, 5), "bounded lifecycle admission");
  }
  spawn.id = maxPlayers + 1;
  check(!limited.spawn(spawn, 5),
        "lifecycle capacity rejects presentation growth");
  limited.despawn(2, 10);
  spawn.id = 2;
  check(!limited.spawn(spawn, 9) && limited.spawn(spawn, 11),
        "stale spawn cannot resurrect a tombstone");
  Fixture f(8);
  auto key = f.build(5);
  check(f.apply(key, 5).has_value(), "initial keyframe");
  f.aircrafts[1].state.pos_ned.x += 10;
  auto delta = f.build(10);
  check(f.apply(delta, 10)->baseline != 0, "ACK baseline delta");
  ReplicationReceiver empty;
  for (const auto &a : f.aircrafts)
    empty.spawn(a, 0);
  for (const auto &b : delta.packets)
    empty.receive(b, 10);
  check(empty.needsRecovery() && empty.stats().baselineMisses == 1,
        "missing baseline clean failure");
  check(f.sender.acknowledge(0, true), "recovery request");
  auto refresh = f.build(35);
  std::optional<NetFrame> frame;
  for (const auto &b : refresh.packets) {
    auto n = empty.receive(b, 35);
    if (n)
      frame = n;
  }
  check(frame && !frame->baseline && !empty.needsRecovery(),
        "requested keyframe recovery");
  check(!f.sender.acknowledge(1000000), "future ACK");
  Fixture unknown(2);
  auto entry = unknown.build(5);
  for (const auto &p : entry.packets)
    unknown.receiver.receive(p, 5);
  check(unknown.receiver.acknowledged() == 0 &&
            unknown.receiver.needsRecovery(),
        "unknown entity cannot create itself");
  unknown.lifecycle(entry);
  check(unknown.apply(unknown.build(35), 35).has_value(),
        "reliable spawn recovery");
  for (Tick t = 40; t < 1000; t += 5)
    check(f.apply(f.build(t), t).has_value(), "history warmup");
  check(f.sender.historySize() == baselineLimit &&
            f.receiver.historySize() == baselineLimit &&
            f.sender.acknowledge(1),
        "history and expired ACK");
  auto periodic = f.build(1000);
  check(f.apply(periodic, 1000)->baseline != 0, "expired stale ACK is ignored");
  std::puts("recovery: ACK, missing/stale baseline, future ACK, unknown "
            "entity, periodic/requested keyframe, bounded history PASS");
}
void acknowledgements() {
  Fixture f(2);
  f.apply(f.build(5), 5);
  f.build(10);
  auto current = f.build(15);
  for (const auto &p : current.packets)
    f.receiver.receive(p, 15);
  check(f.sender.acknowledge(3) && f.sender.acknowledged() == 3, "current ACK");
  const auto misses = f.sender.stats().baselineMisses;
  check(f.sender.acknowledge(3) && f.sender.acknowledge(1) &&
            f.sender.acknowledge(2, true) && f.sender.acknowledged() == 3,
        "duplicate/older/reordered ACK cannot rewind");
  check(f.sender.stats().baselineMisses == misses &&
            f.apply(f.build(20), 20)->baseline == 3,
        "stale recovery flag cannot force keyframe");
  check(!f.sender.acknowledge(5) && !f.sender.acknowledge(UINT64_MAX, true),
        "future/invalid ACK");
  for (Tick t = 25; t < 1000; t += 5)
    f.apply(f.build(t), t);
  check(f.sender.acknowledge(1, true) &&
            f.sender.stats().baselineMisses == misses &&
            f.apply(f.build(1000), 1000)->baseline != 0,
        "expired old ACK ignored before history lookup");
  Fixture expired(2);
  expired.apply(expired.build(5), 5);
  for (Tick t = 10; t <= 400; t += 5)
    expired.build(t);
  check(expired.sender.acknowledge(2) && expired.sender.acknowledged() == 1 &&
            expired.sender.stats().baselineMisses == 1,
        "newer expired ACK requests bounded recovery without acceptance");
  check(nextSnapshotSequence(UINT64_MAX - 1) == UINT64_MAX,
        "last nonwrapping sequence");
  bool exhausted = false;
  try {
    (void)nextSnapshotSequence(UINT64_MAX);
  } catch (const std::overflow_error &) {
    exhausted = true;
  }
  check(exhausted, "counter exhaustion explicitly rejects wrap to zero");
  std::puts(
      "ACK current/duplicate/older/reorder/future/expired/exhaustion PASS");
}
void recoveryStorm() {
  // All input publications assert recovery, even when ACKs prove healthy.
  for (unsigned loss : {0u, 5u, 100u}) {
    Fixture f(2);
    std::mt19937 rng(3711 + loss);
    std::optional<ReplicationOutput> late;
    for (unsigned i = 0; i < 100; ++i) {
      const Tick t = (i + 1) * 5;
      check(f.sender.acknowledge(f.receiver.acknowledged(), true), "storm ACK");
      auto o = f.build(t);
      f.lifecycle(o);
      if (!i)
        late = o;
      for (const auto &p : o.packets)
        if (rng() % 100 >= loss)
          if (auto frame = f.receiver.receive(p, t))
            f.sender.acknowledge(frame->sequence);
      check(f.sender.historySize() <= baselineLimit &&
                f.receiver.historySize() <= baselineLimit &&
                f.receiver.assemblySize() <= assemblyLimit,
            "storm memory bound");
    }
    const auto &s = f.sender.stats();
    check(s.recoveryRequests == 100 && s.recoveryCoalesced >= 80 &&
              s.recoveryKeyframes <= 17 && s.keyframes <= 20,
          "100 recovery flags are rate bounded");
    if (loss < 100)
      check(s.recoveryCompleted > 0 && f.receiver.acknowledged() > 90,
            "healthy client still recovers under loss");
    else {
      check(s.recoveryRetries == s.recoveryKeyframes - 1,
            "missing recovery ACK retries bounded");
      // The first recovery frame arriving after retries is still usable.
      std::optional<NetFrame> applied;
      for (const auto &p : late->packets)
        if (auto frame = f.receiver.receive(p, 501))
          applied = frame;
      check(applied && f.sender.acknowledge(applied->sequence),
            "late expired ACK is handled safely");
      f.apply(f.build(515), 515);
      f.apply(f.build(520), 520);
      check(f.receiver.acknowledged() > 100 &&
                f.sender.stats().recoveryCompleted > 0,
            "loss stops: recovery resumes");
    }
    std::printf("storm loss=%u requests=%llu coalesced=%llu keyframes=%llu "
                "retries=%llu completed=%llu\n",
                loss, (unsigned long long)s.recoveryRequests,
                (unsigned long long)s.recoveryCoalesced,
                (unsigned long long)s.recoveryKeyframes,
                (unsigned long long)s.recoveryRetries,
                (unsigned long long)s.recoveryCompleted);
  }
  Fixture f(2);
  f.apply(f.build(5), 5);
  // Recovery ACK can precede old unreliable packets; their flags stay stale.
  f.sender.acknowledge(f.receiver.acknowledged(), true);
  f.apply(f.build(35), 35);
  f.sender.acknowledge(1, true);
  check(f.apply(f.build(40), 40)->baseline != 0,
        "old recovery after newer ACK");
  // Actual receiver baseline loss with an otherwise current sender ACK.
  f.receiver = ReplicationReceiver{};
  for (const auto &a : f.aircrafts)
    f.receiver.spawn(a, 0);
  auto missing = f.build(70);
  for (const auto &p : missing.packets)
    f.receiver.receive(p, 70);
  check(f.receiver.needsRecovery(), "actual baseline loss");
  f.sender.acknowledge(0, true);
  check(f.apply(f.build(75), 75).has_value() && !f.receiver.needsRecovery(),
        "reset baseline recovery");
}
void steering() {
  auto a = aircraft();
  Aircraft decoded;
  for (double value : {-1., 0., 1., -.435, .812}) {
    a.controls.steering = value;
    auto s = projectAircraft(a, false, Tier::Near, 5, {});
    check(expandAircraft(a.id, s, {}, decoded) &&
              std::abs(decoded.controls.steering - value) <= .5 / 127 + 1e-12,
          "steering endpoints and roundtrip");
  }
  std::mt19937 rng(3713);
  std::uniform_real_distribution<double> values(-1, 1);
  for (unsigned i = 0; i < 20000; ++i) {
    a.controls.steering = values(rng);
    check(expandAircraft(a.id, projectAircraft(a, false, Tier::Near, 5, {}), {},
                         decoded) &&
              std::abs(decoded.controls.steering - a.controls.steering) <=
                  .5 / 127 + 1e-12,
          "steering randomized roundtrip");
  }
  auto malformed = projectAircraft(a, false, Tier::Near, 5, {});
  malformed.fields[4][5] = 128;
  check(!expandAircraft(a.id, malformed, {}, decoded),
        "reserved steering code");
  Fixture f(2);
  auto first = f.apply(f.build(5), 5);
  auto unchanged = f.build(10);
  auto second = f.apply(unchanged, 10);
  check(!(second->entities.at(2).fields[4] != first->entities.at(2).fields[4]),
        "steering unchanged field");
  f.aircrafts[1].controls.steering = -1;
  auto changed = f.build(15);
  auto third = f.apply(changed, 15);
  check(
      changed.packets[0].size() > unchanged.packets[0].size() &&
          third->entities.at(2).fields[4] != second->entities.at(2).fields[4] &&
          expandAircraft(2, third->entities.at(2), third->reference, decoded) &&
          decoded.controls.steering == -1,
      "steering delta field mask changed");
}
void presentationSync() {
  RemoteTrack track;
  auto a = aircraft();
  for (Tick t = 0; t <= 240; t += 5) {
    const double x = double(t) / 240;
    a.state.pos_ned.x = double(t);
    a.state.time = double(t) * tickSeconds;
    a.controls.gear01 = x;
    a.controls.flap01 = x;
    a.controls.spoiler01 = x;
    a.controls.steering = 2 * x - 1;
    a.controls.throttle[0] = x;
    a.state.afterburner[0] = x;
    a.state.nozzle_angle[0] = x * .3;
    a.life.ammo = std::uint16_t(t);
    a.state.fcs_enabled = t >= 120;
    a.state.fuel_mass = double(t);
    track.push(t, a);
    track.setTier(t < 60    ? Tier::Near
                  : t < 120 ? Tier::Medium
                  : t < 180 ? Tier::Far
                            : Tier::Near);
    auto b = track.sampleAircraft(double(t) - 12);
    const double physicalTick = b.state.pos_ned.x;
    const double fraction = physicalTick / 240;
    check(std::abs(b.controls.gear01 - fraction) < 1e-10 &&
              std::abs(b.controls.flap01 - fraction) < 1e-10 &&
              std::abs(b.controls.spoiler01 - fraction) < 1e-10 &&
              std::abs(b.controls.throttle[0] - fraction) < 1e-10 &&
              std::abs(b.controls.steering - (2 * fraction - 1)) < 1e-10 &&
              std::abs(b.state.afterburner[0] - fraction) < 1e-10 &&
              std::abs(b.state.nozzle_angle[0] - fraction * .3) < 1e-10 &&
              b.life.ammo <= physicalTick &&
              physicalTick - b.life.ammo < 5.01 &&
              b.state.fuel_mass == b.life.ammo &&
              b.state.fcs_enabled == (b.life.ammo >= 120),
          "tier changes preserve coherent continuous/discrete metadata time");
  }
  // Direct State and Aircraft access resolve the same delay, without advancing
  // twice.
  auto b = track.sampleAircraft(230);
  check(std::abs(track.sample(230).pos_ned.x - b.state.pos_ned.x) < 1e-12,
        "same request samples resolve same presentation time");
}
void budgets() {
  check(withinApplicationBudget(snapshotPayload) &&
            !withinApplicationBudget(snapshotPayload + 1) &&
            !withinApplicationBudget(0),
        "exact application budget boundary");
  auto a = aircraft();
  NetFrame frame;
  frame.sequence = 1;
  frame.tick = 5;
  frame.entities[1] = projectAircraft(a, true, Tier::Owner, 5, {});
  auto &s = frame.entities[2];
  // The owner gained 48 bytes of distributed payload inertia in v13.
  // Preserve the exact 1100-byte boundary with a smaller synthetic record.
  unsigned left = 286;
  for (unsigned i = 0; i < 16; ++i) {
    unsigned n = std::min(left, NetFields::capacities[i]);
    s.fields[i] = Bytes(n, 0);
    left -= n;
  }
  auto packets = packetize(frame, nullptr);
  check(packets.size() == 1 && packets[0].size() == snapshotPayload,
        "exactly-at-limit encoded chunk");
  s.fields[15] = Bytes(1, 0);
  packets = packetize(frame, nullptr);
  check(packets.size() == 2, "limit plus one splits safely");
  for (const auto &p : packets)
    check(p.size() <= snapshotPayload, "split budget");
  frame.entities = {};
  for (unsigned i = 1; i <= fragmentLimit; ++i) {
    a.id = i;
    frame.entities[i] = projectAircraft(a, true, Tier::Owner, 5, {});
  }
  check(packetize(frame, nullptr).size() == fragmentLimit,
        "maximum chunk count");
  a.id = fragmentLimit + 1;
  frame.entities[a.id] = projectAircraft(a, true, Tier::Owner, 5, {});
  bool rejected = false;
  try {
    (void)packetize(frame, nullptr);
  } catch (const std::length_error &) {
    rejected = true;
  }
  check(rejected, "chunk count plus one explicit error");
  // Repartition a genuine keyframe into the maximum permitted chunk count.
  // It still has exactly one owner and all records retain their original bytes.
  Fixture maximum(64);
  auto full = maximum.build(5);
  maximum.lifecycle(full);
  std::vector<Bytes> records;
  for (const auto &packet : full.packets) {
    std::size_t offset = 99;
    for (unsigned record = 0; record < packet[34]; ++record) {
      const auto start = offset;
      const unsigned mask =
          unsigned(packet[offset + 18]) * 256 + packet[offset + 19];
      offset += 20;
      for (unsigned field = 0; field < 16; ++field)
        if (mask & (1u << field)) {
          const unsigned length =
              unsigned(packet[offset]) * 256 + packet[offset + 1];
          offset += 2 + length;
        }
      records.emplace_back(packet.begin() + start, packet.begin() + offset);
    }
    check(offset == packet.size(), "valid record partition");
  }
  std::optional<NetFrame> assembled;
  unsigned record = 0;
  for (unsigned chunk = 0; chunk < fragmentLimit; ++chunk) {
    Bytes packet(full.packets[0].begin(), full.packets[0].begin() + 99);
    const unsigned count = chunk == 0 ? 1 : chunk <= 12 ? 4 : 5;
    packet[32] = chunk;
    packet[33] = fragmentLimit;
    packet[34] = count;
    for (unsigned i = 0; i < count; ++i) {
      const auto &bytes = records.at(record++);
      packet.insert(packet.end(), bytes.begin(), bytes.end());
    }
    check(packet.size() <= snapshotPayload, "maximum valid chunks fit budget");
    if (auto received = maximum.receiver.receive(packet, 5, 1))
      assembled = received;
  }
  check(assembled && assembled->entities.size() == 64 &&
            record == records.size(),
        "maximum valid chunk count assembled atomically");
  Fixture f(2);
  auto o = f.build(5);
  f.lifecycle(o);
  auto bytes = o.packets[0];
  bytes[33] = fragmentLimit + 1;
  check(!f.receiver.receive(bytes, 5), "declared chunk count plus one");
  bytes = o.packets[0];
  bytes[119] = 255;
  bytes[120] = 255;
  check(!f.receiver.receive(bytes, 5), "malformed declared field size");
  bytes = o.packets[0];
  bytes.pop_back();
  check(!f.receiver.receive(bytes, 5), "truncated chunk");
  bytes.resize(snapshotPayload + 1);
  check(!f.receiver.receive(bytes, 5), "oversized receive");
  Message combat;
  combat.type = Type::Combat;
  combat.events.resize(maxCombatEvents + 1);
  rejected = false;
  try {
    (void)encode(combat);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  check(rejected, "oversized future combat batch explicit error");
}
void combatSizes() {
  Message fire;
  fire.type = Type::Fire;
  fire.entity = 1;
  fire.fire = {1, 5, 0, 0, true};
  fire.sequence = 1;
  fire.tick = 5;
  check(encode(fire).size() == 54, "fire intent size");
  std::printf("combat fire_intent=%zu", encode(fire).size());
  Message batch;
  batch.type = Type::Combat;
  batch.tick = 5;
  CombatEvent e;
  e.id = 1;
  e.tick = 5;
  e.owner = 1;
  e.target = 2;
  e.projectile = 1;
  e.health = 50;
  e.lifetime = 1;
  for (auto kind : {CombatKind::Shot, CombatKind::Hit, CombatKind::Destroyed,
                    CombatKind::Respawn}) {
    e.kind = kind;
    batch.events = {e};
    auto bytes = encode(batch);
    Message decoded;
    std::string why;
    check(bytes.size() == 127 && decode(bytes, decoded, why),
          "combat kind size");
    std::printf(" kind_%u=%zu", unsigned(kind), bytes.size());
  }
  batch.events.clear();
  for (unsigned i = 0; i < maxCombatEvents; ++i) {
    e.id = i + 1;
    batch.events.push_back(e);
  }
  auto bytes = encode(batch);
  check(bytes.size() == 1045 && bytes.size() <= applicationPayload,
        "maximum current combat batch budget");
  std::printf(" largest_batch=%zu budget=%zu\n", bytes.size(),
              applicationPayload);
}
void baselineFuzz() {
  Fixture f(8);
  std::mt19937 rng(3719);
  std::deque<std::pair<Tick, ReplicationOutput>> delayed;
  for (Tick t = 5; t <= 20000; t += 5) {
    const auto accepted = f.sender.acknowledged();
    const auto published = f.sender.stats().snapshots;
    std::uint64_t ack = rng() % (published + 20);
    if (rng() % 10 == 0)
      ack = UINT64_MAX;
    f.sender.acknowledge(ack, rng() % 3 != 0);
    check(f.sender.acknowledged() >= accepted, "fuzz ACK monotonicity");
    auto o = f.build(t);
    f.lifecycle(o);
    if (rng() % 3 == 0)
      delayed.push_back({t, o});
    for (auto p : o.packets) {
      if (rng() % 5 == 0)
        continue;
      if (rng() % 10 == 0) { // Unavailable/malformed baseline ID.
        for (unsigned b = 24; b < 32; ++b)
          p[b] = 255;
      }
      if (auto frame = f.receiver.receive(p, t))
        f.sender.acknowledge(frame->sequence, false);
    }
    if (!delayed.empty() && delayed.front().first + 40 <= t) {
      for (const auto &p : delayed.front().second.packets)
        f.receiver.receive(p, t);
      delayed.pop_front();
    }
    check(f.sender.historySize() <= baselineLimit &&
              f.receiver.historySize() <= baselineLimit &&
              f.receiver.assemblySize() <= assemblyLimit &&
              delayed.size() <= 9 && f.sender.memoryBytes() < 4'000'000 &&
              f.receiver.memoryBytes() < 4'000'000,
          "baseline fuzz bounded ownership");
  }
  check(f.sender.stats().recoveryKeyframes <= 667 &&
            f.sender.stats().rejectedAcks > 0 &&
            f.receiver.stats().snapshots > 100,
        "baseline fuzz keyframes bounded and progress");
  std::puts(
      "baseline fuzz publications=4000 ACK storms/loss/late/malformed PASS");
}

void interest() {
  check(interestTier(5001, Tier::Near) == Tier::Near &&
            interestTier(5501, Tier::Near) == Tier::Medium,
        "near hysteresis");
  check(interestTier(20001, Tier::Medium) == Tier::Medium &&
            interestTier(22001, Tier::Medium) == Tier::Far,
        "medium hysteresis");
  check(interestTier(50001, Tier::Far) == Tier::Far &&
            interestTier(55001, Tier::Far) == Tier::Outside,
        "far hysteresis");
  check(interestTier(4999, Tier::Far) == Tier::Near,
        "immediate combat promotion");
  Fixture f(4);
  f.aircrafts[1].state.pos_ned.x = 1000;
  f.aircrafts[2].state.pos_ned.x = 10000;
  f.aircrafts[3].state.pos_ned.x = 30000;
  std::array<unsigned, 4> updates{};
  std::array<Tick, 4> previous{};
  for (Tick t = 5; t <= 600; t += 5) {
    auto frame = f.apply(f.build(t), t);
    check(frame.has_value(), "tier frame");
    for (const auto &[id, s] : frame->entities) {
      if (s.sampledTick != previous[id - 1])
        ++updates[id - 1];
      previous[id - 1] = s.sampledTick;
    }
  }
  check(updates[0] == 120 && updates[1] == 120 && updates[2] >= 48 &&
            updates[2] <= 54 && updates[3] >= 10 && updates[3] <= 14,
        "tier update cadence");
  f.aircrafts[3].state.pos_ned.x = 100000;
  auto out = f.build(605);
  check(std::any_of(out.lifecycle.begin(), out.lifecycle.end(),
                    [](const auto &m) {
                      return m.type == Type::Despawn && m.entity == 4;
                    }),
        "AOI reliable exit");
  check(f.apply(out, 605)->entities.size() == 3,
        "out of interest snapshot state");
  // Despawn may overtake an older unreliable keyframe without resurrecting it.
  auto old = f.build(610);
  f.aircrafts[3].state.pos_ned.x = 30000;
  auto entry = f.build(615);
  check(f.apply(entry, 615)->entities.size() == 4, "AOI reliable entry");
  for (const auto &b : old.packets)
    check(!f.receiver.receive(b, 620), "stale AOI snapshot resurrected entity");
  std::printf("interest 5s owner/near/medium/far=%u/%u/%u/%u hysteresis "
              "lifecycle PASS\n",
              updates[0], updates[1], updates[2], updates[3]);
}
void interpolationTiers() {
  RemoteTrack track;
  auto a = aircraft();
  a.state.vel_ned = {120, 0, 0};
  double previous = 0, maxStep = 0;
  bool initialized = false;
  for (Tick t = 1; t <= 1800; ++t) {
    const auto tier = t < 600    ? Tier::Near
                      : t < 1200 ? Tier::Medium
                                 : Tier::Far;
    track.setTier(tier);
    if (t == 1 || t % tierPeriod(tier) == 0) {
      a.state.pos_ned = {double(t), 0, -3000};
      a.state.time = t * tickSeconds;
      track.push(t, a);
    }
    auto sample = track.sample(double(t) - 12);
    check(finiteState(sample), "tier interpolation finite");
    if (initialized) {
      const double step = std::abs(sample.pos_ned.x - previous);
      if (step > 1.25 + 1e-8)
        std::fprintf(
            stderr,
            "tier jump tick=%llu tier=%u previous=%.6f sample=%.6f step=%.6f\n",
            (unsigned long long)t, unsigned(tier), previous, sample.pos_ned.x,
            step);
      maxStep = std::max(maxStep, step);
    }
    previous = sample.pos_ned.x;
    initialized = true;
  }
  check(maxStep <= 1.25 + 1e-8 && track.size() <= 32,
        "tier transition smoothness");
  const auto frozen = track.sample(100000);
  const auto later = track.sample(200000);
  check((frozen.pos_ned - later.pos_ned).norm() < 1e-8,
        "tier extrapolation freeze");
  std::printf("interpolation tiers max_step_m=%.6f sample_bound=32 "
              "extrapolation=50ms PASS\n",
              maxStep);
}
void input() {
  for (unsigned n : {1, 2, 4, 8}) {
    Message m;
    m.type = Type::Input;
    m.entity = 1;
    m.generation = 3;
    m.baseline = 7;
    auto a = aircraft();
    for (unsigned i = 0; i < n; ++i)
      m.commands.push_back({100 + i, 200 + i, quantizeControls(a.controls)});
    auto bytes = encode(m);
    Message decoded;
    std::string why;
    check(decode(bytes, decoded, why) && decoded.commands.size() == n &&
              decoded.baseline == 7,
          "input roundtrip");
    check(bytes.size() == 46 + 27 * n && bytes.size() <= snapshotPayload,
          "input size");
    for (unsigned i = 0; i < n; ++i)
      check(sameControls(m.commands[i].controls, decoded.commands[i].controls),
            "input quantization idempotent");
    std::printf("input depth=%u before_bytes=%u after_bytes=%zu\n", n,
                37 + 60 * n, bytes.size());
  }
  Message m;
  m.type = Type::SnapshotAck;
  m.baseline = 3;
  auto bytes = encode(m);
  auto hugeTick = bytes;
  for (unsigned i = 8; i < 16; ++i)
    hugeTick[i] = 255;
  Message invalid;
  std::string invalidReason;
  check(!decode(hugeTick, invalid, invalidReason),
        "unrepresentable timeline tick");
  bytes[5] = 8;
  Message out;
  std::string why;
  check(!decode(bytes, out, why) && why == "protocol version mismatch",
        "v8 clean rejection");
}
void fuzz() {
  std::mt19937 rng(377);
  Fixture f(8);
  auto output = f.build(5);
  f.lifecycle(output);
  for (unsigned i = 0; i < 50000; ++i) {
    Bytes bytes;
    if (i % 2) {
      bytes = output.packets[rng() % output.packets.size()];
      for (unsigned n = 0; n < 1 + rng() % 5; ++n)
        bytes[rng() % bytes.size()] = std::uint8_t(rng());
    } else {
      bytes.resize(rng() % 1200);
      for (auto &v : bytes)
        v = std::uint8_t(rng());
    }
    f.receiver.receive(bytes, i / 20);
    check(f.receiver.assemblySize() <= assemblyLimit &&
              f.receiver.historySize() <= baselineLimit,
          "fuzz bounded storage");
  }
  std::printf("fuzz packets=50000 decode_failures=%llu assemblies=%zu "
              "history=%zu PASS\n",
              (unsigned long long)f.receiver.stats().decodeFailures,
              f.receiver.assemblySize(), f.receiver.historySize());
}
} // namespace
int main(int argc, char **argv) {
  try {
    std::string test = argc > 1 ? argv[1] : "quantization";
    if (test == "quantization")
      quantization();
    else if (test == "packetization")
      packetization();
    else if (test == "recovery")
      recovery();
    else if (test == "acknowledgements")
      acknowledgements();
    else if (test == "recovery_storm")
      recoveryStorm();
    else if (test == "steering")
      steering();
    else if (test == "presentation_sync")
      presentationSync();
    else if (test == "budgets")
      budgets();
    else if (test == "combat_sizes")
      combatSizes();
    else if (test == "baseline_fuzz")
      baselineFuzz();
    else if (test == "interest")
      interest();
    else if (test == "interpolation_tiers")
      interpolationTiers();
    else if (test == "input")
      input();
    else if (test == "fuzz")
      fuzz();
    else
      throw std::invalid_argument("unknown replication suite");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "REPLICATION FAIL %s\n", e.what());
    return 1;
  }
}
