#pragma once
#include "ofs/net/combat.hpp"
#include "ofs/net/weapons.hpp"
#include "ofs/simulator.hpp"
#include <map>
namespace ofs::net {
struct WorldStats {
  std::uint64_t rejected{}, late{}, applied{};
  std::size_t maxQueue{};
  double lastTickUs{}, maxTickUs{};
};
struct Player {
  Simulator sim;
  AircraftWeapons weapons;
  unsigned spawnSlot{};
  std::map<Tick, Command> inputs;
  std::uint64_t acknowledged{}, highestSequence{};
  Tick lastInput{};
  Life life;
  std::map<Tick, FireCommand> fireInputs;
  std::uint64_t fireSequence{};
  Tick fireTick{}, lastFireInput{};
  bool firing{};
  AircraftType type{AircraftType::A320};
  bool bot{};
  EntityId botTarget{};
  Tick evadeUntil{}, missileReady{};
  double lastHealth{100};
};
class World {
public:
  explicit World(bool airborne = true, std::optional<GunConfig> gun = std::nullopt);
  EntityId join(AircraftType type = AircraftType::A320);
  EntityId joinBot(AircraftType type = AircraftType::Typhoon);
  std::size_t botCount() const;
  void leave(EntityId);
  bool enqueue(EntityId, const std::vector<Command> &,
               std::uint32_t generation = 0);
  bool enqueueWeapon(EntityId, const WeaponAction &);
  // Stores on the armed aircraft closest to `viewer`, near enough to be seen.
  std::vector<Loadout> loadoutsNear(EntityId viewer) const;
  MissileCombat &missiles() { return missiles_; }
  const MissileCombat &missiles() const { return missiles_; }
  bool enqueueFire(EntityId, const FireCommand &);
  Combat &combat() { return combat_; }
  const Combat &combat() const { return combat_; }
  void step();
  void setWeather(const Weather&);
  const Weather& weather() const { return weather_; }
  Message snapshot() const;
  Aircraft aircraft(EntityId) const;
  Tick tick() const { return tick_; }
  const std::map<EntityId, Player> &players() const { return players_; }
  const WorldStats &stats() const { return stats_; }

private:
  Weather weather_;
  Combat combat_;
  MissileCombat missiles_;
  void spawn(EntityId, Player &);
  void controlBot(EntityId, Player &);
  bool airborne_;
  struct Spawn { State state; Controls controls; };
  std::map<AircraftType, Spawn> spawns_;
  std::optional<GunConfig> gunOverride_;
  const GunConfig& gunFor(AircraftType type) const;
  Tick tick_{};
  EntityId nextId_{1};
  std::map<EntityId, Player> players_;
  WorldStats stats_;
};
} // namespace ofs::net
