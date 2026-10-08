#pragma once
// The pilot's tolerance of load.
//
// Sustained positive g drains blood from the head: sight narrows and greys,
// then goes, and if the load is held the pilot loses consciousness and lets go
// of the controls for a few seconds. Negative g does the opposite and reddens
// the view. A fit pilot with a g-suit, straining, is assumed. The rates are
// chosen for the game and are not a physiological prediction.
//
// The model belongs to whoever is flying: a client applies it to its own
// pilot, and nothing about it is replicated.

#include <algorithm>
#include <cmath>

namespace ofs {

struct PilotStrain {
  // Positive load below this can be held indefinitely; negative load above it.
  static constexpr double kSustainedG = 6.0, kNegativeG = -2.0;
  // Sight begins to go at this much strain, and is gone at 1.
  static constexpr double kGreyOut = .35;
  static constexpr double kUnconsciousSeconds = 3.5, kWakingSeconds = 2.5;

  double strain{};       // 0 rested .. 1 sight gone
  double red{};          // 0 blacking out .. 1 redding out: which way the load was
  double unconscious{};  // seconds left without the controls
  double waking{};       // seconds left of coming round, sight returning

  // Advances by `dt` seconds under a load factor of `g`.
  void update(double g, double dt) {
    if (!std::isfinite(g) || !(dt > 0)) return;
    dt = std::min(dt, .25);
    if (unconscious > 0) {
      // Out cold: the load no longer matters until the pilot comes round.
      unconscious = std::max(0., unconscious - dt);
      strain = 1;
      if (unconscious == 0) waking = kWakingSeconds;
      return;
    }
    const double over = g > kSustainedG ? g - kSustainedG : 0;
    const double under = g < kNegativeG ? kNegativeG - g : 0;
    if (over > 0 || under > 0) {
      // Nine g is good for about six seconds from rested, twelve for under three.
      const double rate = over > 0 ? std::pow(over, 1.3) / 25 : std::pow(under, 1.3) / 8;
      strain += rate * dt;
      red += ((under > 0 ? 1. : 0.) - red) * std::min(1., dt * 4);
    } else {
      // Blood comes back faster the lighter the load.
      const double ease = g >= 0 ? 1 - .55 * std::clamp(g / kSustainedG, 0., 1.) : 1 - .55 * std::clamp(g / kNegativeG, 0., 1.);
      strain -= (waking > 0 ? .4 : .26) * ease * dt;
    }
    waking = std::max(0., waking - dt);
    if (strain >= 1) {
      strain = 1;
      unconscious = kUnconsciousSeconds;
      waking = 0;
    }
    strain = std::max(0., strain);
    if (strain == 0) red = 0;
  }

  // The pilot cannot fly: the stick and the trigger are let go.
  bool incapacitated() const { return unconscious > 0; }
  // How much of the view is lost, 0 clear .. 1 nothing.
  double vision() const {
    if (unconscious > 0) return 1;
    const double t = std::clamp((strain - kGreyOut) / (1 - kGreyOut), 0., 1.);
    return t * t * (3 - 2 * t);
  }
  void reset() { *this = {}; }
};

}  // namespace ofs
