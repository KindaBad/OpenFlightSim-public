#include "mouse_aim.hpp"
#include "ofs/aircraft_definition.hpp"
#include "ofs/trim.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>
using namespace ofs;
using namespace ofs::client;
namespace {
void check(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}
double degreesOff(const State& state, const MouseAim& aim) {
  return std::acos(clamp(state.att.rotate({1, 0, 0}).dot(aim.direction()), -1, 1)) * kRad2Deg;
}
struct Flight {
  double off{}, roll{}, maxBank{}, altitudeLost{}, maxLoad{}, minLoad{9};
};
// Flies the instructor alone, with the pilot's hands off, from steady trim.
Flight fly(const AircraftDefinition& definition, const TrimRequest& request, double yawDeg, double pitchDeg,
           double seconds, const Controls& pilot = {}) {
  const auto trim = solveTrim(definition.flight, request);
  check(trim.converged, std::string(definition.key) + " trims");
  Simulator sim(definition.flight);
  sim.setState(trim.state);
  sim.setControls(trim.controls);
  MouseAim aim;
  aim.sync(true, sim.state());
  check(degreesOff(sim.state(), aim) < 1e-6, "engaging mouse aim starts on the nose");
  aim.yaw += yawDeg * kDeg2Rad;
  aim.pitch += pitchDeg * kDeg2Rad;
  Flight result;
  const double start = sim.instruments().alt_msl;
  for (int tick = 0; tick < seconds * 120; ++tick) {
    Controls controls = trim.controls;
    controls.elevator_stick = pilot.elevator_stick;
    controls.aileron_stick = pilot.aileron_stick;
    controls.rudder_pedal = pilot.rudder_pedal;
    applyMouseAim(controls, mouseAimCommand(sim, aim.direction()));
    sim.setControls(controls);
    sim.step(1. / 120);
    const auto flight = sim.instruments();
    result.maxBank = std::max(result.maxBank, std::abs(flight.roll_deg));
    result.altitudeLost = std::max(result.altitudeLost, start - flight.alt_msl);
    result.maxLoad = std::max(result.maxLoad, flight.g_load);
    result.minLoad = std::min(result.minLoad, flight.g_load);
  }
  result.off = degreesOff(sim.state(), aim);
  result.roll = sim.instruments().roll_deg;
  return result;
}
}  // namespace

