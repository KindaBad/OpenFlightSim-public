#pragma once
#include "ofs/net/combat.hpp"
#include "ofs/weapons.hpp"
#include <algorithm>
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
  Unlock,
  Flare,
  Chaff,
  Eject, // the pilot abandons the aircraft; `station` is unused
  // A bomber asks for the bomb load numbered `station`, which it is given the
  // next time it is rearmed.
  Loadout
};
struct WeaponAction {
  std::uint64_t sequence{};
  Tick tick{};
  std::uint32_t generation{};
  WeaponActionKind kind{WeaponActionKind::NextTarget};
  std::uint8_t station{}; // Launch and Loadout only, never a transform or hit claim.
};
struct Missile {
  std::uint64_t id{};
  EntityRef owner, target;
  WeaponType type{WeaponType::Infrared};
  Tick born{};
  weapons::MissileState state;
  // The decoy its seeker is following in place of its target, or zero.
  std::uint64_t decoy{};
  // The side that fired it, in a team game: it does that side no harm.
  Team team{Team::None};
};
// A bomb that has gone off, for whatever stands on the ground near it.
struct Blast {
  WeaponType type{WeaponType::Bomb500};
  EntityRef owner;
  Team team{Team::None};
  Vec3 position;
};
// Ground defences fire as entities of their own: this bit and the index of
// the structure in ofs::structures().
constexpr EntityId defenceEntityBase = EntityId{1} << 62;
inline bool defenceEntity(EntityId id) { return (id >> 62) == 1; }
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
  unsigned bombsReleased{};
  Tick readyTick{};
  bool seekerReady{};
  // The mounted infrared seeker hunts on its own: it takes the hottest target
  // near the nose and follows it inside its gimbal until launch or loss.
  EntityRef acquisitionTarget;
  weapons::SeekerState acquisition;
  double lockProgress{}; // 0..1 toward a releasable launch
  // A target the pilot broke away from is passed over for a moment, so the
  // seeker moves on to the next one instead of retaking it.
  EntityRef seekerRejected;
  Tick seekerRejectedUntil{};
  weapons::Envelope envelope;
  // Countermeasures left, how many have gone, and when the next may go.
  std::uint8_t flares{}, chaff{};
  unsigned decoysReleased{};
  Tick decoyReady{};
};
// Stores still hanging on a nearby aircraft, for presentation only.
struct Loadout {
  EntityRef entity;
  std::uint8_t mounted{}; // bit per station of Inventory::reset(type)
};
constexpr std::size_t maxLoadouts = 16;
struct RadarNetState {
  std::uint32_t generation{};
  weapons::RadarMode mode{};
  EntityRef selected, locked;
  std::vector<weapons::Track> tracks;
  WeaponType weapon{WeaponType::Infrared};
  std::vector<WeaponType> stations;
  bool seekerReady{};
  // What the selected weapon is looking at: the seeker's own target for an
  // infrared missile, the radar lock for an active one.
  EntityRef seekerTarget;
  double lockProgress{};
  weapons::Envelope envelope;
  std::vector<Loadout> loadouts;
  std::uint8_t flares{}, chaff{};
  // A bomber's load: what it carries, how many are left and which load it
  // has asked for next.
  WeaponType bombType{WeaponType::None};
  std::uint16_t bombs{};
  std::uint8_t loadout{};
};
std::uint8_t mountedMask(const weapons::Inventory &);
// Mean engine power as a heat seeker sees it. An engine that was shot out
// burns on while there is fuel to feed it, so it does not hide the aircraft.
inline double heatPower(const State &state) {
  double power = 0;
  for (unsigned e = 0; e < 2; ++e)
    power += state.engine_health[e] <= 0 && state.fuel_mass != 0
                 ? std::max(state.n1[e], .55)
                 : state.n1[e];
  return power * .5;
}
MissileNetState projectMissile(const Missile &, EntityId viewer);
bool missileInterest(const Missile &, EntityId viewer, Vec3 observer);
class MissileCombat {
public:
  // Room for two bombers' whole loads in the air beside the missiles.
  static constexpr std::size_t capacity = 320;
  MissileCombat();
  // `autonomous` is a launch with nobody to guide it: the seeker is on its own
  // from the rail.
  bool launch(Tick, EntityRef owner, const State &, Vec3 station, WeaponType,
              const weapons::Track &target, Team team = Team::None,
              bool autonomous = false);
  // Lets a bomb go from `station`, the `count`th this aircraft has dropped.
  bool release(Tick, EntityRef owner, const State &, Vec3 station, WeaponType,
               unsigned count, Team team = Team::None);
  std::vector<Blast> takeBlasts();
  // `decoys` are the flares and chaff in the air, which seekers may follow
  // instead of their targets.
  void step(Tick, std::span<CombatTarget>,
            const std::map<EntityId, AircraftWeapons *> &, const Weather &,
            Combat &, std::span<const weapons::Decoy> decoys = {});
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
  std::vector<Blast> blasts_;
  WeaponsStats stats_;
};
RadarNetState radarProjection(const AircraftWeapons &,
                              std::uint32_t generation);
} // namespace ofs::net
