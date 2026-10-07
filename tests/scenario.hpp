#pragma once
#include "ofs/fixed_step.hpp"
#include "ofs/trim.hpp"
#include "ofs/airliner.hpp"
#include <algorithm>
#include <cmath>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
namespace scenario {
inline void check(bool ok, const char* message) { if(!ok) throw std::runtime_error(message); }
// Runtime models are generated binaries and are not version controlled (see
// .gitignore). The asset suites therefore skip instead of failing when the
// .glb is absent in ordinary builds; the asset-validation build requires them. Physics,
// registry and protocol suites never touch these files and always run.
inline bool assetPresent(const std::string& root, std::string_view relative) {
  std::error_code ec;
  return std::filesystem::is_regular_file(std::filesystem::path(root)/relative, ec) ||
    std::filesystem::is_regular_file((std::filesystem::path(root)/relative).string()+".ofspack", ec);
}
inline unsigned unavailableAssets=0;
inline int assetResult() { return unavailableAssets ? 77 : 0; }
inline bool skipMissingAsset(const std::string& root, std::string_view relative) {
  if(assetPresent(root,relative)) return false;
#ifdef OFS_REQUIRE_PRODUCTION_ASSETS
  throw std::runtime_error("Required production asset missing: "+std::string(relative));
#endif
  ++unavailableAssets;
  std::printf("[NOT RUN] %.*s absent; runtime models are not version controlled\n",
              static_cast<int>(relative.size()),relative.data());
  return true;
}
inline bool finite(const ofs::State& s) {
  return std::isfinite(s.pos_ned.norm2()) && std::isfinite(s.vel_ned.norm2()) &&
    std::isfinite(s.omega_body.norm2()) && std::isfinite(s.att.w) && std::isfinite(s.att.x) &&
    std::isfinite(s.att.y) && std::isfinite(s.att.z) && std::isfinite(s.n1[0]) && std::isfinite(s.n1[1]) && std::isfinite(s.time);
}
inline double gearLoad(const ofs::Simulator& sim) {
  return sim.debugFrame().contact_normal_force;
}
struct Metrics {
  std::uint64_t digest{14695981039346656037ull};
  double min_alt{1e30}, max_alt{-1e30}, min_speed{1e30}, max_speed{0}, max_rate{0};
  double peak_contact{0}, max_pitch{0}, min_vs{1e30}, max_vs{-1e30};
  void sample(const ofs::Simulator& s) {
    check(finite(s.state()),"scenario nonfinite state");
    const auto n=s.instruments(); const auto d=s.debugFrame();
    const auto& st=s.state();
    for(double value:{st.pos_ned.x,st.pos_ned.y,st.pos_ned.z,st.vel_ned.x,st.vel_ned.y,st.vel_ned.z,
      st.att.w,st.att.x,st.att.y,st.att.z,st.omega_body.x,st.omega_body.y,st.omega_body.z,st.n1[0],st.n1[1],st.time}) {
      digest ^= std::bit_cast<std::uint64_t>(value); digest *= 1099511628211ull;
    }
    check(std::isfinite(d.total_force_body.norm2()) && std::isfinite(d.total_moment_body.norm2()),"nonfinite forces");
    min_alt=std::min(min_alt,n.alt_msl); max_alt=std::max(max_alt,n.alt_msl);
    min_speed=std::min(min_speed,n.tas); max_speed=std::max(max_speed,n.tas);
    max_rate=std::max(max_rate,s.state().omega_body.norm());
    peak_contact=std::max(peak_contact,gearLoad(s)); max_pitch=std::max(max_pitch,std::abs(n.pitch_deg));
    min_vs=std::min(min_vs,n.vs); max_vs=std::max(max_vs,n.vs);
  }
};
// Scripts only supply controls; no script may constrain state during a run.
// Integer tick index is the clock, independent of accumulated floating time.
template<class Script, class Observer>
Metrics run(ofs::Simulator& sim, int ticks, Script script, Observer observe) {
  Metrics metrics;
  for(int tick=0;tick<ticks;++tick) {
    const auto controls=script(tick,sim);
    sim.setControls(controls); sim.step(ofs::FixedStepClock::tick);
    metrics.sample(sim); observe(tick,sim);
  }
  return metrics;
}
inline Metrics advance(ofs::Simulator& sim, int ticks) {
  return run(sim,ticks,[](int,const auto& s){return s.controls();},[](int,const auto&){});
}
// A test pilot, kept out of runtime physics: moment feedforward plus pitch/rate
// feedback. The sim still integrates all forces and angular acceleration.
inline double pitchInput(const ofs::Simulator& sim, double target_rad) {
  if(sim.state().fcs_enabled && sim.config().control_law==ofs::FlightControlLaw::VectorFighter) {
    const double desiredRate=2.0*(target_rad-sim.instruments().pitch_deg*ofs::kDeg2Rad);
    return ofs::clamp(desiredRate/sim.config().max_pitch_rate-sim.controls().elevator_trim+sim.state().trim_reference,-1,1);
  }
  const auto& cfg=sim.config(); const auto a=sim.evalAero(); const auto t=sim.evalThrust();
  const double cm=a.qbar>10 ? -t.moment_body.y/(a.qbar*cfg.wing_area*cfg.mac) : 0;
  const double de=(cm-cfg.cm0-cfg.cm_alpha*std::sin(a.alpha)+.1*sim.controls().flap01)/cfg.cm_de;
  const double feed=de<0 ? de/cfg.elev_min : -de/cfg.elev_max;
    const double command=feed-sim.controls().elevator_trim+1.8*(target_rad-sim.instruments().pitch_deg*ofs::kDeg2Rad)-4*sim.state().omega_body.y;
  if(sim.state().fcs_enabled && cfg.control_law!=ofs::FlightControlLaw::Direct && a.vtas>30 && a.qbar>50) {
    const double authority=a.qbar*cfg.wing_area*cfg.mac*std::abs(cfg.cm_de*cfg.elev_min);
    if(cfg.aero_kind==ofs::AeroModelKind::AirlinerEngineering) {
      const double desiredRate=(command+sim.controls().elevator_trim-sim.equilibriumElevator())*cfg.response_time*authority/sim.massProperties().inertia.y+sim.state().omega_body.y;
      double bank,pitch,heading;ofs::eulerFromQuat(sim.state().att,bank,pitch,heading);
      const double neutral=std::cos(pitch)/std::max(.5,std::cos(bank));
      const double desired=sim.normalLoad()+desiredRate/.18;
      const double upper=sim.state().flap>.2?2:cfg.g_positive,lower=sim.state().flap>.2?0:cfg.g_negative;
      return ofs::clamp((desired-neutral)/(desired>=neutral?upper-neutral:neutral-lower),-1,1);
    }
    return ofs::clamp(((command+sim.controls().elevator_trim-sim.equilibriumElevator())*cfg.response_time*authority/sim.massProperties().inertia.y+sim.state().omega_body.y)/cfg.max_pitch_rate-sim.controls().elevator_trim+sim.state().trim_reference-(cfg.control_law==ofs::FlightControlLaw::Transport?.12:.08)*(sim.state().att.inverseRotate({0,0,1}).z-sim.normalLoad())/cfg.max_pitch_rate,-1,1);
  }
  return ofs::clamp(command,-1,1);
}
inline double flightPathInput(const ofs::Simulator& sim, double flap, double target_vs) {
  const auto& cfg=sim.config();
  // Test-pilot approximation of 1g lift demand; all response is still physical.
  double alpha=(cfg.mass*ofs::kG0/(std::max(sim.evalAero().qbar,100.)*cfg.wing_area)-cfg.flap_lift*flap)/cfg.cl_alpha+cfg.alpha0;
  const auto n=sim.instruments();
  ofs::TrimRequest request;request.altitude=std::max(20.,n.alt_msl);request.tas=std::max(30.,n.tas);request.flap01=flap;request.gear01=sim.controls().gear01;
  const auto equilibrium=ofs::solveTrim(cfg,request);
  if(equilibrium.converged) alpha=equilibrium.state.att.y!=0?std::asin(ofs::clamp(2*equilibrium.state.att.w*equilibrium.state.att.y,-1,1)):0;
  const double pitch=alpha+std::asin(target_vs/std::max(n.tas,20.))+.02*(target_vs-n.vs);
  return pitchInput(sim,pitch);
}
}
