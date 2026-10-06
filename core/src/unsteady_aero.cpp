#include "ofs/unsteady_aero.hpp"
#include "ofs/aerodynamics.hpp"
namespace ofs {
namespace {
struct Section { double alpha{},separation{},vortex{}; };
Section section(const AircraftConfig& c,const State& s,Vec3 v,unsigned i) {
  const auto local=v+s.omega_body.cross(c.surfaces[i].position-loadedCg(c,s));
  const double alpha=local.norm()>1e-6?std::atan2(local.z,local.x):0;
  const double beta=local.norm()>1e-6?std::atan2(local.y,std::hypot(local.x,local.z)):0;
  const double effective=std::remainder(alpha+(i==0?-1:1)*c.unsteady_beta_gain*beta,2*kPi);
  const double lag=s.aero_memory_initialized?s.alpha_lag[i]:effective;
  const double rate=std::remainder(effective-lag,2*kPi)/c.unsteady_alpha_tau;
  const double incidence=lag+clamp(c.unsteady_alpha_dot_gain*rate,-8*kDeg2Rad,8*kDeg2Rad);
  // Vortex growth precedes breakdown. These are generic mechanism states, not
  // Su-57 wind-tunnel coefficients. Signed CL is applied in evalAero.
  const double strength=std::abs(aerodynamics::vortexLift(incidence,0,1));
  return {effective,aerodynamics::separation(incidence,c.alpha_crit_clean),strength};
}
}
void initializeUnsteady(const AircraftConfig& c,State& s,Vec3 v) {
  if(c.unsteady_alpha_tau<=0||s.aero_memory_initialized)return;
  for(unsigned i=0;i<2;++i) {
    const auto target=section(c,s,v,i);
    s.alpha_lag[i]=target.alpha;s.separation[i]=target.separation;s.vortex_state[i]=target.vortex;
  }
  s.aero_memory_initialized=true;
}
UnsteadyDerivative unsteadyDerivative(const AircraftConfig& c,const State& s,Vec3 v) {
  UnsteadyDerivative out;
  if(c.unsteady_alpha_tau<=0||!s.aero_memory_initialized)return out;
  for(unsigned i=0;i<2;++i) {
    const auto target=section(c,s,v,i);
    out.alpha[i]=std::remainder(target.alpha-s.alpha_lag[i],2*kPi)/c.unsteady_alpha_tau;
    const double tau=target.separation>s.separation[i]?c.unsteady_detach_tau:c.unsteady_attach_tau;
    out.separation[i]=(target.separation-s.separation[i])/tau;
    out.vortex[i]=(target.vortex-s.vortex_state[i])/c.unsteady_vortex_tau;
  }
  return out;
}
}
