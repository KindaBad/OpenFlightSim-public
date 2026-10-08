#pragma once
// Mouse aim: pointer flying in the style of War Thunder.
//
// The mouse does not move the stick. It moves an aim direction that is fixed in
// the world, drawn as a circle, and an "instructor" flies the ordinary flight
// model until the nose reaches it: small corrections are made wings-level with
// elevator and rudder, larger ones by banking into the turn and pulling. The
// camera looks along the aim direction rather than along the airframe, so the
// circle stays near the centre of the screen and the aircraft swings toward it.
//
// The instructor only fills control axes the pilot leaves neutral. Any keyboard
// or gamepad deflection wins on its own axis, so manual rolls, pulls and rudder
// work exactly as they do without mouse aim.
//
// Header-only and free of window or renderer dependencies, so the instructor is
// exercised against the real flight model by the headless tests.

#include "ofs/airliner.hpp"
#include "ofs/simulator.hpp"

#include <algorithm>
#include <cmath>

namespace ofs::client {

struct MouseAim {
  // Aim direction in world NED, as heading and elevation.
  double yaw{}, pitch{};
  // Free-look offset from the aim direction; moves the view, not the aircraft.
  double lookYaw{}, lookPitch{};
  bool active{};

  // Radians per mouse count at a sensitivity of 1.
  static constexpr double kRadiansPerCount = .0012;
  // Short of vertical, so the level-horizon view never reaches its pole.
  static constexpr double kPitchLimit = 1.48;

  Vec3 direction() const { return quatFromEuler(0, pitch, yaw).rotate({1, 0, 0}); }
  double viewYaw() const { return yaw + lookYaw; }
  double viewPitch() const { return clamp(pitch + lookPitch, -kPitchLimit, kPitchLimit); }
  Vec3 viewDirection() const { return quatFromEuler(0, viewPitch(), viewYaw()).rotate({1, 0, 0}); }

  // Points the aim along the nose, so engaging mouse aim never jerks the aircraft.
  void recenter(const State& aircraft) {
    const Vec3 nose = aircraft.att.rotate({1, 0, 0});
    yaw = std::atan2(nose.y, nose.x);
    pitch = clamp(std::asin(clamp(-nose.z, -1, 1)), -kPitchLimit, kPitchLimit);
    lookYaw = lookPitch = 0;
  }

  void move(double dx, double dy, double sensitivity) {
    yaw = std::remainder(yaw + dx * kRadiansPerCount * sensitivity, 2 * kPi);
    pitch = clamp(pitch - dy * kRadiansPerCount * sensitivity, -kPitchLimit, kPitchLimit);
  }

  void look(double dx, double dy, double sensitivity) {
    lookYaw = std::remainder(lookYaw + dx * kRadiansPerCount * sensitivity, 2 * kPi);
    lookPitch = clamp(lookPitch - dy * kRadiansPerCount * sensitivity, -kPi, kPi);
  }

  void clearLook() { lookYaw = lookPitch = 0; }

  // Keeps the aim within `cone` radians of the nose. The flight deck view is
  // fixed to the airframe, so this is what keeps the aim circle on screen.
  void confine(const State& aircraft, double cone) {
    const Vec3 nose = aircraft.att.rotate({1, 0, 0});
    const Vec3 aim = direction();
    if (aim.dot(nose) >= std::cos(cone)) return;
    Vec3 across = aim - nose * aim.dot(nose);
    if (across.norm2() < 1e-12) across = aircraft.att.rotate({0, 0, -1});
    const Vec3 held = nose * std::cos(cone) + across.normalized() * std::sin(cone);
    yaw = std::atan2(held.y, held.x);
    pitch = clamp(std::asin(clamp(-held.z, -1, 1)), -kPitchLimit, kPitchLimit);
  }

  // Called once per frame. Recentres when mouse aim engages and when the
  // aircraft is repositioned (reset, respawn), which no flight can do in a frame.
  void sync(bool enable, const State& aircraft) {
    const bool moved = aircraft.time < lastTime_ || (aircraft.pos_ned - lastPosition_).norm() > 2000;
    if (enable && (!active || moved)) recenter(aircraft);
    active = enable;
    lastTime_ = aircraft.time;
    lastPosition_ = aircraft.pos_ned;
  }

