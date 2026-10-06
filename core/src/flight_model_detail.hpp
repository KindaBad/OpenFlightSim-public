#pragma once
#include "ofs/simulator.hpp"
#include <initializer_list>
#include <utility>
namespace ofs::detail {
inline double smooth(double x) {
  x = clamp(x, 0, 1);
  return x * x * (3 - 2 * x);
}
inline double machCurve(double mach,
                 std::initializer_list<std::pair<double, double>> knots) {
  auto last = *knots.begin();
  for (auto point : knots) {
    if (mach < point.first)
      return lerp(last.second, point.second,
                  smooth((mach - last.first) / (point.first - last.first)));
    last = point;
  }
  return last.second;
}
inline Controls actualControls(const State &s, const Controls &input) {
  Controls c = input;
  c.elevator_stick = s.elevator;
  c.elevator_trim = 0;
  c.aileron_stick = s.aileron;
  c.rudder_pedal = s.rudder;
  c.flap01 = s.flap;
  c.spoiler01 = s.spoiler;
  return c;
}
inline Vec3 inverseInertia(const Simulator::MassProperties &m, const Vec3 &torque) {
  return m.tensor().solve(torque);
}
inline Vec3 inertiaMomentum(const Simulator::MassProperties &m, const Vec3 &omega) {
  return m.tensor().apply(omega);
}

inline double gustPhase(double f, const Vec3 &p, double t) {
  return f * t + 0.00035 * (p.x * f * 1.7 + p.y * f * 1.1) + f;
}

}
