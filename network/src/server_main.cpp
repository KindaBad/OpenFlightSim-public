#include "ofs/net/cli.hpp"
#include "ofs/net/server.hpp"
#include "ofs_version.hpp"
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>
namespace {
volatile std::sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
} // namespace
int main(int argc, char **argv) {
  using namespace ofs::net;
  try {
    ServerConfig config;
    unsigned seconds = 0;
    for (int i = 1; i < argc; ++i) {
      std::string_view a = argv[i];
      if (a == "--bind")
        config.bind = argument(i, argc, argv);
      else if (a == "--port")
        config.port = static_cast<std::uint16_t>(
            number(argument(i, argc, argv), 1, 65535));
      else if (a == "--max-players")
        config.maxClients = number(argument(i, argc, argv), 1, 64);
      else if (a == "--snapshot-hz")
        config.snapshotHz = number(argument(i, argc, argv), 1, 60);
      else if (a == "--seconds")
        seconds = number(argument(i, argc, argv), 1, 86400);
      else if (a == "--bots")
        config.bots = number(argument(i, argc, argv), 0, 8);
      else if (a == "--ground")
        config.airborne = false;
      else if (a == "--missile-reload")
        config.missileReload = number(argument(i, argc, argv), 0, 3600);
      else if (a == "--teams")
        config.teams = true;
      else if (a == "--score-limit")
        config.scoreLimit = number(argument(i, argc, argv), 50, 5000);
      else if (a == "--lan-name")
        config.lobbyName = lobbyName(std::string(argument(i, argc, argv)));
      else
        throw std::invalid_argument(
            "ofs_server [--bind IP] [--port N] [--max-players 1..64] "
            "[--snapshot-hz 1..60] [--bots 0..8] [--ground] [--seconds N] "
            "[--lan-name NAME] [--missile-reload SECONDS] "
            "[--teams] [--score-limit 50..5000]");
    }
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    config.version = OFS_VERSION;
    Server server(config);
    std::printf("[NET] listening %s:%u physics=120Hz snapshots=%uHz max=%u\n",
                config.bind.c_str(), server.port(), config.snapshotHz,
                config.maxClients);
    if (server.announced())
      std::printf("[NET] LAN game \"%s\" answers discovery on UDP %u\n",
                  config.lobbyName.c_str(), config.discoveryPort);
    else if (!config.lobbyName.empty())
      std::printf("[NET] LAN game \"%s\" could not open discovery port %u; "
                  "join by address\n",
                  config.lobbyName.c_str(), config.discoveryPort);
    std::fflush(stdout);
    auto begin = std::chrono::steady_clock::now(), next = begin,
         lastLog = begin;
    std::uint64_t overruns = 0;
    const auto period =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(tickSeconds));
    while (!stopped) {
      server.poll();
      auto now = std::chrono::steady_clock::now();
      unsigned catchup = 0;
      while (now >= next && catchup < 16) {
        server.step();
        next += period;
        ++catchup;
      }
      if (now - next > std::chrono::seconds(1)) {
        ++overruns;
        next = now;
      } // Report overload; never change physics dt.
      if (now - lastLog >= std::chrono::seconds(5)) {
        const auto &s = server.stats();
        const auto &w = server.world().stats();
        std::printf(
            "[NET] clients=%zu tick=%llu messages=%llu/%llu bytes=%llu/%llu "
            "invalid=%llu disconnected=%llu late=%llu queuePeak=%zu "
            "tickUs=%.1f peakUs=%.1f snapshotUs=%.1f sendFailed=%llu "
            "overload=%llu\n",
            server.world().players().size(),
            (unsigned long long)server.world().tick(),
            (unsigned long long)s.received, (unsigned long long)s.sent,
            (unsigned long long)s.bytesIn, (unsigned long long)s.bytesOut,
            (unsigned long long)s.invalid, (unsigned long long)s.disconnects,
            (unsigned long long)w.late, w.maxQueue, w.lastTickUs, w.maxTickUs,
            s.snapshotUs, (unsigned long long)s.sendFailures,
            (unsigned long long)overruns);
        const auto &replication = s.replication;
        std::printf(
            "[REPLICATION] outBps=%.1f inBps=%.1f bytesClientBps=%.1f "
            "snapshotBytes=%llu reliableBytes=%llu inputBytes=%llu "
            "wireOutBps=%.1f wireInBps=%.1f keyframes=%llu deltas=%llu "
            "baselineMisses=%llu recoveries=%llu p50=%zu p95=%zu p99=%zu "
            "max=%zu chunkMax=%zu ownerNearMediumFar=%llu/%llu/%llu/%llu "
            "interestUs=%.2f projectionUs=%.2f deltaEncodeUs=%.2f "
            "memoryBytes=%.0f commonHeaderBytes=%llu decodeFailures=%llu "
            "chunks=%llu entitiesSnapshot=%.2f\n",
            s.outboundBps, s.inboundBps,
            s.outboundBps /
                std::max(std::size_t{1}, server.world().players().size()),
            (unsigned long long)s.snapshotBytes,
            (unsigned long long)s.reliableBytes,
            (unsigned long long)s.inputBytes, s.wireOutboundBps,
            s.wireInboundBps, (unsigned long long)replication.keyframes,
            (unsigned long long)replication.deltas,
            (unsigned long long)replication.baselineMisses,
            (unsigned long long)replication.recoveries,
            replication.percentile(.5), replication.percentile(.95),
            replication.percentile(.99), replication.percentile(1),
            replication.maxChunk, (unsigned long long)replication.tierCounts[0],
            (unsigned long long)replication.tierCounts[1],
            (unsigned long long)replication.tierCounts[2],
            (unsigned long long)replication.tierCounts[3], s.interestUs,
            s.projectionUs, s.deltaEncodeUs, s.networkMemoryBytes,
            (unsigned long long)s.protocolHeaderBytesOut,
            (unsigned long long)s.decodeFailures,
            (unsigned long long)replication.chunks,
            double(replication.entities) /
                std::max(std::uint64_t{1}, replication.snapshots));
        const auto &combat = server.world().combat().stats();
        std::printf(
            "[COMBAT] active=%zu shots=%llu shotsPerSec=%.1f rejected=%llu "
            "hits=%llu kills=%llu respawns=%llu motionUs=%.2f collisionUs=%.2f "
            "bytes=%llu\n",
            server.world().combat().projectiles().size(),
            (unsigned long long)combat.shots,
            double(combat.shots) / (double(server.world().tick()) / 120),
            (unsigned long long)combat.rejectedFire,
            (unsigned long long)combat.hits, (unsigned long long)combat.kills,
            (unsigned long long)combat.respawns, combat.motionUs,
            combat.collisionUs, (unsigned long long)s.combatBytes);
        std::fflush(stdout);
        lastLog = now;
      }
      if (seconds && now - begin >= std::chrono::seconds(seconds))
        break;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    server.shutdown();
    std::printf("[NET] shutdown tick=%llu\n",
                (unsigned long long)server.world().tick());
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "[NET] %s\n", e.what());
    return 1;
  }
}
