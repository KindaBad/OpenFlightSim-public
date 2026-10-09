#pragma once
#include "ofs/aircraft.hpp"
#include "ofs/aircraft_definition.hpp"
#include "ofs/damage.hpp"
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace ofs::net {
using Tick = std::uint64_t;
using EntityId = std::uint64_t;
constexpr std::uint16_t protocolVersion =
    21; // v20 plus retuned Typhoon and JF-17 handling and the JF-17's four radar missiles
constexpr std::size_t maxPlayers = 64, maxPacket = 65536, maxBatch = 8;
constexpr std::size_t applicationPayload = 1100;
// Legacy full-world Snapshot is an offline measurement format only.
constexpr bool withinApplicationBudget(std::size_t bytes) {
  return bytes > 0 && bytes <= applicationPayload;
}
constexpr Tick maxTimelineTick =
    (Tick{1} << 53) - 1024; // exact binary64 presentation timeline
constexpr double tickSeconds = 1.0 / 120.0;
enum class Type : std::uint8_t {
  Hello = 1,
  Welcome,
  Joined,
  Left,
  Input,
  Snapshot,
  Reject,
  Ping,
  Pong,
  Fire,
  Combat,
  Spawn,
  Despawn,
  SnapshotChunk,
  SnapshotAck,
  WeaponAction,
  RadarState,
  MissileSpawn,
  MissileState,
  MissileRemove,
  Chat
};
// Chat is printable ASCII, like pilot names, and short enough to read in flight.
constexpr std::size_t maxChatText = 120;
struct Command {
  std::uint64_t sequence{};
  Tick tick{};
  Controls controls;
};
struct Life {
  double health{100};
  std::uint16_t ammo{600};
  std::uint32_t generation{};
  Tick readyTick{}, respawnTick{};
  std::uint32_t kills{}, deaths{};
  bool alive() const { return health > 0; }
};
struct FireCommand {
  std::uint64_t sequence{};
  Tick tick{};
  std::uint32_t generation{};
  std::uint8_t weapon{}; // Only gun 0 exists.
  bool held{};
};
// The part of the airframe a hit damaged.
using HitRegion = ofs::DamagePart;
// Flare and Chaff are a countermeasure leaving `owner`, with the decoy's
// identity in `projectile`. Serviced is an aircraft repaired, refuelled and
// rearmed after standing on the ground. Ejected is a pilot leaving `target`,
// which is destroyed in the same tick.
enum class CombatKind : std::uint8_t {
  Shot = 1,
  Hit,
  Destroyed,
  Respawn,
  Flare,
  Chaff,
  Serviced,
  Ejected
};
struct CombatEvent {
  std::uint64_t id{}; // Unique event ID, separate from projectile ID.
  Tick tick{};
  CombatKind kind{CombatKind::Shot};
  std::uint64_t projectile{};
  EntityId owner{}, target{};
  std::uint32_t generation{};
  HitRegion region{HitRegion::Fuselage};
  double health{}, lifetime{};
  Vec3 position, velocity;
};
constexpr std::size_t maxCombatEvents = 10, combatEventWireBytes = 102;
static_assert(
    24 + 1 + maxCombatEvents * combatEventWireBytes <= applicationPayload,
    "combat batch exceeds application payload; explicitly repartition it");
// Explicit wire projection: no renderer handles, configuration or debug forces.
struct Aircraft {
  EntityId id{};
  std::uint64_t acknowledged{};
  State state;
  Controls controls;
  Life life{100, 0};
  AircraftType type{AircraftType::A320};
};
struct Message {
  Type type{Type::Hello};
  Tick tick{};
  std::uint64_t sequence{};
  EntityId entity{};
  // Hello and Joined: pilot name. Reject: reason. Chat: the line, from `entity`
  // (zero for a notice from the server itself).
  std::string text;
  std::uint16_t snapshotHz{24};
  Aircraft aircraft;
  std::vector<Command> commands;
  std::vector<Aircraft> aircrafts;
  std::uint32_t generation{};
  FireCommand fire;
  std::vector<CombatEvent> events;
  AircraftType aircraftType{AircraftType::A320}; // Hello request only.
  std::uint64_t baseline{};
  bool recovery{};
  Weather weather; // shared authoritative atmosphere in Welcome/Snapshot
};
std::vector<std::uint8_t> encode(const Message &);
bool decode(std::span<const std::uint8_t>, Message &, std::string &reason);
bool validControls(const Controls &);
bool sameControls(const Controls &, const Controls &);
bool finiteState(const State &);
Controls quantizeControls(const Controls &);
// Explicit owner projection; never a memcpy of State. Also used by lifecycle.
std::vector<std::uint8_t> encodeAircraft(const Aircraft &);
bool decodeAircraft(std::span<const std::uint8_t>, Aircraft &);
} // namespace ofs::net
