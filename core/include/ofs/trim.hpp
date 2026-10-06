#pragma once
#include "ofs/simulator.hpp"
namespace ofs {
// Offline calm-air, wings-level steady flight. gamma positive = climb.
// No runtime feedback or state constraints are installed by this helper.
struct TrimRequest {
  double altitude{1000}, tas{110}, gamma{0};
  double flap01{0}, gear01{0};
};
struct TrimResult {
  bool converged{false};
  int iterations{0};
  State state{};
  Controls controls{};
  Vec3 residual_force_world{}, residual_moment_body{};
};
TrimResult solveTrim(const AircraftConfig& config = a320Config(), const TrimRequest& request = {});
}
