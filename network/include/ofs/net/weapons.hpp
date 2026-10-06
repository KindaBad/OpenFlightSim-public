#pragma once
#include "ofs/net/combat.hpp"
#include "ofs/weapons.hpp"
#include <map>
namespace ofs::net {
using weapons::EntityRef;
using weapons::WeaponType;
enum class WeaponActionKind : std::uint8_t {
  NextTarget,
  PreviousTarget,
  Lock,
  SelectIR,
  SelectRadar,
  Launch,
  Unlock
};
struct WeaponAction {
  std::uint64_t sequence{};
  Tick tick{};
  std::uint32_t generation{};
  WeaponActionKind kind{WeaponActionKind::NextTarget};
  std::uint8_t station{}; // Launch only, never a transform or hit claim.
};
struct Missile {
  std::uint64_t id{};
  EntityRef owner, target;
  WeaponType type{WeaponType::Infrared};
  Tick born{};
  weapons::MissileState state;
};
struct MissileNetState {
  std::uint64_t id{};
  EntityRef owner, target;
  WeaponType type{WeaponType::Infrared};
  Vec3 position, velocity;
  Quat attitude;
  weapons::MotorPhase motor{};
  weapons::SeekerPhase seeker{};
  double age{};
};
struct MissileEvent {
  MissileNetState missile;
  Tick tick{};
  bool detonation{};
};
struct WeaponsStats {
  std::uint64_t launches{}, rejected{}, detonations{}, hits{}, expired{},
      droppedEvents{};
  std::size_t peakMissiles{};
  double radarUs{}, missileUs{}, fuseUs{};
};
struct AircraftWeapons {
  weapons::Inventory inventory;
  weapons::Radar radar;
  std::map<Tick, WeaponAction> actions;
  std::uint64_t lastSequence{};
  Tick readyTick{};
  bool seekerReady{};
  EntityRef acquisitionTarget;
  weapons::SeekerState acquisition;
  weapons::Envelope envelope;
};
struct RadarNetState {
  std::uint32_t generation{};
  weapons::RadarMode mode{};
  EntityRef selected, locked;
  std::vector<weapons::Track> tracks;
  WeaponType weapon{WeaponType::Infrared};
  std::vector<WeaponType> stations;
  bool seekerReady{};
  weapons::Envelope envelope;
};
MissileNetState projectMissile(const Missile &, EntityId viewer);
bool missileInterest(const Missile &, EntityId viewer, Vec3 observer);
class MissileCombat {
public:
  static constexpr std::size_t capacity = 128;
  MissileCombat();
  bool launch(Tick, EntityRef owner, const State &, Vec3 station, WeaponType,
              const weapons::Track &target);
  void step(Tick, std::span<CombatTarget>,
            const std::map<EntityId, AircraftWeapons *> &, const Weather &,
            Combat &);
  void removeOwner(EntityId, Tick);
  const std::vector<Missile> &missiles() const { return missiles_; }
  std::vector<MissileEvent> takeEvents();
  WeaponsStats &stats() { return stats_; }
  const WeaponsStats &stats() const { return stats_; }

private:
  void emit(MissileEvent);
  std::uint64_t nextId_{std::uint64_t{1} << 63};
  std::vector<Missile> missiles_;
  std::vector<MissileEvent> events_;
  WeaponsStats stats_;
};
RadarNetState radarProjection(const AircraftWeapons &,
                              std::uint32_t generation);
} // namespace ofs::net
