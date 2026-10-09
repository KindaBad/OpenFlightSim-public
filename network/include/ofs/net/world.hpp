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
  std::string name;
  // Whoever last damaged this aircraft is credited if it then flies into the ground.
  EntityId lastAttacker{};
  Tick lastAttacked{};
  // Ticks it has stood on the ground toward being repaired and rearmed.
  Tick standing{};
  // When a bot may next answer a missile with a decoy.
  Tick botDecoyReady{};
  // Ticks an empty pylon has waited toward the next missile, in a game that
  // hands them out in flight.
  Tick reloading{};
  // The same wait for the next share of flares, chaff and gun rounds.
  Tick resupplying{};
  // The side flown for in a team game, and the bomb load a bomber asked for.
  Team team{Team::None};
  std::uint8_t loadout{};
  // Kills already counted toward the side's score.
  std::uint32_t countedKills{};
};
// A structure of a team game: how much of it stands, when it will have been
// rebuilt, and the gun or launcher it may be.
struct Site {
  double health{100};
  Tick rebuilt{}, ready{};
  Life gun;
};
// How long after a hit a crash still counts for the attacker.
constexpr Tick killCreditTicks = 20 * 120;
// Flares and chaff in the air at once; the oldest goes when another is needed.
constexpr std::size_t maxDecoys = 256;
class World {
public:
  explicit World(bool airborne = true, std::optional<GunConfig> gun = std::nullopt);
  EntityId join(AircraftType type = AircraftType::A320, std::string name = {},
                Team team = Team::None, unsigned loadout = 0);
  // A team game: Red against Blue, first to `scoreLimit` points. Set before
  // anyone joins.
  void setTeams(unsigned scoreLimit);
  bool teams() const { return teams_; }
  // Stands the base guns and missile sites down, for exercises and tests.
  void setDefences(bool active) { defences_ = active; }
  // Moves a pilot to the other side, in a fresh aircraft.
  bool setTeam(EntityId, Team);
  TeamStatus teamStatus() const;
  const std::vector<Site> &sites() const { return sites_; }
  // What happened on the ground since last asked, for everyone to read.
  std::vector<std::string> takeNotices();
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
  const std::vector<weapons::Decoy> &decoys() const { return decoys_; }
  // Drops a flare or a bundle of chaff from an aircraft that has one left and
  // is not still cycling its dispenser. Reported to clients as a combat event.
  bool releaseDecoy(EntityId, weapons::DecoyType);
  // Ticks an aircraft has to stand on the ground to be repaired and rearmed.
  static Tick serviceTicks();
  // Seconds between missiles handed to an aircraft with an empty pylon, one at
  // a time and in the air as well as on the ground. A quarter of its flares,
  // chaff and gun rounds come back on the same interval. Zero leaves rearming
  // to a stop on the ground.
  void setMissileReload(double seconds);
  double missileReload() const { return double(missileReload_) * tickSeconds; }
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
  // A wing that has gone takes the stores under it along.
  void loseStores(Player &, State &);
  // Ends a life lost to the ground, or to a wing that snapped, rather than
  // directly to a weapon.
  void destroy(EntityId, Player &, Vec3 position, Vec3 velocity);
  // Whether a stop on the ground would restore anything, and the stop itself:
  // the airframe mended, the tanks, gun, pylons and dispensers refilled.
  bool needsService(const Player &) const;
  void service(EntityId, Player &);
  void reload(Player &);
  // The team game: bombs on structures, the defences, the score and the round.
  void applyBlasts();
  void defend(std::span<CombatTarget>);
  void score(Team, int points);
  void startRound();
  Team smallerTeam() const;
  bool teams_{}, defences_{true};
  unsigned scoreLimit_{};
  std::uint32_t score_[2]{};
  Team winner_{Team::None};
  Tick restart_{};
  std::vector<Site> sites_;
  std::vector<std::string> notices_;
  Tick missileReload_{};
  std::vector<weapons::Decoy> decoys_;
  std::uint64_t nextDecoy_{1};
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
