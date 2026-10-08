#include "ofs/net/bot_ai.hpp"
#include <algorithm>
#include <cmath>

namespace ofs::net {
BotDecision flyBot(const Simulator &sim, const State *target, const GunConfig &gun,
                   Tick tick, bool evading, double turnSide) {
  const auto &s = sim.state();
  const auto flight = sim.instruments();
  BotDecision out{sim.controls(), false};
  auto &c = out.controls;
  c.gear01 = c.flap01 = c.spoiler01 = c.brake01 = c.steering = 0;
  c.maneuver_mode = false;
  const double speed = std::max(60., flight.tas);
  Vec3 aim = s.att.rotate({1500, 0, 0});
  double range = 0;
  if (target) {
    const auto muzzle = s.pos_ned + s.att.rotate(gun.muzzle - loadedCg(sim.config(), s));
    const Vec3 relative = target->pos_ned - muzzle;
    const Vec3 velocity = target->vel_ned - s.vel_ned;
    range = relative.norm();
    // Intercept in the moving shooter's frame, including bullet drop.
    const double a = velocity.norm2() - gun.muzzleVelocity * gun.muzzleVelocity;
    const double b = 2 * relative.dot(velocity);
    const double discriminant = b * b - 4 * a * relative.norm2();
    double lead = range / gun.muzzleVelocity;
    if (a < -1 && discriminant >= 0)
      lead = (-b - std::sqrt(discriminant)) / (2 * a);
    lead = std::clamp(lead, 0., 2.);
    aim = relative + velocity * lead - Vec3{0, 0, .5 * kG0 * lead * lead};
    const auto body = s.att.inverseRotate(aim);
    out.firing = !evading && range > 100 && range < std::min(1200., gun.range) &&
        body.x > 0 && std::hypot(body.y, body.z) < std::max(2., body.x * .009) &&
        tick % 192 < 72;
    // Break away before overshooting; damaged bots jink before re-engaging.
    if (range < 180 || evading) {
      aim = s.att.rotate({700, turnSide * 900, -120});
      out.firing = false;
    }
  } else {
    // Gentle patrol with altitude hold until a live human becomes available.
    aim = s.att.rotate({1500, turnSide * 250, 0});
    aim.z = (-1000 - s.pos_ned.z) * .8;
  }
  const double headingError = std::remainder(std::atan2(aim.y, aim.x) -
      flight.hdg_deg * kDeg2Rad, 2 * kPi);
  double desiredBank = std::clamp(std::atan(speed * .55 * headingError / kG0),
                                 -70 * kDeg2Rad, 70 * kDeg2Rad);
  const double flightPathBias = target ? std::clamp((range-1000.)/1800.,0.,1.) : 1.;
  double desiredPitch = std::clamp(std::atan2(-aim.z, std::hypot(aim.x, aim.y)) +
      flightPathBias * std::clamp(flight.alpha_deg, 2., 10.) * kDeg2Rad,
      -20 * kDeg2Rad, 25 * kDeg2Rad);
  // The ground ahead matters as much as the ground below: among mountains a
  // level flight path runs into a hillside within seconds.
  double clearance = flight.agl;
  for (const double seconds : {2., 4., 7.}) {
    const Vec3 ahead = s.pos_ned + s.vel_ned * seconds;
    clearance = std::min(clearance, sim.groundHeightAt(ahead.x, ahead.y) - ahead.z);
  }
  if (clearance < 300 || (flight.agl < 600 && flight.vs < -25)) {
    desiredBank = 0;
    desiredPitch = (clearance < 80 ? 32 : 20) * kDeg2Rad;
    out.firing = false;
  }
  const double bank = flight.roll_deg * kDeg2Rad;
  const double rollRate = 2 * std::remainder(desiredBank - bank, 2 * kPi);
  const double pitchRate = 1.6 * (desiredPitch - flight.pitch_deg * kDeg2Rad) +
      kG0 * std::tan(std::clamp(bank, -1.2, 1.2)) * std::sin(bank) / speed;
  c.aileron_stick = std::clamp(rollRate / sim.config().max_roll_rate, -.85, .85);
  const double loadFeedback = sim.config().control_law == FlightControlLaw::Canard
      ? .08 * (s.att.inverseRotate({0,0,1}).z - flight.g_load) : 0.;
  c.elevator_stick = std::clamp((pitchRate-loadFeedback) / sim.config().max_pitch_rate -
      c.elevator_trim + s.trim_reference, -.5, .8);
  c.rudder_pedal = 0;
  const double desiredSpeed = target ? std::clamp(target->vel_ned.norm() +
      (range - 450) * .025, 110., 200.) : 170.;
  c.throttle[0] = c.throttle[1] = std::clamp(.55 + (desiredSpeed - speed) * .012, .25, 1.);
  return out;
}
} // namespace ofs::net
