#pragma once
#include "ofs/math.hpp"
#include <array>
#include <utility>

namespace ofs::aerodynamics {
// Bounded piecewise-smooth engineering tables, not measured aircraft polars.
template <std::size_t N>
inline double curve(double x, const std::array<std::pair<double,double>,N>& knots) {
  if (!std::isfinite(x) || x <= knots.front().first) return knots.front().second;
  for (std::size_t i=1;i<N;++i) if (x<knots[i].first) {
    const double t=(x-knots[i-1].first)/(knots[i].first-knots[i-1].first);
    return lerp(knots[i-1].second,knots[i].second,t*t*(3-2*t));
  }
  return knots.back().second;
}
inline double liftMach(double mach) {
  constexpr std::array<std::pair<double,double>,7> table{{{0,1},{.6,1},{.85,1.08},{1,1.04},{1.4,.90},{2,.75},{3,.60}}};
  return curve(mach,table);
}
inline double controlMach(double mach) {
  constexpr std::array<std::pair<double,double>,5> table{{{0,1},{.8,1},{1,.85},{2,.65},{3,.5}}};
  return curve(mach,table);
}
inline double separation(double alpha, double positiveStall) {
  // Negative-incidence separation starts earlier for cambered configurations.
  const double onset=alpha>=0?positiveStall:8*kDeg2Rad;
  constexpr std::array<std::pair<double,double>,5> table{{{0,0},{.5,.30},{1,.75},{2,1},{20,1}}};
  return curve((std::abs(alpha)-onset)/(10*kDeg2Rad),table);
}
inline double controlFlow(double alpha, double positiveStall) {
  return 1-.94*separation(alpha,positiveStall);
}
inline double vortexLift(double alpha, double mach, double strength) {
  // Leading-edge suction inspired shape, smoothly lost after vortex breakdown.
  constexpr std::array<std::pair<double,double>,7> breakdown{{{0,0},{8,0},{18,1},{30,1},{45,.45},{65,0},{180,0}}};
  const double a=std::abs(alpha);
  return strength*std::sin(alpha)*std::abs(std::sin(alpha))*std::cos(alpha)*
      curve(a*kRad2Deg,breakdown)*clamp(1-(mach-.6)/1.2,0,1);
}
} // namespace ofs::aerodynamics
