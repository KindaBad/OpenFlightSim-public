#include "ofs/aircraft_definition.hpp"
#include "scenario.hpp"
#include <cstdio>
#include <string>
using namespace ofs;
using namespace scenario;
namespace {
AircraftConfig referenceConfig() {
  auto c = sr71Config();
  c.fuel_flow_scale = 0;
  return c;
}
Simulator trimmed(double speed = 180, double height = 1500, double gear = 0,
                  double gamma = 0, AircraftConfig cfg = referenceConfig()) {
  TrimRequest r;
  r.tas = speed;
  r.altitude = height;
  r.gear01 = gear;
  r.gamma = gamma;
  const auto t = solveTrim(cfg, r);
  std::printf("SR71 trim h=%.0f TAS=%.2f M=%.4f converged=%d throttle=%.6f "
              "alpha=%.5f residualN=%.4f momentNm=%.4f\n",
              height, speed, speed / isaAtAltitude(height).sound, t.converged,
              t.controls.throttle[0],
              std::asin(2 * t.state.att.w * t.state.att.y) * kRad2Deg -
                  gamma * kRad2Deg,
              t.residual_force_world.norm(), t.residual_moment_body.norm());
  check(t.converged, "SR71 trim convergence");
  Simulator sim(cfg);
  sim.setState(t.state);
  sim.setControls(t.controls);
  return sim;
}
void trim() {
  for (const auto &[v, h] :
       {std::pair{150., 1500.}, std::pair{240., 6000.}, std::pair{450., 15000.},
        std::pair{850., 23000.}, std::pair{950., 25000.}}) {
    auto s = trimmed(v, h);
    const auto m = advance(s, 14400);
    std::printf(
        "SR71 steady 120s altitudeRange=%.7g speedRange=%.7g rate=%.7g\n",
        m.max_alt - m.min_alt, m.max_speed - m.min_speed, m.max_rate);
    check(m.max_alt - m.min_alt < 2 && m.max_speed - m.min_speed < .2 &&
              m.max_rate < .001,
          "steady equilibrium");
  }
}
void highMach() {
  for (double fuel : {12000., sr71Config().fuel_capacity}) {
    auto cfg = sr71Config();
    cfg.initial_fuel = fuel;
    cfg.mass = cfg.empty_mass + fuel;
    const auto base = sr71Config();
    cfg.ixx = base.ixx + (fuel - base.initial_fuel) * 9;
    cfg.iyy = base.iyy + (fuel - base.initial_fuel) * 49.205;
    cfg.izz = base.izz + (fuel - base.initial_fuel) * 58.2025;
    auto s = trimmed(950, 25000, 0, 0, cfg);
    auto c = s.controls();
    const double originalFuel = s.state().fuel_mass;
    double maxMach = 0, minMach = 10;
    const auto m = run(
        s, 72000,
        [&](int, const auto &a) {
          const auto n = a.instruments();
          c.elevator_stick =
              flightPathInput(a, 0, clamp((25000 - n.alt_msl) * .02, -3., 3.));
          c.throttle[0] = c.throttle[1] =
              clamp(c.throttle[0] + .001 * (950 - n.tas) * FixedStepClock::tick,
                    0., 1.);
          return c;
        },
        [&](int, const auto &a) {
          const auto n = a.instruments();
          maxMach = std::max(maxMach, n.mach);
          minMach = std::min(minMach, n.mach);
        });
    const auto n = s.instruments();
    const auto t = s.evalThrust();
    const auto a = s.evalAero();
    std::printf(
        "SR71 highMach fuelStart=%.2f finalFuel=%.2f h=%.3f M=%.5f "
        "MachRange=%.5f..%.5f throttle=%.6f AB=%.6f thrust=%.1f drag=%.1f "
        "alpha=%.5f flow=%.5f altitudeRange=%.3f maxRate=%.6f\n",
        originalFuel, s.state().fuel_mass, n.alt_msl, n.mach, minMach, maxMach,
        c.throttle[0], s.state().afterburner[0], t.force_body.x,
        a.drag_body.norm(), a.alpha * kRad2Deg, t.fuel_flow[0] + t.fuel_flow[1],
        m.max_alt - m.min_alt, m.max_rate);
    check(std::abs(n.alt_msl - 25000) < 100 && n.mach > 3 && maxMach < 3.5 &&
              m.max_rate < .05,
          "ten-minute high-Mach finite energy balance");
    check(s.state().fuel_mass < originalFuel - 1000,
          "sustained high-Mach fuel burn");
  }
}
void engines() {
  Simulator s(sr71Config());
  State st;
  st.pos_ned.z = -1000;
  st.vel_ned = {300, 0, 0};
  st.n1[0] = st.n1[1] = .82;
  s.setState(st);
  const auto dry = s.evalThrust();
  st.n1[0] = st.n1[1] = 1;
  st.afterburner[0] = st.afterburner[1] = 1;
  s.setState(st);
  const auto wet = s.evalThrust();
  check(wet.each[0] > dry.each[0] * 1.4 &&
            wet.fuel_flow[0] > dry.fuel_flow[0] * 2.5,
        "distinct reheat thrust and burn");
  st.pos_ned.z = -25000;
  st.vel_ned = {950, 0, 0};
  st.inlet_spike[0] = 1;
  st.inlet_spike[1] = 0;
  s.setState(st);
  const auto mismatch = s.evalThrust();
  check(mismatch.each[0] > mismatch.each[1] * 2, "independent inlet recovery");
  Controls c;
  c.gear01 = 0;
  c.throttle[0] = 1;
  c.throttle[1] = .82;
  s.setControls(c);
  advance(s, 600);
  check(s.state().afterburner[0] > .99 && s.state().afterburner[1] < .001,
        "independent reheat state");
  std::printf("SR71 dry=%.1f wet=%.1f flowDry=%.5f flowWet=%.5f "
              "highMachCorrect=%.1f misplaced=%.1f spikes=%.6f/%.6f\n",
              dry.force_body.x, wet.force_body.x, 2 * dry.fuel_flow[0],
              2 * wet.fuel_flow[0], mismatch.each[0], mismatch.each[1],
              s.state().inlet_spike[0], s.state().inlet_spike[1]);
}
void fuel() {
  Simulator s(sr71Config());
  State st;
  st.pos_ned.z = -2000;
  st.vel_ned = {180, 0, 0};
  st.n1[0] = st.n1[1] = 1;
  st.afterburner[0] = st.afterburner[1] = 1;
  st.fuel_mass = s.config().fuel_capacity;
  s.setState(st);
  const auto full = s.massProperties();
  const double start = s.state().fuel_mass;
  Controls c;
  c.gear01 = 0;
  c.throttle[0] = c.throttle[1] = 1;
  s.setControls(c);
  advance(s, 1200);
  check(s.state().fuel_mass < start - 50, "fuel decreases");
  st = s.state();
  st.fuel_mass = 12000;
  s.setState(st);
  const auto partial = s.massProperties();
  check(full.mass > partial.mass && full.inertia.y > partial.inertia.y &&
            full.cg.x < partial.cg.x,
        "distributed fuel alters mass CG inertia");
  st.fuel_mass = 0;
  s.setState(st);
  check(s.evalThrust().force_body.norm() == 0, "starvation");
  std::printf("SR71 fullMass=%.3f partialMass=%.3f fullCGx=%.6f "
              "partialCGx=%.6f fullIy=%.0f partialIy=%.0f\n",
              full.mass, partial.mass, full.cg.x, partial.cg.x, full.inertia.y,
              partial.inertia.y);
}
void engineOut() {
  for (unsigned failed : {0u, 1u}) {
    auto s = trimmed(450, 15000);
    auto st = s.state();
    st.engine_health[failed] = 0;
    s.setState(st);
    const auto t = s.evalThrust();
    check(t.each[failed] == 0 &&
              (failed == 0 ? t.moment_body.z < 0 : t.moment_body.z > 0),
          "engine position creates asymmetric yaw");
    const auto m = advance(s, 1200);
    check(m.max_rate < 2, "engine-out remains bounded");
    std::printf("SR71 engineOut failed=%u yawMoment=%.2f finalYawRate=%.6f "
                "maxRate=%.6f height=%.2f\n",
                failed, t.moment_body.z, s.state().omega_body.z, m.max_rate,
                s.instruments().alt_msl);
  }
}
void taxi() {
  Simulator s(referenceConfig());
  State st;
  st.pos_ned.z = -2.55;
  s.setState(st);
  Controls c;
  c.brake01 = 1;
  s.setControls(c);
  advance(s, 3600);
  check(s.instruments().tas < 1e-5, "parked stable");
  c.brake01 = 0;
  c.throttle[0] = c.throttle[1] = .35;
  s.setControls(c);
  advance(s, 1200);
  const double speed = s.instruments().tas;
  c.steering = .15;
  s.setControls(c);
  advance(s, 240);
  const double heading = std::remainder(s.instruments().hdg_deg, 360.);
  c.steering = 0;
  c.throttle[0] = c.throttle[1] = 0;
  c.brake01 = 1;
  s.setControls(c);
  advance(s, 3600);
  std::printf("SR71 taxi speed=%.6f heading=%.6f finalSpeed=%.9g height=%.5f\n",
              speed, heading, s.instruments().tas, s.instruments().alt_msl);
  check(speed > 1 && speed < 20 && heading > 1 && s.instruments().tas < 1e-5,
        "taxi steering and braking");
}
void takeoff() {
  Simulator s(referenceConfig());
  State st;
  st.pos_ned.z = -2.55;
  s.setState(st);
  Controls c;
  c.brake01 = 1;
  bool lifted = false;
  double time = 0, x = 0, speed = 0;
  const auto m = run(
      s, 14400,
      [&](int tick, const auto &a) {
        if (tick > 360) {
          c.brake01 = 0;
          c.throttle[0] = c.throttle[1] = 1;
        }
        if (a.instruments().tas > 90)
          c.elevator_stick = pitchInput(a, 10 * kDeg2Rad);
        if (lifted && a.instruments().agl > 10)
          c.gear01 = 0;
        return c;
      },
      [&](int tick, const auto &a) {
        if (!lifted && gearLoad(a) < 1 && a.instruments().agl > 4 &&
            a.instruments().vs > 0) {
          lifted = true;
          time = tick / 120.;
          x = a.state().pos_ned.x;
          speed = a.instruments().tas;
        }
      });
  std::printf("SR71 takeoff time=%.4f distance=%.4f rotation=90 liftoff=%.4f "
              "height120s=%.4f TAS=%.4f pitchMax=%.4f\n",
              time - 3, x, speed, s.instruments().alt_msl, s.instruments().tas,
              m.max_pitch);
  check(lifted && speed > 85 && speed < 140 && x > 800 && x < 4000,
        "plausible takeoff");
  check(s.instruments().alt_msl > 500 && m.max_pitch < 25,
        "sustained initial climb");
}
void landing() {
  auto s = trimmed(105, 150, 1, -2.5 * kDeg2Rad);
  auto c = s.controls();
  bool touched = false;
  double speed = 0, sink = 0, x = 0, prevSpeed = 0, prevSink = 0;
  const auto m = run(
      s, 21600,
      [&](int, const auto &a) {
        auto n = a.instruments();
        prevSpeed = n.tas;
        prevSink = -n.vs;
        if (n.agl < 20 && !touched) {
          c.elevator_stick = flightPathInput(a, 0, -.5);
          c.throttle[0] = c.throttle[1] = .2;
        }
        if (touched) {
          c.elevator_stick = -c.elevator_trim;
          c.brake01 = 1;
          c.throttle[0] = c.throttle[1] = 0;
        }
        return c;
      },
      [&](int, const auto &a) {
        if (!touched && gearLoad(a) > 1000) {
          touched = true;
          speed = prevSpeed;
          sink = prevSink;
          x = a.state().pos_ned.x;
        }
      });
  std::printf("SR71 landing approach=105 touchdown=%.5f sink=%.5f rollout=%.5f "
              "peakContact=%.5f finalSpeed=%.9g minAltitude=%.6f\n",
              speed, sink, s.state().pos_ned.x - x, m.peak_contact,
              s.instruments().tas, m.min_alt);
  check(touched && speed > 85 && speed < 135 && sink > 0 && sink < 2,
        "normal landing touchdown");
  check(s.instruments().tas < 1e-5 && m.min_alt > 1.8 &&
            m.peak_contact < 3 * s.config().mass * kG0,
        "stable rollout stop");
}
void controls() {
  for (int axis = 0; axis < 3; ++axis) {
    auto s = trimmed();
    auto c = s.controls();
    if (axis == 0)
      c.aileron_stick = .2;
    if (axis == 1)
      c.elevator_stick = .2;
    if (axis == 2)
      c.rudder_pedal = .2;
    s.setControls(c);
    advance(s, 120);
    const auto w = s.state().omega_body;
    std::printf("SR71 axis%d rates=%.6f/%.6f/%.6f elevons=%.6f/%.6f\n", axis,
                w.x, w.y, w.z, s.state().elevon_l, s.state().elevon_r);
    check(axis == 0   ? w.x > .01
          : axis == 1 ? w.y > .005
                      : w.z > .005,
          "axis direction");
    if (axis == 0)
      check(s.state().elevon_l < s.state().elevon_r,
            "delta visual roll allocation");
  }
  Simulator s(referenceConfig());
  Controls c;
  c.flap01 = 1;
  c.spoiler01 = 1;
  s.setControls(c);
  check(s.controls().flap01 == 0 && s.controls().spoiler01 == 0,
        "no invented flaps/spoilers");
}
void robustness() {
  for (double v : {0., 30., 100., 450., 950., 1200.})
    for (double angle : {-180., -90., -30., 0., 30., 90., 180.}) {
      Simulator s(referenceConfig());
      State st;
      st.pos_ned.z = -25000;
      st.vel_ned = {v * std::cos(angle * kDeg2Rad), 0,
                    v * std::sin(angle * kDeg2Rad)};
      s.setState(st);
      Controls c;
      c.gear01 = 0;
      c.throttle[0] = c.throttle[1] = 1;
      c.elevator_stick = 1;
      c.aileron_stick = -1;
      c.rudder_pedal = 1;
      s.setControls(c);
      advance(s, 240);
      for (double spike : s.state().inlet_spike)
        check(spike >= 0 && spike <= 1, "bounded inlet state");
    }
  std::puts("SR71 42 extreme finite cases PASS");
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc == 2, "expected scenario");
    const std::string n = argv[1];
    if (n == "trim")
      trim();
    else if (n == "high_mach")
      highMach();
    else if (n == "engines")
      engines();
    else if (n == "fuel")
      fuel();
    else if (n == "engine_out")
      engineOut();
    else if (n == "taxi")
      taxi();
    else if (n == "takeoff")
      takeoff();
    else if (n == "landing")
      landing();
    else if (n == "controls")
      controls();
    else if (n == "robustness")
      robustness();
    else
      throw std::invalid_argument("unknown scenario");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "SR71 FAIL: %s\n", e.what());
    return 1;
  }
}
