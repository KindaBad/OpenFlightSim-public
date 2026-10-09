#pragma once
// How battle damage is shown.
//
// The simulation says how much of each part is left; this says where that part
// is on each aircraft, so the surface shader can tear the right piece of the
// mesh away and the effects can put smoke and fire where the damage is.
// Positions are in the aircraft's reference body axes (X forward, Y right, Z
// down), the frame the weapon stations and exhausts are already given in.

#include "ofs/damage.hpp"

#include <algorithm>
#include <array>

namespace ofs::client {

// Damage to each part, 0 intact .. 1 destroyed, indexed by DamagePart.
struct DamageView {
  std::array<float, damagePartCount> parts{};
  float operator[](DamagePart part) const { return parts[std::size_t(part)]; }
  bool any() const {
    return std::any_of(parts.begin(), parts.end(), [](float damage) { return damage > .004f; });
  }
};

// `hitPoints` is the aircraft's remaining health out of 100. The fuselage has
// no part health of its own in flight: it shows the hit points lost, or the
// structural damage of a hard landing, whichever is worse.
inline DamageView damageView(const State& state, double hitPoints) {
  DamageView view;
  for (std::size_t part = 0; part < damagePartCount; ++part)
    view.parts[part] = float(std::clamp(1. - partHealth(state, DamagePart(part)), 0., 1.));
  view.parts[std::size_t(DamagePart::Fuselage)] =
      std::max(view.parts[std::size_t(DamagePart::Fuselage)], float(std::clamp(1. - hitPoints / 100., 0., 1.)));
  return view;
}

// The pieces of an airframe that can be torn away.
struct DamageGeometry {
  double wingRoot{}, wingTip{};  // |y| of the stub that always remains, and of the tip
  double wingAft{}, wingFore{};  // x range the outer wings occupy, clear of tailplanes and canards
  double finBase{}, finTop{};    // heights above the reference origin
  double finFore{};              // fins stand aft of this x
};

// Measured from each delivered model; an unknown type gets proportions scaled
// from its wingtips.
inline DamageGeometry damageGeometry(AircraftType type) {
  switch (type) {
    case AircraftType::A320: return {6.9, 17.9, -9.0, 1.0, 2.4, 8.2, -14.5};
    case AircraftType::Typhoon: return {1.5, 5.4, -5.6, 1.6, 1.15, 3.6, -2.0};
    case AircraftType::SR71: return {5.3, 8.5, -10.6, 3.0, 1.3, 3.1, -6.2};
    case AircraftType::Su57: return {3.3, 7.05, -5.45, 1.5, .6, 2.2, -4.6};
    case AircraftType::JF17: return {1.2, 4.79, -2.7, 1.0, .90, 3.28, -2.4};
    case AircraftType::B52: return {6.0, 27.8, -12.5, 11.0, 1.7, 8.8, -15.0};
  }
  const auto& visual = aircraftDefinition(type).visual;
  const double tip = std::abs(visual.wingtip[1].y);
  return {tip * .3, tip, visual.wingtip[1].x - tip * .3, visual.wingtip[1].x + tip * .6, tip * .15, tip * .5, -tip * .4};
}

// A point on a wing: `span` 0 at the stub, 1 at the tip.
inline Vec3 wingPoint(AircraftType type, bool right, double span) {
  const auto geometry = damageGeometry(type);
  const Vec3 tip = aircraftDefinition(type).visual.wingtip[right ? 1 : 0];
  const Vec3 root{(geometry.wingAft + geometry.wingFore) * .5, right ? geometry.wingRoot : -geometry.wingRoot, tip.z};
  return root + (tip - root) * span;
}

// Where the remaining wing ends once `damage` of it has gone. The outer panel
// goes first: nothing is lost below 40 %, and a destroyed wing is a stub.
// pbr_fs.glsl cuts the mesh at the same place.
inline double wingRemaining(double damage) {
  const double t = std::clamp((damage - .4) / .6, 0., 1.);
  return 1.25 + (.08 - 1.25) * t * t * (3 - 2 * t);
}

// The same for the fin, by height: 0 at its base, 1 at its top.
inline double finRemaining(double damage) {
  const double t = std::clamp((damage - .4) / .6, 0., 1.);
  return 1.25 + (.1 - 1.25) * t * t * (3 - 2 * t);
}

// The middle of the fin, or of the pair of fins.
inline Vec3 finPoint(AircraftType type) {
  const auto geometry = damageGeometry(type);
  return {geometry.finFore - (geometry.finTop - geometry.finBase) * .6, 0, -(geometry.finBase + geometry.finTop) * .5};
}

// Where a part's smoke and fire come from.
inline Vec3 damagePoint(AircraftType type, DamagePart part) {
  const auto& definition = aircraftDefinition(type);
  switch (part) {
    case DamagePart::LeftWing: return wingPoint(type, false, .45);
    case DamagePart::RightWing: return wingPoint(type, true, .45);
    case DamagePart::Tail: return finPoint(type);
    case DamagePart::LeftEngine: return definition.visual.exhaust[0];
    case DamagePart::RightEngine: return definition.visual.exhaust[1];
    default: return {definition.visual.radius * .08, 0, -definition.visual.radius * .03};
  }
}

}  // namespace ofs::client
