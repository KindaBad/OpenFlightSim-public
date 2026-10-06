#include "ofs/aircraft_definition.hpp"
#include "ofs/aerodynamics.hpp"
#include "ofs/units.hpp"
#include "scenario.hpp"
#include <cstdio>
#include <string>
using namespace ofs;
using namespace scenario;
namespace {
AircraftConfig referenceConfig() {auto c=su57Config();c.fuel_flow_scale=0;return c;}
Simulator trimmed(double speed=180,double altitude=1500,double flap=0,double gear=0,double gamma=0) {
  auto c=referenceConfig();TrimRequest r;r.tas=speed;r.altitude=altitude;r.flap01=flap;r.gear01=gear;r.gamma=gamma;
  const auto t=solveTrim(c,r);
  std::printf("Su57 trim speed=%.2f height=%.1f converged=%d alpha=%.5f throttle=%.5f residual=%.5f/%.5f\n",speed,altitude,t.converged,2*std::atan2(t.state.att.y,t.state.att.w)*kRad2Deg,t.controls.throttle[0],t.residual_force_world.norm(),t.residual_moment_body.norm());
  check(t.converged,"Su57 trim");Simulator s(c);s.setState(t.state);s.setControls(t.controls);return s;
}
void trim() {
 for(auto pair:{std::pair{130.,1500.},std::pair{180.,1500.},std::pair{250.,5000.},std::pair{1.5*isaAtAltitude(11000).sound,11000.}}) {
  auto s=trimmed(pair.first,pair.second);const auto m=advance(s,7200);
  std::printf("Su57 equilibrium drift altitude=%.9f speed=%.9f rate=%.9f\n",m.max_alt-m.min_alt,m.max_speed-m.min_speed,m.max_rate);
  check(m.max_alt-m.min_alt<1 && m.max_speed-m.min_speed<.2 && m.max_rate<.005,"Su57 stable equilibrium");
 }
}
void vectoring() {
 auto cfg=referenceConfig();cfg.wing_area=0;cfg.control_law=FlightControlLaw::Direct;
 Simulator s(cfg);State st;st.pos_ned.z=-3000;st.n1[0]=st.n1[1]=1;st.afterburner[0]=st.afterburner[1]=1;st.fcs_enabled=false;
 s.setState(st);const auto neutral=s.evalThrust();st.nozzle_angle[0]=st.nozzle_angle[1]=-10*kDeg2Rad;s.setState(st);const auto vectored=s.evalThrust();
 check(vectored.moment_body.y>neutral.moment_body.y+100000,"symmetric physical nose-up vector moment");
 Vec3 forces,moments;const auto mass=s.massProperties();
 for(unsigned e=0;e<2;++e) {forces+=vectored.force[e];moments+=(vectored.position[e]-mass.cg).cross(vectored.force[e]);check(std::abs(vectored.force[e].norm()-vectored.each[e])<1e-7,"vectoring preserves thrust magnitude");}
 check((forces-vectored.force_body).norm()<1e-7 && (moments-vectored.moment_body).norm()<1e-7,"F=Tdirection and M=r cross F");
 check(std::abs(vectored.moment_body.z)<1e-6 && std::abs(vectored.moment_body.x)<1e-6,"symmetric canted nozzle cancellation");
 st.nozzle_angle[1]=10*kDeg2Rad;s.setState(st);const auto differential=s.evalThrust();
 check(std::abs(differential.moment_body.z)>10000 && std::abs(differential.moment_body.x)>1000,"differential canted yaw/roll coupling");
 st.nozzle_angle[0]=st.nozzle_angle[1]=0;Simulator plain(cfg);plain.setState(st);auto c=Controls{};c.gear01=0;c.throttle[0]=c.throttle[1]=1;plain.setControls(c);s.setControls(c);s.step(FixedStepClock::tick);plain.step(FixedStepClock::tick);
 check((s.state().omega_body-plain.state().omega_body).norm()>.001,"physical vector state changes integrated motion");
 std::printf("Su57 TV neutralPitch=%.2f vectoredPitch=%.2f differential=%.2f/%.2f/%.2f Nm\n",neutral.moment_body.y,vectored.moment_body.y,differential.moment_body.x,differential.moment_body.y,differential.moment_body.z);
}
void allocation() {
 for(double speed:{25.,100.,300.}) {
  Simulator s(referenceConfig());State st;st.pos_ned.z=-2000;st.vel_ned.x=speed;st.n1[0]=st.n1[1]=1;st.afterburner[0]=st.afterburner[1]=1;s.setState(st);
  Controls c;c.gear01=0;c.throttle[0]=c.throttle[1]=1;c.elevator_stick=.3;s.setControls(c);
  double peak=0;double surface=0;auto previous=s.state();
  for(unsigned i=0;i<60;++i) {s.step(FixedStepClock::tick);for(unsigned e=0;e<2;++e) {
    check(std::abs(s.state().nozzle_angle[e])<=s.config().engines[e].vector_limit+1e-12,"nozzle bound");
    check(std::abs(s.state().nozzle_angle[e]-previous.nozzle_angle[e])<=s.config().engines[e].vector_rate*FixedStepClock::tick+1e-12,"finite TV rate");
    peak=std::max(peak,std::abs(s.state().nozzle_angle[e]));}
    previous=s.state();surface=std::max(surface,std::abs(s.state().elevator));
  }
  check(finite(s.state()),"allocation finite");
  std::printf("Su57 allocation V=%.1f nozzlePeak=%.5fdeg elevatorPeak=%.5f q=%.5f AoA=%.4f\n",speed,peak*kRad2Deg,surface,s.state().omega_body.y,s.instruments().alpha_deg);
  if(speed==25) check(peak>kDeg2Rad && s.state().omega_body.y>0,"low-speed FCS uses physical TV pitch");
  if(speed==300) check(peak<3*kDeg2Rad,"high-pressure aerodynamic allocation dominates");
 }
}
void maneuver() {
 const auto cfg=referenceConfig();
 auto allocation=[&](bool enabled,double speed,double alpha,double gear) {
  Simulator sim(cfg);State state;state.pos_ned.z=-3000;
  state.vel_ned={speed*std::cos(alpha*kDeg2Rad),0,speed*std::sin(alpha*kDeg2Rad)};
  state.n1[0]=state.n1[1]=1;state.afterburner[0]=state.afterburner[1]=1;
  sim.setState(state);Controls c;c.gear01=gear;c.throttle[0]=c.throttle[1]=1;
  c.elevator_stick=.3;c.maneuver_mode=enabled;sim.setControls(c);sim.primeActuators();
  return sim.vectorFighterAllocation();
 };
 const auto normal=allocation(false,110,55,0),super=allocation(true,110,55,0);
 check(super.surfaces.elevator_stick>normal.surfaces.elevator_stick+.02 ||
       super.nozzle[0]<normal.nozzle[0]-.001,"maneuver raises high-AoA pitch request through actuators");
 for(auto [speed,gear]:{std::pair{300.,0.},std::pair{110.,1.}}) {
  const auto a=allocation(false,speed,45,gear),b=allocation(true,speed,45,gear);
  check(a.surfaces.elevator_stick==b.surfaces.elevator_stick && a.nozzle[0]==b.nozzle[0] && a.nozzle[1]==b.nozzle[1],
        "normal protections at high speed or gear down");
 }
 // Compare actual integrated response from the same trim and pilot input.
 // A mode that only changes a target at deeply stalled AoA still feels like
 // normal flight during ordinary combat-speed stick movements.
 auto response=[&](bool enabled,double speed,bool roll) {
  Simulator s(cfg);const auto trim=solveTrim(cfg,TrimRequest{.altitude=3000,.tas=speed});
  check(trim.converged,"mode comparison trim");s.setState(trim.state);auto controls=trim.controls;
  controls.maneuver_mode=enabled;
  controls.elevator_stick=roll?0:.65;controls.aileron_stick=roll?.3:0;
  controls.throttle[0]=controls.throttle[1]=1;s.setControls(controls);
  double peak=0;
  for(unsigned tick=0;tick<180;++tick) {
   s.step(FixedStepClock::tick);check(finite(s.state()),"mode comparison finite");
   peak=std::max(peak,std::abs(roll?s.state().omega_body.x:s.state().omega_body.y));
  }
  return peak;
 };
 for(double speed:{140.,180.,210.}) {
  const double normalPitch=response(false,speed,false),maneuverPitch=response(true,speed,false);
  const double normalRoll=response(false,speed,true),maneuverRoll=response(true,speed,true);
  std::printf("Su57 mode response V=%.0f pitch=%.3f/%.3f roll=%.3f/%.3f rad/s\n",speed,normalPitch,maneuverPitch,normalRoll,maneuverRoll);
  check(maneuverPitch>normalPitch*1.5,"maneuver mode delivers distinctly faster combat-speed pitch");
  check(maneuverRoll>normalRoll*1.15,"maneuver mode delivers faster bank changes");
 }
 Simulator sim(cfg);auto t=solveTrim(cfg,TrimRequest{.altitude=3000,.tas=140});
 check(t.converged,"maneuver entry trim");sim.setState(t.state);auto c=t.controls;
 c.elevator_stick=1;c.throttle[0]=c.throttle[1]=1;c.maneuver_mode=true;sim.setControls(c);
 auto previous=sim.state();double peak=0;
 for(unsigned tick=0;tick<600;++tick) {
  if(tick==300){c.maneuver_mode=false;c.elevator_stick=-.25;sim.setControls(c);}
  sim.step(FixedStepClock::tick);check(finite(sim.state()),"mode entry/recovery finite");
  peak=std::max(peak,sim.instruments().alpha_deg);
  for(unsigned engine=0;engine<2;++engine) {
   check(std::abs(sim.state().nozzle_angle[engine])<=cfg.engines[engine].vector_limit+1e-10,"maneuver physical nozzle bound");
   check(std::abs(sim.state().nozzle_angle[engine]-previous.nozzle_angle[engine])<=cfg.engines[engine].vector_rate*FixedStepClock::tick+1e-10,"maneuver finite nozzle slew");
  }
  previous=sim.state();
 }
 std::printf("Su57 maneuver peakAoA=%.3f recoveryAoA=%.3f speed=%.3f\n",peak,sim.instruments().alpha_deg,sim.instruments().tas);
 check(peak>cfg.alpha_limit*kRad2Deg && sim.instruments().alpha_deg<peak-10,"high AoA entry and normal-mode recovery");
}
void poles() {
 for(const auto& d:aircraftDefinitions()) {
  Simulator s(d.flight);State st;st.pos_ned.z=-3000;Controls c;c.gear01=0;s.setControls(c);
  double lowControl=0,highControl=0,peakLift=0,highLift=0;
  for(double angle:{5.,d.flight.alpha_crit_clean*kRad2Deg,45.,90.}) {
   st.vel_ned={120*std::cos(angle*kDeg2Rad),0,120*std::sin(angle*kDeg2Rad)};s.setState(st);c.elevator_stick=0;s.setControls(c);const auto a=s.evalAero();c.elevator_stick=1;s.setControls(c);const auto b=s.evalAero();
   if(angle==5) lowControl=std::abs(b.moment_body.y-a.moment_body.y);
   if(angle==45) {highControl=std::abs(b.moment_body.y-a.moment_body.y);highLift=a.cl;}
   if(angle==d.flight.alpha_crit_clean*kRad2Deg) peakLift=a.cl;
   check(a.drag_body.dot(st.vel_ned)<=1e-6,"aerodynamic drag removes energy");
   std::printf("polar %s alpha=%.1f CL=%.6f CD=%.6f control=%.2fNm\n",d.key.data(),angle,a.cl,a.cd,std::abs(b.moment_body.y-a.moment_body.y));
  }
  check(highControl<.3*lowControl && highLift<peakLift,"flow blanking and post-stall lift reduction");
 }
}
void energy() {
 auto cfg=referenceConfig();cfg.thrust_sl_static_each=cfg.afterburner_thrust_each=0;cfg.engines_configured=false;cfg.control_law=FlightControlLaw::Direct;
 Simulator s(cfg);State st;st.pos_ned.z=-6000;st.vel_ned={140,0,0};st.att=quatFromEuler(0,55*kDeg2Rad,0);st.fcs_enabled=false;s.setState(st);Controls c;c.gear01=0;s.setControls(c);s.primeActuators();
 auto energy=[&]{const auto m=s.massProperties();const auto w=s.state().omega_body;return .5*m.mass*s.state().vel_ned.norm2()-m.mass*kG0*s.state().pos_ned.z+.5*(m.inertia.x*w.x*w.x+m.inertia.y*w.y*w.y+m.inertia.z*w.z*w.z+2*m.ixz*w.x*w.z);};
 const auto initial=energy();const auto metrics=advance(s,240);
 std::printf("Su57 post-stall energy entry=140 minSpeed=%.4f heightChange=%.4f loss=%.4fMJ\n",metrics.min_speed,s.instruments().alt_msl-6000,(initial-energy())/1e6);
 check(energy()<initial && metrics.min_speed<130,"post-stall loses mechanical energy and speed");
}
void taxi() {
  Simulator sim(referenceConfig());State s;s.pos_ned.z=-2.45;sim.setState(s);Controls c;c.brake01=1;sim.setControls(c);advance(sim,3600);
  check(sim.instruments().tas<1e-5,"parked stable");
  c.brake01=0;c.throttle[0]=c.throttle[1]=.3;sim.setControls(c);advance(sim,1200);
  const auto speed=sim.instruments().tas;check(speed>1 && speed<20,"controlled taxi speed");
  c.steering=.2;sim.setControls(c);advance(sim,240);const auto heading=std::remainder(sim.instruments().hdg_deg,360.);
  check(heading>1,"right steering");c.steering=0;c.throttle[0]=c.throttle[1]=0;c.brake01=1;sim.setControls(c);advance(sim,2400);
  std::printf("Su57 taxi TAS=%.4f rightHeading=%.4f finalSpeed=%.9g height=%.4f\n",speed,heading,sim.instruments().tas,sim.instruments().alt_msl);
  check(sim.instruments().tas<1e-5 && sim.instruments().alt_msl>1.5,"stable braking stop");
}
void takeoff() {
  Simulator sim(referenceConfig());State s;s.pos_ned.z=-2.45;sim.setState(s);Controls c;c.brake01=1;c.flap01=.35;
  bool lifted=false;double time=0,x=0,speed=0;
  auto m=run(sim,9600,[&](int tick,const auto& a){
    if(tick>360){c.brake01=0;c.throttle[0]=c.throttle[1]=1;}
    if(a.instruments().tas>75)c.elevator_stick=pitchInput(a,12*kDeg2Rad);
    if(lifted && a.instruments().agl>8){c.gear01=0;c.flap01=std::max(0.,c.flap01-FixedStepClock::tick/10);}
    return c;
  },[&](int tick,const auto& a){if(!lifted && gearLoad(a)<1 && a.instruments().agl>3 && a.instruments().vs>0){lifted=true;time=tick/120.;x=a.state().pos_ned.x;speed=a.instruments().tas;}});
  std::printf("Su57 takeoff releaseTime=%.4f distance=%.4f TAS=%.4f finalHeight=%.4f finalTAS=%.4f maxPitch=%.4f\n",time-3,x,speed,sim.instruments().alt_msl,sim.instruments().tas,m.max_pitch);
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
    std::printf("Su57 control axis=%d p/q/r=%.6f/%.6f/%.6f\n",axis,w.x,w.y,w.z);
  }
  auto sim=trimmed();auto m=run(sim,2400,[](int,const auto& a){auto c=a.controls();auto n=a.instruments();
    c.aileron_stick=clamp(.8*(30-n.roll_deg)*kDeg2Rad/a.config().max_roll_rate,-.3,.3);
    c.elevator_stick=flightPathInput(a,0,clamp(.1*(1500-n.alt_msl),-5,5));
    c.throttle[0]=c.throttle[1]=clamp(.5+.02*(180-n.tas),0,1);return c;},[](int,const auto&){});
  check(std::abs(sim.instruments().roll_deg-30)<5 && m.min_alt>1300 && sim.instruments().hdg_deg>15,"sustained turn");
  std::printf("Su57 turn heading=%.5f bank=%.5f load=%.5f height=%.5f TAS=%.5f\n",sim.instruments().hdg_deg,sim.instruments().roll_deg,sim.instruments().g_load,sim.instruments().alt_msl,sim.instruments().tas);
}
void stall() {
  auto sim=trimmed(110,2500);auto s=sim.state();s.att=quatFromEuler(0,32*kDeg2Rad,0);s.vel_ned={90,0,0};sim.setState(s);
  check(sim.instruments().stall_warn,"initial stall warning");
  auto m=run(sim,3000,[](int tick,const auto& a){auto c=a.controls();c.throttle[0]=c.throttle[1]=1;c.elevator_stick=tick<120?-.2:pitchInput(a,2*kDeg2Rad);return c;},[](int,const auto&){});
  check(!sim.instruments().stall_warn && sim.instruments().alpha_deg<10 && m.min_alt>1800,"stall recovery");
  std::printf("Su57 stall recovery alpha=%.5f TAS=%.5f altitudeLoss=%.5f\n",sim.instruments().alpha_deg,sim.instruments().tas,2500-sim.instruments().alt_msl);
}
void landing() {
  auto sim=trimmed(85,100,1,1,-2.5*kDeg2Rad);auto c=sim.controls();bool touched=false;double time=0,speed=0,sink=0,x=0,prevSpeed=0,prevSink=0;
  auto m=run(sim,14400,[&](int,const auto& a){auto n=a.instruments();prevSpeed=n.tas;prevSink=-n.vs;
    if(n.agl<12 && !touched){c.elevator_stick=flightPathInput(a,1,-.8);c.throttle[0]=c.throttle[1]=.2;}
    if(touched){c.elevator_stick=-c.elevator_trim;c.brake01=1;c.spoiler01=1;c.throttle[0]=c.throttle[1]=0;}return c;
  },[&](int tick,const auto& a){if(!touched && gearLoad(a)>1000){touched=true;time=tick/120.;speed=prevSpeed;sink=prevSink;x=a.state().pos_ned.x;}});
  std::printf("Su57 landing time=%.5f TAS=%.5f sink=%.5f rollout=%.5f peakContact=%.5f finalSpeed=%.9g\n",time,speed,sink,sim.state().pos_ned.x-x,m.peak_contact,sim.instruments().tas);
  check(touched && speed>65 && speed<110 && sink>0 && sink<2,"normal landing touchdown");
  check(sim.instruments().tas<1e-5 && m.min_alt>1.5 && m.peak_contact<3*sim.config().mass*kG0,"stable landing stop");
}
void robustness() {
  for(double v:{0.,.5,20.,90.,250.,600.})for(double angle:{-180.,-90.,-30.,0.,30.,90.,180.}){
    Simulator sim(referenceConfig());State s;s.pos_ned.z=-10000;s.vel_ned={v*std::cos(angle*kDeg2Rad),0,v*std::sin(angle*kDeg2Rad)};sim.setState(s);
    Controls c;c.gear01=0;c.elevator_stick=1;c.aileron_stick=-1;c.rudder_pedal=1;c.flap01=1;c.throttle[0]=c.throttle[1]=1;sim.setControls(c);advance(sim,240);
    check(sim.state().afterburner[0]>=0 && sim.state().afterburner[0]<=1,"bounded engine state");
  }
  std::puts("Su57 42 extreme finite-state cases PASS");
}
}
int main(int argc,char** argv) {
 try {check(argc==2,"scenario name");const std::string name=argv[1];
 if(name=="maneuver") maneuver();else if(name=="trim") trim();else if(name=="vectoring") vectoring();else if(name=="allocation") allocation();else if(name=="polars") poles();else if(name=="energy") energy();else if(name=="taxi") taxi();else if(name=="takeoff") takeoff();else if(name=="controls") controls();else if(name=="stall") stall();else if(name=="landing") landing();else if(name=="robustness") robustness();else throw std::invalid_argument("unknown scenario");
 std::printf("PASS su57.%s\n",name.c_str());return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"SU57 FAIL: %s\n",e.what());return 1;}
}
