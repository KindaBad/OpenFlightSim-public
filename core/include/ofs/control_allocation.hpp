#pragma once
#include "ofs/math.hpp"
#include <array>
namespace ofs {
// Deterministic projected coordinate descent of a bounded quadratic objective:
// ||C*x-request||^2 + regularization*sum(x_i^2/weight_i).
// Bounds can combine travel, rate, failures and available actuator authority.
std::array<double,5> boundedControlAllocation(const std::array<Vec3,5>& columns,
  const Vec3& request,const std::array<double,5>& weight,
  const std::array<double,5>& lower,const std::array<double,5>& upper,
  double regularization=.0025);
}
