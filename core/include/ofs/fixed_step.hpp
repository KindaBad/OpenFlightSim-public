#pragma once
#include <cmath>
#include <cstdint>

namespace ofs {
// Scheduling only: physics receives the identical dt each tick. Long stalls
// drop elapsed time, never enlarge a physics tick. No graphics dependencies.
class FixedStepClock {
 public:
  static constexpr double tick = 1.0 / 120.0;
  static constexpr unsigned max_steps = 16;
  static constexpr double max_frame = 0.25;
  template <class Tick> unsigned advance(double elapsed, Tick&& simulate) {
    if (!std::isfinite(elapsed) || elapsed <= 0.0) return 0;
    if (elapsed > max_frame) {
      dropped_ += elapsed - max_frame;
      elapsed = max_frame;
    }
    accumulator_ += elapsed;
    unsigned steps = 0;
    while (accumulator_ + 1e-12 >= tick && steps < max_steps) {
      simulate(tick);
      accumulator_ -= tick;
      if (accumulator_ < 0.0) accumulator_ = 0.0;
      ++steps;
      ++ticks_;
    }
    if (accumulator_ >= tick) {
      const double remainder = std::fmod(accumulator_, tick);
      dropped_ += accumulator_ - remainder;
      accumulator_ = remainder;
    }
    return steps;
  }
  double alpha() const { return accumulator_ / tick; }
  double droppedTime() const { return dropped_; }
  std::uint64_t ticks() const { return ticks_; }
  void reset() { accumulator_ = 0; dropped_ = 0; ticks_ = 0; }
 private:
  double accumulator_{0};
  double dropped_{0};
  std::uint64_t ticks_{0};
};
}  // namespace ofs
