#pragma once
#include "ofs/math.hpp"
#include <array>
namespace ofs {
// Deterministic projected coordinate descent of a bounded quadratic objective:
// ||C*x-request||^2 + regularization*sum((x_i-anchor_i)^2/weight_i).
// Bounds can combine travel, rate, failures and available actuator authority.
// The anchor is where each increment is pulled toward; a zero anchor keeps
// motion small, a -position anchor also returns the surface to neutral.
std::array<double,5> boundedControlAllocation(const std::array<Vec3,5>& columns,
  const Vec3& request,const std::array<double,5>& weight,
  const std::array<double,5>& lower,const std::array<double,5>& upper,
  double regularization=.0025,const std::array<double,5>& anchor={});
}
