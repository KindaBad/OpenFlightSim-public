#include "ofs/simulator.hpp"
#include "ofs/fixed_step.hpp"
#include "ofs/c_api.h"
#include "ofs/pilot.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void near(double a, double b, double tolerance, const char* message) {
  require(std::isfinite(a) && std::abs(a - b) <= tolerance, message);
}
ofs::State airborne(double alpha = 0) {
  ofs::State s;
  s.pos_ned = {0, 0, -2000};
  s.vel_ned = {110 * std::cos(alpha), 0, 110 * std::sin(alpha)};
  return s;
}
void math() {
  using namespace ofs;
  const auto q = quatFromEuler(0, 10 * kDeg2Rad, 0);
  require(q.rotate({1, 0, 0}).z < 0, "positive pitch must point nose UP in NED");
  require(quatFromEuler(0, 0, kPi / 2).rotate({1, 0, 0}).y > .99, "yaw right is east");
  require(quatFromEuler(kPi / 2, 0, 0).rotate({0, 1, 0}).z > .99, "right roll lowers right wing");
  double r, p, y;
  const auto mixed = quatFromEuler(.2, .3, .4);
  eulerFromQuat(mixed, r, p, y);
  near(r, .2, 1e-12, "roll roundtrip"); near(p, .3, 1e-12, "pitch roundtrip"); near(y, .4, 1e-12, "yaw roundtrip");
  const Vec3 v{4, 5, 6};
  near((mixed.inverseRotate(mixed.rotate(v)) - v).norm(), 0, 1e-12, "inverse rotation");
}
void atmosphere() {
  const auto sea = ofs::isaAtAltitude(0), eleven = ofs::isaAtAltitude(11000);
  near(sea.pressure, 101325, .01, "sea pressure"); near(sea.rho, 1.225, .0001, "sea density");
  near(eleven.temp, 216.65, .001, "tropopause temperature");
  near(eleven.pressure, 22632, 2, "tropopause pressure");
  near(eleven.pressure, ofs::isaAtAltitude(11000.001).pressure, .01, "layer continuity");
  require(ofs::isaAtAltitude(20000).rho < eleven.rho, "density decreases");
  require(ofs::isaAtAltitude(0, 20).rho < sea.rho, "hot air less dense");
  near(ofs::isaAtAltitude(50000).pressure, ofs::isaAtAltitude(47000).pressure, 0, "47km altitude clamp");
  near(ofs::isaAtAltitude(25000).temp,221.65,.001,"lower stratosphere lapse");
  near(ofs::isaAtAltitude(32000).temp,228.65,.001,"32km layer temperature");
  near(ofs::isaAtAltitude(32000).pressure,ofs::isaAtAltitude(32000.001).pressure,.001,"32km layer continuity");
  require(ofs::isaAtAltitude(25000).rho<ofs::isaAtAltitude(20000).rho,"high-altitude density decreases");
}
void aero() {
  using namespace ofs;
  Simulator sim; sim.setState(airborne(5 * kDeg2Rad));
  const auto a = sim.evalAero();
  require(a.force_body.z < 0 && a.cd > 0 && a.qbar > 0, "lift up, positive drag");
  require(a.force_body.dot(sim.state().vel_ned) < 0, "aerodynamic force dissipates forward energy");
  near(a.alpha, 5 * kDeg2Rad, 1e-12, "angle of attack");
  Controls c; c.elevator_stick = 1; sim.setControls(c);
  require(sim.evalAero().moment_body.y > a.moment_body.y, "pull increases nose-up moment");
  c = {}; c.rudder_pedal = 1; sim.setControls(c);
  require(sim.evalAero().moment_body.z > a.moment_body.z, "right rudder increases right yaw");
  c = {}; c.aileron_stick = 1; sim.setControls(c);
  require(sim.evalAero().moment_body.x > a.moment_body.x, "right aileron increases right roll");
  sim.setControls({}); sim.setState(airborne(14 * kDeg2Rad)); const auto peak = sim.evalAero();
  sim.setState(airborne(25 * kDeg2Rad)); require(sim.evalAero().cl < peak.cl, "post-stall lift collapse");
  c = {}; c.flap01 = 1; sim.setState(airborne(5 * kDeg2Rad)); sim.setControls(c);
  require(sim.evalAero().cl > a.cl && sim.evalAero().cd > a.cd, "flaps add lift and drag");
}
void integration() {
  using namespace ofs;
  Simulator a, b; a.setState(airborne()); b.setState(airborne());
  Controls c; c.throttle[0] = c.throttle[1] = .65; c.gear01 = 0;
  a.setControls(c); b.setControls(c);
  Weather w; w.turbulence01 = .1; a.setWeather(w); b.setWeather(w);
  for (int i = 0; i < 120 * 120; ++i) { a.step(1.0 / 120); b.step(1.0 / 120); }
  near(a.state().time, 120, 1e-8, "two minutes advanced");
  require(std::isfinite(a.state().pos_ned.norm()) && std::isfinite(a.state().omega_body.norm()), "long run finite");
  near((a.state().pos_ned - b.state().pos_ned).norm(), 0, 0, "same build deterministic position");
  near(a.state().att.w, b.state().att.w, 0, "same build deterministic attitude");
  const auto q = a.state().att;
  near(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z, 1, 1e-12, "quaternion stays unit");
  require(a.state().n1[0] > .64 && a.state().n1[0] < .66, "engine spool converges");
  const double t = a.state().time;
  a.step(-1); a.step(std::numeric_limits<double>::infinity()); a.step(std::nan(""));
  near(a.state().time, t, 0, "invalid elapsed time ignored");
  c.elevator_stick = std::nan(""); c.throttle[0] = 2; a.setControls(c);
  near(a.controls().elevator_stick, 0, 0, "NaN input neutralized"); near(a.controls().throttle[0], 1, 0, "throttle clamped");
  // Free-fall reference with forces removed, away from contacts.
  auto cfg = a320Config(); cfg.wing_area = 0; cfg.thrust_sl_static_each = 0;
  Simulator fall(cfg); State s; s.engine_health[0]=s.engine_health[1]=0; s.pos_ned.z = -1000; fall.setState(s); fall.step(.1);
  near(fall.state().vel_ned.z, kG0*.1, 1e-10, "gravity integration");
}
void contact() {
  using namespace ofs;
  auto verticalForce = [](double speed, bool gear) {
    Simulator s; State st; st.pos_ned.z = gear ? -3.4 : -1.0; st.vel_ned.z = speed;
    Controls c; c.gear01 = gear ? 1 : 0; s.setState(st); s.setControls(c); s.step(1.0 / 240);
    return s.debugFrame().total_force_body.z;
  };
  require(verticalForce(2, true) < verticalForce(0, true), "gear damper resists compression");
  require(verticalForce(2, false) < verticalForce(0, false), "belly damper resists sinking");
  Simulator parked; State s; s.pos_ned.z = -3.4; parked.setState(s);
  Controls c; c.brake01 = 1; parked.setControls(c);
  for (int i = 0; i < 120 * 60; ++i) parked.step(1.0 / 120);
  require(parked.instruments().alt_msl > 2.5 && parked.instruments().alt_msl < 4, "gear settles above ground");
  require(std::abs(parked.state().vel_ned.z) < .1, "gear settles vertically");
}
void clockTest() {
  using namespace ofs;
  FixedStepClock a, b; unsigned calls = 0;
  a.advance(FixedStepClock::tick*.5, [&](double){ ++calls; });
  require(calls == 0, "zero ticks for partial frame"); near(a.alpha(), .5, 1e-12, "interpolation half tick");
  a.advance(FixedStepClock::tick*.5, [&](double dt){ near(dt, FixedStepClock::tick, 0, "fixed dt"); ++calls; });
  require(calls == 1, "partials combine");
  a.reset(); Simulator simA,simB; simA.setState(airborne()); simB.setState(airborne());
  for(int i = 0; i < 60; ++i) a.advance(1.0/60, [&](double dt){simA.step(dt);});
  for(int i = 0; i < 144; ++i) b.advance(1.0/144, [&](double dt){simB.step(dt);});
  near((simA.state().pos_ned-simB.state().pos_ned).norm(),0,0,"same physics at different render frequencies");
  require(a.ticks() == 120 && b.ticks() == 120, "independent of render frequency");
  const auto steps = a.advance(10, [](double){});
  require(steps == FixedStepClock::max_steps, "bounded catchup");
  require(a.droppedTime() > 9.8 && a.alpha() >= 0 && a.alpha() < 1, "stall time dropped");
  const auto t = a.ticks(); a.advance(std::nan(""), [](double){}); a.advance(-1, [](double){});
  require(a.ticks() == t, "invalid frame ignored");
}
void coordinates() {
  using namespace ofs;
  RenderOrigin origin; const Vec3 p{1e9+.125, -1e9+.25, -1234.5}; origin.rebaseTo({1e9, -1e9, -1200});
  const auto local = origin.toRender(p);
  near(local.x, .125, 0, "global subtraction retains fractional meter");
  near(local.y, .25, 0, "east offset"); near(local.z, -34.5, 0, "altitude offset");
  require(!origin.rebaseIfNeeded(p), "small offset does not rebase");
  require(origin.rebaseIfNeeded(p + Vec3{10000, 0, 0}), "far position rebases");
}
void cApi() {
  OfsSim* c = ofs_create(); require(c != nullptr, "C create");
  OfsState state{}; state.pos_d = -1000; state.vel_n = 100; state.pitch_deg = 5;
  ofs_set_state(c, &state); ofs::Simulator cpp;
  ofs::State st; st.pos_ned.z = -1000; st.vel_ned.x = 100; st.att = ofs::quatFromEuler(0, 5*ofs::kDeg2Rad, 0); cpp.setState(st);
  for(int i = 0; i < 120; ++i) { ofs_step(c, 1.0/120); cpp.step(1.0/120); }
  near(ofs_get_state(c).pos_n, cpp.state().pos_ned.x, 0, "C/C++ matching integration");
  near(ofs_get_instruments(c).pitch_deg, cpp.instruments().pitch_deg, 0, "C/C++ pitch convention");
  ofs_destroy(c); ofs_destroy(nullptr); ofs_step(nullptr, .1);
  near(ofs_get_state(nullptr).time, 0, 0, "null C accessor");
}
// The pilot's tolerance of load: what can be held, what cannot, and coming round.
void pilotStrain() {
  using namespace ofs;
  const auto hold = [](double g, double seconds) {
    PilotStrain pilot;
    for (double t = 0; t < seconds; t += 1. / 120) pilot.update(g, 1. / 120);
    return pilot;
  };
  const auto until = [](double g) {
    PilotStrain pilot;
    double t = 0;
    for (; t < 600 && !pilot.incapacitated(); t += 1. / 120) pilot.update(g, 1. / 120);
    return t;
  };
  require(hold(1, 60).strain == 0 && hold(5.9, 300).vision() == 0, "ordinary manoeuvring costs nothing");
  require(hold(-1.5, 300).strain == 0, "mild negative load costs nothing");
  const double nine = until(9), twelve = until(12), seven = until(7.5);
  require(nine > 4 && nine < 8, "nine g can be held for a few seconds only");
  require(twelve > 1.5 && twelve < nine * .6, "more load takes the pilot sooner");
  require(seven > nine * 2 && seven < 30, "a little over the limit lasts much longer");
  const double negative = until(-4);
  require(negative > 1.5 && negative < 6, "negative load is tolerated less");
  // Sight goes before consciousness does, by degrees.
  PilotStrain pilot;
  double greyAt = -1, last = 0;
  for (double t = 0; !pilot.incapacitated(); t += 1. / 120) {
    pilot.update(9, 1. / 120);
    require(pilot.vision() >= last - 1e-12 && pilot.vision() <= 1, "sight narrows steadily under load");
    last = pilot.vision();
    if (greyAt < 0 && pilot.vision() > 0) greyAt = t;
  }
  require(greyAt > 1 && greyAt < nine * .6 && pilot.red < .01, "the view greys well before it is lost, and is not reddened");
  // Out cold for a fixed time whatever the load does, then recovering.
  double out = 0;
  for (; pilot.incapacitated(); out += 1. / 120) {
    pilot.update(out < 1 ? 9 : 1, 1. / 120);
    require(pilot.vision() == 1, "nothing is seen while unconscious");
  }
  near(out, PilotStrain::kUnconsciousSeconds, .02, "unconscious for the stated time");
  double clear = 0;
  for (; pilot.vision() > 0 && clear < 60; clear += 1. / 120) pilot.update(1, 1. / 120);
  require(clear > .5 && clear < 4, "sight returns over a second or two");
  for (double t = 0; t < 20; t += 1. / 120) pilot.update(1, 1. / 120);
  require(pilot.strain == 0 && !pilot.incapacitated(), "and the pilot recovers completely");
  // Easing off before the limit recovers without a blackout.
  PilotStrain eased;
  for (double t = 0; t < nine * .7; t += 1. / 120) eased.update(9, 1. / 120);
  const double strained = eased.strain;
  for (double t = 0; t < 3; t += 1. / 120) eased.update(3, 1. / 120);
  require(!eased.incapacitated() && eased.strain < strained * .6, "easing the turn brings sight back");
  require(hold(-4, 2).red > .9, "negative load reddens the view");
  PilotStrain bad;
  bad.update(std::nan(""), 1); bad.update(9, -1); bad.update(9, std::nan(""));
  require(bad.strain == 0, "nonsense input changes nothing");
}
int main(int argc, char** argv) {
  try {
    require(argc == 2, "expected suite name"); const std::string_view name = argv[1];
    if(name == "math") math(); else if(name == "atmosphere") atmosphere();
    else if(name == "aero") aero(); else if(name == "integration") integration();
    else if(name == "contact") contact(); else if(name == "clock") clockTest();
    else if(name == "coordinates") coordinates(); else if(name == "c_api") cApi();
    else if(name == "pilot") pilotStrain();
    else throw std::runtime_error("unknown suite");
    std::cout << "PASS " << name << '\n'; return 0;
  } catch(const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
