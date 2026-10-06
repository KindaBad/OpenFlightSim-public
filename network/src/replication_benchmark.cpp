// Deterministic full-path measurements: production physics, prediction, wire
// parsers, sender/receiver and simulated delivery. No sleeps or hidden threads.
#include "ofs/net/client.hpp"
#include "ofs/net/replication.hpp"
#include "ofs/net/world.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <stdexcept>
using namespace ofs;
using namespace ofs::net;
namespace {
using Clock = std::chrono::steady_clock;
double us(Clock::time_point t) {
  return std::chrono::duration<double, std::micro>(Clock::now() - t).count();
}
void check(bool v, const char *why) {
  if (!v)
    throw std::runtime_error(why);
}
double percentile(std::vector<double> v, double p) {
  if (v.empty())
    return 0;
  std::sort(v.begin(), v.end());
  return v[std::size_t(p * (v.size() - 1))];
}
struct Link {
  ReplicationSender sender;
  ReplicationReceiver receiver;
  Prediction prediction;
  EntityId id{};
  Tick lastReliable{};
  std::uint64_t inputBytes{}, snapshotBytes{}, reliableBytes{}, ackBytes{},
      lost{}, lostSnapshots{}, snapshotPackets{}, frames{}, combatEvents{};
  Tick recoveryAckAt{};
  bool recoveryAckSent{};
  double peakError{};
  std::map<EntityId, RemoteTrack> remotes;
};
struct Delivery {
  enum Kind { Snapshot, Lifecycle, Input, Ack, Fire, Combat };
  Kind kind{};
  unsigned client{};
  Bytes bytes;
};
void run(unsigned count, bool spread, unsigned seconds,
         const std::string &condition, bool combat) {
  const double loss = condition == "poor"                                ? .05
                      : (condition == "moderate" || condition == "wind") ? .02
                                                                         : 0.;
  const unsigned lag = condition == "poor"                                ? 9
                       : (condition == "moderate" || condition == "wind") ? 3
                                                                          : 0;
  const unsigned jitter = condition == "poor" ? 2
                          : (condition == "moderate" || condition == "wind")
                              ? 1
                              : 0;
  std::mt19937 rng(370);
  GunConfig gun;
  gun.dispersion = 0;
  World world(true, combat ? std::optional<GunConfig>(gun) : std::nullopt);
  if (condition == "wind") {
    Weather weather;
    weather.wind_ned = {12, 18, -.5};
    weather.turbulence01 = .08;
    weather.temp_offset_c = 5;
    world.setWeather(weather);
  }
  std::vector<std::unique_ptr<Link>> links;
  for (unsigned i = 0; i < count; ++i) {
    auto link = std::make_unique<Link>();
    const auto type = i % 4 == 0   ? AircraftType::A320
                      : i % 4 == 1 ? AircraftType::Typhoon
                      : i % 4 == 2 ? AircraftType::SR71
                                   : AircraftType::Su57;
    link->id = world.join(type);
    auto &p = const_cast<Player &>(world.players().at(link->id));
    auto state = p.sim.state();
    if (spread) {
      state.pos_ned.x = double(i % 8) * 18000;
      state.pos_ned.y = double(i / 8) * 18000;
      state.pos_ned.z = -3000 - double(i % 3) * 1500;
    }
    if (combat && (i == 1 || i == 3)) {
      state.pos_ned = {i == 3 ? 450. : 0, 0, -3000};
      state.att = {};
      state.vel_ned = {140, 0, 0};
    }
    p.sim.setState(state);
    link->prediction.initialize(0, world.aircraft(link->id), 18,
                                world.weather());
    links.push_back(std::move(link));
  }
  std::map<Tick, std::vector<Delivery>> queue;
  std::size_t queued = 0, queuePeak = 0, reliableQueuedBytes = 0,
              reliableQueuePeak = 0;
  auto submit = [&](Tick t, unsigned client, Delivery::Kind kind, Bytes bytes,
                    bool reliable) {
    if (kind == Delivery::Snapshot)
      ++links[client]->snapshotPackets;
    if (!reliable && double(rng()) / double(rng.max()) < loss) {
      ++links[client]->lost;
      if (kind == Delivery::Snapshot)
        ++links[client]->lostSnapshots;
      return;
    }
    const unsigned noise = jitter ? rng() % (2 * jitter + 1) : 0;
    Tick due = t + lag + noise;
    if (reliable) {
      due = std::max(due, links[client]->lastReliable);
      links[client]->lastReliable = due;
    }
    queue[due].push_back({kind, client, std::move(bytes)});
    if (reliable) {
      reliableQueuedBytes += queue[due].back().bytes.size();
      reliableQueuePeak = std::max(reliableQueuePeak, reliableQueuedBytes);
    }
    ++queued;
    queuePeak = std::max(queuePeak, queued);
    check(queued <= 8192, "delivery queue bound");
  };
  auto acknowledge = [&](Tick t, unsigned client, bool usable) {
    auto &link = *links[client];
    if (!usable && (!link.receiver.needsRecovery() ||
                    (link.recoveryAckSent && t < link.recoveryAckAt + 12)))
      return;
    Message ack;
    ack.type = Type::SnapshotAck;
    ack.baseline = link.receiver.acknowledged();
    ack.recovery = link.receiver.needsRecovery();
    if (ack.recovery) {
      link.recoveryAckAt = t;
      link.recoveryAckSent = true;
    }
    auto bytes = encode(ack);
    link.ackBytes += bytes.size();
    submit(t, client, Delivery::Ack, std::move(bytes), false);
  };
  double decodeAtTick = 0, reconcileAtTick = 0;
  auto deliver = [&](Tick t) {
    while (!queue.empty() && queue.begin()->first <= t) {
      auto packets = std::move(queue.begin()->second);
      queue.erase(queue.begin());
      for (auto &d : packets) {
        --queued;
        if (d.kind == Delivery::Lifecycle || d.kind == Delivery::Combat)
          reliableQueuedBytes -= d.bytes.size();
        auto &link = *links[d.client];
        Message m;
        std::string why;
        if (d.kind == Delivery::Snapshot) {
          const auto ds = Clock::now();
          auto f = link.receiver.receive(d.bytes, t, link.id);
          decodeAtTick += us(ds);
          if (f) {
            ++link.frames;
            for (const auto &[id, s] : f->entities) {
              Aircraft a;
              check(expandAircraft(id, s, f->reference, a),
                    "snapshot expansion");
              if (id == link.id) {
                const auto rs = Clock::now();
                link.prediction.reconcile(f->tick, a);
                reconcileAtTick += us(rs);
                // Deliberate authority-only gun fixture placements are measured
                // as rebases, outside ordinary flight prediction error samples.
                if (!(combat && (d.client == 1 || d.client == 3) && t >= 240 &&
                      t < 480))
                  link.peakError =
                      std::max(link.peakError, link.prediction.stats().error);
              } else {
                link.remotes[id].setTier(s.tier);
                link.remotes[id].push(s.sampledTick, a);
              }
            }
          }
          acknowledge(t, d.client, bool(f));
        } else {
          check(decode(d.bytes, m, why), "generated message parse");
          if (d.kind == Delivery::Lifecycle) {
            if (m.type == Type::Spawn)
              link.receiver.spawn(m.aircraft, m.tick);
            else {
              link.receiver.despawn(m.entity, m.tick);
              link.remotes.erase(m.entity);
            }
          }
          if (d.kind == Delivery::Fire)
            check(world.enqueueFire(link.id, m.fire),
                  "impaired fire admission");
          if (d.kind == Delivery::Combat)
            link.combatEvents += m.events.size();
          if (d.kind == Delivery::Input) {
            check(world.enqueue(link.id, m.commands, m.generation),
                  "input admission");
            check(link.sender.acknowledge(m.baseline, m.recovery),
                  "input ACK admission");
          }
          if (d.kind == Delivery::Ack)
            check(link.sender.acknowledge(m.baseline, m.recovery),
                  "ACK admission");
        }
      }
    }
  };
  std::vector<double> physics, serverCost, query, projection, encodeCost,
      decodeCost, reconcileCost, interpolationCost, sizes, errors, orientations,
      velocities;
  std::uint64_t measuredIn = 0, measuredOut = 0, replicatedEntities = 0,
                frames = 0;
  std::size_t maxChunk = 0;
  double maxMemory = 0, maxReceiverMemory = 0, maxInterpolationMemory = 0;
  std::size_t senderHistoryPeak = 0, receiverHistoryPeak = 0,
              assemblyBytesPeak = 0, assemblyPeak = 0;
  std::vector<std::uint64_t> oldIn(count), oldOut(count);
  for (Tick t = 1; t <= Tick(seconds) * 120; ++t) {
    decodeAtTick = reconcileAtTick = 0;
    deliver(t);
    for (unsigned i = 0; i < count; ++i) {
      auto &l = *links[i];
      if (l.receiver.expire(t))
        acknowledge(t, i, false);
      auto a = world.aircraft(l.id);
      if (a.life.alive()) {
        auto controls = l.prediction.simulator().controls();
        controls.aileron_stick = .008 * std::sin(double(t) * .025);
        l.prediction.advance(controls);
        if (t % 2 == 0) {
          Message input;
          input.type = Type::Input;
          input.entity = l.id;
          input.generation = a.life.generation;
          input.baseline = l.receiver.acknowledged();
          input.recovery = l.receiver.needsRecovery();
          const auto &pending = l.prediction.pending();
          auto first = pending.size() > 4 ? pending.size() - 4 : 0;
          for (std::size_t j = first; j < pending.size(); ++j)
            input.commands.push_back(pending[j]);
          if (!input.commands.empty()) {
            auto bytes = encode(input);
            l.inputBytes += bytes.size();
            submit(t, i, Delivery::Input, std::move(bytes), false);
          }
        }
      }
      if (combat && aircraftDefinition(a.type).gun && t % 12 == 0) {
        Message fire;
        fire.type = Type::Fire;
        fire.entity = l.id;
        fire.fire = {t / 12, t + 18, a.life.generation, 0, true};
        fire.tick = fire.fire.tick;
        fire.sequence = fire.fire.sequence;
        auto bytes = encode(fire);
        l.inputBytes += bytes.size();
        submit(t, i, Delivery::Fire, std::move(bytes), false);
      }
    }
    const auto serverStart = Clock::now();
    if (combat && t >= 240 && t < 360) {
      auto &shooter = const_cast<Player &>(world.players().at(links[1]->id));
      auto &target = const_cast<Player &>(world.players().at(links[3]->id));
      if (shooter.life.alive() && target.life.alive()) {
        auto state = shooter.sim.state();
        state.pos_ned = {double(t) * 110 / 120, 0, -3000};
        state.vel_ned = {110, 0, 0};
        state.omega_body = {};
        shooter.sim.setState(state);
        state.pos_ned += state.att.rotate(Vec3{180, 0, 0});
        target.sim.setState(state);
      }
    }
    world.step();
    const double phy = world.stats().lastTickUs;
    auto events = world.combat().takeEvents();
    for (unsigned i = 0; i < count; ++i) {
      Message message;
      message.type = Type::Combat;
      message.tick = t;
      message.sequence = t;
      for (const auto &event : events)
        if (event.owner == links[i]->id || event.target == links[i]->id ||
            links[i]->sender.interested(event.owner) ||
            links[i]->sender.interested(event.target)) {
          message.events.push_back(event);
          if (message.events.size() == maxCombatEvents) {
            auto bytes = encode(message);
            links[i]->reliableBytes += bytes.size();
            submit(t, i, Delivery::Combat, std::move(bytes), true);
            message.events.clear();
          }
        }
      if (!message.events.empty()) {
        auto bytes = encode(message);
        links[i]->reliableBytes += bytes.size();
        submit(t, i, Delivery::Combat, std::move(bytes), true);
      }
    }
    double qcost = 0, pcost = 0, ecost = 0, icost = 0;
    const bool publication = count >= 5 || (5 - t % 5) % 5 < count;
    if (publication) {
      auto source = world.snapshot();
      InterestGrid grid;
      grid.rebuild(source.aircrafts);
      qcost += grid.indexUs();
      pcost += grid.projectionUs();
      for (unsigned i = 0; i < count; ++i) {
        if ((t + i) % 5 != 0)
          continue;
        auto &l = *links[i];
        auto o =
            l.sender.build(t, l.id, source.aircrafts, grid, world.weather());
        if (!spread)
          check(l.sender.stats().tierCounts[0] == 1 &&
                    l.sender.stats().tierCounts[1] == count - 1,
                "clustered workload escaped near tier");
        qcost += l.sender.stats().queryUs;
        pcost += l.sender.stats().projectionUs;
        ecost += l.sender.stats().deltaEncodeUs;
        for (auto &event : o.lifecycle) {
          auto bytes = encode(event);
          l.reliableBytes += bytes.size();
          submit(t, i, Delivery::Lifecycle, std::move(bytes), true);
        }
        std::size_t size = 0;
        for (auto &bytes : o.packets) {
          maxChunk = std::max(maxChunk, bytes.size());
          size += bytes.size();
          submit(t, i, Delivery::Snapshot, std::move(bytes), false);
        }
        l.snapshotBytes += size;
        if (t > 120) {
          sizes.push_back(double(size));
          replicatedEntities +=
              l.sender.stats().tierCounts[0] + l.sender.stats().tierCounts[1] +
              l.sender.stats().tierCounts[2] + l.sender.stats().tierCounts[3];
          ++frames;
        }
      }
      double senderMemory = 0, receiverMemory = 0;
      std::size_t senderHistory = 0, receiverHistory = 0, assemblyBytes = 0;
      for (const auto &l : links) {
        senderHistory += l->sender.historyMemoryBytes();
        receiverHistory += l->receiver.historyMemoryBytes();
        assemblyBytes += l->receiver.assemblyMemoryBytes();
        assemblyPeak = std::max(assemblyPeak, l->receiver.stats().assemblyPeak);
        senderMemory += l->sender.memoryBytes();
        receiverMemory += l->receiver.memoryBytes();
      }
      senderHistoryPeak = std::max(senderHistoryPeak, senderHistory);
      receiverHistoryPeak = std::max(receiverHistoryPeak, receiverHistory);
      assemblyBytesPeak = std::max(assemblyBytesPeak, assemblyBytes);
      maxMemory = std::max(maxMemory, senderMemory);
      maxReceiverMemory = std::max(maxReceiverMemory, receiverMemory);
    }
    const double serverUs = us(serverStart);
    deliver(t);
    for (auto &l : links) {
      auto rstart = Clock::now();
      for (auto &[id, track] : l->remotes) {
        (void)id;
        check(track.size() <= 32, "interpolation history bound");
        auto sample = track.sampleAircraft(double(t) - 12);
        check(finiteState(sample.state), "interpolation finite");
      }
      icost += us(rstart);
      check(l->sender.historySize() <= baselineLimit &&
                l->receiver.historySize() <= baselineLimit &&
                l->receiver.assemblySize() <= assemblyLimit,
            "replication histories bounded");
      check(l->prediction.pending().size() <= 512,
            "pending prediction bounded");
    }
    double interpolationMemory = 0;
    for (const auto &l : links)
      for (const auto &[id, track] : l->remotes) {
        (void)id;
        interpolationMemory += track.size() * sizeof(RemoteSample);
      }
    maxInterpolationMemory =
        std::max(maxInterpolationMemory, interpolationMemory);
    if (t > 120) {
      physics.push_back(phy);
      serverCost.push_back(serverUs);
      if (publication) {
        query.push_back(qcost);
        projection.push_back(pcost);
        encodeCost.push_back(ecost);
      }
      decodeCost.push_back(decodeAtTick);
      reconcileCost.push_back(reconcileAtTick);
      interpolationCost.push_back(icost);
      for (unsigned i = 0; i < count; ++i) {
        auto &l = *links[i];
        const auto in = l.inputBytes + l.ackBytes;
        const auto out = l.snapshotBytes + l.reliableBytes;
        measuredIn += in - oldIn[i];
        measuredOut += out - oldOut[i];
        if (!(combat && (i == 1 || i == 3) && t >= 240 && t < 480)) {
          errors.push_back(l.prediction.stats().error);
          orientations.push_back(l.prediction.stats().orientationCorrection);
          velocities.push_back(l.prediction.stats().velocityCorrection);
        }
        oldIn[i] = in;
        oldOut[i] = out;
      }
    } else
      for (unsigned i = 0; i < count; ++i) {
        oldIn[i] = links[i]->inputBytes + links[i]->ackBytes;
        oldOut[i] = links[i]->snapshotBytes + links[i]->reliableBytes;
      }
  }
  std::uint64_t keyframes = 0, deltas = 0, misses = 0, recoveries = 0, lost = 0,
                decodeFailures = 0, replays = 0, usableFrames = 0,
                snapshotPackets = 0, lostSnapshots = 0, combatEvents = 0;
  std::uint64_t requests = 0, coalesced = 0, recoveryKeys = 0, retries = 0,
                completed = 0, expiredAssemblies = 0, senderMisses = 0;
  double peakError = 0;
  std::size_t historyPeak = 0;
  for (const auto &l : links) {
    const auto &s = l->sender.stats();
    requests += s.recoveryRequests;
    coalesced += s.recoveryCoalesced;
    recoveryKeys += s.recoveryKeyframes;
    retries += s.recoveryRetries;
    completed += s.recoveryCompleted;
    senderMisses += s.baselineMisses;
    expiredAssemblies += l->receiver.stats().expiredAssemblies;
    keyframes += s.keyframes;
    deltas += s.deltas;
    misses += l->receiver.stats().baselineMisses;
    recoveries += l->receiver.stats().recoveries;
    lost += l->lost;
    snapshotPackets += l->snapshotPackets;
    lostSnapshots += l->lostSnapshots;
    replays += l->prediction.stats().replayCount;
    usableFrames += l->frames;
    combatEvents += l->combatEvents;
    decodeFailures += l->receiver.stats().decodeFailures;
    peakError = std::max(peakError, l->peakError);
    historyPeak = std::max(historyPeak, s.historyPeak);
    check(l->frames >= seconds * 8, "replication starvation");
    check(l->receiver.acknowledged() >= (seconds * 24) - 8,
          "final usable baseline freshness");
  }
  if (combat) {
    check(world.combat().stats().kills > 0 &&
              world.combat().stats().respawns > 0,
          "soak destruction and respawn");
    check(std::any_of(links.begin(), links.end(),
                      [](const auto &link) { return link->combatEvents > 0; }),
          "soak reliable combat delivery");
  }
  check(decodeFailures == 0, "valid traffic decode failure");
  const auto generated = std::uint64_t(count) * seconds * 24;
  const double usablePercent = 100. * usableFrames / generated;
  // Shipping target regression: old audit ~89.3%, ~0.66 MB/s. Allow variation
  // from loss/reorder while catching a substantial throughput/traffic
  // regression.
  if (count == 16 && condition == "poor" && !spread) {
    check(usablePercent >= 85., "16-player loss usable frame regression");
    check(measuredOut / double(seconds - 1) / 1e6 <= .80,
          "16-player loss bandwidth regression");
  }
  // This reports scaling sensitivity; it is not a 64-player shipping gate.
  check(maxMemory <=
            count * (baselineLimit *
                         (sizeof(NetFrame) +
                          maxPlayers * sizeof(NetEntities::value_type)) +
                     200000),
        "sender memory ceiling");
  check(maxReceiverMemory <=
            count * (baselineLimit *
                         (sizeof(NetFrame) +
                          maxPlayers * sizeof(NetEntities::value_type)) +
                     200000),
        "receiver memory ceiling");
  check(peakError < 30, "prediction runaway");
  const double duration = seconds - 1;
  auto mean = [](const std::vector<double> &v) {
    double s = 0;
    for (double x : v)
      s += x;
    return v.empty() ? 0 : s / v.size();
  };
  std::printf(
      "%s,%u,%s,%u,%.6f,%.6f,%.1f,%.0f,%.0f,%.0f,%.0f,%zu,%.2f,%llu,%llu,%.3f,%"
      ".3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.0f,%.0f,%.0f,%.5f,%."
      "5f,%.5f,%.5f,%.5f,%llu,%llu,%llu,%llu,%zu,%zu,%llu,%llu,"
      "%llu,%llu,%llu,%llu,%llu,",
      spread ? "distributed" : "clustered", count, condition.c_str(), seconds,
      measuredOut / duration / 1e6, measuredIn / duration / 1e6,
      measuredOut / duration / count, percentile(sizes, .5),
      percentile(sizes, .95), percentile(sizes, .99), percentile(sizes, 1),
      maxChunk, double(replicatedEntities) / std::max<std::uint64_t>(1, frames),
      (unsigned long long)keyframes, (unsigned long long)deltas, mean(physics),
      percentile(physics, .99), mean(serverCost), percentile(serverCost, .99),
      percentile(serverCost, 1), mean(query), mean(projection),
      mean(encodeCost), mean(decodeCost), mean(reconcileCost),
      mean(interpolationCost), maxMemory, maxReceiverMemory,
      maxInterpolationMemory, percentile(errors, .95), percentile(errors, .99),
      peakError, percentile(orientations, .99), percentile(velocities, .99),
      (unsigned long long)misses, (unsigned long long)recoveries,
      (unsigned long long)lost, (unsigned long long)decodeFailures, queuePeak,
      historyPeak, (unsigned long long)world.combat().stats().kills,
      (unsigned long long)world.combat().stats().respawns,
      (unsigned long long)replays, (unsigned long long)usableFrames,
      (unsigned long long)snapshotPackets, (unsigned long long)lostSnapshots,
      (unsigned long long)combatEvents);
  std::printf(
      "%llu,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%zu,%zu,%zu,%zu,%zu,%.3f\n",
      (unsigned long long)generated, usablePercent,
      (unsigned long long)requests, (unsigned long long)coalesced,
      (unsigned long long)recoveryKeys, (unsigned long long)retries,
      (unsigned long long)completed, (unsigned long long)senderMisses,
      (unsigned long long)expiredAssemblies, senderHistoryPeak,
      receiverHistoryPeak, assemblyBytesPeak, assemblyPeak, reliableQueuePeak,
      percentile(serverCost, .95));
  std::fflush(stdout);
  for (const auto &link : links)
    world.leave(link->id);
  check(world.players().empty() && world.combat().projectiles().empty(),
        "soak disconnect cleanup");
  links.clear();
  queue.clear();
}
} // namespace
int main(int argc, char **argv) {
  try {
    std::puts(
        "world,clients,conditions,sim_seconds,outbound_MBps,inbound_MBps,"
        "outbound_Bps_client,snapshot_p50,snapshot_p95,snapshot_p99,snapshot_"
        "max,chunk_max,entities_client,keyframes,deltas,physics_mean_us,"
        "physics_p99_us,server_mean_us,server_p99_us,server_max_us,interest_"
        "mean_us,projection_mean_us,delta_encode_mean_us,decode_mean_us,"
        "reconcile_mean_us,interpolation_mean_us,sender_memory_bytes,receiver_"
        "memory_bytes,interpolation_sample_bytes,error_p95_m,error_p99_m,error_"
        "peak_m,orientation_p99_rad,velocity_p99_mps,baseline_misses,keyframe_"
        "recoveries,dropped_packets,decode_failures,delivery_queue_peak,"
        "history_peak,kills,respawns,replayed_commands,usable_frames,snapshot_"
        "packets,snapshot_packets_lost,reliable_combat_events,generated_frames,"
        "usable_percent,recovery_requests,recovery_coalesced,recovery_"
        "keyframes,recovery_retries,recovery_completed,sender_baseline_misses,"
        "expired_assemblies,sender_history_bytes,receiver_history_bytes,"
        "assembly_bytes,assembly_peak,reliable_queue_bytes,server_p95_us");
    const std::string mode = argc > 1 ? argv[1] : "all";
    if (mode == "all") {
      for (unsigned n : {2, 8, 16, 32, 64})
        run(n, false, 30, "normal", false);
      for (unsigned n : {16, 32, 64})
        run(n, true, 30, "normal", false);
    } else if (mode == "clustered64")
      run(64, false, 10, "normal", false);
    else if (mode == "clustered16")
      run(16, false, 10, "normal", false);
    else if (mode == "loss16")
      run(16, false, 10, "poor", false);
    else if (mode == "loss64")
      run(64, false, 10, "poor", false);
    else if (mode == "distributed64")
      run(64, true, 10, "normal", false);
    else if (mode == "normal" || mode == "moderate" || mode == "poor" ||
             mode == "wind")
      run(8, false, 20, mode, false);
    else if (mode == "soak")
      run(8, true, 180, "poor", true);
    else
      throw std::invalid_argument(
          "ofs_replication_benchmark "
          "[all|clustered16|clustered64|distributed64|loss16|loss64|normal|"
          "moderate|poor|wind|soak]");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "MEASUREMENT FAIL %s\n", e.what());
    return 1;
  }
}
