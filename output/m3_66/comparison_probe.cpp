#include "ofs/aircraft_definition.hpp"
#include "ofs/simulator.hpp"
#include "ofs/trim.hpp"
#include <cstdio>
int main() {
  using namespace ofs;
  auto cfg=typhoonConfig();
#ifdef M366
  cfg.fuel_flow_scale=0;
#endif
  auto trim=solveTrim(cfg,{1000,180});
  Simulator sim(cfg);sim.setState(trim.state);auto c=trim.controls;c.throttle[0]=c.throttle[1]=1;sim.setControls(c);
  for(int i=0;i<1200;++i)sim.step(1./120);
  std::printf("Typhoon full-reheat acceleration from trimmed180 10s TAS=%.6f altitude=%.6f Mach=%.6f\n",sim.instruments().tas,sim.instruments().alt_msl,sim.instruments().mach);
  Simulator high(cfg);State state;state.pos_ned.z=-11000;state.vel_ned.x=1.5*isaAtAltitude(11000).sound;high.setState(state);auto a=high.evalAero();
  auto sup=solveTrim(cfg,{11000,state.vel_ned.x});
  std::printf("Typhoon M1.5 FL360 zero-AoA CD=%.6f trim=%d throttle=%.6f\n",a.cd,sup.converged,sup.controls.throttle[0]);
}
