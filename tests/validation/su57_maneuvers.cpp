#include "ofs/aircraft_definition.hpp"
#include "scenario.hpp"
#include <filesystem>
#include <fstream>
using namespace ofs;
using scenario::check;
namespace {
double energy(const Simulator& sim) {
  const auto& s=sim.state();const auto m=sim.massProperties();
  return .5*m.mass*s.vel_ned.norm2()-m.mass*kG0*s.pos_ned.z+.5*s.omega_body.dot(m.tensor().apply(s.omega_body));
}
struct Trace {
  double initialSpeed{},minSpeed{1e30},finalSpeed{},heightChange{},energyChange{},maxAlpha{},maxBeta{},maxG{};
  Vec3 maxRates{},aeroMoment{},vectorMoment{};double nozzle[2]{},aeroWork{},thrustWork{},energyResidual{};
  void print(std::string_view name) const {
    std::printf("Su57 %.*s initial/min/final_mps=%.3f/%.3f/%.3f dh_m=%+.3f dE_MJ=%+.6f alpha/beta_deg=%.3f/%.3f maxG=%.3f rates_rad_s=%.4f/%.4f/%.4f nozzle_deg=%.3f/%.3f aeroMoment_Nm=%.1f/%.1f/%.1f vectorMoment_Nm=%.1f/%.1f/%.1f aeroWork_MJ=%+.6f thrustWork_MJ=%+.6f energyResidual_J=%+.3f\n",
      int(name.size()),name.data(),initialSpeed,minSpeed,finalSpeed,heightChange,energyChange/1e6,maxAlpha,maxBeta,maxG,
      maxRates.x,maxRates.y,maxRates.z,nozzle[0]*kRad2Deg,nozzle[1]*kRad2Deg,aeroMoment.x,aeroMoment.y,aeroMoment.z,
      vectorMoment.x,vectorMoment.y,vectorMoment.z,aeroWork/1e6,thrustWork/1e6,energyResidual);
  }
};
Trace trace(std::string_view name,Simulator& sim,Controls controls,int ticks,const std::filesystem::path& output) {
  const double initialEnergy=energy(sim),height=sim.instruments().alt_msl;
  Trace result;result.initialSpeed=sim.instruments().tas;result.minSpeed=result.initialSpeed;
  std::ofstream csv(output/(std::string(name)+".csv"));
  csv<<"time_s,tas_mps,altitude_m,energy_J,alpha_deg,beta_deg,g,p_rad_s,q_rad_s,r_rad_s,nozzle_L_rad,nozzle_R_rad,aero_Mx_Nm,aero_My_Nm,aero_Mz_Nm,TV_Mx_Nm,TV_My_Nm,TV_Mz_Nm\n";
  const auto peak=[](Vec3 a,Vec3 b){return Vec3{std::max(a.x,std::abs(b.x)),std::max(a.y,std::abs(b.y)),std::max(a.z,std::abs(b.z))};};
  auto power=[&]{const auto body=sim.state().att.inverseRotate(sim.state().vel_ned);const auto a=sim.evalAero();const auto t=sim.evalThrust();return std::pair{a.force_body.dot(body)+a.moment_body.dot(sim.state().omega_body),t.force_body.dot(body)+t.moment_body.dot(sim.state().omega_body)};};
  sim.setControls(controls);auto previous=power();
  for(int tick=0;tick<ticks;++tick) {
    sim.step(1./120);check(scenario::finite(sim.state()),"maneuver state finite");const auto p=power();
    result.aeroWork+=(previous.first+p.first)*.5/120;result.thrustWork+=(previous.second+p.second)*.5/120;previous=p;
    const auto n=sim.instruments();const auto a=sim.evalAero();const auto t=sim.evalThrust();
    result.minSpeed=std::min(result.minSpeed,n.tas);result.maxAlpha=std::max(result.maxAlpha,std::abs(n.alpha_deg));
    result.maxBeta=std::max(result.maxBeta,std::abs(n.beta_deg));result.maxG=std::max(result.maxG,n.g_load);
    result.maxRates=peak(result.maxRates,sim.state().omega_body);result.aeroMoment=peak(result.aeroMoment,a.moment_body);result.vectorMoment=peak(result.vectorMoment,t.moment_body);
    for(unsigned e=0;e<2;++e){result.nozzle[e]=std::max(result.nozzle[e],std::abs(sim.state().nozzle_angle[e]));check(result.nozzle[e]<=sim.config().engines[e].vector_limit+1e-12,"nozzle limit");}
    check(n.alt_msl>1000,"airborne fixture excludes ground impulse work");
    if(tick%12==0)csv<<sim.state().time<<','<<n.tas<<','<<n.alt_msl<<','<<energy(sim)<<','<<n.alpha_deg<<','<<n.beta_deg<<','<<n.g_load<<','<<sim.state().omega_body.x<<','<<sim.state().omega_body.y<<','<<sim.state().omega_body.z<<','<<sim.state().nozzle_angle[0]<<','<<sim.state().nozzle_angle[1]<<','<<a.moment_body.x<<','<<a.moment_body.y<<','<<a.moment_body.z<<','<<t.moment_body.x<<','<<t.moment_body.y<<','<<t.moment_body.z<<'\n';
  }
  result.finalSpeed=sim.instruments().tas;result.heightChange=sim.instruments().alt_msl-height;result.energyChange=energy(sim)-initialEnergy;
  result.energyResidual=result.energyChange-result.aeroWork-result.thrustWork;
  result.print(name);
  // Semi-implicit Euler integrates at 240 Hz. This bound is a numerical energy
  // closure requirement, independent of any real-aircraft maneuver target.
  check(std::abs(result.energyResidual)<.02*(std::abs(result.aeroWork)+std::abs(result.thrustWork))+1000,"mechanical energy change accounted for by force/moment work");
  return result;
}
}
int main(int argc,char** argv) {try {
  check(argc==3,"suite and root");const std::string_view name=argv[1];const auto output=std::filesystem::path(argv[2])/"output/m3_68_1/su57";
  std::filesystem::create_directories(output);auto cfg=su57Config();cfg.fuel_flow_scale=0;
  if(name=="poststall") {
    for(double angle:{35.,55.,70.}) {
      Simulator sim(cfg);State s;s.pos_ned.z=-6000;s.vel_ned.x=140;s.att=quatFromEuler(0,angle*kDeg2Rad,0);
      s.fcs_enabled=false;s.engine_health[0]=s.engine_health[1]=0;sim.setState(s);Controls c;c.gear01=0;sim.setControls(c);sim.primeActuators();
      const auto r=trace("unpowered_"+std::to_string(int(angle)),sim,c,240,output);
      check(r.energyChange<0&&r.aeroWork<0&&r.minSpeed<r.initialSpeed,"high-AoA maneuver loses energy/speed");
      check(r.maxAlpha>=angle-1,"initial high-AoA/deep post-stall captured");
    }
  }else if(name=="vector_comparison") {
    double pitchRate[2]{};
    for(unsigned enabled=0;enabled<2;++enabled) {
      auto local=cfg;if(!enabled)for(auto& engine:local.engines)engine.vector_limit=0;
      Simulator sim(local);State s;s.pos_ned.z=-6000;s.vel_ned.x=25;s.n1[0]=s.n1[1]=1;s.afterburner[0]=s.afterburner[1]=1;
      sim.setState(s);Controls c;c.gear01=0;c.throttle[0]=c.throttle[1]=1;c.elevator_stick=.3;
      const auto r=trace(enabled?"TV_enabled":"TV_disabled",sim,c,120,output);pitchRate[enabled]=sim.state().omega_body.y;
      check(enabled?r.nozzle[0]>kDeg2Rad:r.nozzle[0]==0,"enabled versus disabled actuator authority");
    }
    check(pitchRate[1]>pitchRate[0]+.02,"physical vectoring improves low-speed pitch response");
  }else if(name=="high_g") {
    TrimRequest req;req.tas=300;req.altitude=6000;const auto trim=solveTrim(cfg,req);check(trim.converged,"high-G entry trim");
    Simulator sim(cfg);sim.setState(trim.state);auto c=trim.controls;c.elevator_stick=1;c.throttle[0]=c.throttle[1]=1;
    const auto r=trace("high_g",sim,c,360,output);check(r.maxG>4&&r.maxG<12,"high-G physical response/soft limiter envelope");
  }else if(name=="engine_out") {
    TrimRequest req;req.tas=180;req.altitude=6000;const auto trim=solveTrim(cfg,req);check(trim.converged,"engine-out entry trim");
    double yaw[2]{};
    for(unsigned failed=0;failed<2;++failed) {
      Simulator sim(cfg);auto s=trim.state;s.fcs_enabled=false;s.engine_health[failed]=0;sim.setState(s);
      auto c=trim.controls;c.throttle[0]=c.throttle[1]=1;
      trace(failed==0?"left_failed":"right_failed",sim,c,120,output);yaw[failed]=sim.state().omega_body.z;
      check(sim.evalThrust().each[failed]==0,"engine-out stays failed");
    }
    check(yaw[0]<0&&yaw[1]>0,"independent engine-out yaw signs");
  }else throw std::invalid_argument("unknown maneuver");
  return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL Su57 maneuver: %s\n",e.what());return 1;}}
