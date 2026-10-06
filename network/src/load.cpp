#include "ofs/net/cli.hpp"
#include "ofs/net/client.hpp"
#include "ofs/net/server.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
int main(int argc, char **argv) {
  using namespace ofs::net;
  try {
    const bool combat = argc == 2 && std::string_view(argv[1]) == "--combat";
    const bool external = argc == 4 && std::string_view(argv[1]) == "--server";
    const bool peers = argc == 4 && std::string_view(argv[1]) == "--peers";
    if (argc > 1 && !combat && !external && !peers)
      throw std::invalid_argument(
          "ofs_net_load [--combat|--server PORT CLIENTS|--peers PORT CLIENTS]");
    const auto externalPort = external || peers ? number(argv[2], 1, 65535) : 0;
    const auto externalCount = external || peers ? number(argv[3], 1, 64) : 0;
    if (peers) {
      std::vector<std::unique_ptr<Client>> clients;
      for (unsigned i = 0; i < externalCount; ++i) {
        auto client = std::make_unique<Client>("external" + std::to_string(i));
        client->connect("127.0.0.1", static_cast<std::uint16_t>(externalPort));
        clients.push_back(std::move(client));
      }
      auto start = std::chrono::steady_clock::now(), last = start, next = start;
      const auto period =
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<double>(tickSeconds));
      bool complete = false;
      while (std::chrono::steady_clock::now() - start <
             std::chrono::seconds(10)) {
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        unsigned steps = 0;
        while (now >= next && steps++ < 16) {
          for (auto &client : clients)
            if (client->ready())
              client->predict(client->prediction().simulator().controls());
          next += period;
        }
        complete =
            complete ||
            std::all_of(clients.begin(), clients.end(), [&](const auto &c) {
              return c->ready() && c->remotes().size() == externalCount - 1 &&
                     c->stats().snapshots > 100;
            });
        for (auto &client : clients)
          client->poll(dt);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      if (!complete)
        throw std::runtime_error(
            "external peers did not replicate the complete world");
      return 0;
    }
    std::puts("clients,ticks,server_tick_mean_us,server_tick_p95_us,snapshot_"
              "encode_send_mean_us,payload_in_Bps,payload_out_Bps,wire_in_Bps,"
              "wire_out_Bps,send_failures,queue_peak,combat_fanout_Bps,peak_"
              "rounds,motion_mean_us,collision_mean_us,combat_encode_mean_us,"
              "full_server_mean_us,full_server_p95_us,full_server_p99_us,"
              "full_server_max_us,snapshot_p99_us,snapshot_max_us,transport_"
              "mean_us,link_stats_mean_us,interest_mean_us,projection_mean_us,"
              "delta_encode_mean_us,pending_transport_bytes_peak");
    for (unsigned count :
         (external ? std::vector<unsigned>{externalCount}
          : combat ? std::vector<unsigned>{2, 8, 16}
                   : std::vector<unsigned>{2, 8, 16, 32, 64})) {
      ServerConfig config;
      config.bind = "127.0.0.1";
      config.port = static_cast<std::uint16_t>(externalPort);
      config.maxClients = count;
      Server server(config);
      std::vector<std::unique_ptr<Client>> clients;
      for (unsigned i = 0; !external && i < count; ++i) {
        auto c = std::make_unique<Client>("load" + std::to_string(i),
                                          combat ? ofs::AircraftType::Typhoon
                                                 : ofs::AircraftType::A320);
        c->connect("127.0.0.1", server.port());
        clients.push_back(std::move(c));
      }
      auto start = std::chrono::steady_clock::now(), last = start, next = start;
      const auto period =
          std::chrono::duration_cast<std::chrono::steady_clock::duration>(
              std::chrono::duration<double>(tickSeconds));
      std::vector<double> tickUs, serverUs, snapshotTimes;
      double pollingUs = 0;
      double snapshotsUs = 0, motionUs = 0, collisionUs = 0, combatEncodeUs = 0;
      double transportUs = 0, linkUs = 0, queryUs = 0, projectionUs = 0,
             encodeUs = 0;
      std::uint64_t snapshotCount = 0;
      while (std::chrono::steady_clock::now() - start <
             std::chrono::seconds(8)) {
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;
        const auto pollStart = std::chrono::steady_clock::now();
        server.poll();
        pollingUs += std::chrono::duration<double, std::micro>(
                         std::chrono::steady_clock::now() - pollStart)
                         .count();
        unsigned steps = 0;
        while (now >= next && steps < 16) {
          const auto stepStart = std::chrono::steady_clock::now();
          server.step();
          serverUs.push_back(pollingUs +
                             std::chrono::duration<double, std::micro>(
                                 std::chrono::steady_clock::now() - stepStart)
                                 .count());
          pollingUs = 0;
          tickUs.push_back(server.world().stats().lastTickUs);
          motionUs += server.world().combat().stats().motionUs;
          collisionUs += server.world().combat().stats().collisionUs;
          combatEncodeUs += server.stats().combatSerializationUs;
          if (server.stats().publicationCount != snapshotCount) {
            snapshotCount = server.stats().publicationCount;
            snapshotsUs += server.stats().snapshotUs;
            snapshotTimes.push_back(server.stats().snapshotUs);
            transportUs += server.stats().transportUs;
            linkUs += server.stats().linkStatsUs;
            queryUs += server.stats().interestUs;
            projectionUs += server.stats().projectionUs;
            encodeUs += server.stats().deltaEncodeUs;
          }
          for (auto &c : clients)
            if (c->ready()) {
              c->setFiring(combat);
              c->predict(c->prediction().simulator().controls());
            }
          next += period;
          ++steps;
        }
        for (auto &c : clients)
          c->poll(dt);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      double wireIn = 0, wireOut = 0;
      for (const auto &c : clients) {
        if (!c->ready() || c->remotes().size() != count - 1 ||
            !c->stats().snapshots)
          throw std::runtime_error("load client missing world");
        wireIn += c->stats().wireOut;
        wireOut += c->stats().wireIn;
      }
      if (external) {
        wireIn = server.stats().wireInboundBps;
        wireOut = server.stats().wireOutboundBps;
      }
      if (server.world().players().size() != count ||
          server.world().tick() < 930 || server.stats().sendFailures)
        throw std::runtime_error("server load limit exceeded");
      double total = 0;
      for (auto t : tickUs)
        total += t;
      std::sort(tickUs.begin(), tickUs.end());
      double serverTotal = 0;
      for (auto t : serverUs)
        serverTotal += t;
      std::sort(serverUs.begin(), serverUs.end());
      std::sort(snapshotTimes.begin(), snapshotTimes.end());
      const auto &stats = server.stats();
      std::printf(
          "%u,%llu,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%.1f,%llu,%zu,%.1f,%"
          "zu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
          "%.3f,%.3f,%.3f,%.3f,%.3f,%zu\n",
          count, (unsigned long long)server.world().tick(),
          total / tickUs.size(), tickUs[tickUs.size() * 95 / 100],
          snapshotsUs / snapshotCount, double(stats.bytesIn) / 8,
          double(stats.bytesOut) / 8, wireIn, wireOut,
          (unsigned long long)stats.sendFailures,
          server.world().stats().maxQueue, double(stats.combatBytes) / 8,
          server.world().combat().stats().peakProjectiles,
          motionUs / tickUs.size(), collisionUs / tickUs.size(),
          combatEncodeUs / tickUs.size(), serverTotal / serverUs.size(),
          serverUs[serverUs.size() * 95 / 100],
          serverUs[serverUs.size() * 99 / 100], serverUs.back(),
          snapshotTimes[snapshotTimes.size() * 99 / 100], snapshotTimes.back(),
          transportUs / snapshotCount, linkUs / snapshotCount,
          queryUs / snapshotCount, projectionUs / snapshotCount,
          encodeUs / snapshotCount, stats.pendingTransportBytesPeak);
      std::fflush(stdout);
      if (external)
        server.shutdown();
      for (auto &c : clients)
        c->disconnect();
      for (unsigned i = 0; i < 200 && !server.world().players().empty(); ++i) {
        server.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      if (!server.world().players().empty())
        throw std::runtime_error("load entities leaked");
    }
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "LOAD FAIL %s\n", e.what());
    return 1;
  }
}
