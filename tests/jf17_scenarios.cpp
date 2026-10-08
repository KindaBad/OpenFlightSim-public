#include "scenario.hpp"
#include "ofs/aircraft_definition.hpp"
#include <cstdio>
#include <string>
using namespace ofs;
using namespace scenario;
namespace {
AircraftConfig referenceConfig() { auto c=jf17Config();c.fuel_flow_scale=0;return c; }
Simulator trimmed(double speed=180,double height=1500,double flap=0,double gear=0,double gamma=0,Simulator::GroundModel ground=Simulator::GroundModel::Terrain) {
  TrimRequest r;r.tas=speed;r.altitude=height;r.flap01=flap;r.gear01=gear;r.gamma=gamma;
  auto t=solveTrim(referenceConfig(),r);check(t.converged,"JF-17 trim convergence");
  Simulator sim(referenceConfig(),ground);sim.setState(t.state);sim.setControls(t.controls);return sim;
}
void engines() {
  Simulator sim(referenceConfig());State s;s.pos_ned.z=-1000;sim.setState(s);
  check(sim.config().engine_count==1,"one engine");
  Controls c;c.gear01=0;c.throttle[0]=c.throttle[1]=.85;sim.setControls(c);advance(sim,1200);
  check(sim.state().afterburner[0]==0 && sim.state().afterburner[1]==0,"military has no afterburner");
  s=sim.state();s.pos_ned.z=0;s.vel_ned={};s.n1[0]=s.n1[1]=.85;s.afterburner[0]=s.afterburner[1]=0;sim.setState(s);
  const auto military=sim.evalThrust();const double dry=military.force_body.x;
  s.n1[0]=s.n1[1]=1;s.afterburner[0]=s.afterburner[1]=1;sim.setState(s);
  const auto full=sim.evalThrust();const double reheat=full.force_body.x;
  check(std::abs(dry-49400)<50 && std::abs(reheat-84400)<85,"published RD-93 dry and reheat ratings");
  check(full.each[1]==0 && military.each[1]==0 && std::abs(full.moment_body.z)<1e-6,"the second engine slot makes no thrust or yaw");
  c.throttle[0]=c.throttle[1]=1;sim.setControls(c);advance(sim,600);
  check(sim.state().afterburner[0]>.99 && sim.state().afterburner[1]==0,"reheat lights on the one engine only");
  c.throttle[0]=.849;sim.setControls(c);advance(sim,120);
  check(sim.state().afterburner[0]<.01,"responsive extinction below military detent");
  std::printf("JF-17 thrust dry=%.0fN reheat=%.0fN\n",dry,reheat);
}
void cruise() {
  for(double speed:{110.,180.,250.}) {
    // Equilibrium is measured over flat reference ground, independent of
    // scenery: at 250 m/s this run travels 30 km into the mountain range.
    auto sim=trimmed(speed,1500,0,0,0,Simulator::GroundModel::FlatRunway);auto m=advance(sim,14400);
    std::printf("JF-17 cruise TAS=%.0f throttle=%.6f pitch=%.5f heightDrift=%.6g speedDrift=%.6g\n",speed,sim.controls().throttle[0],sim.instruments().pitch_deg,m.max_alt-m.min_alt,m.max_speed-m.min_speed);
    check(m.max_alt-m.min_alt<1 && m.max_speed-m.min_speed<.1 && m.max_rate<1e-4,"120s JF-17 equilibrium");
  }
}
void taxi() {
  Simulator sim(referenceConfig());State s;s.pos_ned.z=-2.044;sim.setState(s);Controls c;c.brake01=1;sim.setControls(c);advance(sim,3600);
  check(sim.instruments().tas<1e-5,"parked stable");
  c.brake01=0;c.throttle[0]=c.throttle[1]=.3;sim.setControls(c);advance(sim,1200);
  const auto speed=sim.instruments().tas;check(speed>1 && speed<20,"controlled taxi speed");
  c.steering=.2;sim.setControls(c);advance(sim,240);const auto heading=std::remainder(sim.instruments().hdg_deg,360.);
  check(heading>1,"right steering");c.steering=0;c.throttle[0]=c.throttle[1]=0;c.brake01=1;sim.setControls(c);advance(sim,2400);
  std::printf("JF-17 taxi TAS=%.4f rightHeading=%.4f finalSpeed=%.9g height=%.4f\n",speed,heading,sim.instruments().tas,sim.instruments().alt_msl);
  check(sim.instruments().tas<1e-5 && sim.instruments().alt_msl>1.5,"stable braking stop");
}
void takeoff() {
  Simulator sim(referenceConfig());State s;s.pos_ned.z=-2.044;sim.setState(s);Controls c;c.brake01=1;c.flap01=.35;
  bool lifted=false;double time=0,x=0,speed=0;
  auto m=run(sim,9600,[&](int tick,const auto& a){
    if(tick>360){c.brake01=0;c.throttle[0]=c.throttle[1]=1;}
    if(a.instruments().tas>75)c.elevator_stick=pitchInput(a,12*kDeg2Rad);
    if(lifted && a.instruments().agl>8){c.gear01=0;c.flap01=std::max(0.,c.flap01-FixedStepClock::tick/10);}
    return c;
  },[&](int tick,const auto& a){if(!lifted && gearLoad(a)<1 && a.instruments().agl>3 && a.instruments().vs>0){lifted=true;time=tick/120.;x=a.state().pos_ned.x;speed=a.instruments().tas;}});
  std::printf("JF-17 takeoff releaseTime=%.4f distance=%.4f TAS=%.4f finalHeight=%.4f finalTAS=%.4f maxPitch=%.4f\n",time-3,x,speed,sim.instruments().alt_msl,sim.instruments().tas,m.max_pitch);
  check(lifted && speed>65 && speed<125 && x>200 && x<2000,"takeoff envelope");
  check(sim.instruments().alt_msl>500 && m.max_pitch<25 && c.gear01==0,"sustained stable climb");
}
void controls() {
  for(int axis=0;axis<3;++axis) {
    auto sim=trimmed();auto c=sim.controls();
    if(axis==0)c.aileron_stick=.2;
    if(axis==1)c.elevator_stick=.15;
    if(axis==2)c.rudder_pedal=.2;
    sim.setControls(c);advance(sim,60);const auto w=sim.state().omega_body;
    check(axis==0?w.x>.02:axis==1?w.y>.01:w.z>.005,"axis response direction");
    std::printf("JF-17 control axis=%d p/q/r=%.6f/%.6f/%.6f\n",axis,w.x,w.y,w.z);
  }
  auto sim=trimmed();auto m=run(sim,2400,[](int,const auto& a){auto c=a.controls();auto n=a.instruments();
    c.aileron_stick=clamp(.8*(30-n.roll_deg)*kDeg2Rad/a.config().max_roll_rate,-.3,.3);
    c.elevator_stick=flightPathInput(a,0,clamp(.1*(1500-n.alt_msl),-5,5));
    c.throttle[0]=c.throttle[1]=clamp(.5+.02*(180-n.tas),0,1);return c;},[](int,const auto&){});
  check(std::abs(sim.instruments().roll_deg-30)<5 && m.min_alt>1300 && sim.instruments().hdg_deg>15,"sustained turn");
  std::printf("JF-17 turn heading=%.5f bank=%.5f load=%.5f height=%.5f TAS=%.5f\n",sim.instruments().hdg_deg,sim.instruments().roll_deg,sim.instruments().g_load,sim.instruments().alt_msl,sim.instruments().tas);
}
void stall() {
  auto sim=trimmed(110,2500);auto s=sim.state();s.att=quatFromEuler(0,32*kDeg2Rad,0);s.vel_ned={90,0,0};sim.setState(s);
  check(sim.instruments().stall_warn,"initial stall warning");
  auto m=run(sim,3000,[](int tick,const auto& a){auto c=a.controls();c.throttle[0]=c.throttle[1]=1;c.elevator_stick=tick<120?-.2:pitchInput(a,2*kDeg2Rad);return c;},[](int,const auto&){});
  check(!sim.instruments().stall_warn && sim.instruments().alpha_deg<10 && m.min_alt>1800,"stall recovery");
  std::printf("JF-17 stall recovery alpha=%.5f TAS=%.5f altitudeLoss=%.5f\n",sim.instruments().alpha_deg,sim.instruments().tas,2500-sim.instruments().alt_msl);
}
void landing() {
  auto sim=trimmed(85,100,1,1,-2.5*kDeg2Rad);auto c=sim.controls();bool touched=false;double time=0,speed=0,sink=0,x=0,prevSpeed=0,prevSink=0;
  auto m=run(sim,14400,[&](int,const auto& a){auto n=a.instruments();prevSpeed=n.tas;prevSink=-n.vs;
    if(n.agl<18 && !touched){c.elevator_stick=flightPathInput(a,1,-.8);c.throttle[0]=c.throttle[1]=clamp(.4+.03*(85-n.tas),0,.7);}
    if(touched){c.elevator_stick=-c.elevator_trim;c.brake01=1;c.spoiler01=1;c.throttle[0]=c.throttle[1]=0;}return c;
  },[&](int tick,const auto& a){if(!touched && gearLoad(a)>1000){touched=true;time=tick/120.;speed=prevSpeed;sink=prevSink;x=a.state().pos_ned.x;}});
  std::printf("JF-17 landing time=%.5f TAS=%.5f sink=%.5f rollout=%.5f peakContact=%.5f finalSpeed=%.9g\n",time,speed,sink,sim.state().pos_ned.x-x,m.peak_contact,sim.instruments().tas);
  check(touched && speed>65 && speed<110 && sink>0 && sink<2,"normal landing touchdown");
  check(sim.instruments().tas<1e-5 && m.min_alt>1.5 && m.peak_contact<3*sim.config().mass*kG0,"stable landing stop");
}
void robustness() {
  for(double v:{0.,.5,20.,90.,250.,600.})for(double angle:{-180.,-90.,-30.,0.,30.,90.,180.}){
    Simulator sim(referenceConfig());State s;s.pos_ned.z=-10000;s.vel_ned={v*std::cos(angle*kDeg2Rad),0,v*std::sin(angle*kDeg2Rad)};sim.setState(s);
    Controls c;c.gear01=0;c.elevator_stick=1;c.aileron_stick=-1;c.rudder_pedal=1;c.flap01=1;c.throttle[0]=c.throttle[1]=1;sim.setControls(c);advance(sim,240);
    check(sim.state().afterburner[0]>=0 && sim.state().afterburner[0]<=1,"bounded engine state");
  }
  std::puts("JF-17 42 extreme finite-state cases PASS");
}
}
int main(int argc,char**argv){try{check(argc==2,"expected scenario");std::string n=argv[1];
  if(n=="engines")engines();else if(n=="cruise")cruise();else if(n=="taxi")taxi();else if(n=="takeoff")takeoff();else if(n=="controls")controls();else if(n=="stall")stall();else if(n=="landing")landing();else if(n=="robustness")robustness();else throw std::invalid_argument("unknown scenario");
  return 0;}catch(const std::exception&e){std::fprintf(stderr,"JF-17 FAIL: %s\n",e.what());return 1;}}
