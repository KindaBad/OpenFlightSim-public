#include "scenario.hpp"
#include "ofs/c_api.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
using namespace ofs;
using namespace scenario;
namespace {
AircraftConfig referenceConfig() { auto c=a320Config();c.fuel_flow_scale=0;return c; }
void near(double actual,double expected,double tolerance,const char* message) {
  check(std::isfinite(actual) && std::abs(actual-expected)<=tolerance,message);
}
Simulator trimmed(TrimRequest request={}) {
  const auto trim=solveTrim(referenceConfig(),request);
  check(trim.converged,"trim solver did not converge");
  Simulator s(referenceConfig()); s.setState(trim.state); s.setControls(trim.controls); return s;
}
void trimTest() {
  auto sim=trimmed(); const auto initial=sim.instruments(); const auto a=sim.evalAero(); const auto t=sim.evalThrust();
  const auto trim=solveTrim();
  check(trim.residual_force_world.norm()<.01 && trim.residual_moment_body.norm()<.01,"trim force/moment residual");
  check(initial.alpha_deg>4 && initial.alpha_deg<10,"reasonable standard trim AoA");
  check(sim.controls().throttle[0]>.2 && sim.controls().throttle[0]<.7,"reasonable trim power");
  double altitude=0,speed=0,pitch=0,roll=0,heading=0,rate=0;
  run(sim,14400,[](int,const auto& s){return s.controls();},[&](int,const auto& s){
    const auto n=s.instruments();
    altitude=std::max(altitude,std::abs(n.alt_msl-initial.alt_msl));
    speed=std::max(speed,std::abs(n.tas-initial.tas));
    pitch=std::max(pitch,std::abs(n.pitch_deg-initial.pitch_deg));
    roll=std::max(roll,std::abs(n.roll_deg)); heading=std::max(heading,std::abs(std::remainder(n.hdg_deg,360.)));
    rate=std::max(rate,s.state().omega_body.norm());
    check(!n.stall_warn,"trim stall warning");
  });
  check(altitude<1 && speed<.1 && pitch<.1 && roll<.01 && heading<.01 && rate<1e-4,"120s straight level drift");
  std::printf("trim: pitch/AoA=%.9f deg elevator_trim=%.9f stick=0 throttle=%.9f elevator_surface=%.9f deg\n",initial.pitch_deg,trim.controls.elevator_trim,trim.controls.throttle[0],trim.controls.elevator_trim*sim.config().elev_min*kRad2Deg);
  std::printf("trim forces: lift=%.6f drag=%.6f thrust=%.6f weight=%.6f N residual_force=%.9g N moment=%.9g Nm\n",a.lift_body.norm(),a.drag_body.norm(),t.force_body.x,sim.config().mass*kG0,trim.residual_force_world.norm(),trim.residual_moment_body.norm());
  std::printf("level 120s max drift: altitude=%.9g m TAS=%.9g m/s pitch=%.9g roll=%.9g heading=%.9g deg rate=%.9g rad/s VS=%.9g ax=%.9g\n",altitude,speed,pitch,roll,heading,rate,sim.instruments().vs,sim.state().att.rotate(sim.debugFrame().total_force_body).x/sim.config().mass);
  // Open-loop airframe perturbation regression: disable FBW feedback explicitly.
  auto disturbed=trimmed(); auto st=disturbed.state(); st.att=quatFromEuler(0,(initial.pitch_deg+1)*kDeg2Rad,0); st.fcs_enabled=false; disturbed.setState(st);
  const auto m=advance(disturbed,14400);
  std::printf("perturbation alt=%f..%f tas=%f..%f rate=%f\n",m.min_alt,m.max_alt,m.min_speed,m.max_speed,m.max_rate);
  check(m.min_alt>980 && m.max_alt<1020 && m.min_speed>108 && m.max_speed<112 && m.max_rate<.05,"trim perturbation bounded");
  std::printf("pitch perturbation +1deg 120s: altitude_range=%.6f..%.6f TAS_range=%.6f..%.6f peak_rate=%.6f rad/s\n",m.min_alt,m.max_alt,m.min_speed,m.max_speed,m.max_rate);
  TrimRequest impossible; impossible.tas=20; check(!solveTrim(referenceConfig(),impossible).converged,"unflyable trim must fail");
  impossible.tas=std::nan(""); check(!solveTrim(referenceConfig(),impossible).converged,"invalid trim must fail");
}
void controlTest() {
  for(int axis=0;axis<3;++axis) for(double sign:{-1.,1.}) {
    auto s=trimmed(); auto c=s.controls();
    if(axis==0)c.aileron_stick=sign*.2;
    if(axis==1)c.elevator_stick=sign*.2;
    if(axis==2)c.rudder_pedal=sign*.2;
    s.setControls(c); advance(s,30);
    const auto w=s.state().omega_body; const double rate=axis==0?w.x:axis==1?w.y:w.z;
    check(sign*rate>.001 && std::abs(rate)<.2,"control sign and finite inertia response");
    std::printf("control axis=%d input=%+.1f rate_at_0.25s=%+.6f rad/s\n",axis,sign*.2,rate);
  }
  // Evaluate increments at identical AoA, beta, rates, altitude: pressure scales V^2.
  for(int axis=0;axis<3;++axis) {
    double moments[2]{};
    for(int j=0;j<2;++j) {
      Simulator s(referenceConfig()); State st; st.pos_ned.z=-1000; st.vel_ned.x=j==0?60:120; s.setState(st);
      const auto neutral=s.evalAero().moment_body; Controls c;
      if(axis==0)c.aileron_stick=.2;
      if(axis==1)c.elevator_stick=.2;
      if(axis==2)c.rudder_pedal=.2;
      s.setControls(c); const auto delta=s.evalAero().moment_body-neutral;
      moments[j]=axis==0?delta.x:axis==1?delta.y:delta.z;
    }
    // Raw airframe response follows dynamic pressure at fixed actual deflection.
    // The former expectation incorrectly included generic FCS load softening.
    const double loadRatio=4;
    check(std::abs(moments[1]/moments[0]-loadRatio)<.025,"pressure/load scaling including control lift-drag moment coupling");
  }
  auto s=trimmed(); const auto initial=s.state(); auto c=s.controls(); c.elevator_stick=1; s.setControls(c);
  near(s.state().omega_body.norm(),initial.omega_body.norm(),0,"setting control must not change rates");
  s.step(FixedStepClock::tick); check(s.state().omega_body.norm()<.02,"no instantaneous pitch rate jump");
  // C API extension retains trim across legacy control calls.
  auto* api=ofs_create(); check(api!=nullptr,"C API create");
  ofs_set_elevator_trim(api,.15); ofs_set_controls(api,0,0,0,0,0,0,.4,.4,0,0);
  auto state=OfsState{}; state.pos_d=-1000; state.vel_n=110; ofs_set_state(api,&state); ofs_step(api,FixedStepClock::tick);
  Simulator cpp; State st; st.pos_ned.z=-1000;st.vel_ned.x=110;cpp.setState(st);Controls cc;cc.elevator_trim=.15;cc.gear01=0;cc.throttle[0]=cc.throttle[1]=.4;cpp.setControls(cc);cpp.step(FixedStepClock::tick);
  near(ofs_get_state(api).q,cpp.state().omega_body.y,0,"C trim parity");ofs_destroy(api);
}
Simulator::AeroResult atAlpha(double alpha,double flap=0) {
  Simulator s(referenceConfig()); State st; st.pos_ned.z=-1000;st.vel_ned={110*std::cos(alpha*kDeg2Rad),0,110*std::sin(alpha*kDeg2Rad)};
  s.setState(st);Controls c;c.gear01=0;c.flap01=flap;s.setControls(c);return s.evalAero();
}
void stallTest() {
  check(atAlpha(10).cl>atAlpha(5).cl && atAlpha(14).cl>atAlpha(10).cl,"prestall lift slope");
  check(atAlpha(25).cl<atAlpha(14).cl && atAlpha(25).cd>atAlpha(14).cd*2,"stall lift loss and drag rise");
  check(atAlpha(90).cd>1.5 && std::abs(atAlpha(90).cl)<.01,"broadside bounded plate behavior");
  for(double flap:{0.,1.}) for(int deg=-180;deg<=180;++deg) {
    const auto a=atAlpha(deg,flap);
    check(std::isfinite(a.force_body.norm2()) && std::isfinite(a.moment_body.norm2()),"extreme alpha finite");
    check(std::abs(a.cl)<referenceConfig().cl_max_full_flap+0.2 && a.cd>0 && a.cd<2.5,"extreme alpha bounded coefficients");
    const auto next=atAlpha(deg+.001,flap);
    check(std::abs(next.cl-a.cl)<.002 && std::abs(next.cd-a.cd)<.002,"stall coefficient continuity");
    const Vec3 velocity{110*std::cos(deg*kDeg2Rad),0,110*std::sin(deg*kDeg2Rad)};
    check(a.force_body.dot(velocity)<=0,"aero dissipates energy at extreme alpha");
  }
  for(double deg:{10.,14.,20.,25.,45.,90.}) {const auto a=atAlpha(deg);std::printf("stall curve alpha=%.0f CL=%.6f CD=%.6f\n",deg,a.cl,a.cd);}
  Simulator s(referenceConfig()); State st; st.pos_ned.z=-2000;st.vel_ned.x=80;st.att=quatFromEuler(0,24*kDeg2Rad,0);st.n1[0]=st.n1[1]=.6;s.setState(st);
  Controls c;c.gear01=0;c.throttle[0]=c.throttle[1]=.6;s.setControls(c);check(s.instruments().stall_warn,"stall initially warns");
  double alpha_after_push=0;
  const auto m=run(s,3000,[&](int tick,const auto& sim){c.elevator_stick=tick<240?-1:pitchInput(sim,2*kDeg2Rad);return c;},[&](int tick,const auto& sim){if(tick==239)alpha_after_push=sim.instruments().alpha_deg;});
  std::printf("recovery check initial push alpha=%f final=%f speed=%f minAlt=%f\n",alpha_after_push,s.instruments().alpha_deg,s.instruments().tas,m.min_alt);
  check(alpha_after_push<12 && s.instruments().alpha_deg<8 && !s.instruments().stall_warn && m.min_alt>1500,"stall recover by reducing AoA");
  std::printf("stall recovery 25s: AoA_after_2s=%.6f final_AoA=%.6f TAS=%.6f altitude_loss=%.6f m peak_rate=%.6f\n",alpha_after_push,s.instruments().alpha_deg,s.instruments().tas,2000-s.instruments().alt_msl,m.max_rate);
}
void taxiTest() {
  Simulator s(referenceConfig());State st;st.pos_ned.z=-3.4;s.setState(st);Controls c;c.brake01=1;s.setControls(c);
  advance(s,7200); const auto parked=s.state();check(parked.vel_ned.norm()<1e-5,"parking brakes stable stop");
  const auto quiet=advance(s,1200);check(quiet.max_alt-quiet.min_alt<1e-6 && s.state().omega_body.norm()<1e-6,"no parked jitter");
  std::printf("park: altitude=%.6f pitch=%.6f speed=%.9g altitude_range_last_10s=%.9g\n",s.instruments().alt_msl,s.instruments().pitch_deg,s.state().vel_ned.norm(),quiet.max_alt-quiet.min_alt);
  c.brake01=0;s.setControls(c);advance(s,7200);const double idle=s.instruments().tas;check(idle<3,"idle taxi speed reasonable");
  c.throttle[0]=c.throttle[1]=.5;s.setControls(c);advance(s,1800);const double taxi=s.instruments().tas;check(taxi>idle+1 && taxi<15,"taxi accelerates with power");
  double heading[2]{};
  for(int j=0;j<2;++j) {Simulator turning(referenceConfig());turning.setState(s.state());auto turn=c;turn.steering=j==0?-.2:.2;turning.setControls(turn);advance(turning,360);heading[j]=std::remainder(turning.instruments().hdg_deg,360.);check((j==0?-1:1)*heading[j]>1,"nosewheel steering direction");check(turning.instruments().alt_msl>2.8,"turn stays on gear");}
  Simulator unbraked(referenceConfig()),slightly_braked(referenceConfig());unbraked.setState(s.state());slightly_braked.setState(s.state());
  unbraked.setControls(c);auto slight=c;slight.brake01=.001;slightly_braked.setControls(slight);
  unbraked.step(FixedStepClock::tick);slightly_braked.step(FixedStepClock::tick);
  check(slightly_braked.state().vel_ned.x<=unbraked.state().vel_ned.x,"small brake input must not remove rolling resistance");
  const double start=s.state().pos_ned.x;c.throttle[0]=c.throttle[1]=0;c.brake01=1;s.setControls(c);
  advance(s,2400);const double stop=s.state().pos_ned.x;const auto held=advance(s,1200);
  check(s.state().vel_ned.norm()<1e-5 && std::abs(s.state().pos_ned.x-stop)<1e-4 && held.max_alt-held.min_alt<1e-5,"taxi braking stable stop");
  check(stop-start>0 && stop-start<100,"taxi brake distance");
  std::printf("taxi: idle_speed=%.6f powered_speed=%.6f steering_heading_left=%.6f right=%.6f brake_distance=%.6f final_speed=%.9g\n",idle,taxi,heading[0],heading[1],stop-start,s.instruments().tas);
}
struct TakeoffResult {std::uint64_t digest{};State final;double rotation{},liftoff{},distance{},speed{};};
TakeoffResult takeoffRun(bool print) {
  Simulator s(referenceConfig()); State st;st.pos_ned.z=-3.4;s.setState(st);Controls c;c.brake01=1;c.flap01=.3;s.setControls(c);
  bool rotated=false,lifted=false,retracted=false;int airborne_ticks=0;TakeoffResult result;double lift_pitch=0,lift_alpha=0,lift_vs=0;
  const auto m=run(s,14400,[&](int tick,const auto& sim){
    const auto n=sim.instruments();
    if(tick>=600){c.brake01=0;c.throttle[0]=c.throttle[1]=1;
      // Small deterministic directional disturbance during the early roll.
      c.steering=tick>=1200 && tick<1260?.02:0;
      c.rudder_pedal=clamp(-.6*std::remainder(n.hdg_deg,360.)*kDeg2Rad-1.2*sim.state().omega_body.z,-.2,.2);
      c.aileron_stick=clamp(-n.roll_deg*kDeg2Rad-2*sim.state().omega_body.x,-.2,.2);
      if(n.tas>78){if(!rotated){rotated=true;result.rotation=n.tas;}c.elevator_stick=n.pitch_deg<3 && !lifted?.9:pitchInput(sim,11*kDeg2Rad);}
    }
    if(lifted && n.agl>10)retracted=true;
    if(retracted){c.gear01=0;c.elevator_stick=pitchInput(sim,10*kDeg2Rad);c.flap01=std::max(0.,c.flap01-FixedStepClock::tick/20);}
    return c;
  },[&](int tick,const auto& sim){
    const auto n=sim.instruments();if(gearLoad(sim)<1 && n.agl>4 && n.vs>0)++airborne_ticks;else airborne_ticks=0;
    if(airborne_ticks==24 && !lifted){lifted=true;result.liftoff=(tick+1)*FixedStepClock::tick;result.distance=sim.state().pos_ned.x;result.speed=n.tas;lift_pitch=n.pitch_deg;lift_alpha=n.alpha_deg;lift_vs=n.vs;}
  });
  check(rotated && lifted && retracted && c.gear01==0,"takeoff rotation liftoff gear retraction");
  check(result.speed>75 && result.speed<105 && result.distance>500 && result.distance<2200 && result.liftoff<50,"plausible takeoff envelope");
  check(s.instruments().alt_msl>1000 && s.instruments().vs>10 && m.max_pitch<17 && m.max_speed<220,"sustained controllable climb");
  check(std::abs(std::remainder(s.instruments().hdg_deg,360.))<5 && std::abs(s.instruments().roll_deg)<1,"takeoff directional control");
  result.digest=m.digest; result.final=s.state();if(print)std::printf("takeoff: rotation=%.6f liftoff_speed=%.6f m/s time_from_release=%.6f distance=%.6f pitch=%.6f AoA=%.6f VS=%.6f gear_up=1\nclimb at 120s: altitude=%.6f TAS=%.6f VS=%.6f pitch=%.6f AoA=%.6f max_speed=%.6f\n",result.rotation,result.speed,result.liftoff-5,result.distance,lift_pitch,lift_alpha,lift_vs,s.instruments().alt_msl,s.instruments().tas,s.instruments().vs,s.instruments().pitch_deg,s.instruments().alpha_deg,m.max_speed);
  return result;
}
void takeoffTest() {
  const auto a=takeoffRun(true),b=takeoffRun(false);
  check(a.digest==b.digest,"takeoff whole trajectory deterministic");
  near((a.final.pos_ned-b.final.pos_ned).norm(),0,0,"takeoff deterministic position");near((a.final.vel_ned-b.final.vel_ned).norm(),0,0,"takeoff deterministic velocity");near((a.final.omega_body-b.final.omega_body).norm(),0,0,"takeoff deterministic rates");near(a.final.att.w,b.final.att.w,0,"takeoff deterministic attitude");near(a.liftoff,b.liftoff,0,"takeoff deterministic event");
}
void cruiseTest() {
  for(auto condition:std::array<std::array<double,2>,3>{{{1000,110},{5000,160},{7000,200}}}) {
    TrimRequest r;r.altitude=condition[0];r.tas=condition[1];auto s=trimmed(r);const auto m=advance(s,14400);
    check(m.max_alt-m.min_alt<1 && m.max_speed-m.min_speed<.1 && m.max_rate<1e-4,"cruise equilibrium");
    std::printf("cruise 120s altitude=%.0f TAS=%.0f: throttle=%.9f pitch=%.6f alt_range=%.9g speed_range=%.9g\n",r.altitude,r.tas,s.controls().throttle[0],s.instruments().pitch_deg,m.max_alt-m.min_alt,m.max_speed-m.min_speed);
  }
  TrimRequest warning_condition; warning_condition.altitude=7000;warning_condition.tas=160;
  const auto warning=trimmed(warning_condition).instruments();
  check(warning.tas>1.13*warning.vstall && warning.ias<1.13*warning.vstall && !warning.stall_warn,"stall warning compares TAS with density-scaled stall TAS");
  std::printf("stall warning at 7000m: TAS=%.6f IAS=%.6f stall_TAS=%.6f warning=0\n",warning.tas,warning.ias,warning.vstall);
  double thrust[3]{};for(int j=0;j<3;++j){Simulator s(referenceConfig());State st;st.pos_ned.z=j==1?-7000:-1000;st.vel_ned.x=j==2?220:110;st.n1[0]=st.n1[1]=1;s.setState(st);thrust[j]=s.evalThrust().force_body.x;}
  check(thrust[1]<thrust[0] && thrust[2]<thrust[0] && thrust[1]>50000,"thrust altitude speed lapse");
  std::printf("full thrust: at_1000m_110=%.6f at_7000m_110=%.6f at_1000m_220=%.6f N\n",thrust[0],thrust[1],thrust[2]);
  auto level=trimmed(),pull=trimmed();auto c=pull.controls();c.elevator_stick=.1;pull.setControls(c);advance(level,600);advance(pull,600);
  check(pull.instruments().alt_msl>level.instruments().alt_msl+5 && pull.instruments().tas<level.instruments().tas-.2,"pitch trades speed for altitude");
  std::printf("energy trade 5s pull +.1 same power: height_gain=%.6f speed_change=%.6f VS=%.6f\n",pull.instruments().alt_msl-level.instruments().alt_msl,pull.instruments().tas-level.instruments().tas,pull.instruments().vs);
  // Full power eventually balances drag rather than accelerating without bound.
  auto fast=trimmed();auto raw=fast.state();raw.fcs_enabled=false;fast.setState(raw); double tail_min=1e30,tail_max=0;
  const auto maximum=run(fast,43200,[](int,const auto& sim) {
    auto controls=sim.controls();controls.throttle[0]=controls.throttle[1]=1;
    controls.elevator_stick=flightPathInput(sim,0,clamp(.05*(1000-sim.instruments().alt_msl),-3,3));
    return controls;
  },[&](int tick,const auto& sim){if(tick>=39600){tail_min=std::min(tail_min,sim.instruments().tas);tail_max=std::max(tail_max,sim.instruments().tas);}});
  std::printf("terminal check speed=%f range=%f max=%f altitude=%f\n",fast.instruments().tas,tail_max-tail_min,maximum.max_speed,fast.instruments().alt_msl);
  check(maximum.max_speed<300 && tail_max-tail_min<.1,"full power reaches drag-limited speed");
  std::printf("full power level test pilot 360s: terminal_TAS=%.6f last_30s_speed_range=%.9g max_speed=%.6f altitude=%.6f\n",fast.instruments().tas,tail_max-tail_min,maximum.max_speed,fast.instruments().alt_msl);

}
struct LandingResult {std::uint64_t digest{};State final;double time{},speed{},sink{},rollout{},stop_time{},peak{};};
LandingResult landingRun(bool print) {
  TrimRequest request;request.altitude=120;request.tas=78;request.flap01=1;request.gear01=1;request.gamma=-2.5*kDeg2Rad;
  auto s=trimmed(request);auto c=s.controls();const auto initial=s.instruments();
  bool touched=false,stopped=false;LandingResult result;double touch_x=0,touch_pitch=0,touch_alpha=0,max_bounce=0,compression=0;
  double previous_vs=initial.vs,previous_speed=initial.tas;
  const auto m=run(s,12000,[&](int,const auto& sim){
    const auto n=sim.instruments();previous_vs=n.vs;previous_speed=std::hypot(sim.state().vel_ned.x,sim.state().vel_ned.y);
    if(n.agl<15 && !touched){
      // Desired 1g alpha at current pressure, then a shallow flare flight path.
      c.elevator_stick=flightPathInput(sim,c.flap01,-.9);c.throttle[0]=c.throttle[1]=.25;
    }
    if(touched){c.elevator_stick=-c.elevator_trim;c.brake01=1;c.spoiler01=1;c.throttle[0]=c.throttle[1]=0;}
    return c;
  },[&](int tick,const auto& sim){
    const auto n=sim.instruments();const double load=gearLoad(sim);
    if(load>1000 && !touched){touched=true;result.time=(tick+1)*FixedStepClock::tick;result.speed=previous_speed;result.sink=-previous_vs;touch_x=sim.state().pos_ned.x;touch_pitch=n.pitch_deg;touch_alpha=n.alpha_deg;}
    if(touched){
      max_bounce=std::max(max_bounce,n.agl);
      for(const auto& p:{sim.config().gear_nose,sim.config().gear_main_l,sim.config().gear_main_r})compression=std::max(compression,(sim.state().pos_ned+sim.state().att.rotate(p)).z);
      if(n.tas<.05 && !stopped){stopped=true;result.stop_time=(tick+1)*FixedStepClock::tick-result.time;result.rollout=sim.state().pos_ned.x-touch_x;}
    }
  });
  result.peak=m.peak_contact;result.final=s.state();result.digest=m.digest;
  check(touched && stopped && result.sink>0 && result.sink<2 && result.speed>65 && result.speed<85,"controlled normal touchdown");
  check(result.rollout>300 && result.rollout<1300 && result.stop_time<40,"landing rollout and stop");
  check(max_bounce<4.5 && compression<s.config().oleo_stroke && m.min_alt>3 && result.peak<2*s.config().mass*kG0,"no catastrophic bounce or bottomed gear");
  check(s.state().vel_ned.norm()<1e-5 && s.state().omega_body.norm()<1e-5,"landing stable stop");
  if(print)std::printf("approach: initial_TAS=%.6f pitch=%.6f AoA=%.6f VS=%.6f throttle=%.6f full_flap=1 gear_down=1\nlanding: touchdown_time=%.6f forward_speed=%.6f sink=%.6f pitch=%.6f AoA=%.6f peak_contact=%.6f N max_compression=%.6f max_CG_alt_after_contact=%.6f rollout=%.6f stop_time=%.6f final_speed=%.9g\n",initial.tas,initial.pitch_deg,initial.alpha_deg,initial.vs,solveTrim(referenceConfig(),request).controls.throttle[0],result.time,result.speed,result.sink,touch_pitch,touch_alpha,result.peak,compression,max_bounce,result.rollout,result.stop_time,s.instruments().tas);
  return result;
}
void landingTest() {const auto a=landingRun(true),b=landingRun(false);check(a.digest==b.digest,"landing whole trajectory deterministic");near((a.final.pos_ned-b.final.pos_ned).norm(),0,0,"landing deterministic position");near(a.time,b.time,0,"landing deterministic touchdown");near(a.peak,b.peak,0,"landing deterministic contact");}
void fullCycleTest() {
  // The original aircraft acceptance cycle assumes an infinite flat runway.
  // Real hills and crashes are covered independently by environment tests.
  Simulator s(referenceConfig(),Simulator::GroundModel::FlatRunway); State st; st.pos_ned.z=-3.4; s.setState(st);
  Controls c; c.brake01=1; c.flap01=.3; s.setControls(c);
  enum class Phase { takeoff, climb, cruise, descent, flare, rollout, stopped };
  Phase phase=Phase::takeoff;
  int cruise_tick=0, touch_tick=0, stop_tick=0;
  double touchdown_speed=0,touchdown_sink=0,touch_x=0,rollout=0;
  double cruise_alt_error=0,cruise_speed_error=0;
  const auto m=run(s,50400,[&](int tick,const auto& sim) {
    const auto n=sim.instruments();
    if(tick>=600 && phase==Phase::takeoff) {
      c.brake01=0; c.throttle[0]=c.throttle[1]=1;
      c.elevator_stick=n.tas>78 ? (n.pitch_deg<3?.9:pitchInput(sim,11*kDeg2Rad)) : 0;
      if(n.agl>15) phase=Phase::climb;
    }
    if(phase==Phase::climb || phase==Phase::cruise || phase==Phase::descent) {
      if(phase==Phase::climb && n.agl>380) {phase=Phase::cruise; cruise_tick=tick;}
      if(phase==Phase::cruise && tick-cruise_tick>=7200) phase=Phase::descent;
      const bool descending=phase==Phase::descent;
      const double flap_target=descending?1:0;
      c.flap01+=clamp(flap_target-c.flap01,-FixedStepClock::tick/20,FixedStepClock::tick/20);
      c.gear01=descending?1:0;
      const double target_speed=descending?lerp(110.,78.,c.flap01):110;
      const double target_vs=descending?-3:clamp(.08*(400-n.agl),-3,8);
      TrimRequest r; r.altitude=n.alt_msl; r.tas=target_speed; r.flap01=c.flap01; r.gear01=c.gear01;
      r.gamma=std::asin(target_vs/target_speed);
      const auto trim=solveTrim(sim.config(),r);
      check(trim.converged,"cycle test pilot trim feedforward failed");
      c.throttle[0]=c.throttle[1]=clamp(trim.controls.throttle[0]+.03*(target_speed-n.tas),0,1);
      c.elevator_stick=flightPathInput(sim,c.flap01,target_vs);
      if(descending && n.agl<15) phase=Phase::flare;
    }
    if(phase==Phase::flare) {
      c.elevator_stick=flightPathInput(sim,c.flap01,-.9); c.throttle[0]=c.throttle[1]=.25;
      if(gearLoad(sim)>1000) {
        phase=Phase::rollout;touch_tick=tick;touch_x=sim.state().pos_ned.x;
        touchdown_speed=n.tas;touchdown_sink=-n.vs;
      }
    }
    if(phase==Phase::rollout) {
      c.elevator_stick=0;c.brake01=1;c.spoiler01=1;c.throttle[0]=c.throttle[1]=0;
      if(n.tas<.05) {phase=Phase::stopped;stop_tick=tick;rollout=sim.state().pos_ned.x-touch_x;}
    }
    return c;
  },[&](int tick,const auto& sim) {
    if(phase==Phase::cruise && tick-cruise_tick>2400) {
      cruise_alt_error=std::max(cruise_alt_error,std::abs(sim.instruments().alt_msl-400));
      cruise_speed_error=std::max(cruise_speed_error,std::abs(sim.instruments().tas-110));
    }
  });
  std::printf("cycle check phase=%d alt=%f tas=%f cruise=%d touch=%d maxalt=%f errors=%f/%f\n",int(phase),s.instruments().alt_msl,s.instruments().tas,cruise_tick,touch_tick,m.max_alt,cruise_alt_error,cruise_speed_error);
  check(phase==Phase::stopped && cruise_tick>0 && stop_tick>touch_tick,"full cycle completed without state resets");
  check(cruise_alt_error<5 && cruise_speed_error<.2,"cycle stable cruise with test pilot");
  check(touchdown_speed>65 && touchdown_speed<85 && touchdown_sink>0 && touchdown_sink<2,"cycle gentle landing");
  check(m.min_alt>3 && m.max_alt<410 && m.peak_contact<2*s.config().mass*kG0 && s.instruments().tas<1e-5,"cycle contact and stable stop");
  std::printf("continuous cycle 420s: cruise_start=%.6f max_cruise_alt_error_after_20s=%.6f speed_error=%.6f touchdown_time=%.6f touchdown_speed=%.6f sink=%.6f rollout=%.6f stop_time=%.6f final_distance=%.6f final_speed=%.9g\n",cruise_tick*FixedStepClock::tick,cruise_alt_error,cruise_speed_error,touch_tick*FixedStepClock::tick,touchdown_speed,touchdown_sink,rollout,(stop_tick-touch_tick)*FixedStepClock::tick,s.state().pos_ned.x,s.instruments().tas);
}
void diagnosticsTest() {
  auto s=trimmed();auto c=s.controls();c.rudder_pedal=.1;s.setControls(c);s.step(FixedStepClock::tick);
  const auto d=s.debugFrame();Vec3 sum;bool side=false;
  for(std::size_t i=0;i<d.count;++i){sum+=d.forces[i].force_body;if(std::strcmp(d.forces[i].name,"fin")==0)side=true;}
  check(side,"side force diagnostics");near((sum-d.total_force_body).norm(),0,1e-8,"diagnostic forces sum to actual integrated force");
  // Aero lift/side basis must be perpendicular to flow at combined AoA/beta.
  State st;st.pos_ned.z=-2000;st.vel_ned={80,30,40};s.setState(st);const auto a=s.evalAero();
  near(a.lift_body.dot(st.vel_ned),0,1e-7,"wind lift orthogonality");near(a.side_body.dot(st.vel_ned),0,1e-7,"wind side orthogonality");near(a.lift_body.dot(a.side_body),0,1e-5,"wind frame orthogonal lift/side");
  // A pitched ground contact still acts along world vertical when friction is off.
  auto cfg=referenceConfig();cfg.mu_roll=cfg.mu_side=cfg.mu_brake_max=0;Simulator contact(cfg);st={};st.pos_ned.z=-3.4;st.att=quatFromEuler(.03,.04,0);contact.setState(st);contact.step(1./240);
  const auto frame=contact.debugFrame();
  for(std::size_t i=0;i<frame.count;++i)if(frame.forces[i].name[0]=='g' && frame.forces[i].name[1]=='e') {
    const auto world=st.att.rotate(frame.forces[i].force_body);near(world.x,0,1e-8,"runway normal north");near(world.y,0,1e-8,"runway normal east");check(world.z<0,"runway normal up");
  }
}
void robustnessTest() {
  for(double speed:{0.,.01,.49,.51,5.,40.,200.,600.}) for(double alpha:{-180.,-90.,-30.,0.,30.,90.,180.}) {
    Simulator s(referenceConfig());State st;st.pos_ned.z=-10000;st.vel_ned={speed*std::cos(alpha*kDeg2Rad),0,speed*std::sin(alpha*kDeg2Rad)};s.setState(st);
    Controls c;c.gear01=0;c.elevator_stick=1;c.aileron_stick=-1;c.rudder_pedal=1;c.flap01=1;c.spoiler01=1;c.throttle[0]=1;s.setControls(c);
    advance(s,240);near(s.state().att.w*s.state().att.w+s.state().att.x*s.state().att.x+s.state().att.y*s.state().att.y+s.state().att.z*s.state().att.z,1,1e-12,"extreme state quaternion unit");
  }
  auto s=trimmed();State bad=s.state();bad.vel_ned.x=std::nan("");s.setState(bad);near(s.state().vel_ned.x,110,0,"reject nonfinite state");
  Weather w;w.wind_ned.x=std::nan("");w.turbulence01=std::numeric_limits<double>::infinity();w.temp_offset_c=std::nan("");s.setWeather(w);advance(s,120);
  auto disabled=referenceConfig();disabled.wing_area=0;Simulator ballistic(disabled);State ball;ball.pos_ned.z=-1000;ball.vel_ned.x=110;ballistic.setState(ball);advance(ballistic,120);
  check(std::isfinite(ballistic.instruments().vstall),"zero aerodynamic area has finite diagnostic sentinel");
  auto cfg=referenceConfig();cfg.mass=0;bool rejected=false;try{Simulator invalid(cfg);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"invalid mass rejected");
  std::printf("stability: 56 speed/AoA extreme-input cases x 2s finite; invalid state/weather sanitized; unit quaternions\n");
}
}
int main(int argc,char** argv) {
  const std::array<std::pair<std::string_view,void(*)()>,10> suites{{{"trim",trimTest},{"controls",controlTest},{"stall",stallTest},{"taxi",taxiTest},{"takeoff",takeoffTest},{"cruise",cruiseTest},{"landing",landingTest},{"cycle",fullCycleTest},{"diagnostics",diagnosticsTest},{"robustness",robustnessTest}}};
  try {check(argc==2,"expected scenario or all");bool found=false;for(const auto& [name,fn]:suites)if(argv[1]==name || std::string_view(argv[1])=="all"){found=true;fn();std::printf("PASS %.*s\n",static_cast<int>(name.size()),name.data());}check(found,"unknown scenario");return 0;}
  catch(const std::exception& e){std::fprintf(stderr,"FAIL %s: %s\n",argc>1?argv[1]:"",e.what());return 1;}
}
