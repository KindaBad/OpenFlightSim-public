#pragma once
#include "ofs/net/protocol.hpp"
#include "ofs/simulator.hpp"

namespace ofs::net {
struct BotDecision {
  Controls controls;
  bool firing{};
};
// Commands the ordinary flight model; never assigns an aircraft transform.
BotDecision flyBot(const Simulator &, const State *target, const GunConfig &,
                   Tick tick, bool evading = false, double turnSide = 1);
} // namespace ofs::net
