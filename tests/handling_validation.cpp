// Dimensional flight acceptance: the pilot supplies controls, physics supplies motion.
#include "scenario.hpp"
#include "ofs/aircraft_definition.hpp"
#include <cstdio>
#include <string>
using namespace ofs;using namespace scenario;
void turns() {
 for(const auto& d:aircraftDefinitions()) {
  auto cfg=d.flight;cfg.fuel_flow_scale=0;
  const double speed=d.type==AircraftType::A320?130:d.type==AircraftType::SR71?220:180;
  TrimRequest r;r.tas=speed;r.altitude=2500;auto t=solveTrim(cfg,r);check(t.converged,"shared turn entry trim");
  Simulator sim(cfg);sim.setState(t.state);sim.setControls(t.controls);
  double load=0,bank=0,tas=0,actualRate=0,beta=0;unsigned count=0;
  double previousHeading=sim.instruments().hdg_deg*kDeg2Rad;
  run(sim,6000,[&](int,const auto& s){auto c=t.controls;auto n=s.instruments();
   c.aileron_stick=clamp(.8*(30-n.roll_deg)*kDeg2Rad/cfg.max_roll_rate,-.4,.4);
   c.elevator_stick=flightPathInput(s,0,clamp(.12*(2500-n.alt_msl),-5,5));
   c.throttle[0]=c.throttle[1]=clamp(t.controls.throttle[0]+.03*(speed-n.tas),0,1);return c;
  },[&](int tick,const auto& s){auto n=s.instruments();const double heading=n.hdg_deg*kDeg2Rad;
   const double rate=std::remainder(heading-previousHeading,2*kPi)/FixedStepClock::tick;previousHeading=heading;
   if(tick>=3600){load+=n.g_load;bank+=n.roll_deg*kDeg2Rad;tas+=n.tas;actualRate+=rate;beta+=std::abs(n.beta_deg);++count;}
  });
  load/=count;bank/=count;tas/=count;actualRate/=count;beta/=count;
  const double expectedLoad=1/std::cos(bank),expectedRate=kG0*std::tan(bank)/tas;
  std::printf("TURN %s bank=%.5f TAS=%.5f beta=%.5f load=%.6f expectedLoad=%.6f headingRate=%.6f expectedRate=%.6f altitude=%.5f\n",d.key.data(),bank*kRad2Deg,tas,beta,load,expectedLoad,actualRate,expectedRate,sim.instruments().alt_msl);
  check(std::abs(bank-30*kDeg2Rad)<3*kDeg2Rad && std::abs(sim.instruments().alt_msl-2500)<30,"sustained bank and height");
  check(std::abs(load/expectedLoad-1)<.10 && std::abs(actualRate/expectedRate-1)<.15 && beta<5,"bank/load/heading geometry and coordination");
 }
}
void departures() {
 for(const auto& d:aircraftDefinitions()) {
  auto cfg=d.flight;cfg.fuel_flow_scale=0;Simulator sim(cfg);State st;
  st.pos_ned.z=-5000;st.vel_ned={100,0,0};st.att=quatFromEuler(0,(cfg.alpha_crit_clean+5*kDeg2Rad),0);
  st.omega_body.x=.15;st.fcs_enabled=false;
  sim.setState(st);Controls c;c.gear01=0;sim.setControls(c);sim.primeActuators();
  const auto clean=sim.evalAero();check(clean.local_alpha[0]!=clean.local_alpha[1],"roll changes local wing AoA");
  st.surface_health[0]=.72;sim.setState(st);const auto asymmetric=sim.evalAero();
  check(std::abs(asymmetric.moment_body.x-clean.moment_body.x)>1000,"asymmetric local separation/damage produces departure moment");
  const auto m=advance(sim,240);
  std::printf("DEPARTURE %s wingAlpha=%.4f/%.4f rollMoment=%.1f/%.1f peakRate=%.5f minSpeed=%.5f altitudeLoss=%.5f\n",d.key.data(),clean.local_alpha[0]*kRad2Deg,clean.local_alpha[1]*kRad2Deg,clean.moment_body.x,asymmetric.moment_body.x,m.max_rate,m.min_speed,5000-sim.instruments().alt_msl);
  check(m.min_speed<100 && m.max_rate<10,"finite separated-flow energy loss");
 }
}
void bodyContacts() {
 for(const auto& d:aircraftDefinitions()) {
  auto cfg=d.flight;cfg.wing_area=0;cfg.fuel_flow_scale=0;cfg.engines_configured=false;
  cfg.thrust_sl_static_each=cfg.afterburner_thrust_each=0;cfg.control_law=FlightControlLaw::Direct;
  Simulator sim(cfg);State st;st.engine_health[0]=st.engine_health[1]=0;st.pos_ned.z=-3.5;st.vel_ned={20,0,2};st.att=quatFromEuler(8*kDeg2Rad,0,0);st.fcs_enabled=false;
  sim.setState(st);Controls c;c.gear01=0;c.throttle[0]=c.throttle[1]=0;sim.setControls(c);sim.primeActuators();
  bool touched=false;double maximumContact=0,minimumHeight=100,maximumRate=0;
  for(int i=0;i<1440;++i){sim.step(FixedStepClock::tick);minimumHeight=std::min(minimumHeight,sim.instruments().alt_msl);maximumRate=std::max(maximumRate,sim.state().omega_body.norm());
   const auto& frame=sim.debugFrame();for(unsigned k=0;k<frame.count;++k){const auto& f=frame.forces[k];if(std::string(f.name)=="body_contact"){touched=true;maximumContact=std::max(maximumContact,f.force_body.norm());check(f.pos_body.norm()>0,"body contact retains physical lever arm");}}
   check(finite(sim.state()),"body contact integration finite");
  }
  std::printf("BODY_CONTACT %s touched=%d peakForce=%.4f minHeight=%.5f peakRate=%.5f finalSpeed=%.6f\n",d.key.data(),touched,maximumContact,minimumHeight,maximumRate,sim.state().vel_ned.norm());
  check(touched && minimumHeight>.1 && maximumRate<10 && sim.state().vel_ned.norm()<1,"gear-up contact supports airframe and dissipates skid energy");
 }
}
int main(int argc,char** argv){try{check(argc==2,"scenario");std::string n=argv[1];if(n=="turns")turns();else if(n=="departures")departures();else if(n=="body_contacts")bodyContacts();else throw std::invalid_argument("scenario");return 0;}catch(const std::exception& e){std::fprintf(stderr,"HANDLING FAIL: %s\n",e.what());return 1;}}