 private:
  double lastTime_{};
  Vec3 lastPosition_{};
};

// Stick positions the instructor asks for, in the Controls conventions.
struct MouseAimCommand {
  double elevator{}, aileron{}, rudder{};
};

// Flies the nose toward `aimDirection` (world NED) through the normal controls.
inline MouseAimCommand mouseAimCommand(const Simulator& sim, const Vec3& aimDirection) {
  const State& s = sim.state();
  const AircraftConfig& cfg = sim.config();
  const Controls& held = sim.controls();
  MouseAimCommand out;
  if (aimDirection.norm2() < 1e-12) return out;
  const Vec3 aim = aimDirection.normalized();
  const Vec3 body = s.att.inverseRotate(aim);
  const double up = -body.z, right = body.y;
  // Angle between the nose and the aim, split into the airframe's pitch and
  // yaw planes. Valid all the way round, so an aim behind the tail still pulls.
  const double off = std::atan2(std::hypot(up, right), body.x);
  const double plane = std::hypot(up, right) > 1e-9 ? std::atan2(right, up) : 0.0;
  double pitchError = off * std::cos(plane);
  const double yawError = off * std::sin(plane);
  // The stick asks for pitch rate about the trim reference.
  const auto stickFor = [&](double rate, double low, double high) {
    return clamp(rate / cfg.max_pitch_rate, low, high) - held.elevator_trim + s.trim_reference;
  };

  if (sim.debugFrame().contact_normal_force > 0) {
    // On the wheels: steer with the rudder and rotate when the aim is raised.
    out.rudder = clamp(2.5 * std::atan2(right, std::max(body.x, .2)), -1, 1);
    out.elevator = clamp(stickFor(2. * pitchError, 0, 1), -1, 1);
    return out;
  }

  // Bank to turn. The lift the correction needs is a level component toward
  // the aim's heading plus the share of 1 g that holds the flight path up,
  // raised or lowered by the elevation error. A small error therefore leaves
  // the wings level, a large one banks steeply, a sinking nose shallows the
  // bank, and an aim well below the nose rolls inverted and pulls.
  const Vec3 nose = s.att.rotate({1, 0, 0});
  const Vec3 worldUp{0, 0, -1};
  const Vec3 upAcross = worldUp - nose * worldUp.dot(nose);
  // Error at which the turn asks for as much lift as level flight: a 45 degree bank.
  constexpr double kBankAngle = .25;
  // Load factor the wing can carry at this speed and the airframe is cleared
  // for, with a margin. Banking or pulling past it turns the manoeuvre into a
  // descending spiral or a stall.
  const Instruments flight = sim.instruments();
  const double speedRatio = flight.tas / std::max(flight.vstall, 1.);
  const double loadFactor = clamp(.8 * speedRatio * speedRatio, 1.1,
                                  clamp(.9 * cfg.g_positive, 1.2, 6.));
  const double bankLoad = clamp(.85 * loadFactor, 1.03, 3.);
  const double steepest = std::sqrt(bankLoad * bankLoad - 1);
  Vec3 lift;
  const Vec3 level = nose.cross(worldUp);
  if (level.norm() > .15) {
    const double noseElevation = std::asin(clamp(-nose.z, -1, 1));
    const double aimElevation = std::asin(clamp(-aim.z, -1, 1));
    // Heading means less the nearer either direction is to vertical.
    const double headingError = std::remainder(std::atan2(aim.y, aim.x) - std::atan2(nose.y, nose.x), 2 * kPi) *
                                std::min(std::cos(noseElevation), std::cos(aimElevation));
    lift = level.normalized() * clamp(headingError / kBankAngle, -steepest, steepest) +
           upAcross * (1 + clamp((aimElevation - noseElevation) / kBankAngle, -3, 3));
  } else {
    // Nose near vertical: there is no heading, so roll straight onto the aim.
    const Vec3 across = aim - nose * aim.dot(nose);
    lift = across * (std::min(off / kBankAngle, steepest) / std::max(across.norm(), 1e-9)) + upAcross;
  }
  const Vec3 liftBody = s.att.inverseRotate(lift);
  const double demand = std::hypot(liftBody.y, liftBody.z);
  // Fades out where the demand vanishes and the bank direction is undefined.
  const double rollError = std::atan2(liftBody.y, -liftBody.z) * std::min(1., demand / .2);
  out.aileron = clamp(2.5 * rollError / cfg.max_roll_rate, -1, 1);

  // Pulling before the lift vector is around would drag the nose the wrong way.
  const double aligned = std::max(0., std::cos(std::min(std::abs(rollError), .5 * kPi)));
  if (pitchError > 0) pitchError *= aligned;
  // Far off the nose the airframe's own pitch plane says little about which
  // way round to go, so the pull simply follows the lift vector.
  pitchError += (off * aligned - pitchError) * clamp((off - .8) / .6, 0, 1);
  // Keep the pull inside the load factors the wing and the pilot accept. The
  // control laws do not map stick to rate alike, so this closes on measured g,
  // led by the g the present pitch rate is about to produce.
  const double gravity = s.att.rotate({0, 0, 1}).z;  // share of 1 g the wing carries untouched
  const double load = std::max(flight.g_load, gravity + s.omega_body.y * flight.tas / kG0);
  double high = clamp(1 - (load - (loadFactor + .5)), 0, 1);
  const double low = clamp(-.5 + 2 * (-.75 - flight.g_load), -.5, 0);
  // Stall guard: authority to pull runs out as the wing nears the angle the
  // stall warning sounds at, and past it the nose is eased down. Manoeuvre
  // mode flies beyond that angle on purpose.
  if (!held.maneuver_mode) {
    const double critical = cfg.aero_kind == AeroModelKind::AirlinerEngineering
        ? a320HighLift(held.flap01).alpha_critical
        : lerp(cfg.alpha_crit_clean, 12 * kDeg2Rad, held.flap01);
    const double margin = critical - 3 * kDeg2Rad - flight.alpha_deg * kDeg2Rad;
    high = std::min(high, clamp(margin / (5 * kDeg2Rad), -.15, 1));
    if (flight.stall_warn) high = std::min(high, 0.);
  }
  const double elevator = stickFor(2.2 * pitchError, std::min(low, high), high);
  out.elevator = clamp(elevator, -1, 1);
  // Rudder is for fine aim only; held through a turn it just builds sideslip.
  out.rudder = clamp(2. * yawError, -.3, .3) * clamp(1 - (off - .1) / .15, 0, 1);
  return out;
}

// Applies the instructor to every axis the pilot left neutral.
inline void applyMouseAim(Controls& controls, const MouseAimCommand& command) {
  if (controls.elevator_stick == 0) controls.elevator_stick = command.elevator;
  if (controls.aileron_stick == 0) controls.aileron_stick = command.aileron;
  if (controls.rudder_pedal == 0) controls.rudder_pedal = command.rudder;
  controls.steering = controls.rudder_pedal;
}

}  // namespace ofs::client
