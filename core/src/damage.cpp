#include "ofs/damage.hpp"

#include <algorithm>
#include <initializer_list>
#include <cmath>

namespace ofs {
namespace {

// Surface slots as configureSurfaces lays them out.
constexpr std::size_t kLeftWing = 0, kRightWing = 1, kPitchLeft = 2, kPitchRight = 3, kFin = 4, kBody = 5;
// A holed wing drags more than a clean one. The flight model scales a wing's
// whole force by its health, so the stored factor also undoes that scaling:
// the drag actually felt rises with damage until little wing is left to drag.
constexpr double kWingDragGain = 1.5, kWingDragLimit = 8, kBodyDragGain = 1.5;
double wingDrag(double health) {
  return std::min(kWingDragLimit, (1 + kWingDragGain * (1 - health)) / std::max(health, .05));
}
// Tailplanes behind a shattered fin keep some authority, so the aircraft can
// still be pitched after the tail has gone.
constexpr double kTailplaneFloor = .35, kTailplaneShare = .6;

bool engine(DamagePart part) { return part == DamagePart::LeftEngine || part == DamagePart::RightEngine; }

double distanceToSegment(Vec3 point, Vec3 a, Vec3 b) {
  const Vec3 along = b - a;
  const double length2 = along.norm2();
  const double t = length2 > 0 ? std::clamp((point - a).dot(along) / length2, 0., 1.) : 0.;
  return (point - (a + along * t)).norm();
}

}  // namespace

double partStrength(DamagePart part) {
  switch (part) {
    case DamagePart::LeftWing:
    case DamagePart::RightWing: return 100;
    case DamagePart::Tail: return 80;
    case DamagePart::LeftEngine:
    case DamagePart::RightEngine: return 60;
    default: return 100;
  }
}

double partHitPointShare(DamagePart part) {
  switch (part) {
    case DamagePart::LeftWing:
    case DamagePart::RightWing:
    case DamagePart::Tail: return .5;
    case DamagePart::LeftEngine:
    case DamagePart::RightEngine: return .6;
    default: return 1;
  }
}

double partHealth(const State& state, DamagePart part) {
  switch (part) {
    case DamagePart::LeftWing: return state.surface_health[kLeftWing];
    case DamagePart::RightWing: return state.surface_health[kRightWing];
    case DamagePart::Tail: return state.surface_health[kFin];
    case DamagePart::LeftEngine: return state.engine_health[0];
    case DamagePart::RightEngine: return state.engine_health[1];
    default: return state.surface_health[kBody];
  }
}

double applyPartDamage(const AircraftConfig& config, State& state, DamagePart part, double damage) {
  if (!std::isfinite(damage) || damage <= 0) return 0;
  const bool wasDestroyed = part != DamagePart::Fuselage && partDestroyed(state, part);
  const double loss = damage / partStrength(part);
  switch (part) {
    case DamagePart::LeftWing:
    case DamagePart::RightWing: {
      const std::size_t wing = part == DamagePart::LeftWing ? kLeftWing : kRightWing;
      auto& health = state.surface_health[wing];
      health = std::max(0., health - loss);
      state.surface_drag[wing] = std::max(state.surface_drag[wing], wingDrag(health));
      break;
    }
    case DamagePart::Tail: {
      auto& fin = state.surface_health[kFin];
      fin = std::max(0., fin - loss);
      if (config.pitch_arm < 0)  // tailplanes sit with the fin; canards do not
        for (const std::size_t surface : {kPitchLeft, kPitchRight})
          state.surface_health[surface] =
              std::max(std::min(state.surface_health[surface], kTailplaneFloor),
                       state.surface_health[surface] - loss * kTailplaneShare);
      break;
    }
    case DamagePart::LeftEngine:
    case DamagePart::RightEngine: {
      auto& health = state.engine_health[part == DamagePart::LeftEngine ? 0 : 1];
      health = std::max(0., health - loss);
      if (health < engineFailureHealth) health = 0;
      break;
    }
    default:
      // The fuselage carries the hit points themselves; all it shows in flight is drag.
      state.surface_drag[kBody] = std::min(1 + kBodyDragGain, state.surface_drag[kBody] + kBodyDragGain * damage / 100);
      break;
  }
  return damage * (wasDestroyed ? 1 : partHitPointShare(part));
}

double wingLoadLimit(double health) {
  if (health >= weakenedWingHealth) return 1e9;
  // From a little over cruising flight for a wing hanging by its spar, up to
  // most of a fighter's envelope for one that is only holed.
  return 3 + 14 * std::max(0., health);
}

bool applyOverstress(State& state, double loadFactor) {
  if (!std::isfinite(loadFactor)) return false;
  bool failed = false;
  for (const std::size_t wing : {kLeftWing, kRightWing}) {
    auto& health = state.surface_health[wing];
    if (health <= 0 || std::abs(loadFactor) <= wingLoadLimit(health)) continue;
    health = 0;
    state.surface_drag[wing] = std::max(state.surface_drag[wing], wingDrag(0));
    failed = true;
  }
  return failed;
}

EngineBay engineBay(const AircraftDefinition& definition, unsigned index) {
  // Sized from the aircraft, so a transport's nacelle and a fighter's buried
  // engine both get a bay of believable proportions.
  const double radius = definition.visual.radius;
  EngineBay bay;
  bay.aft = definition.visual.exhaust[index < 2 ? index : 1];
  bay.fore = bay.aft + Vec3{std::clamp(.3 * radius, 2.8, 5.), 0, 0};
  bay.radius = std::clamp(.07 * radius, .6, 1.3);
  return bay;
}

DamagePart classifyHit(const AircraftDefinition& definition, DamagePart region, Vec3 point) {
  if (engine(region)) return region;
  // The collision spheres stand a little proud of the skin, so a hit counts
  // for an engine from slightly outside its bay.
  constexpr double kMargin = .75;
  double nearest = 1e9;
  DamagePart part = region;
  for (unsigned index = 0; index < definition.flight.engine_count && index < 2; ++index) {
    const auto bay = engineBay(definition, index);
    const double distance = distanceToSegment(point, bay.aft, bay.fore) - bay.radius;
    if (distance < kMargin && distance < nearest) {
      nearest = distance;
      part = index == 0 ? DamagePart::LeftEngine : DamagePart::RightEngine;
    }
  }
  return part;
}

}  // namespace ofs
