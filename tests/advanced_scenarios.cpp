#include "ofs/aircraft_definition.hpp"
#include "ofs/c_api.h"
#include "ofs/telemetry.hpp"
#include "scenario.hpp"
#include <cstdio>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
using namespace ofs;
using namespace scenario;
namespace {
Simulator trimmed(AircraftConfig cfg = typhoonConfig(), double speed = 180,
                  double altitude = 1500) {
  TrimRequest r;
  r.tas = speed;
  r.altitude = altitude;
  const auto t = solveTrim(cfg, r);
  if (!t.converged)
    std::printf("trim failed mass=%f speed=%f height=%f throttle=%f "
                "residualForce=%f moment=%f\n",
                cfg.mass, speed, altitude, t.controls.throttle[0],
                t.residual_force_world.norm(), t.residual_moment_body.norm());
  check(t.converged, "advanced trim");
  Simulator sim(cfg);
  sim.setState(t.state);
  sim.setControls(t.controls);
  return sim;
}
void cMemory() {
  auto *a = ofs_create();
  auto *b = ofs_create();
  check(a && b, "C memory create");
  OfsState state{};
  state.pos_d = -1500;
  state.vel_n = 180;
  ofs_set_state(a, &state);
  ofs_set_controls(a, .2, -.1, .1, .25, .1, 0, .65, .65, 0, 0);
  for (int tick = 0; tick < 120; ++tick)
    ofs_step(a, FixedStepClock::tick);
  auto memory = ofs_get_physics_memory(a);
  memory.aero_memory_initialized=1;memory.alpha_lag[0]=.4;memory.alpha_lag[1]=.5;
  memory.separation[0]=.3;memory.separation[1]=.4;memory.vortex_state[0]=.2;memory.vortex_state[1]=.3;
  ofs_set_physics_memory(a,&memory);
  state = ofs_get_state(a);
  ofs_set_state(b, &state);
  ofs_set_physics_memory(b, &memory);
  ofs_set_controls(b, .2, -.1, .1, .25, .1, 0, .65, .65, 0, 0);
  for (int tick = 0; tick < 120; ++tick) {
    ofs_step(a, FixedStepClock::tick);
    ofs_step(b, FixedStepClock::tick);
  }
  const auto sa = ofs_get_state(a), sb = ofs_get_state(b);
  auto mb = ofs_get_physics_memory(b);
  memory = ofs_get_physics_memory(a);
  check(std::abs(sa.pos_n - sb.pos_n) < 1e-7 &&
            std::abs(sa.pos_d - sb.pos_d) < 1e-7 &&
            std::abs(memory.fuel_mass - mb.fuel_mass) < 1e-8 &&
            std::abs(memory.elevator - mb.elevator) < 1e-8 && mb.aero_memory_initialized &&
            mb.alpha_lag[0]==.4 && mb.alpha_lag[1]==.5 && mb.separation[0]==.3 &&
            mb.separation[1]==.4 && mb.vortex_state[0]==.2 && mb.vortex_state[1]==.3,
        "C API full physics snapshot/replay");
  ofs_destroy(a);
  ofs_destroy(b);
}
void atmosphere() {
  for (double h : {-500., 0., 5000., 11000., 20000.}) {
    auto a = isaAtAltitude(h);
    check(std::abs(a.pressure - a.rho * 287.05 * a.temp) < 1e-8,
          "ideal gas relation");
    check(std::abs(a.sound * a.sound - 1.4 * 287.05 * a.temp) < 1e-8,
          "sound speed relation");
    Simulator s;
    State state;
    state.pos_ned.z = -h;
    state.vel_ned.x = a.sound * .78;
    s.setState(state);
    auto i = s.instruments();
    check(std::abs(i.mach - .78) < 1e-12, "Mach definition");
    if (h == 0)
      check(std::abs(i.cas - i.tas) < 1e-7, "sea level CAS/TAS");
    if (h > 0)
      check(i.ias < i.tas, "CAS lower than TAS at altitude");
    std::printf("ISA altitude=%.0f T=%.2f p=%.2f rho=%.6f sound=%.4f TAS=%.4f "
                "IAS=%.4f\n",
                h, a.temp, a.pressure, a.rho, a.sound, i.tas, i.ias);
  }
}
void surfaces() {
  auto s = trimmed();
  const auto level = s.evalAero();
  auto state = s.state();
  state.omega_body = {.5, .15, .2};
  s.setState(state);
  auto a = s.evalAero();
  check(a.local_alpha[0] != a.local_alpha[1],
        "roll local airflow differs across wings");
  Vec3 sum;
  for (auto f : a.surfaces)
    sum += f.force_body;
  check((sum - a.force_body).norm() < 1e-7, "component force sum");
  check(a.moment_body.x < level.moment_body.x,
        "local roll damping opposes roll");
  double near = 0, far = 0;
  state.omega_body = {};
  state.pos_ned.z = -3;
  s.setState(state);
  near = s.evalAero().ground_effect;
  state.pos_ned.z = -100;
  s.setState(state);
  far = s.evalAero().ground_effect;
  check(near < far && far > .99, "smooth ground effect fading");
  std::printf(
      "local wing alpha=%.5f/%.5f qbar=%.2f/%.2f groundEffect=%.5f/%.5f\n",
      a.local_alpha[0], a.local_alpha[1], a.local_qbar[0], a.local_qbar[1],
      near, far);
}
void actuators() {
  auto s = trimmed();
  auto c = s.controls();
  c.elevator_stick = 1;
  c.aileron_stick = -1;
  c.rudder_pedal = 1;
  c.flap01 = 1;
  c.spoiler01 = 1;
  const auto initial = s.state();
  s.setControls(c);
  check(s.state().elevator == initial.elevator,
        "controls do not teleport actuator");
  s.step(FixedStepClock::tick);
  check(std::abs(s.state().elevator - initial.elevator) <=
            s.config().actuator_rate * FixedStepClock::tick + 1e-12,
        "actuator rate bound");
  check(s.state().flap <= s.config().flap_rate * FixedStepClock::tick + 1e-12,
        "flap transit physical");
  auto clone = s;
  advance(s, 120);
  advance(clone, 120);
  check((s.state().pos_ned - clone.state().pos_ned).norm() == 0 &&
            s.state().elevator == clone.state().elevator,
        "serializable actuator replay");
  for (auto cfg : {a320Config(), su57Config(), typhoonConfig()}) {
    auto low = trimmed(cfg, 110);
    auto fast = trimmed(cfg, 250);
    auto input = low.controls();
    input.aileron_stick = .4;
    low.setControls(input);
    input = fast.controls();
    input.aileron_stick = .4;
    fast.setControls(input);
    advance(low, 120);
    advance(fast, 120);
    check(std::abs(low.state().omega_body.x) < cfg.max_roll_rate &&
              std::abs(fast.state().omega_body.x) < cfg.max_roll_rate,
          "FCS roll rate envelope at both speeds");
    std::printf("roll command=.4 rate110=%.5f rate250=%.5f limit=%.3f\n",
                low.state().omega_body.x, fast.state().omega_body.x,
                cfg.max_roll_rate);
  }
}
void mass() {
  auto s = trimmed(a320Config(), 110);
  auto initial = s.massProperties();
  auto a = s.evalAero();
  auto state = s.state();
  state.payload_mass += 4000;
  state.payload_offset = {2, 1, .3};
  s.setState(state);
  auto loaded = s.massProperties();
  check(loaded.mass == initial.mass + 4000, "payload mass sum");
  check(loaded.cg.x > initial.cg.x && loaded.cg.y > 0,
        "CG responds to loading");
  check(loaded.inertia.y > initial.inertia.y &&
            loaded.inertia.z > initial.inertia.z,
        "parallel axis inertia loading");
  check((s.evalAero().moment_body - a.moment_body).norm() > 1000,
        "CG changes aerodynamic moment");
  auto explicitMass=a320Config();explicitMass.empty_mass=47000;explicitMass.fuel_flow_scale=0;
  auto explicitTrim=trimmed(explicitMass,110);
  check(explicitTrim.massProperties().mass==69000,"explicit basic mass overrides legacy reference total");
  const auto steady=advance(explicitTrim,3600);
  check(steady.max_alt-steady.min_alt<.001 && steady.max_speed-steady.min_speed<.001,"trim balances explicit loaded mass");
  std::printf("loaded mass=%.1f cg=%.5f/%.5f/%.5f inertia=%.0f/%.0f/%.0f\n",
              loaded.mass, loaded.cg.x, loaded.cg.y, loaded.cg.z,
              loaded.inertia.x, loaded.inertia.y, loaded.inertia.z);
}
void fuel() {
  auto cfg = typhoonConfig();
  Simulator dry(cfg), wet(cfg);
  State state;
  state.pos_ned.z = -10000;
  state.vel_ned.x = 230;
  state.n1[0] = state.n1[1] = .85;
  dry.setState(state);
  Controls c;
  c.gear01 = 0;
  c.throttle[0] = c.throttle[1] = .85;
  dry.setControls(c);
  dry.primeActuators();
  state.n1[0] = state.n1[1] = 1;
  state.afterburner[0] = state.afterburner[1] = 1;
  wet.setState(state);
  c.throttle[0] = c.throttle[1] = 1;
  wet.setControls(c);
  wet.primeActuators();
  const double d = dry.evalThrust().fuel_flow[0] +
                   dry.evalThrust().fuel_flow[1],
               w = wet.evalThrust().fuel_flow[0] +
                   wet.evalThrust().fuel_flow[1];
  check(w > 2.5 * d, "reheat substantially higher fuel consumption");
  const double fuel = dry.state().fuel_mass,
               initialMass = dry.massProperties().mass;
  advance(dry, 1200);
  check(dry.state().fuel_mass < fuel && dry.massProperties().mass < initialMass,
        "fuel burn reduces mass");
  const double burned = fuel - dry.state().fuel_mass;
  state = dry.state();
  state.fuel_mass = 0;
  dry.setState(state);
  check(dry.evalThrust().force_body.norm() == 0,
        "fuel exhaustion stops thrust");
  state.fuel_mass = .0001;
  dry.setState(state);
  advance(dry, 120);
  check(dry.state().fuel_mass == 0, "no negative fuel");
  std::printf("fuel dry=%.6fkg/s reheat=%.6fkg/s ratio=%.3f burn10s=%.6fkg\n",
              d, w, w / d, burned);
}
void engineOut() {
  for (auto cfg : {a320Config(), typhoonConfig()}) {
    auto s = trimmed(cfg, cfg.mass > 20000 ? 110 : 180);
    auto state = s.state();
    state.n1[0] = state.n1[1] = .8;
    s.setState(state);
    const auto symmetric = s.evalThrust();
    state.engine_health[1] = 0;
    s.setState(state);
    auto asymmetric = s.evalThrust();
    check(asymmetric.each[1] == 0 && asymmetric.each[0] > 0 &&
              asymmetric.moment_body.z > 0,
          "left engine yaw from force offset");
    check(std::abs(symmetric.moment_body.z) < 1e-8,
          "symmetric thrust yaw cancellation");
    advance(s, 30);
    check(s.state().omega_body.z > 0, "engine out integrated yaw");
    std::printf("engineOut mass=%.0f thrustL/R=%.2f/%.2f yawMoment=%.3f "
                "yawRate=.25s %.5f\n",
                cfg.mass, asymmetric.each[0], asymmetric.each[1],
                asymmetric.moment_body.z, s.state().omega_body.z);
  }
}
void wind() {
  auto cfg = typhoonConfig();
  Simulator calm(cfg), moving(cfg);
  State state;
  state.pos_ned.z = -2000;
  state.vel_ned = {160, 0, 0};
  calm.setState(state);
  for (Vec3 wind : {Vec3{25, 0, 0}, Vec3{-25, 0, 0}, Vec3{0, 20, 0}}) {
    Weather w;
    w.wind_ned = wind;
    moving.setWeather(w);
    state.vel_ned = {160 + wind.x, wind.y, wind.z};
    moving.setState(state);
    check((moving.evalAero().force_body - calm.evalAero().force_body).norm() <
              1e-8,
          "air mass Galilean invariance");
  }
  state.vel_ned = {160, 0, 0};
  moving.setState(state);
  check(moving.evalAero().beta < 0 && moving.evalAero().force_body.y > 0,
        "crosswind force follows relative air");
  Weather head;
  head.wind_ned = {-20, 0, 0};
  moving.setWeather(head);
  check(moving.instruments().tas == 180, "headwind TAS");
  head.wind_ned = {20, 0, 0};
  moving.setWeather(head);
  check(moving.instruments().tas == 140, "tailwind TAS");
}
void damage() {
  auto s = trimmed();
  auto state = s.state();
  const auto normal = s.evalAero();
  state.surface_health[0] = .3;
  s.setState(state);
  auto damaged = s.evalAero();
  check(damaged.lift_body.norm() < normal.lift_body.norm() &&
            std::abs(damaged.moment_body.x - normal.moment_body.x) > 1000,
        "asymmetric wing degradation");
  advance(s, 60);
  check(std::abs(s.state().omega_body.x) > .01,
        "asymmetric degradation integrated roll");
  std::printf(
      "damage leftWing=.3 liftNormal=%.2f liftDamaged=%.2f rollMoment=%.2f\n",
      normal.lift_body.norm(), damaged.lift_body.norm(), damaged.moment_body.x);
}
void supersonic() {
  auto cfg = typhoonConfig();
  auto fighter = trimmed(cfg, isaAtAltitude(11000).sound * 1.5, 11000);
  check(fighter.evalAero().wave_cd > 0 && fighter.evalAero().cd < .2,
        "bounded supersonic drag");
  advance(fighter, 1200);
  check(fighter.instruments().mach > 1.4,
        "Typhoon sustained supersonic 10 seconds");
  auto explicitEngines=fighter.config();explicitEngines.afterburner_thrust_each=0;
  const auto engineTrim=solveTrim(explicitEngines,{11000,1.5*isaAtAltitude(11000).sound});
  check(engineTrim.converged && engineTrim.state.afterburner[0]>0 && engineTrim.state.afterburner[1]>0,"trim uses explicit engine components");
  std::printf("Typhoon supersonic Mach=%.5f throttle=%.5f alpha=%.5f cd=%.5f\n",
              fighter.instruments().mach, fighter.controls().throttle[0],
              fighter.instruments().alpha_deg, fighter.evalAero().cd);
  auto singleDry=typhoonConfig();singleDry.engine_count=1;singleDry.afterburner_thrust_each=0;
  Simulator falcon(singleDry);
  State initial;
  initial.pos_ned.z = -11000;
  initial.vel_ned.x = isaAtAltitude(11000).sound * 1.5;
  falcon.setState(initial);
  Controls c;
  c.gear01 = 0;
  c.throttle[0] = 1;
  falcon.setControls(c);
  advance(falcon, 1200);
  check(falcon.instruments().mach > 1,
        "single dry engine fixture stays finite in supersonic deceleration");
  std::printf("Single-engine fixture supersonic deceleration after10s Mach=%.5f (no steady "
              "Mach1.5 trim claimed)\n",
              falcon.instruments().mach);
  auto s = trimmed(a320Config(), isaAtAltitude(11000).sound * .78, 11000);
  advance(s, 1200);
  check(std::abs(s.instruments().mach - .78) < .01,
        "transport high altitude cruise");
  std::printf("A320 FL360 Mach=%.5f IAS=%.5f throttle=%.5f\n",
              s.instruments().mach, s.instruments().ias,
              s.controls().throttle[0]);
}
void fcs() {
  auto cfg = typhoonConfig();
  auto controlled = trimmed(cfg);
  auto open = controlled;
  auto state = open.state();
  state.fcs_enabled = false;
  open.setState(state);
  auto c = controlled.controls();
  c.elevator_stick = .25;
  controlled.setControls(c);
  open.setControls(c);
  auto closedMetric = advance(controlled, 240), openMetric = advance(open, 240);
  check(closedMetric.max_rate < openMetric.max_rate,
        "augmentation controls commanded response");
  check(std::abs(controlled.state().canard) > 0 &&
            std::abs(controlled.state().elevon_l) > 0,
        "canard/trailing allocator output");
  auto a = controlled.evalAero();
  check(a.surfaces[2].force_body.norm() > 0 &&
            a.surfaces[0].force_body.norm() > 0,
        "canard and wings exert forces");
  auto turn = trimmed(cfg, 220);
  c = turn.controls();
  c.elevator_stick = .65;
  c.throttle[0] = c.throttle[1] = 1;
  turn.setControls(c);
  double peak = 0;
  run(
      turn, 600, [](int, const auto &s) { return s.controls(); },
      [&](int, const auto &s) {
        peak = std::max(peak, s.instruments().g_load);
      });
  check(peak > 1.5 && peak < cfg.g_positive + 3,
        "fighter G response remains bounded");
  std::printf(
      "Typhoon augmentation closed/open peakRate=%.5f/%.5f highGPeak=%.5f\n",
      closedMetric.max_rate, openMetric.max_rate, peak);
}
void energy() {
  for (auto cfg : {a320Config(), su57Config(), typhoonConfig()}) {
    cfg.thrust_sl_static_each = 0;
    cfg.afterburner_thrust_each = 0;
    cfg.fuel_flow_scale = 0;
    cfg.control_law = FlightControlLaw::Direct;
    Simulator s(cfg);
    State state;
    state.engine_health[0]=state.engine_health[1]=0;
    state.pos_ned.z = -6000;
    state.vel_ned.x = 180;
    s.setState(state);
    Controls c;
    c.gear01 = 0;
    s.setControls(c);
    s.primeActuators();
    auto energy = [&] {
      const auto m = s.massProperties();
      auto w = s.state().omega_body;
      return .5 * m.mass * s.state().vel_ned.norm2() -
             m.mass * kG0 * s.state().pos_ned.z +
             .5 * (m.inertia.x * w.x * w.x + m.inertia.y * w.y * w.y +
                   m.inertia.z * w.z * w.z + 2 * m.ixz * w.x * w.z);
    };
    double initial = energy();
    advance(s, 240);
    check(energy() < initial, "unpowered glide dissipates total energy");
    std::printf("glide mass=%.0f energyLoss2s=%.3fJ\n", cfg.mass,
                initial - energy());
  }
}
void tensor() {
  auto cfg = typhoonConfig();
  cfg.wing_area = 0;
  cfg.thrust_sl_static_each = cfg.afterburner_thrust_each = 0;
  cfg.fuel_flow_scale = 0;
  cfg.ixz = 4000;
  Simulator s(cfg);
  State state;
  state.pos_ned.z = -10000;
  state.omega_body = {.3, .2, .1};
  s.setState(state);
  auto momentum = [&] {
    const auto m = s.massProperties();
    const auto w = s.state().omega_body;
    return s.state().att.rotate({m.inertia.x * w.x + m.ixz * w.z,
                                 m.inertia.y * w.y,
                                 m.ixz * w.x + m.inertia.z * w.z});
  };
  const auto initial = momentum();
  advance(s, 1200);
  check((momentum() - initial).norm() / initial.norm() < .005,
        "off diagonal free rotation angular momentum");
}
void envelope() {
  unsigned cases = 0;
  for (auto cfg : {a320Config(), su57Config(), typhoonConfig()})
    for (double speed : {0., 1e-7, 20., 250., 700., 1000.})
      for (Vec3 flow : {Vec3{1, 0, 0}, Vec3{-1, 0, 0}, Vec3{0, 1, 0},
                        Vec3{0, 0, 1}, Vec3{.1, .9, -.7}})
        for (bool inverted : {false, true}) {
          Simulator s(cfg);
          State state;
          state.pos_ned.z = -19000;
          state.vel_ned = flow.normalized() * speed;
          state.att = quatFromEuler(inverted ? kPi : 0, 0, 0);
          state.omega_body = {1.5, .7, .4};
          s.setState(state);
          Controls c;
          c.gear01 = 0;
          c.elevator_stick = 1;
          c.aileron_stick = -1;
          c.rudder_pedal = 1;
          c.throttle[0] = c.throttle[1] = 1;
          s.setControls(c);
          auto result = advance(s, 240);
          check(result.max_rate < 50,
                "extreme envelope rotation remains bounded");
          ++cases;
        }
  std::printf(
      "advanced envelope %u inverted/extreme flow/rate cases x 2 seconds\n",
      cases);
}
void telemetry(const char *path) {
  auto s = trimmed();
  std::ostringstream memory;
  std::ofstream file;
  if (path)
    file.open(path);
  std::ostream &out = path ? static_cast<std::ostream &>(file) : memory;
  check(!path || file.good(), "CSV destination");
  telemetryHeader(out);
  run(
      s, 1200,
      [](int tick, const auto &sim) {
        auto c = sim.controls();
        c.aileron_stick = tick < 240 ? .1 : 0;
        return c;
      },
      [&](int tick, const auto &sim) {
        if (tick % 12 == 0)
          telemetryRow(out, sim);
      });
  if (!path) {
    const auto csv = memory.str();
    check(std::count(csv.begin(), csv.end(), '\n') == 101,
          "CSV cadence/row count");
    check(csv.find("thrust_l,thrust_r,fuel,mass") != std::string::npos,
          "CSV engine/mass fields");
  }
}
} // namespace
int main(int argc, char **argv) {
  try {
    check(argc >= 2, "scenario name required");
    std::string name = argv[1];
    if (name == "c_memory")
      cMemory();
    else if (name == "atmosphere")
      atmosphere();
    else if (name == "surfaces")
      surfaces();
    else if (name == "actuators")
      actuators();
    else if (name == "mass")
      mass();
    else if (name == "fuel")
      fuel();
    else if (name == "engine_out")
      engineOut();
    else if (name == "wind")
      wind();
    else if (name == "damage")
      damage();
    else if (name == "supersonic")
      supersonic();
    else if (name == "fcs")
      fcs();
    else if (name == "energy")
      energy();
    else if (name == "tensor")
      tensor();
    else if (name == "envelope")
      envelope();
    else if (name == "telemetry")
      telemetry(argc > 2 ? argv[2] : nullptr);
    else
      throw std::invalid_argument("unknown advanced scenario");
    std::printf("PASS advanced.%s\n", name.c_str());
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "ADVANCED FAIL: %s\n", e.what());
    return 1;
  }
}
