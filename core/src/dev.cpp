#include "ofs/aircraft_definition.hpp"

namespace ofs {
// A test aircraft with no top speed, on the SR-71's airframe and model. Its
// engines keep their full rating at any height and speed and, at full power,
// cancel all of its drag (unlimited_speed), so it gains speed for as long as
// the throttle is open: about 2.5 g with reheat. Fuel lasts for hours, and it
// handles like a fighter rather than the Blackbird. Not a real aircraft.
AircraftConfig devConfig() {
  AircraftConfig c = sr71Config();
  c.unlimited_speed = true;
  c.variable_inlets = false;
  c.thrust_sl_static_each = 300000;
  c.afterburner_thrust_each = 600000;
  c.afterburner_threshold = .82;
  c.engine_tau = .8;
  c.dry_tsfc = 1e-7;
  c.reheat_tsfc = 2e-7;
  c.max_pitch_rate = .55;
  c.max_roll_rate = 2.4;
  c.response_time = .35;
  c.g_positive = 9;
  c.g_negative = -3;
  c.alpha_limit = 20 * kDeg2Rad;
  c.actuator_rate = 3;
  c.control_q_limit = 1e7;
  return c;
}
} // namespace ofs
