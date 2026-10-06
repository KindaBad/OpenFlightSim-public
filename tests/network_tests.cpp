#include "ofs/net/client.hpp"
#include "ofs/net/server.hpp"
#include "ofs/trim.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <limits>
#include <random>
#include <stdexcept>
#include <thread>
using namespace ofs;
using namespace ofs::net;
namespace {
void check(bool ok, const char *reason) {
  if (!ok)
    throw std::runtime_error(reason);
}
Aircraft trimmed() {
  auto t = solveTrim(a320Config());
  check(t.converged, "test trim");
  return {7, 0, t.state, t.controls, {100, 0}, AircraftType::A320};
}
void protocol() {
  auto a = trimmed();
  a.controls.maneuver_mode = true;
  a.state.pos_ned = {1000000000.125, -2.5, -1000};
  a.state.fuel_mass = 4321.125;
  a.state.payload_mass = 2222.25;
  a.state.payload_offset = {.3, -.2, .1};
  a.state.elevator = .123;
  a.state.aileron = -.234;
  a.state.rudder = .345;
  a.state.flap = .456;
  a.state.spoiler = .567;
  a.state.pilot_pitch = .111;
  a.state.trim_reference = .021;
  a.state.engine_health[1] = .2;
  a.state.surface_health[0] = .4;
  a.state.surface_drag[3] = 1.7;
  a.state.aero_memory_initialized = true;
  a.state.alpha_lag[0] = .4;
  a.state.alpha_lag[1] = .45;
  a.state.separation[0] = .3;
  a.state.separation[1] = .35;
  a.state.vortex_state[0] = .2;
  a.state.vortex_state[1] = .25;
  for (auto type :
       {Type::Hello, Type::Welcome, Type::Joined, Type::Left, Type::Input,
        Type::Snapshot, Type::Reject, Type::Ping, Type::Pong}) {
    Message m;
    m.type = type;
    m.tick = 0x0000030405060708ULL;
    m.sequence = 9;
    m.entity = 7;
    m.text = "test";
    m.aircraft = a;
    m.aircrafts = {a};
    m.commands = {{1, 25, a.controls}};
    auto bytes = encode(m);
    if (type == Type::Input) {
      m.tick = m.commands.back().tick;
      m.sequence = m.commands.back().sequence;
      bytes = encode(m);
    }
    check(bytes[0] == 0x4f && bytes[3] == 0x4e && bytes[4] == 0 &&
              bytes[5] == protocolVersion &&
              (type == Type::Input ||
               (bytes[8] == 0 && bytes[10] == 3 && bytes[15] == 8)),
          "golden big endian header");
    Message n;
    std::string reason;
    check(decode(bytes, n, reason), "roundtrip");
    check(n.tick == m.tick && n.type == m.type, "header roundtrip");
    if(type==Type::Input)check(n.commands[0].controls.maneuver_mode,"compact maneuver input roundtrip");
    if(type==Type::Snapshot)check(n.aircrafts[0].controls.maneuver_mode,"full maneuver snapshot roundtrip");
    if (type == Type::Snapshot)
      check(n.aircrafts[0].state.pos_ned.x == a.state.pos_ned.x,
            "double precision position");
    if (type == Type::Snapshot) {
      const auto &st = n.aircrafts[0].state;
      check(st.fuel_mass == a.state.fuel_mass &&
                st.payload_mass == a.state.payload_mass &&
                st.elevator == a.state.elevator && st.flap == a.state.flap &&
                st.spoiler == a.state.spoiler &&
                st.pilot_pitch == a.state.pilot_pitch &&
                st.trim_reference == a.state.trim_reference &&
                st.engine_health[1] == .2 && st.surface_health[0] == .4 &&
                st.surface_drag[3] == 1.7 &&
                (st.payload_offset - a.state.payload_offset).norm() == 0 &&
                st.aero_memory_initialized && st.alpha_lag[0] == .4 &&
                st.alpha_lag[1] == .45 && st.separation[0] == .3 &&
                st.separation[1] == .35 && st.vortex_state[0] == .2 &&
                st.vortex_state[1] == .25,
            "full advanced state roundtrip");
    }
    for (std::size_t i = 0; i < bytes.size(); ++i)
      check(!decode(std::span(bytes).first(i), n, reason),
            "truncation accepted");
    bytes.push_back(0);
    check(!decode(bytes, n, reason), "trailing bytes accepted");
  }
  Message m;
  m.type = Type::Input;
  m.entity = 7;
  m.commands = {{1, 20, a.controls}};
  for (double bad : {std::numeric_limits<double>::quiet_NaN(),
                     std::numeric_limits<double>::infinity(),
                     -std::numeric_limits<double>::infinity(), 1.01, -1.01}) {
    m.commands[0].controls.elevator_stick = bad;
    Message n;
    std::string reason;
    check(!decode(encode(m), n, reason), "invalid control accepted");
  }
  m.commands[0].controls = a.controls;
  m.commands[0].controls.throttle[0] = -.01;
  Message n;
  std::string reason;
  check(!decode(encode(m), n, reason), "negative throttle accepted");
  m.type = Type::Hello;
  m.text = "test";
  auto bytes = encode(m);
  bytes[5] = protocolVersion + 1;
  check(!decode(bytes, n, reason) && reason == "protocol version mismatch",
        "version mismatch");
  // Exercise every wire control and each bounded count, not just the pitch
  // axis.
  m.type = Type::Input;
  m.entity = 7;
  m.commands = {{1, 20, a.controls}};
  for (unsigned field = 0; field < 11; ++field) {
    auto b = encode(m);
    const unsigned offset = 50 + field * 2;
    const bool axis = field < 3 || field >= 9;
    b[offset] = axis ? 0x80 : 0xff;
    b[offset + 1] = axis ? 0 : 0xff;
    check(!decode(b, n, reason), "reserved quantized input accepted");
  }
  bytes = encode(m);
  bytes[72] = 2;
  check(!decode(bytes,n,reason),"invalid maneuver flag rejected");
  auto normalControls=a.controls;normalControls.maneuver_mode=false;
  check(!sameControls(normalControls,a.controls) && quantizeControls(a.controls).maneuver_mode,
        "mode toggles survive input coalescing and quantization");
  bytes = encode(m);
  bytes[45] = 9;
  check(!decode(bytes, n, reason), "oversized command count accepted");
  m.type = Type::Snapshot;
  m.aircrafts = {a, a};
  check(!decode(encode(m), n, reason), "duplicate entity accepted");
  m.aircrafts = {a};
  bytes = encode(m);
  bytes[64] = 255; // count follows the 40-byte Weather record
  bytes[65] = 255;
  check(!decode(bytes, n, reason), "oversized entity count accepted");
  a.state.vel_ned.x = std::numeric_limits<double>::infinity();
  m.aircrafts = {a};
  check(!decode(encode(m), n, reason),
        "nonfinite authoritative state accepted");
  bytes.resize(maxPacket + 1);
  check(!decode(bytes, n, reason), "oversized message accepted");
  std::mt19937 rng(41);
  for (unsigned i = 0; i < 50000; ++i) {
    std::vector<std::uint8_t> b(rng() % 512);
    for (auto &v : b)
      v = static_cast<std::uint8_t>(rng());
    decode(b, n, reason);
  }
  Message fleet;
  fleet.type = Type::Snapshot;
  for (unsigned i = 0; i < maxPlayers; ++i) {
    auto record = trimmed();
    record.id = i + 1;
    fleet.aircrafts.push_back(record);
  }
  const auto fleetBytes = encode(fleet);
  check(
      fleetBytes.size() == 66 + 611 * maxPlayers &&
          decode(fleetBytes, n, reason) && n.aircrafts.size() == maxPlayers,
      "complete 64-aircraft v8 snapshot with inlet, TV and aerodynamic memory");
  for (double bad : {std::numeric_limits<double>::quiet_NaN(),
                     std::numeric_limits<double>::infinity(), 1.1, -1.1}) {
    auto corrupt = a;
    corrupt.state.vel_ned.x = 110;
    corrupt.state.trim_reference = bad;
    Message snapshot;
    snapshot.type = Type::Snapshot;
    snapshot.aircrafts = {corrupt};
    check(!decode(encode(snapshot), n, reason),
          "invalid FCS trim memory accepted");
  }
  for (double bad : {std::numeric_limits<double>::quiet_NaN(),
                     std::numeric_limits<double>::infinity(), 1.01, -.01}) {
    auto corrupt = trimmed();
    corrupt.state.separation[0] = bad;
    Message snapshot;
    snapshot.type = Type::Snapshot;
    snapshot.aircrafts = {corrupt};
    check(!decode(encode(snapshot), n, reason),
          "invalid separation memory accepted");
    corrupt = trimmed();
    corrupt.state.vortex_state[1] = bad;
    snapshot.aircrafts = {corrupt};
    check(!decode(encode(snapshot), n, reason),
          "invalid vortex memory accepted");
  }
  std::puts("protocol: all types, endian golden, truncations, NaN/Inf/ranges, "
            "50000 malformed packets pass");
}
void world() {
  World w;
  auto a = w.join(), b = w.join();
  check(a && b && a != b, "stable IDs");
  check((w.aircraft(a).state.pos_ned - w.aircraft(b).state.pos_ned).norm() >=
            100,
        "spawn overlap");
  auto input = w.aircraft(a).controls;
  input.aileron_stick = .2;
  check(w.enqueue(a, {{1, 3, input}}), "enqueue");
  check(w.aircraft(a).controls.aileron_stick == 0, "arrival modified physics");
  w.step();
  w.step();
  check(w.aircraft(a).controls.aileron_stick == 0,
        "input applied before target");
  w.step();
  check(w.aircraft(a).acknowledged == 1 &&
            w.aircraft(a).controls.aileron_stick == .2,
        "target tick application");
  check(w.aircraft(b).controls.aileron_stick == 0, "cross-player input");
  check(!w.enqueue(a, {{900, 4, input}}), "sequence jump accepted");
  check(!w.enqueue(a, {{2, 1000, input}}), "far future accepted");
  input.elevator_trim = std::numeric_limits<double>::quiet_NaN();
  check(!w.enqueue(a, {{2, 4, input}}), "world NaN accepted");
  w.leave(a);
  auto c = w.join();
  check(c > b && !w.players().contains(a), "ID recycled");
  check(w.aircraft(c).state.pos_ned.y == 0, "spawn slot reuse");
  World full;
  for (unsigned i = 0; i < 64; ++i)
    check(full.join() != 0, "capacity spawn");
  check(full.join() == 0, "capacity overflow");
  World ground(false);
  auto groundId = ground.join();
  for (unsigned i = 0; i < 1200; ++i)
    ground.step();
  check(finiteState(ground.aircraft(groundId).state) &&
            ground.aircraft(groundId).state.vel_ned.norm() < 1e-5,
        "ground spawn did not settle");
  World immutable;
  auto immutableId = immutable.join();
  auto cc = immutable.aircraft(immutableId).controls;
  check(immutable.enqueue(immutableId, {{2, 20, cc}}),
        "initial immutable command");
  cc.aileron_stick = .1;
  check(!immutable.enqueue(immutableId, {{1, 10, cc}, {2, 20, cc}}) &&
            immutable.players().at(immutableId).inputs.size() == 1,
        "batch partially mutated or duplicate changed");
  check(!immutable.enqueue(immutableId, {{3, 15, cc}}),
        "tick/sequence conflict accepted");
  cc.aileron_stick = 0;
  check(immutable.enqueue(immutableId, {{1, 10, cc}}),
        "reordered earlier command lost");
  std::puts("world: stable IDs, 64-player bounds, nonoverlapping/reusable "
            "slots, tick queuing, validation pass");
}
void interpolation() {
  RemoteTrack track;
  auto a = trimmed();
  a.state.pos_ned = {0, 0, -1000};
  a.state.vel_ned = {120, 0, 0};
  track.push(120, a);
  auto b = a;
  b.state.pos_ned.x = 6;
  b.state.att = {-a.state.att.w, -a.state.att.x, -a.state.att.y,
                 -a.state.att.z};
  track.push(126, b);
  check(std::abs(track.sample(123).pos_ned.x - 3) < 1e-10,
        "interpolation midpoint");
  check(finiteState(track.sample(123)), "quaternion hemisphere");
  check(std::abs(track.sample(10000).pos_ned.x - 12) < 1e-10,
        "extrapolation unbounded");
  for (Tick t = 127; t < 1000; ++t)
    track.push(t, b);
  check(track.size() == 32, "history unbounded");
  track.push(900, a);
  check(track.newestTick() == 999, "reorder rewound track");
  std::puts("interpolation: continuous midpoint/hemisphere, stale reorder, "
            "50ms freeze, 32-sample bound pass");
}
void presentationClock() {
  ServerConfig cfg;
  cfg.bind = "127.0.0.1";
  cfg.port = 0;
  Server server(cfg);
  Transport::conditions("local");
  Client client("clock");
  client.connect("127.0.0.1", server.port());
  const auto start = std::chrono::steady_clock::now();
  auto last = start;
  while (!client.ready()) {
    const auto now = std::chrono::steady_clock::now();
    check(now - start < std::chrono::seconds(5),
          "clock test handshake deadline");
    server.poll();
    server.step();
    client.poll(std::chrono::duration<double>(now - last).count());
    last = now;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  // No server step/send between samples: isolate real client presentation clock
  // advancement from packet corrections and physics. This requires no sleep.
  client.poll(0);
  check(client.stats().unfragmentedPayload >= static_cast<int>(snapshotPayload),
        "GNS unfragmented payload exceeds application target");
  std::printf("GNS configured UDP payload=1200 unfragmentedMessagePayload=%d "
              "appMaximum=%zu\n",
              client.stats().unfragmentedPayload, snapshotPayload);
  const double before = client.stats().renderTick;
  client.poll(2);
  check(std::abs(client.stats().renderTick - before - 240) < 1e-8,
        "long frame retains all 240 presentation ticks");
  const double after = client.stats().renderTick;
  client.poll(-1);
  check(client.stats().renderTick == after,
        "negative elapsed cannot rewind clock");
  client.disconnect();
  std::puts("presentation clock: 2 s frame advances 240 ticks; smoothing cap "
            "cannot discard wall time PASS");
}
void prediction() {
  World world;
  auto id = world.join();
  Prediction p;
  p.initialize(0, world.aircraft(id), 0);
  auto c = world.aircraft(id).controls;
  c.aileron_stick = .2;
  auto command = p.advance(c);
  check(p.simulator().state().omega_body.x > 0 &&
            p.simulator().state().aileron > 0,
        "prediction waits for network");
  check(world.enqueue(id, {command}), "predicted command rejected");
  world.step();
  p.reconcile(world.tick(), world.aircraft(id));
  check((p.simulator().state().pos_ned - world.aircraft(id).state.pos_ned)
                .norm() < 1e-7,
        "same-core replay mismatch");
  auto perturbed = p.simulator().state();
  perturbed.pos_ned.y += 5;
  p.simulator().setState(perturbed);
  for (unsigned i = 0; i < 20; ++i) {
    auto cmd = p.advance(c);
    world.enqueue(id, {cmd});
    world.step();
  }
  p.reconcile(world.tick(), world.aircraft(id));
  check(p.stats().reconciliations > 0 && p.stats().error > 4.9,
        "divergence not measured");
  check((p.simulator().state().pos_ned - world.aircraft(id).state.pos_ned)
                .norm() < 1e-8,
        "authority not restored");
  for (unsigned i = 0; i < 1000; ++i)
    p.advance(c);
  check(p.pending().size() <= 512, "pending unbounded");
  p.reconcile(world.tick(), world.aircraft(id));
  check(p.stats().rebases == 1 && p.pending().empty(),
        "old snapshot replay unbounded");
  // Loss: server deliberately misses commands; authoritative baseline plus
  // pending tick replay must converge.
  p.initialize(world.tick(), world.aircraft(id), 0);
  for (unsigned i = 0; i < 120; ++i) {
    auto cmd = p.advance(c);
    if (i % 3)
      world.enqueue(id, {cmd});
    world.step();
    if (i % 5 == 4)
      p.reconcile(world.tick(), world.aircraft(id));
  }
  p.reconcile(world.tick(), world.aircraft(id));
  check((p.simulator().state().pos_ned - world.aircraft(id).state.pos_ned)
                .norm() < 1e-8,
        "loss did not converge");
  World moving;
  auto movingId = moving.join();
  Prediction behind;
  behind.initialize(0, moving.aircraft(movingId), 0);
  for (unsigned i = 0; i < 120; ++i)
    moving.step();
  behind.reconcile(moving.tick(), moving.aircraft(movingId));
  check(behind.stats().maxError < 1e-8 &&
            (behind.simulator().state().pos_ned -
             moving.aircraft(movingId).state.pos_ned)
                    .norm() < 1e-8,
        "elapsed catch-up was classified as prediction divergence");
  std::puts("prediction: immediate response, shared physics, injected 5m "
            "divergence, loss convergence, bounded replay pass");
}
void windPrediction() {
  Weather w;
  w.wind_ned = {12, 18, -.5};
  w.turbulence01 = .08;
  w.temp_offset_c = 5;
  World world;
  world.setWeather(w);
  auto id = world.join(AircraftType::Typhoon);
  Prediction predicted;
  predicted.initialize(world.tick(), world.aircraft(id), 0, world.weather());
  auto c = world.aircraft(id).controls;
  for (unsigned tick = 0; tick < 240; ++tick) {
    c.aileron_stick = tick < 120 ? .1 : 0;
    auto cmd = predicted.advance(c);
    check(world.enqueue(id, {cmd}), "wind command admission");
    world.step();
    auto snapshot = world.snapshot();
    Message decoded;
    std::string reason;
    check(decode(encode(snapshot), decoded, reason),
          "weather/state snapshot decode");
    check((decoded.weather.wind_ned - w.wind_ned).norm() == 0 &&
              decoded.weather.temp_offset_c == 5 &&
              decoded.weather.turbulence01 == .08,
          "world atmosphere replicated");
    if (tick % 5 == 4)
      predicted.reconcile(world.tick(), decoded.aircrafts[0]);
  }
  check(
      (predicted.simulator().state().pos_ned - world.aircraft(id).state.pos_ned)
              .norm() < 1e-8,
      "wind/fuel/actuator same-core prediction convergence");
  std::printf("wind prediction 240 ticks: error=%.9g snapshotBytes=%zu\n",
              predicted.stats().maxError, encode(world.snapshot()).size());
}
struct RunMetrics {
  double peakError{}, latestError{}, maxRemoteStep{}, peakGap{};
  std::size_t pendingPeak{}, queuePeak{}, historyPeak{};
  std::uint64_t snapshots{}, bytesIn{}, bytesOut{}, ticks{}, late{};
  double tickWallError{};
};
RunMetrics exercise(const std::string &preset, unsigned seconds, bool lifecycle,
                    bool security) {
  ServerConfig config;
  config.bind = "127.0.0.1";
  config.port = 0;
  config.maxClients = 4;
  Server server(config);
  Transport::conditions(preset);
  Client a("A"), b("B");
  a.connect("127.0.0.1", server.port());
  b.connect("127.0.0.1", server.port());
  std::unique_ptr<Client> extra;
  bool sawTwo = false, sawSeparate = false, sawResponse = false,
       sawObserved = false, sawJoin = false, sawLeave = false,
       invalidSent = false, invalidRemoved = false, perturbed = false,
       corrected = false;
  bool sawResponseA = false, sawResponseB = false;
  EntityId extraId{};
  std::uint64_t sequenceA = 0;
  RunMetrics result;
  State previousRemote{};
  bool remoteSample = false;
  std::uint32_t previousRemoteGeneration{};
  double lastSnapshotAt = 0, connectedAt = -1;
  Tick pulseStart = 0;
  std::uint64_t lastSnapshots = 0;
  auto start = std::chrono::steady_clock::now(), last = start, next = start;
  double accumulator = 0;
  const auto period =
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          std::chrono::duration<double>(tickSeconds));
  while (true) {
    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start).count();
    if (elapsed >= seconds)
      break;
    double dt = std::chrono::duration<double>(now - last).count();
    last = now;
    server.poll();
    unsigned steps = 0;
    while (now >= next && steps < 16) {
      server.step();
      next += period;
      ++steps;
    }
    a.poll(dt);
    b.poll(dt);
    if (extra)
      extra->poll(dt);
    if (a.ready() && b.ready()) {
      if (connectedAt < 0) {
        connectedAt = elapsed;
        pulseStart =
            std::max(a.prediction().tick(), b.prediction().tick()) + 120;
      }
      sawTwo = true;
      check(a.entity() != b.entity(), "clients share aircraft");
      check(server.world().players().contains(a.entity()) &&
                server.world().players().contains(b.entity()),
            "welcome has no authoritative aircraft");
      sawSeparate = true;
      accumulator += dt;
      while (accumulator + 1e-12 >= tickSeconds) {
        auto ca = a.prediction().simulator().controls(),
             cb = b.prediction().simulator().controls();
        const Tick flyingA = a.prediction().tick(),
                   flyingB = b.prediction().tick();
        // Both clients schedule the same absolute two-second server interval.
        // Handshake/lead differences cannot shift their opposite roll pulses.
        ca.aileron_stick =
            flyingA >= pulseStart && flyingA < pulseStart + 240 ? .08 : 0;
        cb.aileron_stick =
            flyingB >= pulseStart && flyingB < pulseStart + 240 ? -.08 : 0;
        a.predict(ca);
        b.predict(cb);
        if (extra && extra->ready() && !security)
          extra->predict(extra->prediction().simulator().controls());
        accumulator -= tickSeconds;
      }
      auto stateA = server.world().aircraft(a.entity()).state,
           stateB = server.world().aircraft(b.entity()).state;
      check(finiteState(stateA) && finiteState(stateB) &&
                finiteState(a.prediction().simulator().state()) &&
                finiteState(b.prediction().simulator().state()),
            "nonfinite multiplayer state");
      // Loss can retire the two clients' commands on different ticks. Require
      // the correct physical response from each; a simultaneous sample is not
      // part of the transport contract.
      sawResponseA = sawResponseA || stateA.omega_body.x > .001;
      sawResponseB = sawResponseB || stateB.omega_body.x < -.001;
      sawResponse = sawResponseA && sawResponseB;
      if (!b.remotes().empty()) {
        auto it = b.remotes().find(a.entity());
        if (it != b.remotes().end()) {
          auto remote = it->second.sample(b.stats().renderTick);
          check(finiteState(remote), "nonfinite remote");
          if (std::abs(remote.omega_body.x) > .001)
            sawObserved = true;
          // A new life deliberately teleports to its spawn. Continuity applies
          // within one generation; the interpolator resets at this boundary.
          if (remoteSample &&
              previousRemoteGeneration == it->second.generation()) {
            double step = (remote.pos_ned - previousRemote.pos_ned).norm();
            result.maxRemoteStep = std::max(result.maxRemoteStep, step);
            const double motionAllowance =
                std::max(previousRemote.vel_ned.norm(), remote.vel_ned.norm()) *
                dt;
            if (step >= 50 + motionAllowance)
              std::fprintf(
                  stderr,
                  "remote discontinuity step=%.3f dt=%.3f allowance=%.3f "
                  "render=%.3f newest=%.3f server=%llu\n",
                  step, dt, motionAllowance, b.stats().renderTick,
                  it->second.newestTick(),
                  (unsigned long long)server.world().tick());
            check(step < 50 + motionAllowance,
                  "remote interpolation discontinuity");
          }
          previousRemoteGeneration = it->second.generation();
          previousRemote = remote;
          remoteSample = true;
        }
      }
      result.peakError =
          std::max({result.peakError, a.prediction().stats().maxError,
                    b.prediction().stats().maxError});
      result.pendingPeak =
          std::max({result.pendingPeak, a.prediction().pending().size(),
                    b.prediction().pending().size()});
      result.historyPeak =
          std::max(result.historyPeak, a.stats().historySamples);
      check(a.prediction().pending().size() <= 512 &&
                b.prediction().pending().size() <= 512,
            "unbounded pending inputs");
      for (const auto &[id, p] : server.world().players()) {
        (void)id;
        check(p.inputs.size() <= 256, "unbounded server inputs");
        check(finiteState(p.sim.state()), "extra state nonfinite");
      }
      if (elapsed > 4 && !perturbed) {
        auto s = a.prediction().simulator().state();
        s.pos_ned.y += 5;
        a.prediction().simulator().setState(s);
        sequenceA = a.prediction().pending().empty()
                        ? 0
                        : a.prediction().pending().back().sequence;
        perturbed = true;
      }
      if (perturbed && elapsed > 6 &&
          a.prediction().stats().reconciliations > 0 &&
          a.prediction().stats().error < .5)
        corrected = true;
      if (lifecycle && elapsed > 2.5 && !extra && !sawLeave) {
        extra = std::make_unique<Client>("joiner");
        extra->connect("127.0.0.1", server.port());
      }
      if (extra && extra->ready() && !extraId)
        extraId = extra->entity();
      if (extraId && a.remotes().contains(extraId))
        sawJoin = true;
      if (lifecycle && extra && elapsed > 5) {
        extra->disconnect();
        extra.reset();
      }
      if (extraId && elapsed > 6 &&
          !server.world().players().contains(extraId) &&
          !a.remotes().contains(extraId))
        sawLeave = true;
      if (security && elapsed > 2 && !extra && !invalidSent) {
        extra = std::make_unique<Client>("invalid");
        extra->connect("127.0.0.1", server.port());
      }
      if (security && extra && extra->ready() && !invalidSent) {
        Message m;
        m.type = Type::Input;
        m.entity = extra->entity();
        m.commands = {{1, server.world().tick() + 1,
                       extra->prediction().simulator().controls()}};
        for (double bad :
             {std::numeric_limits<double>::quiet_NaN(),
              std::numeric_limits<double>::infinity(), 1.2, -1.2}) {
          m.commands[0].controls.elevator_stick = bad;
          extra->sendTestPacket(encode(m));
        }
        m.commands[0].controls = extra->prediction().simulator().controls();
        m.entity = a.entity();
        extra->sendTestPacket(encode(m)); // ownership spoof
        m.entity = extra->entity();
        m.commands[0].sequence = 100000;
        extra->sendTestPacket(encode(m)); // sequence abuse
        m.commands[0].sequence = 1;
        m.commands[0].tick = server.world().tick() + 1000;
        extra->sendTestPacket(encode(m)); // future tick abuse
        std::vector<std::uint8_t> malformed = {0, 1, 2};
        extra->sendTestPacket(malformed);
        invalidSent = true;
      }
      if (security && invalidSent && extraId &&
          !server.world().players().contains(extraId)) {
        invalidRemoved = true;
        extra.reset();
      }
      if (a.stats().snapshots != lastSnapshots) {
        if (lastSnapshotAt > 0)
          result.peakGap = std::max(result.peakGap, elapsed - lastSnapshotAt);
        lastSnapshotAt = elapsed;
        lastSnapshots = a.stats().snapshots;
      }
    } else if (sawTwo)
      throw std::runtime_error("established clients disconnected");
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  check(sawTwo && sawSeparate, "two-client handshake");
  if (!sawResponse || !sawObserved)
    std::fprintf(stderr,
                 "roll response authorityA/B=%d/%d remote=%d pulseStart=%llu "
                 "tick=%llu\n",
                 sawResponseA, sawResponseB, sawObserved,
                 (unsigned long long)pulseStart,
                 (unsigned long long)server.world().tick());
  check(sawResponse && sawObserved,
        "authoritative controls or replication did not move aircraft");
  check(perturbed && corrected,
        "network reconciliation did not correct injected divergence");
  if (lifecycle)
    check(sawJoin && sawLeave, "join/leave not replicated");
  if (security)
    check(invalidSent && invalidRemoved && server.stats().invalid >= 8,
          "invalid input rejection did not survive");
  check(a.stats().snapshots > seconds * 12, "snapshot starvation");
  check(a.prediction().stats().error < .5 && b.prediction().stats().error < .5,
        "final reconciliation did not converge");
  check(result.peakError < 20, "runaway prediction error");
  check(server.world().stats().maxQueue <= 120, "queue exceeded tick horizon");
  check(a.stats().historySamples <= 64, "remote history grew");
  (void)sequenceA;
  result.latestError =
      std::max(a.prediction().stats().error, b.prediction().stats().error);
  result.snapshots = a.stats().snapshots;
  result.bytesIn = a.stats().bytesIn;
  result.bytesOut = a.stats().bytesOut;
  result.ticks = server.world().tick();
  result.queuePeak = server.world().stats().maxQueue;
  result.late = server.world().stats().late;
  result.tickWallError = std::abs(double(result.ticks) - double(seconds) * 120);
  check(result.tickWallError < 24, "authoritative tick drift");
  a.disconnect();
  b.disconnect();
  if (extra)
    extra->disconnect();
  for (unsigned i = 0; i < 100 && !server.world().players().empty(); ++i) {
    server.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  check(server.world().players().empty(), "entity leak after disconnect");
  Transport::conditions("local");
  std::printf(
      "network preset=%s seconds=%u tick=%llu snapshots=%llu peakError=%.6f "
      "finalError=%.6f pendingPeak=%zu queuePeak=%zu historyPeak=%zu "
      "remoteStep=%.4f gap=%.4f payloadInBps=%.1f payloadOutBps=%.1f late=%llu "
      "tickError=%.0f injected5m=corrected entitiesAfterLeave=0\n",
      preset.c_str(), seconds, (unsigned long long)result.ticks,
      (unsigned long long)result.snapshots, result.peakError,
      result.latestError, result.pendingPeak, result.queuePeak,
      result.historyPeak, result.maxRemoteStep, result.peakGap,
      double(result.bytesIn) / seconds, double(result.bytesOut) / seconds,
      (unsigned long long)result.late, result.tickWallError);
  return result;
}
void limits() {
  ServerConfig config;
  config.bind = "127.0.0.1";
  config.port = 0;
  config.maxClients = 2;
  Server server(config);
  Client a("limits"), b("version");
  a.connect("127.0.0.1", server.port());
  b.connect("127.0.0.1", server.port());
  auto pump = [&](double seconds, Client *third = nullptr,
                  Transport *raw = nullptr) {
    auto start = std::chrono::steady_clock::now(), next = start, last = start;
    while (
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count() < seconds) {
      auto now = std::chrono::steady_clock::now();
      double dt = std::chrono::duration<double>(now - last).count();
      last = now;
      server.poll();
      while (now >= next) {
        server.step();
        next += std::chrono::microseconds(8333);
      }
      a.poll(dt);
      b.poll(dt);
      if (third)
        third->poll(dt);
      if (raw)
        raw->poll();
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  pump(.4);
  check(a.ready() && b.ready(), "limits handshake");
  auto firstId = a.entity();
  Client full("full");
  full.connect("127.0.0.1", server.port());
  pump(.3, &full);
  check(!full.ready() && server.world().players().size() == 2,
        "full server admitted player");
  Message hello;
  hello.type = Type::Hello;
  hello.text = "wrong version";
  auto bytes = encode(hello);
  bytes[5] = protocolVersion + 1;
  b.sendTestPacket(bytes);
  pump(.3);
  check(!b.ready() && server.world().players().size() == 1,
        "version not immediately rejected");
  Transport idle;
  idle.connect("127.0.0.1", server.port());
  pump(.3, nullptr, &idle);
  for (unsigned i = 0; i < 610; ++i)
    server.step();
  server.poll();
  pump(.1, nullptr, &idle);
  check(server.stats().disconnects >= 2, "missing hello timeout");
  Message ping;
  ping.type = Type::Ping;
  bytes = encode(ping);
  for (unsigned i = 0; i < 300; ++i)
    a.sendTestPacket(bytes);
  pump(.3);
  check(!a.ready() && server.world().players().empty(),
        "rate limit did not remove offender");
  a.connect("127.0.0.1", server.port());
  pump(.4);
  check(a.ready() && a.entity() > firstId,
        "reconnect reused identity or failed");
  server.shutdown();
  pump(.2);
  check(!a.ready() && server.world().players().empty(),
        "shutdown did not disconnect client");
  for (unsigned hz : {20, 24, 25, 30, 60}) {
    ServerConfig rateConfig;
    rateConfig.bind = "127.0.0.1";
    rateConfig.port = 0;
    rateConfig.snapshotHz = hz;
    Server rateServer(rateConfig);
    for (unsigned i = 0; i < 120; ++i)
      rateServer.step();
    check(rateServer.stats().snapshotCount == hz,
          "configurable snapshot clock");
  }
  std::puts("limits: full server, immediate version reject, 5s hello timeout, "
            "message flood, reconnect/new ID, clean server shutdown pass");
}
void longrun() {
  // Accelerated 420 s, same authoritative stepping/redundant input model, with
  // repeated churn.
  World world;
  auto a = world.join(), b = world.join();
  auto controls = world.aircraft(a).controls;
  std::uint64_t seq = 0;
  for (unsigned t = 0; t < 50400; ++t) {
    ++seq;
    if (t % 7)
      world.enqueue(a, {{seq, world.tick() + 2, controls}});
    if (t % 11)
      world.enqueue(b, {{seq, world.tick() + 2, controls}});
    if (t % 1200 == 0) {
      auto id = world.join();
      world.leave(id);
    }
    world.step();
    for (const auto &[id, p] : world.players()) {
      (void)id;
      check(finiteState(p.sim.state()) && p.inputs.size() <= 120,
            "longrun finite/queue");
    }
  }
  check(world.tick() == 50400 && world.players().size() == 2,
        "longrun tick/entity drift");
  world.leave(a);
  world.leave(b);
  check(world.players().empty(), "longrun leaks");
  std::printf("accelerated world: 420s 50400 ticks, 42 join/leaves, "
              "queuePeak=%zu, finite, no remaining entities\n",
              world.stats().maxQueue);
}
} // namespace
int main(int argc, char **argv) {
  try {
    std::string suite = argc > 1 ? argv[1] : "protocol";
    if (suite == "protocol")
      protocol();
    else if (suite == "world")
      world();
    else if (suite == "interpolation")
      interpolation();
    else if (suite == "presentation_clock")
      presentationClock();
    else if (suite == "prediction")
      prediction();
    else if (suite == "wind_prediction")
      windPrediction();
    else if (suite == "connection")
      exercise("local", 10, true, false);
    else if (suite == "security")
      exercise("local", 10, false, true);
    else if (suite == "good" || suite == "moderate" || suite == "bad")
      exercise(suite, 12, false, false);
    else if (suite == "soak")
      exercise("moderate", 180, true, false);
    else if (suite == "limits")
      limits();
    else if (suite == "longrun")
      longrun();
    else
      throw std::runtime_error("unknown network suite");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "NETWORK FAIL: %s\n", e.what());
    return 1;
  }
}
