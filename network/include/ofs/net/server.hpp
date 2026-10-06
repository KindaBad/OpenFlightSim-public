#pragma once
#include "ofs/net/replication.hpp"
#include "ofs/net/transport.hpp"
#include "ofs/net/weapons_protocol.hpp"
#include "ofs/net/world.hpp"
#include <map>
namespace ofs::net {
struct ServerConfig {
  std::string bind{"0.0.0.0"};
  std::uint16_t port{27020};
  unsigned maxClients{16}, snapshotHz{24};
  bool airborne{true};
  unsigned bots{}; // Server-owned opponents; reserve at least one human slot.
  std::optional<GunConfig>
      gun; // Scenario/server override; never client editable.
};
struct ServerStats {
  std::uint64_t received{}, sent{}, bytesIn{}, bytesOut{}, invalid{},
      disconnects{}, sendFailures{}, snapshotCount{}, publicationCount{},
      decodeFailures{}, protocolHeaderBytesOut{};
  double snapshotUs{}, combatSerializationUs{}, transportUs{}, linkStatsUs{};
  std::size_t pendingTransportBytesPeak{};
  std::uint64_t combatBytes{}, combatMessages{}, snapshotBytes{},
      reliableBytes{}, inputBytes{}, radarBytes{}, missileBytes{},
      weaponMessages{};
  std::size_t maxWeaponPacket{};
  double interestUs{}, projectionUs{}, deltaEncodeUs{}, networkMemoryBytes{},
      outboundBps{}, inboundBps{}, wireOutboundBps{}, wireInboundBps{};
  ReplicationStats replication;
};
class Server {
public:
  explicit Server(const ServerConfig &config = {});
  ~Server();
  void poll();
  void step();
  void shutdown();
  const World &world() const { return world_; }
  World &world() { return world_; }
  const ServerStats &stats() const { return stats_; }
  std::uint16_t port() const;

private:
  struct Session {
    EntityId entity{};
    Tick connected{}, window{};
    unsigned messages{}, commands{}, fires{}, strikes{};
    unsigned publicationAccumulator{};
    bool publicationDue{};
    LinkStats link;
    ReplicationSender replication;
    WeaponReplicationSender weapons;
    unsigned weaponActions{};
    bool reliableFailure{};
  };
  ServerConfig config_;
  Transport transport_;
  World world_;
  std::map<Connection, Session> sessions_;
  ServerStats stats_;
  std::uint64_t sequence_{};
  unsigned snapshotAccumulator_{};
  void send(Connection, const Message &, bool);
  void broadcast(const Message &, bool);
  void remove(Connection, const std::string &, bool reject = false);
};
} // namespace ofs::net