int main() {
  try {
    // --- Aim point ---
    State level;
    level.att = quatFromEuler(0, 5 * kDeg2Rad, 40 * kDeg2Rad);
    MouseAim aim;
    aim.sync(true, level);
    check(aim.active && std::abs(aim.yaw - 40 * kDeg2Rad) < 1e-9 && std::abs(aim.pitch - 5 * kDeg2Rad) < 1e-9,
          "aim starts along the nose");
    aim.move(100, 0, 1);
    check(aim.yaw > 40 * kDeg2Rad && std::abs(aim.pitch - 5 * kDeg2Rad) < 1e-9, "mouse right turns the aim right");
    aim.move(0, -100, 2);
    check(std::abs(aim.pitch - (5 * kDeg2Rad + 200 * MouseAim::kRadiansPerCount)) < 1e-9,
          "mouse forward raises the aim, scaled by sensitivity");
    aim.move(0, -1e6, 1);
    check(aim.pitch == MouseAim::kPitchLimit && aim.direction().z < 0, "aim stops short of vertical");
    aim.move(0, 1000, 1);
    const Vec3 held = aim.direction();
    aim.look(300, 50, 1);
    check((aim.direction() - held).norm() < 1e-12 && (aim.viewDirection() - held).norm() > .1,
          "free look moves the view and leaves the aim alone");
    aim.clearLook();
    check((aim.viewDirection() - held).norm() < 1e-12, "releasing free look returns to the aim");
    level.time = 1;
    aim.sync(true, level);
    check((aim.direction() - held).norm() < 1e-12, "the aim is held from frame to frame");
    State respawned = level;
    respawned.pos_ned = {9000, 0, -1000};
    respawned.time = 1.01;
    aim.sync(true, respawned);
    check(degreesOff(respawned, aim) < 1e-6, "a repositioned aircraft recentres the aim");
    aim.move(2000, 0, 1);
    aim.confine(respawned, .4);
    check(std::abs(degreesOff(respawned, aim) - .4 * kRad2Deg) < 1e-6, "the flight deck holds the aim in view");
    aim.sync(false, respawned);
    check(!aim.active, "mouse aim disengages");

    // --- Other controls keep priority, axis by axis ---
    const MouseAimCommand command{.4, -.6, .2};
    Controls controls;
    applyMouseAim(controls, command);
    check(controls.elevator_stick == .4 && controls.aileron_stick == -.6 && controls.rudder_pedal == .2 &&
              controls.steering == .2,
          "the instructor flies every neutral axis");
    controls = {};
    controls.aileron_stick = 1;
    applyMouseAim(controls, command);
    check(controls.aileron_stick == 1 && controls.elevator_stick == .4 && controls.rudder_pedal == .2,
          "a held roll key overrides roll only");
    controls = {};
    controls.elevator_stick = -1;
    controls.rudder_pedal = -1;
    applyMouseAim(controls, command);
    check(controls.elevator_stick == -1 && controls.rudder_pedal == -1 && controls.steering == -1 &&
              controls.aileron_stick == -.6,
          "held pitch and rudder keys override pitch and rudder only");

    // --- Instructor, against every aircraft's real flight model ---
    for (const auto& definition : aircraftDefinitions()) {
      const std::string name(definition.key);
      const TrimRequest slow;
      TrimRequest fast;
      fast.altitude = 6000;
      fast.tas = definition.type == AircraftType::A320 ? 210 : definition.type == AircraftType::B52 ? 230 : 280;

      // First response has the right sense on every axis.
      const auto trim = solveTrim(definition.flight, slow);
      check(trim.converged, name + " trims");
      Simulator sim(definition.flight);
      sim.setState(trim.state);
      sim.setControls(trim.controls);
      const auto ask = [&](double yawDeg, double pitchDeg) {
        MouseAim probe;
        probe.sync(true, sim.state());
        probe.yaw += yawDeg * kDeg2Rad;
        probe.pitch += pitchDeg * kDeg2Rad;
        return mouseAimCommand(sim, probe.direction());
      };
      const auto centred = ask(0, 0);
      check(std::abs(centred.aileron) < 1e-6 && std::abs(centred.rudder) < 1e-6, name + ": on the aim, no roll or yaw");
      check(ask(40, 0).aileron > .2 && ask(-40, 0).aileron < -.2, name + ": banks toward the aim");
      check(ask(3, 0).rudder > 0 && ask(-3, 0).rudder < 0, name + ": rudder trims small errors");
      check(ask(0, 4).elevator > centred.elevator && ask(0, -4).elevator < centred.elevator,
            name + ": elevator follows the aim");

      for (const auto& request : {slow, fast}) {
        const std::string at = name + (request.tas > 150 ? " fast" : " slow");
        auto flight = fly(definition, request, -6, 5, 12);
        check(flight.off < 2.5 && flight.maxBank < 30 && flight.altitudeLost < 5, at + ": small correction settles");
        flight = fly(definition, request, 60, 0, 30);
        check(flight.off < 4 && std::abs(flight.roll) < 15 && flight.altitudeLost < 200 && flight.maxLoad < 7.5 &&
                  flight.minLoad > -1,
              at + ": turns onto an aim 60 degrees right and rolls out");
        std::printf("%-14s turn: off %.2f deg, max bank %.0f deg, max %.1f g, lost %.0f m\n", at.c_str(), flight.off,
                    flight.maxBank, flight.maxLoad, flight.altitudeLost);
      }
      // A reversal is the hard case: the shortest way round must not be a dive.
      auto flight = fly(definition, fast, 175, 0, 45);
      check(flight.altitudeLost < 250 && flight.off < 75, name + ": reverses course without diving away");
      // Dive onto an aim below the horizon and hold it.
      flight = fly(definition, slow, 0, -20, 15);
      check(flight.off < 6 && std::abs(flight.roll) < 15 && flight.minLoad > -3, name + ": follows the aim down");

      // The pilot's own roll input wins while it is held, even against the aim.
      Controls pilot;
      pilot.aileron_stick = -1;
      flight = fly(definition, slow, 60, 0, 1.5, pilot);
      check(flight.roll < -10, name + ": a held roll key overrides the instructor");

      // On the wheels the instructor steers and rotates but never rolls.
      Simulator ground(definition.flight);
      State parked;
      parked.pos_ned.z = -(definition.flight.gear_nose.z - .15);
      ground.setState(parked);
      Controls stopped;
      stopped.brake01 = 1;
      ground.setControls(stopped);
      for (int tick = 0; tick < 360; ++tick) ground.step(1. / 120);
      check(ground.debugFrame().contact_normal_force > 0, name + " rests on its gear");
      MouseAim taxi;
      taxi.sync(true, ground.state());
      taxi.yaw += 20 * kDeg2Rad;
      const auto steer = mouseAimCommand(ground, taxi.direction());
      check(steer.rudder > .3 && steer.aileron == 0, name + ": steers toward the aim on the ground");
      taxi.pitch += 10 * kDeg2Rad;
      check(mouseAimCommand(ground, taxi.direction()).elevator > steer.elevator, name + ": raising the aim rotates");
    }
    std::puts("mouse aim tests passed");
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAILED: %s\n", error.what());
    return 1;
  }
}
