#include "ofs/trim.hpp"
#include <algorithm>
#include <array>
#include <cmath>
namespace ofs {
TrimResult solveTrim(const AircraftConfig& config, const TrimRequest& request) {
  TrimResult out;
  if (!std::isfinite(request.altitude) || request.altitude <= 1.1 || !std::isfinite(request.flap01) || !std::isfinite(request.gear01) ||
      !std::isfinite(request.tas) || request.tas < 10 ||
      !std::isfinite(request.gamma) || std::abs(request.gamma) > .3 ||
      !std::isfinite(config.mass) || config.mass <= 0 || config.mac <= 0) return out;
  Simulator sim(config);
  std::array<double,3> x{.12, .1, .5}; // alpha, pilot elevator, throttle
  auto evaluate = [&](const std::array<double,3>& v) {
    State s; s.pos_ned.z = -request.altitude;
    s.vel_ned = {request.tas * std::cos(request.gamma), 0, -request.tas * std::sin(request.gamma)};
    s.att = quatFromEuler(0, v[0] + request.gamma, 0);
    s.n1[0] = s.n1[1] = v[2];
    const auto& physical=sim.config();
    if(physical.variable_inlets)
      for(auto& spike:s.inlet_spike) spike=inletSpikeTarget(request.tas/isaAtAltitude(request.altitude).sound);
    for(unsigned engine=0;engine<physical.engine_count;++engine)
      if(physical.engines[engine].reheat_thrust>physical.engines[engine].dry_thrust && physical.afterburner_threshold<1)
        s.afterburner[engine]=clamp((v[2]-physical.afterburner_threshold)/(1-physical.afterburner_threshold),0,1);
    Controls c; c.elevator_trim = v[1]; c.throttle[0] = c.throttle[1] = v[2];
    c.gear01 = request.gear01; c.flap01 = request.flap01;
    sim.setState(s); sim.setControls(c); sim.primeActuators();
    const auto a = sim.evalAero(); const auto t = sim.evalThrust();
    out.state = sim.state(); out.controls = sim.controls();
    const double weight=sim.massProperties().mass*kG0;
    out.residual_force_world = s.att.rotate(a.force_body + t.force_body) + Vec3{0,0,weight};
    out.residual_moment_body = a.moment_body + t.moment_body;
    return std::array<double,3>{out.residual_force_world.x/weight,
      out.residual_force_world.z/weight, out.residual_moment_body.y/(weight*config.mac)};
  };
  auto norm = [](auto v) { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); };
  for (int iteration=0; iteration<40; ++iteration) {
    out.iterations = iteration+1;
    const auto r = evaluate(x);
    if (norm(r) < 1e-10) {
      out.converged=true;
      if(out.controls.gear01>.5) for(const auto& point:{config.gear_nose,config.gear_main_l,config.gear_main_r})
        if((out.state.pos_ned+out.state.att.rotate(point-sim.massProperties().cg)).z>=0) out.converged=false;
      return out;
    }
    double matrix[3][4]{};
    for (int j=0; j<3; ++j) {
      // Inward finite difference at actuator/throttle bounds. An outward
      // perturbation is sanitized to the same value and makes a false singular Jacobian.
      const double epsilon=j>0 && x[j]>=1-1e-5 ? -1e-5 : 1e-5;
      auto perturbed=x; perturbed[j]+=epsilon; const auto rp=evaluate(perturbed);
      for (int i=0;i<3;++i) matrix[i][j]=(rp[i]-r[i])/epsilon;
    }
    for(int i=0;i<3;++i) matrix[i][3]=-r[i];
    for(int j=0;j<3;++j) {
      int pivot=j;
      for(int i=j+1;i<3;++i) if(std::abs(matrix[i][j])>std::abs(matrix[pivot][j])) pivot=i;
      for(int k=j;k<4;++k) std::swap(matrix[j][k],matrix[pivot][k]);
      if(std::abs(matrix[j][j])<1e-12) { evaluate(x); return out; }
      const double div=matrix[j][j]; for(int k=j;k<4;++k) matrix[j][k]/=div;
      for(int i=0;i<3;++i) if(i!=j) {
        const double scale=matrix[i][j]; for(int k=j;k<4;++k) matrix[i][k]-=scale*matrix[j][k];
      }
    }
    bool improved=false;
    for(double scale=1;scale>=1.0/128;scale*=.5) {
      auto candidate=x;
      for(int j=0;j<3;++j) candidate[j]+=scale*matrix[j][3];
      candidate[0]=clamp(candidate[0],-7*kDeg2Rad,13*kDeg2Rad);
      candidate[1]=clamp(candidate[1],-1,1); candidate[2]=clamp(candidate[2],0,1);
      if(norm(evaluate(candidate))<norm(r)) { x=candidate; improved=true; break; }
    }
    if(!improved) break;
  }
  evaluate(x); return out;
}
}
