#include "ofs/simulator.hpp"
#include "ofs/aerodynamics.hpp"
#include "ofs/control_allocation.hpp"
#include "flight_model_detail.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace ofs {
using namespace detail;
Simulator::ContinuousEvaluation Simulator::evaluateContinuous(const State& s,const Controls& c,const Weather& w) const {
  ContinuousEvaluation out;
  out.aero=evalAero(s,c,w);out.propulsion=evalThrust(s,w);
  const auto mass=massProperties(s);
  out.derivative.aerodynamic_memory=unsteadyDerivative(cfg_,s,s.att.inverseRotate(s.vel_ned-windAt(s.pos_ned,s.time,w)));
  out.derivative.position=s.vel_ned;
  out.derivative.velocity=s.att.rotate(out.aero.force_body+out.propulsion.force_body)/mass.mass+Vec3{0,0,kG0};
  out.derivative.angular_velocity=mass.tensor().solve(out.aero.moment_body+out.propulsion.moment_body-s.omega_body.cross(mass.tensor().apply(s.omega_body)));
  const auto dq=s.att*Quat{0,s.omega_body.x,s.omega_body.y,s.omega_body.z};
  out.derivative.attitude={dq.w*.5,dq.x*.5,dq.y*.5,dq.z*.5};
  return out;
}


} // namespace ofs
