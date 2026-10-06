#include "ofs/net/client.hpp"
#include "ofs/net/server.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <thread>
using namespace ofs;
using namespace ofs::net;
namespace {
void check(bool v, const char *why) {
  if (!v)
    throw std::runtime_error(why);
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
void align(World &world, EntityId first, EntityId second, unsigned pair) {
  auto &a = const_cast<Player &>(world.players().at(first));
  auto &b = const_cast<Player &>(world.players().at(second));
  auto s = a.sim.state();
  s.pos_ned = {0, double(pair) * 4500, -6000};
  s.vel_ned = {240, 0, 0};
  s.att = {};
  s.omega_body = {};
  a.sim.setState(s);
  s = b.sim.state();
  s.pos_ned = {2400, double(pair) * 4500, -6000};
  s.vel_ned = {-240, 0, 0};
  s.att = quatFromEuler(0, 0, kPi);
  s.omega_body = {};
  b.sim.setState(s);
}
void graphicalServer() {
  ServerConfig config;
  config.bind = "127.0.0.1";
  config.port = 27022;
  Server server(config);
  Client target("M4-target", AircraftType::Typhoon);
  target.connect("127.0.0.1", server.port());
  std::map<EntityId, std::uint32_t> generations;
  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  auto last = start, next = start;
  std::optional<Clock::time_point> duelStarted;
  const auto period = std::chrono::duration_cast<Clock::duration>(
      std::chrono::duration<double>(tickSeconds));
  double accumulator = 0;
  while (Clock::now() - start < std::chrono::seconds(50) &&
         (!duelStarted ||
          Clock::now() - *duelStarted < std::chrono::seconds(24))) {
    const auto now = Clock::now();
    const double dt = std::chrono::duration<double>(now - last).count();
    last = now;
    server.poll();
    target.poll(dt);
    if (server.world().players().size() == 2) {
      auto it = server.world().players().begin();
      const auto a = it++->first, b = it->first;
      const auto &aa = server.world().players().at(a);
      const auto &bb = server.world().players().at(b);
      if (aa.highestSequence > 0 && bb.highestSequence > 0 && aa.life.alive() &&
          bb.life.alive() &&
          (!generations.contains(a) || generations[a] != aa.life.generation ||
           generations[b] != bb.life.generation)) {
        // A tail chase gives a slow native renderer enough time to employ IR.
        auto own =
            const_cast<Player &>(server.world().players().at(b)).sim.state();
        own.pos_ned = {0, 0, -6000};
        own.vel_ned = {240, 0, 0};
        own.att = {};
        own.omega_body = {};
        const_cast<Player &>(server.world().players().at(b)).sim.setState(own);
        auto ahead =
            const_cast<Player &>(server.world().players().at(a)).sim.state();
        ahead.pos_ned = {2000, 0, -6000};
        ahead.vel_ned = {200, 0, 0};
        ahead.att = {};
        ahead.omega_body = {};
        const_cast<Player &>(server.world().players().at(a))
            .sim.setState(ahead);
        if (!duelStarted)
          duelStarted = now;
        generations[a] = aa.life.generation;
        generations[b] = bb.life.generation;
      }
    }
    accumulator += dt;
    while (accumulator >= tickSeconds) {
      if (target.ready())
        target.predict(target.prediction().simulator().controls());
      accumulator -= tickSeconds;
    }
    while (now >= next) {
      server.step();
      next += period;
    }
    target.takeVisualEvents();
    target.takeMissileDetonations();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto &stats = server.world().missiles().stats();
  std::printf(
      "graphical diagnostic players=%zu launches=%llu hits=%llu rejected=%llu "
      "invalid=%llu\n",
      server.world().players().size(), (unsigned long long)stats.launches,
      (unsigned long long)stats.hits, (unsigned long long)stats.rejected,
      (unsigned long long)server.stats().invalid);
  check(stats.launches >= 1 && stats.hits >= 1,
        "graphical server missile gameplay");
  std::printf(
      "graphical missiles launches=%llu hits=%llu detonations=%llu PASS\n",
      (unsigned long long)stats.launches, (unsigned long long)stats.hits,
      (unsigned long long)stats.detonations);
}
void abuse() {
  ServerConfig config;
  config.bind = "127.0.0.1";
  config.port = 0;
  Server server(config);
  Client attacker("weapon-abuse", AircraftType::Typhoon),
      victim("victim", AircraftType::Su57);
  attacker.connect("127.0.0.1", server.port());
  victim.connect("127.0.0.1", server.port());
  auto pump = [&](unsigned ticks) {
    for (unsigned i = 0; i < ticks; ++i) {
      server.poll();
      server.step();
      attacker.poll(tickSeconds);
      victim.poll(tickSeconds);
      std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
  };
  pump(60);
  check(attacker.ready() && victim.ready(), "security handshake");
  WeaponMessage m;
  m.entity = victim.entity();
  m.tick = server.world().tick();
  m.sequence = 1;
  m.action = {1, m.tick, 0, WeaponActionKind::Launch, 0};
  check(attacker.sendTestPacket(encodeWeapon(m)), "send spoofed ownership");
  m.entity = attacker.entity();
  m.action.station = 7;
  attacker.sendTestPacket(encodeWeapon(m));
  m.action.station = 0;
  m.action.generation = 999;
  attacker.sendTestPacket(encodeWeapon(m));
  m.action.generation = 0;
  m.sequence = 999999;
  attacker.sendTestPacket(encodeWeapon(m));
  m.sequence = 2;
  m.tick += 10000;
  attacker.sendTestPacket(encodeWeapon(m));
  m.type = Type::RadarState;
  attacker.sendTestPacket(encodeWeapon(m));
  m.type = Type::MissileState;
  m.missiles.push_back({1,
                        {attacker.entity(), 0},
                        {victim.entity(), 0},
                        WeaponType::Infrared,
                        {0, 0, -1000},
                        {1000, 0, 0},
                        {},
                        weapons::MotorPhase::Boost,
                        weapons::SeekerPhase::Tracking,
                        1});
  attacker.sendTestPacket(encodeWeapon(m));
  attacker.sendTestPacket(std::vector<std::uint8_t>{0, 1, 2});
  pump(90);
  check(!attacker.ready() && victim.ready() &&
            server.world().missiles().stats().launches == 0,
        "spoofing rejected without missile spawn");
  std::printf(
      "weapon security live strikes=%llu launches=0 victimConnected=1\n",
      (unsigned long long)server.stats().invalid);
}
void run(const std::string &condition, unsigned seconds) {
#ifdef OFS_TEST_TIME_SCALE
  constexpr double timeScale = OFS_TEST_TIME_SCALE;
#else
  constexpr double timeScale = 1;
#endif
  const double wallSeconds = seconds * timeScale;
  ServerConfig config;
  config.bind = "127.0.0.1";
  config.port = 0;
  config.maxClients = 16;
  Server server(config);
  Transport::conditions(condition);
  std::vector<std::unique_ptr<Client>> clients;
  for (unsigned i = 0; i < 16; ++i) {
    const auto type = i == 14   ? AircraftType::A320
                      : i == 15 ? AircraftType::SR71
                      : i % 2   ? AircraftType::Su57
                                : AircraftType::Typhoon;
    clients.push_back(
        std::make_unique<Client>("M4-" + std::to_string(i), type));
    clients.back()->connect("127.0.0.1", server.port());
  }
  using Clock = std::chrono::steady_clock;
  auto start = Clock::now(), last = start, next = start;
  bool ready = false;
  std::vector<std::uint32_t> generations(16, UINT32_MAX);
  std::vector<double> ticks;
  Tick lastActions{};
  double predictionAccumulator = 0;
  std::size_t warmRss = 0, peakRss = 0;
  unsigned seen = 0;
  const auto period = std::chrono::duration_cast<Clock::duration>(
      std::chrono::duration<double>(tickSeconds * timeScale));
  for (;;) {
    const auto now = Clock::now();
    const double dt = std::chrono::duration<double>(now - last).count();
    last = now;
    server.poll();
    for (auto &client : clients)
      client->poll(dt / timeScale);
    const bool connected =
        std::all_of(clients.begin(), clients.end(),
                    [](const auto &c) { return c->ready(); });
    if (!ready && connected) {
      ready = true;
      start = next = now;
      predictionAccumulator = 0;
    }
    const double elapsed = std::chrono::duration<double>(now - start).count();
    if (!ready)
      check(elapsed < 8 * timeScale, "16-player live handshake");
    else {
      if (!connected)
        for (const auto &client : clients)
          if (!client->ready())
            std::fprintf(stderr,
                         "disconnected status=%s serverInvalid=%llu "
                         "serverTick=%llu clientTick=%llu\n",
                         client->status().c_str(),
                         (unsigned long long)server.stats().invalid,
                         (unsigned long long)server.world().tick(),
                         (unsigned long long)client->prediction().tick());
      check(connected, "live client connected");
      if (elapsed >= wallSeconds)
        break;
    }
    if (ready) {
      for (unsigned i = 0; i < 16; i += 2) {
        const auto &a = server.world().players().at(clients[i]->entity());
        const auto &b = server.world().players().at(clients[i + 1]->entity());
        if (a.life.alive() && b.life.alive() &&
            (generations[i] != a.life.generation ||
             generations[i + 1] != b.life.generation)) {
          align(server.world(), clients[i]->entity(), clients[i + 1]->entity(),
                i / 2);
          generations[i] = a.life.generation;
          generations[i + 1] = b.life.generation;
        }
      }
      if (server.world().tick() >= lastActions + 60) {
        lastActions = server.world().tick();
        for (unsigned i = 0; i < 14; ++i) {
          auto &client = *clients[i];
          const auto intended = clients[i ^ 1]->entity();
          const auto &radar = client.radar();
          if (!client.life().alive() ||
              radar.generation != client.life().generation)
            continue;
          const auto target = std::find_if(
              radar.tracks.begin(), radar.tracks.end(),
              [&](const auto &t) { return t.entity.id == intended; });
          if (target == radar.tracks.end())
            continue;
          if (radar.selected.id != intended) {
            client.weaponAction(WeaponActionKind::NextTarget);
            continue;
          }
          if (radar.locked.id != intended) {
            client.weaponAction(WeaponActionKind::Lock);
            continue;
          }
          const auto weapon =
              std::count(radar.stations.begin(), radar.stations.end(),
                         WeaponType::Infrared) > 0
                  ? WeaponType::Infrared
                  : WeaponType::ActiveRadar;
          if (radar.weapon != weapon) {
            client.weaponAction(weapon == WeaponType::Infrared
                                    ? WeaponActionKind::SelectIR
                                    : WeaponActionKind::SelectRadar);
            continue;
          }
          if (radar.seekerReady)
            for (unsigned station = 0; station < radar.stations.size();
                 ++station)
              if (radar.stations[station] == weapon) {
                client.weaponAction(WeaponActionKind::Launch, station);
                break;
              }
        }
      }
      predictionAccumulator += dt / timeScale;
      unsigned predicted = 0;
      while (predictionAccumulator >= tickSeconds && predicted++ < 16) {
        for (auto &client : clients) {
          auto controls = client->prediction().simulator().controls();
          client->predict(controls);
        }
        predictionAccumulator -= tickSeconds;
      }
      for (auto &client : clients) {
        seen += client->missiles().size();
        check(client->missiles().size() <= 128 &&
                  client->prediction().pending().size() <= 512,
              "live client bounds");
        client->takeMissileDetonations();
        client->takeVisualEvents();
      }
    }
    unsigned catchup = 0;
    while (now >= next && catchup++ < 16) {
      const auto before = Clock::now();
      server.step();
      ticks.push_back(
          std::chrono::duration<double, std::micro>(Clock::now() - before)
              .count());
      next += period;
    }
    if (elapsed > 5 * timeScale && !warmRss)
      warmRss = rss();
    peakRss = std::max(peakRss, rss());
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  const auto &w = server.world().missiles().stats();
  const auto &c = server.world().combat().stats();
  const auto &n = server.stats();
  std::sort(ticks.begin(), ticks.end());
  std::printf(
      "live preset=%s seconds=%u wallSeconds=%.0f timeScale=%.0f players=16 "
      "launches=%llu detonations=%llu "
      "hits=%llu kills=%llu respawns=%llu seen=%u maxWeaponPacket=%zu "
      "aircraftBps=%.1f radarBps=%.1f missileBps=%.1f totalBps=%.1f "
      "sendFailures=%llu invalid=%llu tickP99us=%.2f RSSwarmKiB=%zu "
      "RSSpeakKiB=%zu peakTransportBytes=%zu\n",
      condition.c_str(), seconds, wallSeconds, timeScale,
      (unsigned long long)w.launches, (unsigned long long)w.detonations,
      (unsigned long long)w.hits, (unsigned long long)c.kills,
      (unsigned long long)c.respawns, seen, n.maxWeaponPacket,
      double(n.snapshotBytes) / wallSeconds, double(n.radarBytes) / wallSeconds,
      double(n.missileBytes) / wallSeconds, double(n.bytesOut) / wallSeconds,
      (unsigned long long)n.sendFailures, (unsigned long long)n.invalid,
      ticks[std::size_t(.99 * (ticks.size() - 1))], warmRss, peakRss,
      n.pendingTransportBytesPeak);
  check(w.launches >= 7 && w.detonations >= 3 && w.hits >= 3 && seen > 0,
        "live missile gameplay");
  check(c.respawns >= 1, "live respawn");
  check(n.sendFailures == 0 && c.droppedEvents == 0, "live sends/events");
  check(n.maxWeaponPacket <= applicationPayload, "live packet budget");
  if (seconds >= 180 && warmRss)
    check(peakRss < warmRss + 16384, "soak memory growth <16MiB");
  for (auto &client : clients) {
    client->disconnect();
    check(client->missiles().empty() && client->radar().tracks.empty(),
          "explicit disconnect clears client weapon presentation");
  }
  for (unsigned i = 0; i < 300 && !server.world().players().empty(); ++i) {
    server.poll();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  check(server.world().players().empty() &&
            server.world().missiles().missiles().empty(),
        "live disconnect cleanup");
  Transport::conditions("local");
}
} // namespace
int main(int argc, char **argv) {
  try {
    const std::string mode = argc > 1 ? argv[1] : "local";
    if (mode == "graphical-server")
      graphicalServer();
    else if (mode == "abuse")
      abuse();
    else
      run(mode == "soak" ? "bad" : mode, mode == "soak" ? 180 : 30);
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "M4 live FAIL: %s\n", e.what());
    return 1;
  }
}
