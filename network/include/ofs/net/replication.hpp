#pragma once
#include "ofs/net/protocol.hpp"
#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>
namespace ofs::net {
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t snapshotPayload = applicationPayload, baselineLimit = 64,
                      fragmentLimit = 16, assemblyLimit = 4;
constexpr Tick keyframeTicks = 240, assemblyTicks = 120;
constexpr Tick recoveryRetryTicks = 30; // 250 ms, including ACK cooldown
static_assert(snapshotPayload == 1100 && fragmentLimit <= 255);
static_assert(fragmentLimit * snapshotPayload <= maxPacket);
// Snapshot IDs never wrap: zero denotes no baseline and wire baselines are
// strictly smaller than their frame. Fail explicitly on counter exhaustion.
std::uint64_t nextSnapshotSequence(std::uint64_t current);
enum class Tier : std::uint8_t { Owner, Near, Medium, Far, Outside };
Tier interestTier(double meters, Tier previous);
unsigned tierPeriod(Tier);
// Bounded inline network field storage avoids thousands of small heap objects
// in each per-client baseline copy. Capacities follow the explicit wire schema.
struct NetFields {
  static constexpr std::array<unsigned, 16> capacities{
      25, 24, 24, 32, 24, 24, 45, 35, 40, 49, 8, 8, 32, 112, 2, 136};
  static_assert(
      [] {
        for (auto capacity : capacities)
          if (capacity > 255)
            return false;
        return true;
      }(),
      "field capacity exceeds inline length encoding");
  static constexpr auto offsets = [] {
    std::array<unsigned, 16> result{};
    for (unsigned i = 1; i < 16; ++i)
      result[i] = result[i - 1] + capacities[i - 1];
    return result;
  }();
  static constexpr unsigned storageBytes = offsets.back() + capacities.back();
  std::array<std::uint8_t, storageBytes> bytes{};
  static_assert(storageBytes + 20 + 32 + 99 <= snapshotPayload,
                "future full field record exceeds application budget");
  std::array<std::uint8_t, 16> lengths{};
  struct ConstView {
    const std::uint8_t *data{};
    unsigned length{};
    std::size_t size() const { return length; }
    bool empty() const { return length == 0; }
    operator std::span<const std::uint8_t>() const { return {data, length}; }
    std::uint8_t operator[](unsigned i) const { return data[i]; }
    bool operator!=(ConstView other) const {
      return length != other.length ||
             !std::equal(data, data + length, other.data);
    }
  };
  struct View : ConstView {
    std::uint8_t *writable{};
    std::uint8_t *sizeField{};
    unsigned limit{};
    View &operator=(const Bytes &value) {
      return operator=(std::span<const std::uint8_t>(value));
    }
    View &operator=(std::span<const std::uint8_t> value) {
      if (value.size() > limit)
        throw std::invalid_argument("network field capacity");
      std::copy(value.begin(), value.end(), writable);
      *sizeField = std::uint8_t(value.size());
      length = value.size();
      return *this;
    }
    std::uint8_t &operator[](unsigned i) { return writable[i]; }
  };
  View operator[](unsigned i) {
    const unsigned offset = offsets[i];
    return {{bytes.data() + offset, lengths[i]},
            bytes.data() + offset,
            &lengths[i],
            capacities[i]};
  }
  ConstView operator[](unsigned i) const {
    const unsigned offset = offsets[i];
    return {bytes.data() + offset, lengths[i]};
  }
};
// Network-only quantized field groups. Simulation never stores these.
struct AircraftNetState {
  bool owner{};
  Tier tier{Tier::Near};
  Tick sampledTick{};
  NetFields fields;
};
AircraftNetState projectAircraft(const Aircraft &, bool owner, Tier, Tick,
                                 Vec3 reference);
bool expandAircraft(EntityId, const AircraftNetState &, Vec3 reference,
                    Aircraft &);
// Small sorted contiguous entity table: one baseline allocation instead of a
// tree node per aircraft. IDs keep serialization order deterministic.
class NetEntities {
public:
  using value_type = std::pair<EntityId, AircraftNetState>;
  using iterator = std::vector<value_type>::iterator;
  using const_iterator = std::vector<value_type>::const_iterator;
  iterator begin() { return data_.begin(); }
  iterator end() { return data_.end(); }
  const_iterator begin() const { return data_.begin(); }
  const_iterator end() const { return data_.end(); }
  std::size_t size() const { return data_.size(); }
  std::size_t capacity() const { return data_.capacity(); }
  void reserve(std::size_t count) {
    if (count > maxPlayers)
      throw std::length_error("entity count");
    data_.reserve(count);
  }
  iterator find(EntityId id) {
    auto it = lower(id);
    return it != end() && it->first == id ? it : end();
  }
  const_iterator find(EntityId id) const {
    auto it = lower(id);
    return it != end() && it->first == id ? it : end();
  }
  bool contains(EntityId id) const { return find(id) != end(); }
  AircraftNetState &at(EntityId id) {
    auto it = find(id);
    if (it == end())
      throw std::out_of_range("entity");
    return it->second;
  }
  const AircraftNetState &at(EntityId id) const {
    auto it = find(id);
    if (it == end())
      throw std::out_of_range("entity");
    return it->second;
  }
  AircraftNetState &operator[](EntityId id) {
    auto it = lower(id);
    if (it == end() || it->first != id) {
      if (size() >= maxPlayers)
        throw std::length_error("entity count");
      it = data_.insert(it, {id, {}});
    }
    return it->second;
  }
  iterator erase(iterator it) { return data_.erase(it); }

private:
  iterator lower(EntityId id) {
    return std::lower_bound(begin(), end(), id, [](const auto &a, EntityId b) {
      return a.first < b;
    });
  }
  const_iterator lower(EntityId id) const {
    return std::lower_bound(begin(), end(), id, [](const auto &a, EntityId b) {
      return a.first < b;
    });
  }
  std::vector<value_type> data_;
};
struct NetFrame {
  std::uint64_t sequence{}, baseline{};
  Tick tick{};
  Vec3 reference;
  Weather weather;
  NetEntities entities;
};
std::vector<Bytes> packetize(const NetFrame &, const NetFrame *baseline);
struct ReplicationStats {
  std::uint64_t snapshots{}, keyframes{}, deltas{}, recoveries{},
      baselineMisses{}, decodeFailures{}, duplicates{}, expiredAssemblies{},
      stale{}, chunks{}, bytes{}, entities{}, rejectedAcks{}, staleAcks{},
      recoveryRequests{}, recoveryCoalesced{}, recoveryKeyframes{},
      recoveryRetries{}, recoveryCompleted{};
  std::size_t maxChunk{}, historyPeak{}, assemblyPeak{};
  std::array<std::uint64_t, 4> tierCounts{};
  double queryUs{}, projectionUs{}, deltaEncodeUs{}, decodeUs{};
  std::deque<std::size_t> snapshotSizes;
  void record(std::size_t bytes);
  std::size_t percentile(double fraction) const;
};
class InterestGrid {
public:
  void rebuild(std::span<const Aircraft>);
  std::vector<EntityId> query(Vec3 origin) const;
  const std::map<EntityId, const Aircraft *> &aircrafts() const {
    return aircrafts_;
  }
  AircraftNetState remote(EntityId, Tier, Tick, Vec3) const;
  double indexUs() const { return indexUs_; }
  double projectionUs() const { return projectionUs_; }

private:
  using Cell = std::tuple<std::int64_t, std::int64_t, std::int64_t>;
  static Cell cell(Vec3);
  std::map<Cell, std::vector<EntityId>> cells_;
  std::map<EntityId, const Aircraft *> aircrafts_;
  std::map<EntityId, AircraftNetState> remote_;
  double indexUs_{}, projectionUs_{};
};
struct ReplicationOutput {
  std::vector<Message> lifecycle;
  std::vector<Bytes> packets;
};
class ReplicationSender {
public:
  ReplicationOutput build(Tick, EntityId owner, std::span<const Aircraft>,
                          const InterestGrid &, const Weather & = {});
  bool acknowledge(std::uint64_t sequence, bool recovery = false);
  const ReplicationStats &stats() const { return stats_; }
  std::size_t historySize() const { return history_.size(); }
  std::size_t memoryBytes() const;
  std::size_t historyMemoryBytes() const { return historyBytes_; }
  std::uint64_t acknowledged() const { return acknowledged_; }
  bool interested(EntityId id) const { return known_.contains(id); }

private:
  std::deque<NetFrame> history_;
  std::size_t historyBytes_{};
  std::map<EntityId, Aircraft> known_;
  std::map<EntityId, Tier> tiers_;
  std::map<EntityId, Tick> sentAt_, loadingAt_;
  std::uint64_t sequence_{}, acknowledged_{};
  Tick lastKeyframe_{}, lastBuildTick_{}, lastRecoveryTick_{};
  std::uint64_t recoverySequence_{}, recoveryFirstSequence_{};
  enum class Recovery { Normal, Requested, Waiting };
  Recovery recovery_{Recovery::Requested};
  bool recoveryPending_{};
  ReplicationStats stats_;
};
class ReplicationReceiver {
public:
  // Apply only complete frames. Incomplete/invalid/stale input never mutates
  // authority.
  std::optional<NetFrame> receive(std::span<const std::uint8_t>, Tick now,
                                  EntityId expectedOwner = 0);
  bool expire(Tick now);
  bool spawn(const Aircraft &, Tick);
  void despawn(EntityId, Tick);
  std::uint64_t acknowledged() const { return acknowledged_; }
  bool needsRecovery() const { return recovery_; }
  const ReplicationStats &stats() const { return stats_; }
  std::size_t historySize() const { return history_.size(); }
  std::size_t assemblySize() const { return assemblies_.size(); }
  std::size_t assemblyMemoryBytes() const;
  std::size_t memoryBytes() const;
  std::size_t historyMemoryBytes() const { return historyBytes_; }

private:
  struct Assembly {
    NetFrame header;
    Tick arrived{};
    std::uint8_t count{};
    std::map<unsigned, Bytes> chunks;
  };
  std::deque<NetFrame> history_;
  std::size_t historyBytes_{};
  std::map<std::uint64_t, Assembly> assemblies_;
  std::map<EntityId, std::pair<Aircraft, Tick>> known_;
  std::map<EntityId, Tick> tombstones_;
  std::uint64_t acknowledged_{};
  bool recovery_{true};
  ReplicationStats stats_;
};
} // namespace ofs::net
