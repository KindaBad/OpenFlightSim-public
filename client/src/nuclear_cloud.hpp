#pragma once
// The cloud of a nuclear burst.
//
// Neither soft particles nor a solid surface looks like one: particles do not
// hold a shape from across the map and a surface looks like clay. The cloud is
// a volume, marched in client/shaders/nuke_fs.glsl through four bodies given
// here as numbers: the fireball that becomes a vortex ring under a dome, the
// stem, the base surge running out along the ground and a collar of
// condensation round the stem. This file says where each is and how hot, from
// the age of the burst alone, so every client draws the same cloud.
// Sized for the eye, to stand over the ground this weapon flattens, and not
// from weapon effects data.

#include <algorithm>
#include <array>
#include <cmath>

namespace ofs::client {

// Seconds a cloud is drawn for, the last of them thinning to nothing.
inline constexpr double kCloudSeconds = 190, kCloudFadeSeconds = 45;

struct NuclearVolume {
  double height;                 // of the ring's core above the ground, m
  double ring, tube;             // radius of the ring's core and of its tube, m
  double stem;                   // radius of the stem, m
  double surge, surgeHeight;     // how far the base surge has run and how high it stands, m
  double surgeDensity;
  double heat;                   // 1 is orange heat; above it white
  double opacity;
  double roll;                   // how far the billows have welled up through the cap, in tube radii times 0.16
  double scroll;                 // how far the gas in the stem has climbed, m
  double skirtHeight, skirtRadius, skirt;
  double climb;                  // how fast the cap is rising, m/s
  std::array<float, 3> capTint, dustTint;
  std::array<float, 3> glow;     // colour and strength of the fireball's light
  double reach;                  // distance that light carries, m
  double boundRadius, boundHeight;
};

inline NuclearVolume nuclearVolume(double age) {
  const auto smooth = [](double low, double high, double value) {
    const double t = std::clamp((value - low) / (high - low), 0., 1.);
    return t * t * (3 - 2 * t);
  };
  const auto mix = [](double a, double b, double t) { return a + (b - a) * std::clamp(t, 0., 1.); };
  age = std::max(age, 0.);
  NuclearVolume v{};
  // The fireball is at nearly full size within a second and a half, then
  // swells slowly as it turns into the ring.
  v.tube = age < 3 ? 640 * std::pow(std::max(age, .02) / 3, .38) : 640 + 520 * (1 - std::exp(-(age - 3) / 26.));
  // It lies on the ground it was set off on, and lifts off after two seconds.
  const double lifted = 6100 * (1 - std::exp(-std::max(0., age - 2) / 30.));
  v.height = .42 * std::min(v.tube, 640.) + lifted;
  v.climb = age > 2 ? 6100 / 30. * std::exp(-(age - 2) / 30.) : 0;
  // The ring opens as the fireball rises, and goes on spreading once it has stopped.
  v.ring = 1750 * (1 - std::exp(-std::max(0., age - 3.5) / 27.)) + 7 * std::max(0., age - 70);
  v.stem = std::max(1., 300 * smooth(1.5, 9, age) + 190 * (1 - std::exp(-age / 45.)));
  v.surge = 320 + 3500 * (1 - std::exp(-age / 21.));
  v.surgeHeight = 70 + 250 * (1 - std::exp(-age / 14.));
  v.surgeDensity = .85 * smooth(.2, 2.5, age) * (1 - .8 * smooth(40, 120, age));
  v.heat = age < 1.2 ? 1.35 : 1.35 * std::exp(-(age - 1.2) / 13.);
  v.opacity = 1 - smooth(kCloudSeconds - kCloudFadeSeconds, kCloudSeconds, age);
  v.roll = 5.5 * (1 - std::exp(-std::max(0., age - 3.5) / 24.));
  v.scroll = .8 * lifted + 18 * age;
  v.skirtHeight = v.height * .60;
  v.skirtRadius = v.stem * 2.0;
  v.skirt = .55 * smooth(9, 17, age) * (1 - smooth(48, 80, age));
  // Brown with soot and burnt air at first, paling as water condenses in it.
  const double pale = smooth(16, 85, age);
  v.capTint = {float(mix(.17, .80, pale)), float(mix(.13, .78, pale)), float(mix(.105, .76, pale))};
  v.dustTint = {float(mix(.44, .60, pale)), float(mix(.33, .52, pale)), float(mix(.20, .40, pale))};
  const double light = age < .5 ? 1 : std::exp(-(age - .5) / 5.5);
  // White at first, yellowing and reddening as it cools.
  const double cooled = smooth(.5, 14, age);
  v.glow = {float(light * 60), float(light * mix(52, 27, cooled)), float(light * mix(42, 9, cooled))};
  v.reach = 2600 + 1400 * smooth(0, 6, age);
  v.boundRadius = std::max({v.ring + v.tube * 1.5, v.surge + v.surgeHeight * 3.6, v.skirtRadius * 2});
  v.boundHeight = v.height + v.tube * 1.5;
  return v;
}

}  // namespace ofs::client
