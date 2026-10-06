#include "case.hpp"
#include "ofs/aircraft_definition.hpp"
#include "ofs/airliner.hpp"
#include <fstream>
#include <sstream>
using namespace ofs;
using scenario::check;
namespace {
AircraftConfig configuration(const validation::Value& c) {
  auto cfg=aircraftDefinition(aircraftTypeFromName(c["aircraft"].text())).flight;
  // Use the existing configuration reference tensor. Load changes belong in
  // State so massProperties applies the fuel and payload parallel-axis terms.
  cfg.fuel_flow_scale=0;
  return cfg;
}
State loadedState(const validation::Value& c) {
  State s;s.pos_ned.z=-c["altitude_m"].number();s.fuel_mass=c["fuel_kg"].number();
  s.payload_mass=c["payload_kg"].number();s.fcs_enabled=false;
  return s;
}
double measure(const validation::Value& c) {
  const auto cfg=configuration(c);Simulator sim(cfg,Simulator::GroundModel::FlatRunway);
  auto s=loadedState(c);check(sim.setState(s),"case load accepted");
  check(std::abs(sim.massProperties().mass-c["mass_kg"].number())<1e-6,"case mass/fuel/payload inconsistent");
  Controls controls;controls.flap01=c["flap01"].number();controls.gear01=c["gear01"].number();sim.setControls(controls);
  const auto metric=c["metric"].text();
  if(metric=="approach_knots"||metric=="stall_knots") {
    double maximum=0;
    for(unsigned step=0;step<=250;++step) {
      const auto alpha=step*.1*kDeg2Rad;
      s.vel_ned={70*std::cos(alpha),0,70*std::sin(alpha)};
      check(sim.setState(s),"polar state accepted");maximum=std::max(maximum,sim.evalAero().cl);
    }
    check(maximum>0,"lift maximum exists");
    const double equivalent=std::sqrt(2*c["mass_kg"].number()*kG0/(1.225*cfg.wing_area*maximum));
    return equivalent*(metric=="approach_knots"?c["stall_ratio"].number():1)/.514444444444;
  }
  if(metric.starts_with("inertia_")) {
    const auto m=sim.massProperties();
    if(metric=="inertia_xx")return m.inertia.x;
    if(metric=="inertia_yy")return m.inertia.y;
    if(metric=="inertia_zz")return m.inertia.z;
    if(metric=="inertia_xz")return m.ixz;
    throw std::runtime_error("unknown inertia component");
  }
  if(metric=="thrust_each_N") {
    s.n1[0]=s.n1[1]=1;
    s.afterburner[0]=s.afterburner[1]=c["engine_mode"].text()=="afterburner"?1:0;
    if(cfg.afterburner_threshold<1&&s.afterburner[0]==0)s.n1[0]=s.n1[1]=cfg.afterburner_threshold;
    check(sim.setState(s),"static engine state accepted");return sim.evalThrust().each[0];
  }
  if(metric=="trim_throttle") {
    // Trim loads through configuration while preserving the original load
    // tensor definition in the separate trajectory evaluator.
    auto trimCfg=cfg;trimCfg.initial_fuel=s.fuel_mass;trimCfg.initial_payload=s.payload_mass;
    trimCfg.mass=c["mass_kg"].number();
    TrimRequest request;request.altitude=c["altitude_m"].number();request.tas=c["mach"].number()*isaAtAltitude(request.altitude).sound;
    request.flap01=controls.flap01;request.gear01=controls.gear01;
    const auto trim=solveTrim(trimCfg,request);check(trim.converged,"cruise trim must converge");
    return trim.controls.throttle[0];
  }
  throw std::runtime_error("unknown case metric");
}
}
int main(int argc,char** argv) {try {
  check(argc==3,"case file and case id required");std::ifstream input(argv[1]);check(bool(input),"case file exists");
  std::ostringstream contents;contents<<input.rdbuf();const auto text=contents.str();ofs::client::json::Parser parser(text);
  unsigned selected=0;bool passed=true;
  for(const auto& c:parser.root().items()) {
    validation::requireMetadata(c);
    if(c["id"].text()!=argv[2])continue;
    ++selected;passed=validation::report(c,measure(c))&&passed;
  }
  check(selected==1,"case id unique and present");return passed?0:1;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL validation: %s\n",e.what());return 1;}}
