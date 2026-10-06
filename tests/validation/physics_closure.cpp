#include "ofs/aircraft_definition.hpp"
#include "ofs/aerodynamics.hpp"
#include "animation.hpp"
#include "scenario.hpp"
#include <filesystem>
#include <fstream>
using namespace ofs;
using scenario::check;
namespace {
void near(double a,double b,double tolerance,const char* what){check(std::isfinite(a)&&std::abs(a-b)<=tolerance,what);}
void mass() {
  for(const auto& d:aircraftDefinitions()) {
    Simulator sim(d.flight);const auto& cfg=sim.config();const double basic=cfg.empty_mass>0?cfg.empty_mass:cfg.mass-cfg.initial_fuel-cfg.initial_payload;
    for(double fuel:{0.,cfg.initial_fuel*.5,cfg.initial_fuel})
      for(double payload:{0.,cfg.initial_payload,4000.})for(double offset:{-2.,0.,2.}) {
        State s;s.fuel_mass=fuel;s.payload_mass=payload;s.payload_offset={offset,.2,-.1};
        check(sim.setState(s),"all-aircraft load state accepted");const auto m=sim.massProperties();const auto I=m.tensor();
        near(m.mass,basic+fuel+payload,1e-8,"mass in kg");check(I.positiveDefinite(),"load tensor positive definite");
        const Vec3 u{1,2,3},v{-2,5,4};near(u.dot(I.apply(v)),v.dot(I.apply(u)),1e-7,"tensor symmetric");
        std::vector<double> principal;
        for(const auto axis:I.principalAxes()) {
          const double value=axis.dot(I.apply(axis));principal.push_back(value);
          check(value>0&&std::sqrt(value/m.mass)<40,"finite physical radius of gyration");
        }
        std::sort(principal.begin(),principal.end());check(principal[2]<=principal[0]+principal[1]+1e-6,"mass tensor obeys principal triangle inequality");
        if(fuel>0) {
          auto lower=s;lower.fuel_mass-=.1;const auto next=sim.massProperties(lower);
          near(next.mass,m.mass-.1,1e-8,"fuel burn mass increment");
          check((next.cg-m.cg).norm()<1e-4&&(next.inertia-m.inertia).norm()/m.inertia.norm()<1e-4,"smooth fuel burn CG/inertia");
        }
        // Independently undo the parallel axis shift and compare distributed
        // payload inertia increment, including shape at the current load.
        auto extra=s;extra.payload_mass+=1;const auto n=sim.massProperties(extra);
        const auto origin=[](const auto& p){return p.inertia+Vec3{p.cg.y*p.cg.y+p.cg.z*p.cg.z,p.cg.x*p.cg.x+p.cg.z*p.cg.z,p.cg.x*p.cg.x+p.cg.y*p.cg.y}*p.mass;};
        const auto p=cfg.payload_position+s.payload_offset;
        const auto expected=cfg.payload_inertia_per_kg+Vec3{p.y*p.y+p.z*p.z,p.x*p.x+p.z*p.z,p.x*p.x+p.y*p.y};
        check((origin(n)-origin(m)-expected).norm()<1e-6,"distributed payload shape plus parallel axis");
      }
    const auto m=sim.massProperties(State{});
    std::printf("mass %.*s reference kg=%.3f CG_m=%.6f/%.6f/%.6f tensor kgm2=%.3f/%.3f/%.3f xy/xz/yz=%.3f/%.3f/%.3f; 27 load cases PASS\n",int(d.key.size()),d.key.data(),m.mass,m.cg.x,m.cg.y,m.cg.z,m.inertia.x,m.inertia.y,m.inertia.z,m.ixy,m.ixz,m.iyz);
  }
}
void vectoring() {
  const auto& def=aircraftDefinition(AircraftType::Su57);Simulator sim(def.flight);
  double maximumError=0,maximumEulerError=0;unsigned cases=0;
  for(double fuel:{0.,1000.,6500.,10300.})for(double payload:{0.,100.,4000.})
    for(double offset:{-2.,0.,2.})for(unsigned failed:{0u,1u,2u,3u})
      for(auto angles:{std::pair{0.,0.},std::pair{-10.,-10.},std::pair{-10.,10.}}) {
        State s;s.pos_ned.z=-3000;s.vel_ned={60,3,2};s.omega_body={.1,.2,-.1};s.fuel_mass=fuel;s.payload_mass=payload;s.payload_offset={offset,0,0};
        s.n1[0]=s.n1[1]=1;s.afterburner[0]=s.afterburner[1]=1;
        s.engine_health[0]=(failed&1)?0:1;s.engine_health[1]=(failed&2)?0:1;
        s.nozzle_angle[0]=angles.first*kDeg2Rad;s.nozzle_angle[1]=angles.second*kDeg2Rad;s.actuators_initialized=true;
        check(sim.setState(s),"vector load state accepted");const auto m=sim.massProperties();const auto t=sim.evalThrust();Vec3 force,moment;
        for(unsigned e=0;e<2;++e) {
          const auto& engine=sim.config().engines[e];
          const auto rotate=[&](Vec3 v){const double a=s.nozzle_angle[e];return v*std::cos(a)+engine.vector_axis.cross(v)*std::sin(a)+engine.vector_axis*engine.vector_axis.dot(v)*(1-std::cos(a));};
          const auto f=rotate(engine.direction)*t.each[e];
          const auto r=engine.nozzle_pivot+rotate(engine.position-engine.nozzle_pivot)-m.cg;
          force+=f;moment+=r.cross(f);
          near((f-t.force[e]).norm(),0,1e-8,"independent nozzle force geometry");
          if((failed&(1u<<e))||fuel==0)near(f.norm(),0,0,"failed/starved engine produces zero vector force");
        }
        maximumError=std::max(maximumError,(moment-t.moment_body).norm());
        check(maximumError<1e-7&&(force-t.force_body).norm()<1e-7,"all vector moment is r cross F about loaded CG");
        ofs::client::AircraftPose pose;pose.update(sim.state(),{},def,0);
        near(pose.channel("vector_L"),sim.state().nozzle_angle[0],1e-12,"visual left nozzle equals physical state");
        near(pose.channel("vector_R"),sim.state().nozzle_angle[1],1e-12,"visual right nozzle equals physical state");
        const auto eval=sim.evaluateContinuous(sim.state(),{},{});
        const auto expected=m.tensor().solve(eval.aero.moment_body+moment-s.omega_body.cross(m.tensor().apply(s.omega_body)));
        maximumEulerError=std::max(maximumEulerError,(expected-eval.derivative.angular_velocity).norm());
        check(maximumEulerError<1e-10,"FCS cannot bypass physical rigid-body moment equation");++cases;
      }
  std::printf("vectoring %u load/failure/angle cases max_M_minus_r_cross_F=%.12g Nm max_Euler_error=%.12g rad/s2; visual=physical PASS\n",cases,maximumError,maximumEulerError);
}
void engines(const std::filesystem::path& root) {
  std::filesystem::create_directories(root/"output/m3_68_1");std::ofstream csv(root/"output/m3_68_1/engine-curves.csv");
  csv<<"aircraft,altitude_m,mach,throttle,spool,afterburner,thrust_each_N,fuel_each_kg_s,classification\n";
  for(const auto& d:aircraftDefinitions()) {
    Simulator sim(d.flight);const auto& cfg=sim.config();
    for(double h:{0.,5000.,11000.,20000.})for(double mach:{0.,.5,.9,1.5,3.}) {
      double previous=-1;
      for(unsigned k=0;k<=100;++k) {
        const double power=k/100.;State s;s.pos_ned.z=-h;s.vel_ned.x=mach*isaAtAltitude(h).sound;s.n1[0]=s.n1[1]=power;
        s.inlet_spike[0]=s.inlet_spike[1]=inletSpikeTarget(mach);
        if(cfg.afterburner_threshold<1)s.afterburner[0]=s.afterburner[1]=clamp((power-cfg.afterburner_threshold)/(1-cfg.afterburner_threshold),0,1);
        check(sim.setState(s),"engine sample state");const auto t=sim.evalThrust();
        check(t.each[0]>=previous-1e-8&&std::isfinite(t.fuel_flow[0])&&t.fuel_flow[0]>=0,"monotone power/finite fuel curve");previous=t.each[0];
        csv<<d.key<<','<<h<<','<<mach<<','<<power<<','<<s.n1[0]<<','<<s.afterburner[0]<<','<<t.each[0]<<','<<t.fuel_flow[0]<<",CALIBRATED_APPROXIMATION\n";
        s.engine_health[0]=0;check(sim.setState(s),"engine failure accepted");const auto failed=sim.evalThrust();
        near(failed.each[0],0,0,"left engine failed thrust");near(failed.fuel_flow[0],0,0,"left engine failed flow");near(failed.each[1],t.each[1],1e-8,"right engine independent");
      }
    }
    if(cfg.afterburner_threshold<1) {
      const double epsilon=1e-7;double values[2]{};
      for(unsigned side=0;side<2;++side){State s;s.n1[0]=s.n1[1]=cfg.afterburner_threshold+(side?epsilon:-epsilon);
        s.afterburner[0]=s.afterburner[1]=side?epsilon/(1-cfg.afterburner_threshold):0;
        sim.setState(s);values[side]=sim.evalThrust().each[0];}
      check(std::abs(values[1]-values[0])<1,"continuous afterburner threshold");
    }
    State s;s.pos_ned.z=-5000;s.vel_ned.x=180;sim.setState(s);Controls c;c.gear01=0;c.throttle[0]=1;c.throttle[1]=0;sim.setControls(c);
    sim.step(1./120);check(sim.state().n1[0]>sim.state().n1[1]&&sim.state().n1[0]<1,"independent finite spool response");
    std::printf("engines %.*s 2020 stabilized curve samples, failure independence, AB continuity/spool PASS; curves approximate\n",int(d.key.size()),d.key.data());
  }
}
}
int main(int argc,char** argv){try{check(argc>=2,"suite");const std::string_view suite=argv[1];if(suite=="mass")mass();else if(suite=="vectoring")vectoring();else if(suite=="engines"){check(argc==3,"root");engines(argv[2]);}else throw std::invalid_argument("unknown suite");return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL physics closure: %s\n",e.what());return 1;}}
