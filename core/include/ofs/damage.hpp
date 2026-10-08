#pragma once
// Regional battle damage.
//
// A hit is attributed to one part of the airframe. The part's condition is the
// surface and engine health the flight model already reads, so a holed wing
// lifts less and a wrecked engine stops without a second damage state to
// replicate, predict or replay. Strengths and shares are gameplay values, not
// a lethality claim about any real aircraft.

#include "ofs/aircraft_definition.hpp"

#include <cstddef>
#include <cstdint>

namespace ofs {

// Stable wire values: combat events carry the part that was struck.
enum class DamagePart : std::uint8_t { Fuselage, LeftWing, RightWing, Tail, LeftEngine, RightEngine };
inline constexpr std::size_t damagePartCount = 6;

// Hit points, of the airframe's 100, that a part absorbs before it is destroyed.
double partStrength(DamagePart part);
// Share of a hit on an intact part that the aircraft as a whole also loses. A
// wing or the tail soaks up most of a hit; the fuselage passes all of it on.
double partHitPointShare(DamagePart part);
// Condition of a part, 1 intact .. 0 destroyed. For the fuselage this is the
// structural integrity that ground impacts wear down.
double partHealth(const State& state, DamagePart part);
inline bool partDestroyed(const State& state, DamagePart part) { return partHealth(state, part) <= 0; }
// Below this an engine has taken too much to keep running and flames out.
inline constexpr double engineFailureHealth = .3;

// Applies `damage` hit points to one part and returns the hit points the whole
// aircraft loses. A part that is already destroyed protects nothing, so hits
// there count in full.
double applyPartDamage(const AircraftConfig& config, State& state, DamagePart part, double damage);

// The part struck by a hit at `point`, in the aircraft's reference body axes,
// that entered a collision sphere of `region`. Engines have no spheres of
// their own: a hit beside one is the engine's, whichever sphere it entered.
DamagePart classifyHit(const AircraftDefinition& definition, DamagePart region, Vec3 point);

// A shot-up wing no longer carries the loads it was built for. Below this
// health it has a load limit, and pulling harder than that snaps it off.
inline constexpr double weakenedWingHealth = .5;
// Load factor, in g, a wing of `health` survives; unlimited while it is sound.
double wingLoadLimit(double health);
// Breaks off every weakened wing that `loadFactor` overloads, and reports
// whether one failed.
bool applyOverstress(State& state, double loadFactor);

// Both wings are gone: nothing is left to fly on.
inline bool wingless(const State& state) {
  return partDestroyed(state, DamagePart::LeftWing) && partDestroyed(state, DamagePart::RightWing);
}

// Where an engine sits for damage and its effects: a capsule running forward
// from the exhaust, in reference body axes.
struct EngineBay {
  Vec3 aft, fore;
  double radius{};
};
EngineBay engineBay(const AircraftDefinition& definition, unsigned engine);

}  // namespace ofs
