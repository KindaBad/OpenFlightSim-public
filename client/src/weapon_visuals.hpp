#pragma once
// Presentation of the weapons an aircraft carries.
//
// The server reports which stations still hold a missile ten times a second,
// while a launched missile reaches the screen through the interpolated missile
// stream a moment later. StoreDisplay bridges the two, so a missile is seen on
// its pylon until the same missile is seen leaving it.

#include "ofs/aircraft_definition.hpp"
#include "ofs/weapons.hpp"

#include <array>
#include <cstdint>
#include <map>

namespace ofs::client {

// World position of a weapon station on an aircraft drawn in `state`.
inline Vec3 stationPosition(const State& state, AircraftType type, const Vec3& station) {
  return state.pos_ned + state.att.rotate(station - loadedCg(aircraftDefinition(type).flight, state));
}

// Extra distance to show a just-launched missile ahead of its interpolated
// position. The pilot's own aircraft is drawn at the predicted present while
// missiles are drawn slightly in the past; without this a missile would leave
// the rail from somewhere behind the wing. The correction fades as the missile
// flies clear.
inline Vec3 launchLead(const Vec3& ownVelocity, double lagSeconds, double age) {
  const double t = clamp((age - .4) / 1.2, 0, 1);
  return ownVelocity * (clamp(lagSeconds, 0, .4) * (1 - t * t * (3 - 2 * t)));
}

class StoreDisplay {
 public:
  // `mounted` has a bit per station still loaded; `launched[type]` counts this
  // aircraft's missiles of that weapon type that appeared this frame. Returns
  // the stations to draw a missile on.
  std::uint8_t update(std::uint64_t entity, std::uint32_t generation, const weapons::Inventory& loadout,
                      std::uint8_t mounted, const std::array<unsigned, 3>& launched, double dt) {
    auto& view = views_[entity];
    view.seen = true;
    if (view.generation != generation || !view.known) {
      view = {};
      view.seen = view.known = true;
      view.generation = generation;
      view.mounted = mounted;
    }
    auto released = launched;
    for (std::size_t i = 0; i < loadout.stations.size() && i < view.hold.size(); ++i) {
      const std::uint8_t bit = std::uint8_t(1u << i);
      if ((view.mounted & bit) && !(mounted & bit)) view.hold[i] = kHoldSeconds;
      if (mounted & bit) view.hold[i] = 0;
      if (view.hold[i] <= 0) continue;
      auto& count = released[std::size_t(loadout.stations[i].mounted) % released.size()];
      view.hold[i] = count ? 0 : view.hold[i] - dt;
      if (count) --count;
    }
    view.mounted = mounted;
    std::uint8_t shown = mounted;
    for (std::size_t i = 0; i < view.hold.size(); ++i)
      if (view.hold[i] > 0) shown |= std::uint8_t(1u << i);
    return shown;
  }
  // Drops aircraft that were not updated since the last call.
  void endFrame() {
    std::erase_if(views_, [](const auto& entry) { return !entry.second.seen; });
    for (auto& [entity, view] : views_) {
      (void)entity;
      view.seen = false;
    }
  }

 private:
  // Longer than the missile stream's interpolation delay, shorter than a blink.
  static constexpr double kHoldSeconds = .35;
  struct View {
    std::uint32_t generation{};
    std::uint8_t mounted{};
    std::array<double, 8> hold{};
    bool seen{}, known{};
  };
  std::map<std::uint64_t, View> views_;
};

}  // namespace ofs::client
