#pragma once
#include "ofs/net/weapons.hpp"
#include <deque>
#include <set>
namespace ofs::net {
constexpr std::size_t missileWireBytes = 63, maxMissileRecords = 16;
static_assert(24 + 1 + maxMissileRecords * missileWireBytes <=
              applicationPayload);
struct WeaponMessage {
  Type type{Type::WeaponAction};
  Tick tick{};
  std::uint64_t sequence{};
  EntityId entity{};
  WeaponAction action;
  RadarNetState radar;
  std::vector<MissileNetState> missiles;
  std::vector<MissileEvent> removals;
};
std::vector<std::uint8_t> encodeWeapon(const WeaponMessage &);
bool decodeWeapon(std::span<const std::uint8_t>, WeaponMessage &);
// Per-client AOI lifecycle independent of lossy state snapshots. No baselines
// or partial-frame assembly: every compact record is independently applicable.
class WeaponReplicationSender {
public:
  struct Packet {
    std::vector<std::uint8_t> bytes;
    bool reliable{};
  };
  std::vector<Packet> build(Tick, EntityId, Vec3, const RadarNetState &,
                            std::span<const Missile>,
                            std::span<const MissileEvent>);
  std::size_t known() const { return known_.size(); }

private:
  std::map<std::uint64_t, MissileNetState> known_;
  std::uint64_t sequence_{};
};
class WeaponReplicationReceiver {
public:
  bool receive(const WeaponMessage &, Tick now);
  void expire(Tick now);
  void reset();
  RadarNetState radar;
  const std::map<std::uint64_t, MissileNetState> &missiles() const {
    return missiles_;
  }
  std::vector<MissileNetState> sample(double tick) const;
  std::vector<MissileEvent> takeDetonations();
  std::vector<MissileEvent> takeMissileTerminations();
  std::size_t tombstones() const { return retired_.size(); }

private:
  Tick radarTick_{};
  std::map<std::uint64_t, MissileNetState> missiles_;
  std::map<std::uint64_t, Tick> sampled_, retired_;
  std::vector<MissileEvent> detonations_;
  std::vector<MissileEvent> terminations_;
  std::map<std::uint64_t, std::deque<std::pair<Tick, MissileNetState>>>
      history_;
};
} // namespace ofs::net
