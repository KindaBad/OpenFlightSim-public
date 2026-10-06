#include "ofs/dynamics.hpp"
#include <stdexcept>
namespace ofs {
namespace {
Quat add(Quat q, Quat d, double h) { return {q.w+h*d.w,q.x+h*d.x,q.y+h*d.y,q.z+h*d.z}; }
State stage(const State& s,const ContinuousDerivative& d,double h) {
  State t=s;t.pos_ned+=d.position*h;t.vel_ned+=d.velocity*h;
  t.omega_body+=d.angular_velocity*h;t.att=add(s.att,d.attitude,h).normalized();t.time+=h;
  for(unsigned i=0;i<2;++i) {
    t.alpha_lag[i]=std::remainder(t.alpha_lag[i]+h*d.aerodynamic_memory.alpha[i],2*kPi);
    t.separation[i]+=h*d.aerodynamic_memory.separation[i];
    t.vortex_state[i]+=h*d.aerodynamic_memory.vortex[i];
  }
  return t;
}
}
State integrateContinuous(const State& s,double h,ContinuousIntegrator method,
                          const std::function<ContinuousDerivative(const State&)>& evaluate) {
  if(!std::isfinite(h)||h<=0)throw std::invalid_argument("Continuous dt must be finite and positive");
  const auto a=evaluate(s);
  if(method==ContinuousIntegrator::SemiImplicitEuler) {
    State out=stage(s,a,h);out.pos_ned=s.pos_ned+out.vel_ned*h;
    const Quat spin{0,out.omega_body.x,out.omega_body.y,out.omega_body.z};
    const auto dq=s.att*spin;out.att=add(s.att,dq,.5*h).normalized();return out;
  }
  const auto b=evaluate(stage(s,a,h*.5)),c=evaluate(stage(s,b,h*.5)),d=evaluate(stage(s,c,h));
  ContinuousDerivative sum;
  sum.position=(a.position+b.position*2+c.position*2+d.position)/6;
  sum.velocity=(a.velocity+b.velocity*2+c.velocity*2+d.velocity)/6;
  sum.angular_velocity=(a.angular_velocity+b.angular_velocity*2+c.angular_velocity*2+d.angular_velocity)/6;
  sum.attitude={(a.attitude.w+2*b.attitude.w+2*c.attitude.w+d.attitude.w)/6,
    (a.attitude.x+2*b.attitude.x+2*c.attitude.x+d.attitude.x)/6,
    (a.attitude.y+2*b.attitude.y+2*c.attitude.y+d.attitude.y)/6,
    (a.attitude.z+2*b.attitude.z+2*c.attitude.z+d.attitude.z)/6};
  for(unsigned i=0;i<2;++i) {
    sum.aerodynamic_memory.alpha[i]=(a.aerodynamic_memory.alpha[i]+2*b.aerodynamic_memory.alpha[i]+2*c.aerodynamic_memory.alpha[i]+d.aerodynamic_memory.alpha[i])/6;
    sum.aerodynamic_memory.separation[i]=(a.aerodynamic_memory.separation[i]+2*b.aerodynamic_memory.separation[i]+2*c.aerodynamic_memory.separation[i]+d.aerodynamic_memory.separation[i])/6;
    sum.aerodynamic_memory.vortex[i]=(a.aerodynamic_memory.vortex[i]+2*b.aerodynamic_memory.vortex[i]+2*c.aerodynamic_memory.vortex[i]+d.aerodynamic_memory.vortex[i])/6;
  }
  return stage(s,sum,h);
}
}
