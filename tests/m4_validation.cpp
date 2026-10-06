// Production fixed-step world + v13 codecs + v10 recovery architecture.
// The impairment harness advances simulated time; GNS has separate live tests.
#include "ofs/net/client.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
using namespace ofs;
using namespace ofs::net;
using namespace ofs::weapons;
namespace {
using Clock = std::chrono::steady_clock;
void check(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}
double micros(Clock::time_point start) {
  return std::chrono::duration<double, std::micro>(Clock::now() - start)
      .count();
}
double percentile(std::vector<double> values, double p) {
  std::sort(values.begin(), values.end());
  return values.empty() ? 0 : values[std::size_t(p * (values.size() - 1))];
}
struct Link {
  EntityId id{};
  EntityId intended{};
  ReplicationSender aircraftSender;
  ReplicationReceiver aircraftReceiver;
  WeaponReplicationSender weaponSender;
  WeaponReplicationReceiver weaponReceiver;
  std::uint64_t actionSequence{}, fireSequence{};
  std::uint32_t generation{UINT32_MAX};
  std::set<std::uint64_t> detonations;
  std::uint64_t aircraftBytes{}, weaponBytes{}, radarBytes{}, reliableBytes{},
      frames{}, actions{}, duplicates{}, spawnBytes{}, stateBytes{},
      removeBytes{};
  unsigned receivedMissiles{};
};
struct Delivery {
  unsigned client{};
  Bytes bytes;
  bool weapon{}, action{}, fire{};
};
void alignPair(World &world, EntityId a, EntityId b, unsigned index) {
  auto &pa = const_cast<Player &>(world.players().at(a));
  auto &pb = const_cast<Player &>(world.players().at(b));
  auto sa = pa.sim.state(), sb = pb.sim.state();
  sa.pos_ned = {0, double(index) * 6000, -6000};
  sa.att = {};
  sa.vel_ned = {240, 0, 0};
  sa.omega_body = {};
  sb.pos_ned = sa.pos_ned + Vec3{2400, 0, 0};
  sb.att = quatFromEuler(0, 0, kPi);
  sb.vel_ned = {-240, 0, 0};
  sb.omega_body = {};
  pa.sim.setState(sa);
  pb.sim.setState(sb);
}
void run(unsigned count, unsigned seconds, double loss, unsigned lag,
         unsigned jitter, bool missilesEnabled, unsigned launchModulo = 60) {
  World world;
  std::vector<std::unique_ptr<Link>> links;
  for (unsigned i = 0; i < count; ++i) {
    auto link = std::make_unique<Link>();
    const auto type = i >= count - 2 && count > 2
                          ? (i % 2 ? AircraftType::SR71 : AircraftType::A320)
                      : i % 2 ? AircraftType::Su57
                              : AircraftType::Typhoon;
    link->id = world.join(type);
    links.push_back(std::move(link));
  }
  for (unsigned i = 0; i + 1 < count; i += 2) {
    links[i]->intended = links[i + 1]->id;
    links[i + 1]->intended = links[i]->id;
    alignPair(world, links[i]->id, links[i + 1]->id, i / 2);
  }
  std::mt19937 rng(400);
  std::map<Tick, std::vector<Delivery>> queue;
  std::vector<Tick> reliableDue(count);
  std::uint64_t dropped = 0, delivered = 0, baselineMisses = 0;
  std::size_t maxPacket = 0, queuePeak = 0, queued = 0, historyPeak = 0;
  std::vector<double> tickTime, radarTime, missileTime, fuseTime, encodingTime;
  auto submit = [&](Tick now, unsigned client, Bytes bytes, bool reliable,
                    bool weapon, bool action = false, bool fire = false) {
    check(withinApplicationBudget(bytes.size()), "MTU application payload");
    maxPacket = std::max(maxPacket, bytes.size());
    if (!reliable && std::generate_canonical<double, 32>(rng) < loss) {
      ++dropped;
      return;
    }
    Tick due = now + lag + (jitter ? rng() % (2 * jitter + 1) : 0);
    if (reliable) {
      due = std::max(due, reliableDue[client]);
      reliableDue[client] = due;
    }
    queue[due].push_back({client, std::move(bytes), weapon, action, fire});
    ++queued;
    queuePeak = std::max(queuePeak, queued);
  };
  auto action = [&](Tick tick, unsigned index, WeaponActionKind kind,
                    unsigned station = 0) {
    auto &l = *links[index];
    const auto &p = world.players().at(l.id);
    WeaponMessage m;
    m.entity = l.id;
    m.tick = tick;
    m.sequence = ++l.actionSequence;
    m.action = {m.sequence, tick, p.life.generation, kind,
                std::uint8_t(station)};
    submit(tick, index, encodeWeapon(m), true, true, true);
    ++l.actions;
  };
  auto deliver = [&](Tick now) {
    while (!queue.empty() && queue.begin()->first <= now) {
      auto items = std::move(queue.begin()->second);
      queue.erase(queue.begin());
      queued -= items.size();
      for (auto &item : items) {
        auto &l = *links[item.client];
        ++delivered;
        if ((item.action || item.fire) && !world.players().contains(l.id))
          continue;
        if (item.weapon) {
          WeaponMessage m;
          check(decodeWeapon(item.bytes, m), "weapon decode");
          if (item.action) {
            check(world.enqueueWeapon(l.id, m.action),
                  "reliable validated action");
            check(world.enqueueWeapon(l.id, m.action),
                  "replayed action harmless");
            ++l.duplicates;
          } else {
            check(l.weaponReceiver.receive(m, now), "weapon timeline");
            l.receivedMissiles += m.missiles.size();
            for (const auto &e : l.weaponReceiver.takeDetonations())
              check(l.detonations.insert(e.missile.id).second,
                    "unique detonation delivery");
          }
        } else if (item.bytes[6] == unsigned(Type::SnapshotChunk)) {
          auto frame = l.aircraftReceiver.receive(item.bytes, now, l.id);
          if (frame) {
            ++l.frames;
            check(l.aircraftSender.acknowledge(frame->sequence, false),
                  "usable ACK");
          } else if (l.aircraftReceiver.needsRecovery())
            check(l.aircraftSender.acknowledge(
                      l.aircraftReceiver.acknowledged(), true),
                  "bounded recovery request");
        } else {
          Message m;
          std::string reason;
          check(decode(item.bytes, m, reason), "aircraft/event decode");
          if (item.fire) {
            check(world.enqueueFire(l.id, m.fire), "mixed gun request");
          } else if (m.type == Type::Spawn)
            check(l.aircraftReceiver.spawn(m.aircraft, m.tick),
                  "aircraft reliable spawn");
          else if (m.type == Type::Despawn)
            l.aircraftReceiver.despawn(m.entity, m.tick);
        }
      }
    }
  };
  for (Tick tick = 1; tick <= Tick(seconds) * 120; ++tick) {
    deliver(tick);
    for (unsigned i = 0; i + 1 < count; i += 2) {
      auto &a = *links[i];
      auto &b = *links[i + 1];
      const auto &pa = world.players().at(a.id);
      const auto &pb = world.players().at(b.id);
      if (pa.life.alive() && pb.life.alive() &&
          (a.generation != pa.life.generation ||
           b.generation != pb.life.generation)) {
        alignPair(world, a.id, b.id, i / 2);
        a.generation = pa.life.generation;
        b.generation = pb.life.generation;
      }
    }
    if (missilesEnabled && tick % launchModulo == 0) {
      for (unsigned i = 0; i < count; ++i) {
        auto &l = *links[i];
        const auto &p = world.players().at(l.id);
        if (!p.life.alive() || p.weapons.inventory.stations.empty())
          continue;
        const auto &radar = l.weaponReceiver.radar;
        const bool hasTarget = std::any_of(
            radar.tracks.begin(), radar.tracks.end(),
            [&](const auto &t) { return t.entity.id == l.intended; });
        if (!hasTarget)
          continue;
        if (radar.selected.id != l.intended) {
          action(tick, i, WeaponActionKind::NextTarget);
          continue;
        }
        if (radar.locked.id != l.intended) {
          action(tick, i, WeaponActionKind::Lock);
          continue;
        }
        const auto choice =
            std::count(radar.stations.begin(), radar.stations.end(),
                       WeaponType::Infrared) > 0
                ? WeaponType::Infrared
                : WeaponType::ActiveRadar;
        if (radar.weapon != choice) {
          action(tick, i,
                 choice == WeaponType::Infrared
                     ? WeaponActionKind::SelectIR
                     : WeaponActionKind::SelectRadar);
          continue;
        }
        if (radar.seekerReady) {
          for (unsigned station = 0; station < radar.stations.size(); ++station)
            if (radar.stations[station] == choice) {
              action(tick, i, WeaponActionKind::Launch, station);
              break;
            }
        }
      }
    }
    // Guns retain their independent held/cooldown/ammo path. Late in each
    // cycle they provide a mixed-combat cleanup, without masking missile hits.
    if (tick % 12 == 0)
      for (unsigned i = 0; i < count; ++i) {
        auto &l = *links[i];
        const auto &p = world.players().at(l.id);
        if (!aircraftDefinition(p.type).gun)
          continue;
        Message m;
        m.type = Type::Fire;
        m.entity = l.id;
        m.tick = tick;
        m.sequence = ++l.fireSequence;
        m.fire = {m.sequence, tick, p.life.generation, 0,
                  missilesEnabled && tick % 2400 > 1800 && i % 2 == 0};
        submit(tick, i, encode(m), false, false, false, true);
      }
    const auto start = Clock::now();
    world.step();
    tickTime.push_back(micros(start));
    radarTime.push_back(world.missiles().stats().radarUs);
    missileTime.push_back(world.missiles().stats().missileUs);
    fuseTime.push_back(world.missiles().stats().fuseUs);
    const auto events = world.missiles().takeEvents();
    world.combat().takeEvents();
    for (const auto &[id, p] : world.players()) {
      (void)id;
      check(finiteState(p.sim.state()) && p.inputs.size() <= 256 &&
                p.fireInputs.size() <= 128 && p.weapons.actions.size() <= 32 &&
                p.weapons.radar.tracks().size() <= 16,
            "bounded world state");
    }
    if (tick % 5 == 0) {
      const auto encodingStart = Clock::now();
      auto snapshot = world.snapshot();
      InterestGrid grid;
      grid.rebuild(snapshot.aircrafts);
      for (unsigned i = 0; i < count; ++i) {
        auto &l = *links[i];
        auto update = l.aircraftSender.build(tick, l.id, snapshot.aircrafts,
                                             grid, world.weather());
        for (auto &event : update.lifecycle) {
          auto bytes = encode(event);
          l.reliableBytes += bytes.size();
          submit(tick, i, std::move(bytes), true, false);
        }
        for (auto &bytes : update.packets) {
          l.aircraftBytes += bytes.size();
          submit(tick, i, std::move(bytes), false, false);
        }
        historyPeak =
            std::max(historyPeak, l.aircraftSender.stats().historyPeak);
      }
      encodingTime.push_back(micros(encodingStart));
    }
    for (unsigned i = 0; i < count; ++i) {
      auto &l = *links[i];
      const auto &p = world.players().at(l.id);
      const auto packets =
          l.weaponSender.build(tick, l.id, p.sim.state().pos_ned,
                               radarProjection(p.weapons, p.life.generation),
                               world.missiles().missiles(), events);
      for (const auto &packet : packets) {
        l.weaponBytes += packet.bytes.size();
        switch (Type(packet.bytes[6])) {
        case Type::MissileSpawn:
          l.spawnBytes += packet.bytes.size();
          break;
        case Type::MissileState:
          l.stateBytes += packet.bytes.size();
          break;
        case Type::MissileRemove:
          l.removeBytes += packet.bytes.size();
          break;
        default:
          break;
        }
        if (packet.bytes[6] == unsigned(Type::RadarState))
          l.radarBytes += packet.bytes.size();
        if (packet.reliable)
          l.reliableBytes += packet.bytes.size();
        submit(tick, i, packet.bytes, packet.reliable, true);
      }
      l.weaponReceiver.expire(tick);
      l.aircraftReceiver.expire(tick);
      check(l.weaponReceiver.missiles().size() <= 128 &&
                l.weaponReceiver.tombstones() <= 256 &&
                l.weaponSender.known() <= 128 &&
                l.aircraftReceiver.stats().assemblyPeak <= 4,
            "bounded replication memory");
    }
    check(queued < 10000 && world.missiles().missiles().size() <= 128,
          "queue and pool bounds");
  }
  const auto stats = world.missiles().stats();
  const auto combat = world.combat().stats();
  std::printf("combat evidence launches=%llu detonations=%llu hits=%llu "
              "kills=%llu respawns=%llu\n",
              (unsigned long long)stats.launches,
              (unsigned long long)stats.detonations,
              (unsigned long long)stats.hits, (unsigned long long)combat.kills,
              (unsigned long long)combat.respawns);
  if (missilesEnabled) {
    check(stats.launches >= 2 && stats.detonations >= 1 && stats.hits >= 1,
          "missile launch/hit evidence");
    check(combat.respawns >= 1, "combat destruction/respawn evidence");
  }
  // Disconnect every authority, drain ordered lifecycle; watchdog clears state
  // even after all unreliable state in the final interval is lost.
  for (auto &link : links)
    world.leave(link->id);
  const Tick end = Tick(seconds) * 120;
  auto removed = world.missiles().takeEvents();
  for (unsigned i = 0; i < count; ++i) {
    auto &l = *links[i];
    for (const auto &packet :
         l.weaponSender.build(end + 1, l.id, {}, {}, {}, removed))
      submit(end + 1, i, packet.bytes, true, true);
  }
  for (Tick tick = end + 1; tick <= end + 300; ++tick) {
    deliver(tick);
    for (auto &link : links)
      link->weaponReceiver.expire(tick);
  }
  std::uint64_t spawnBytes = 0, stateBytes = 0, removeBytes = 0;
  std::uint64_t aircraftBytes = 0, weaponBytes = 0, radarBytes = 0,
                reliableBytes = 0, frames = 0, received = 0;
  for (auto &l : links) {
    spawnBytes += l->spawnBytes;
    stateBytes += l->stateBytes;
    removeBytes += l->removeBytes;
    aircraftBytes += l->aircraftBytes;
    weaponBytes += l->weaponBytes;
    radarBytes += l->radarBytes;
    reliableBytes += l->reliableBytes;
    frames += l->frames;
    received += l->receivedMissiles;
    baselineMisses += l->aircraftReceiver.stats().baselineMisses;
    check(l->weaponReceiver.missiles().empty(),
          "no stale missiles after drain");
  }
  check(world.players().empty() && world.missiles().missiles().empty() &&
            queued == 0,
        "authority cleanup");
  check(historyPeak <= baselineLimit, "baseline history bound");
  std::printf("weapon bandwidth spawnBps=%.1f stateBps=%.1f removeBps=%.1f "
              "radarBps=%.1f supportBps=0\n",
              double(spawnBytes) / seconds, double(stateBytes) / seconds,
              double(removeBytes) / seconds, double(radarBytes) / seconds);
  std::printf(
      "players=%u seconds=%u loss=%.2f lag=%u jitter=%u missiles=%d "
      "launches=%llu detonations=%llu missileHits=%llu kills=%llu "
      "respawns=%llu peakMissiles=%zu receivedRecords=%llu dropped=%llu "
      "delivered=%llu frames=%llu aircraftBps=%.1f weaponBps=%.1f "
      "radarBps=%.1f totalBps=%.1f reliableBps=%.1f maxPacket=%zu "
      "queuePeak=%zu baselinePeak=%zu baselineMisses=%llu tickP50us=%.2f "
      "tickP99us=%.2f radarP99us=%.2f missileP99us=%.2f fuseP99us=%.2f "
      "encodingP99us=%.2f cleanup=0\n",
      count, seconds, loss, lag, jitter, missilesEnabled,
      (unsigned long long)stats.launches, (unsigned long long)stats.detonations,
      (unsigned long long)stats.hits, (unsigned long long)combat.kills,
      (unsigned long long)combat.respawns, stats.peakMissiles,
      (unsigned long long)received, (unsigned long long)dropped,
      (unsigned long long)delivered, (unsigned long long)frames,
      double(aircraftBytes) / seconds, double(weaponBytes) / seconds,
      double(radarBytes) / seconds,
      double(aircraftBytes + weaponBytes) / seconds,
      double(reliableBytes) / seconds, maxPacket, queuePeak, historyPeak,
      (unsigned long long)baselineMisses, percentile(tickTime, .5),
      percentile(tickTime, .99), percentile(radarTime, .99),
      percentile(missileTime, .99), percentile(fuseTime, .99),
      percentile(encodingTime, .99));
}
void performance() {
  for (unsigned players : {2u, 8u, 16u})
    for (unsigned missiles : {0u, 16u, 32u, 64u, 128u}) {
      World world;
      std::vector<EntityId> ids;
      for (unsigned i = 0; i < players; ++i)
        ids.push_back(
            world.join(i % 2 ? AircraftType::Su57 : AircraftType::Typhoon));
      std::vector<double> times, physics, radar, encoding;
      for (unsigned tick = 0; tick < 180; ++tick) {
        while (world.missiles().missiles().size() < missiles) {
          Track track{{ids[1], 0}, {30000, 0, -6000}, {240, 0, 0}, 0, 1, 0};
          auto s = world.players().at(ids[0]).sim.state();
          s.pos_ned = {0, 0, -6000};
          check(world.missiles().launch(world.tick(), {ids[0], 0}, s, {},
                                        WeaponType::ActiveRadar, track),
                "benchmark pool admission");
        }
        const auto start = Clock::now();
        world.step();
        times.push_back(micros(start));
        physics.push_back(world.missiles().stats().missileUs);
        radar.push_back(world.missiles().stats().radarUs);
        const auto netStart = Clock::now();
        WeaponReplicationSender sender;
        auto packets =
            sender.build(world.tick(), ids[0], {},
                         radarProjection(world.players().at(ids[0]).weapons, 0),
                         world.missiles().missiles(), {});
        (void)packets;
        encoding.push_back(micros(netStart));
        world.missiles().takeEvents();
        world.combat().takeEvents();
      }
      check(percentile(times, .99) < 8333.333,
            "120 Hz missile physics tick budget");
      std::printf(
          "performance players=%u missiles=%u tickP50us=%.2f tickP99us=%.2f "
          "physicsP99us=%.2f radarP99us=%.2f ownerEncodingP99us=%.2f\n",
          players, missiles, percentile(times, .5), percentile(times, .99),
          percentile(physics, .99), percentile(radar, .99),
          percentile(encoding, .99));
    }
  const auto &d = missileDefinition(WeaponType::Infrared);
  State own;
  own.pos_ned.z = -6000;
  auto s = launchState(d, own, {}, {});
  SensorTarget target{
      {2, 0}, {1000, 0, -6000}, {250, 100, 0}, {}, AircraftType::Typhoon, 1, 1,
      true};
  const auto seekerStart = Clock::now();
  for (unsigned i = 0; i < 10000; ++i)
    updateSeeker(d, s, &target, tickSeconds);
  const auto seekerUs = micros(seekerStart) / 10000;
  Vec3 result;
  const auto guidanceStart = Clock::now();
  for (unsigned i = 0; i < 10000; ++i)
    result += proportionalNavigation({1000, double(i % 100), 0}, {-500, 100, 0},
                                     {700, 0, 0}, 3.5, 350);
  std::printf("kernel seekerMeanUs=%.4f PNMeanUs=%.4f guard=%.3f\n", seekerUs,
              micros(guidanceStart) / 10000, result.y);
}
} // namespace
int main(int argc, char **argv) {
  try {
    const std::string mode = argc > 1 ? argv[1] : "normal";
    if (mode == "performance")
      performance();
    else if (mode == "normal")
      run(16, 30, 0, 0, 0, true);
    else if (mode == "moderate")
      run(16, 30, .02, 4, 2, true);
    else if (mode == "poor")
      run(16, 30, .05, 10, 3, true);
    else if (mode == "soak")
      run(16, 180, .05, 10, 3, true);
    else if (mode == "none")
      run(16, 30, 0, 0, 0, false);
    else if (mode == "heavy")
      run(16, 30, 0, 0, 0, true, 30);
    else if (mode == "players2")
      run(2, 30, 0, 0, 0, true);
    else if (mode == "players8")
      run(8, 30, 0, 0, 0, true);
    else
      throw std::runtime_error("unknown validation mode");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "M4 validation FAIL: %s\n", e.what());
    return 1;
  }
}
