#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace ofs::net {
using Connection = std::uint32_t;
struct TransportEvent {
  enum class Kind { Connected, Disconnected, Packet };
  Kind kind{};
  Connection connection{};
  std::vector<std::uint8_t> bytes;
  std::string reason;
};
struct LinkStats {
  int pingMs{-1};
  float bytesInPerSecond{}, bytesOutPerSecond{};
  int pendingBytes{}, unfragmentedPayload{};
};
// GNS is confined to this implementation; all users exchange ordinary values.
class Transport {
public:
  Transport();
  ~Transport();
  Transport(const Transport &) = delete;
  Transport &operator=(const Transport &) = delete;
  void listen(const std::string &address, std::uint16_t port);
  Connection connect(const std::string &address, std::uint16_t port);
  std::vector<TransportEvent> poll();
  bool send(Connection, std::span<const std::uint8_t>, bool reliable);
  // At most 16 independently bounded application messages; GNS takes ownership
  // of its allocated buffers and schedules the connection once for the batch.
  std::vector<bool> sendBatch(Connection,
                              std::span<const std::vector<std::uint8_t>>);
  void close(Connection, const std::string &reason, bool linger = false);
  LinkStats stats(Connection) const;
  std::uint16_t port() const;
  // Process-global GNS development packet simulator; explicitly opt in only.
  static void conditions(const std::string &preset);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace ofs::net
