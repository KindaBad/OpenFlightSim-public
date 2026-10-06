#include "ofs/f16_reference.hpp"
#include "ofs/simulator.hpp"
#include "ofs/trim.hpp"
#include "f16_imported.hpp"
#include <cstdio>
#include <stdexcept>
using namespace ofs;
int main() {try {
  double maxError=0,maxPercent=0;
  for(const auto& check:f16_data::aero_checks) {
    const auto& u=check.inputs;AeroInputs in;
    in.alpha=u[1];in.beta=u[2];in.phat=u[3]*30*.3048/(2*u[0]);in.qhat=u[4]*11.32*.3048/(2*u[0]);in.rhat=u[5]*30*.3048/(2*u[0]);
    in.elevator=u[6];in.aileron=u[7];in.rudder=u[8];
    const auto c=f16ReferenceAero()->coefficients(in);const std::array actual{c.cx,c.cy,c.cz,c.cl,c.cm,c.cn};
    for(std::size_t k=0;k<6;++k) {
      const double error=std::abs(actual[k]-check.outputs[k]),percent=check.outputs[k]?error/std::abs(check.outputs[k])*100:0;
      maxError=std::max(maxError,error);maxPercent=std::max(maxPercent,percent);
      std::printf("external F16 aero %s coefficient=%zu expected=%.14g actual=%.14g abs=%.6g percent=%s%.6g tolerance=%.6g\n",check.name,k,check.outputs[k],actual[k],error,check.outputs[k]?"":"N/A zero; ",percent,check.tolerance[k]);
      if(error>check.tolerance[k])throw std::runtime_error("NASA aero staticShot mismatch");
    }
  }
  for(const auto& check:f16_data::prop_checks) {
    const auto t=f16ReferenceEngine()->evaluate(check.inputs[0],check.inputs[1],check.inputs[2],true);
    const double error=std::abs(t.thrust-check.output);
    std::printf("external F16 engine %s expected_N=%.12g actual_N=%.12g abs_N=%.6g percent=%.6g tolerance_N=%.6g\n",check.name,check.output,t.thrust,error,check.output?100*error/std::abs(check.output):0,check.tolerance);
    if(error>check.tolerance)throw std::runtime_error("NASA propulsion staticShot mismatch");
  }
  const auto cfg=f16ReferenceConfig();auto request=TrimRequest{};request.tas=150;request.altitude=3048;request.gear01=0;
  const auto trim=solveTrim(cfg,request);if(!trim.converged)throw std::runtime_error("Reference aircraft trim failed");
  Simulator sim(cfg);sim.setIntegrator(ContinuousIntegrator::RungeKutta4);sim.setState(trim.state);sim.setControls(trim.controls);
  const auto eval=sim.evaluateContinuous(sim.state(),sim.controls(),{});
  if(eval.derivative.velocity.norm()>1e-7 || eval.derivative.angular_velocity.norm()>1e-7)throw std::runtime_error("Trim derivative residual");
  // Runtime body-force/moment reconstruction and pilot control signs.
  const auto aero=sim.evalAero();const double scale=aero.qbar*cfg.wing_area;
  const auto neutral=sim.state();auto right=neutral;right.aileron=.1;
  sim.setState(right);if(sim.evalAero().moment_body.x<=aero.moment_body.x)throw std::runtime_error("NASA aileron sign conversion");
  auto pull=neutral;pull.elevator+=.1;sim.setState(pull);if(sim.evalAero().moment_body.y<=aero.moment_body.y)throw std::runtime_error("NASA elevator sign conversion");
  if(!std::isfinite(scale)||scale<=0)throw std::runtime_error("Runtime table scaling");
  sim.setState(neutral);
  sim.step(.1);
  std::printf("NASA staticShot validation: 16 aero/96 coefficient comparisons, 9 propulsion cases; max_abs=%.9g max_percent=%.9g; local trim alpha_deg=%.9g elevator=%.9g power=%.9g (trim is mathematical, not independent flight validation)\n",maxError,maxPercent,trim.state.att.y*2*kRad2Deg,trim.controls.elevator_trim,trim.controls.throttle[0]);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL external validation: %s\n",e.what());return 1;}}
