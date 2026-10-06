#include "ofs/simulator.hpp"
#include "ofs/trim.hpp"
#include "ofs/control_allocation.hpp"
#include "ofs/c_api.h"
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string_view>
using namespace ofs;
void require(bool v,const char* msg) {if(!v)throw std::runtime_error(msg);}
void near(double a,double b,double tolerance,const char* msg){require(std::isfinite(a)&&std::abs(a-b)<=tolerance,msg);}
void inertia() {
  const InertiaTensor I{4,5,6,.3,-.4,.2};const Vec3 v{1,2,3};
  require(I.positiveDefinite(),"Full SPD tensor");near((I.solve(I.apply(v))-v).norm(),0,2e-15,"Cholesky inverse identity");
  near(I.determinant(),118.452,1e-12,"Independent expanded determinant");
  require(!InertiaTensor{1,1,1,2,0,0}.positiveDefinite(),"Indefinite XY rejected");
  require(!InertiaTensor{1,1,1,0,0,std::nan("")}.positiveDefinite(),"Nonfinite YZ rejected");
  auto cfg=a320Config();cfg.ixy=1300;cfg.iyz=-1900;Simulator sim(cfg);
  auto s=sim.state();s.payload_offset={1,2,3};sim.setState(s);const auto m=sim.massProperties();
  near(m.ixy,cfg.ixy-s.payload_mass*(cfg.payload_position.x+1)*2+m.mass*m.cg.x*m.cg.y,1e-8,"XY parallel axis");
  const InertiaTensor diagonal{2,3,4};const Vec3 w{1,2,3};
  const auto a=diagonal.solve(-w.cross(diagonal.apply(w)));
  near(a.x,-3,1e-15,"Euler torque-free p derivative");near(a.y,2,1e-15,"Euler torque-free q derivative");near(a.z,-.5,1e-15,"Euler torque-free r derivative");
}
void interpolation() {
  // Independent multilinear polynomial f(x,y,z)=1+2x+3y+4z+5xy+6xyz.
  std::vector<double> values;for(double x:{0.,1.})for(double y:{0.,2.})for(double z:{0.,3.})values.push_back(1+2*x+3*y+4*z+5*x*y+6*x*y*z);
  GridTable t({{0,1},{0,2},{0,3}},values);const std::array point{.25,.5,.75};
  near(t.evaluate(point),1+2*.25+3*.5+4*.75+5*.25*.5+6*.25*.5*.75,1e-14,"3D multilinear polynomial");
  const std::array outside{2.,2.,3.};near(t.evaluate(outside),values.back(),0,"Clamp boundary, no extrapolation");
  bool rejected=false;try{t.evaluate(std::array{std::nan(""),0.,0.});}catch(const std::domain_error&){rejected=true;}require(rejected,"Nonfinite coordinate rejected");
  rejected=false;try{GridTable bad({{0,0}},{0,1});}catch(const std::invalid_argument&){rejected=true;}require(rejected,"Duplicate knots rejected");
  TableAeroModel model({{4,GridTable({{-1,1}}, {-2,2}),{AeroVariable::Elevator},false}});
  AeroInputs inputs;inputs.elevator=.3;near(model.coefficients(inputs).cm,.6,1e-15,"Raw actual surface table");
  EngineDeckModel engine(GridTable({{0,1000},{0,1},{0,.5,1}}, {10,100,200,20,120,240,5,50,100,10,60,120}));
  near(engine.evaluate(0,0,0,false).thrust,0,0,"Stopped engine has zero thrust");
  require(engine.evaluate(0,0,0,true).regime==EngineRegime::Idle,"Running zero power explicitly idle");
  near(engine.evaluate(500,.5,.5,true).thrust,82.5,1e-12,"3D engine interpolation");
}
void purity() {
  Simulator sim;auto s=sim.state();s.pos_ned.z=-2000;s.vel_ned={110,2,3};s.omega_body={.1,.2,.3};s.actuators_initialized=true;s.elevator=.12;
  sim.setState(s);const auto saved=sim.state();const auto a=sim.evaluateContinuous(saved,{},{}),b=sim.evaluateContinuous(saved,{},{});
  near((a.derivative.angular_velocity-b.derivative.angular_velocity).norm(),0,0,"Repeat pure derivative");
  near(sim.state().time,saved.time,0,"Pure derivative does not advance timer");near(sim.state().elevator,saved.elevator,0,"Pure derivative does not move actuator");
  near(sim.state().fuel_mass,saved.fuel_mass,0,"Pure derivative does not consume fuel");
  auto bad=saved;bad.att={0,0,0,0};require(!sim.setState(bad),"Zero quaternion detectable rejection");
  bad=saved;bad.vel_ned.x=std::nan("");require(!sim.setState(bad),"Nonfinite state detectable rejection");
  near(sim.state().vel_ned.x,saved.vel_ned.x,0,"Rejected state atomic");
  auto cfg=a320Config();cfg.wing_area=0;cfg.thrust_sl_static_each=0;Simulator falling(cfg);s={};s.engine_health[0]=s.engine_health[1]=0;s.pos_ned.z=-10000;falling.setState(s);falling.step(.75);near(falling.state().time,.75,1e-12,"Requested duration never truncated");
  auto named=a320Config();configureSurfaces(named);
  named.surfaces[0].name="very_long_debug_force_name_that_exceeds_the_fixed_buffer_safely";
  Simulator diagnostic(named);auto ds=diagnostic.state();ds.pos_ned.z=-2000;ds.vel_ned.x=110;diagnostic.setState(ds);diagnostic.step(1./240);
  require(diagnostic.debugFrame().forces[0].name[31]==0,"Long debug force name safely terminated");
  bool failed=false;try{clamp(std::nan(""),-1,1);}catch(const std::domain_error&){failed=true;}require(failed,"Generic clamp exposes nonfinite physics");
  auto* api=ofs_create();OfsState external{};external.pos_d=-10000;
  require(ofs_try_set_state(api,&external)==1,"C state acceptance");external.vel_n=std::nan("");
  require(ofs_try_set_state(api,&external)==0,"C state detectable rejection");require(ofs_try_step(api,std::nan(""))==0,"C step detectable rejection");ofs_destroy(api);

}
void convergence() {
  const InertiaTensor I{2,3,4,.1,.15,-.2};
  auto torqueFree=[&](const State& s){ContinuousDerivative d;d.position=s.vel_ned;d.velocity={0,0,kG0};d.angular_velocity=I.solve(-s.omega_body.cross(I.apply(s.omega_body)));const auto q=s.att*Quat{0,s.omega_body.x,s.omega_body.y,s.omega_body.z};d.attitude={q.w*.5,q.x*.5,q.y*.5,q.z*.5};return d;};
  State initial;initial.pos_ned.z=-100;initial.vel_ned={1,2,3};initial.omega_body={.7,1.1,.4};
  auto run=[&](double h){State s=initial;for(int n=0;n<int(std::round(2/h));++n)s=integrateContinuous(s,h,ContinuousIntegrator::RungeKutta4,torqueFree);return s;};
  const auto reference=run(1./8192);double previous=0;
  for(double h:{.1,.05,.025,.0125}) {
    const auto s=run(h);const double error=(s.omega_body-reference.omega_body).norm();
    const double energy=std::abs(.5*s.omega_body.dot(I.apply(s.omega_body))-.5*initial.omega_body.dot(I.apply(initial.omega_body)));
    const double momentum=(s.att.rotate(I.apply(s.omega_body))-initial.att.rotate(I.apply(initial.omega_body))).norm();
    const double attitude=std::hypot(std::hypot(s.att.w-reference.att.w,s.att.x-reference.att.x),std::hypot(s.att.y-reference.att.y,s.att.z-reference.att.z));
    std::printf("mathematical RK4 h=%.5f omega_error=%.12g energy_error=%.12g angular_momentum_error=%.12g attitude_error=%.12g order=%.4f\n",h,error,energy,momentum,attitude,previous?std::log2(previous/error):0);
    if(previous)require(previous/error>12 && previous/error<20,"Observed fourth-order angular convergence");
    near(s.pos_ned.z,initial.pos_ned.z+initial.vel_ned.z*2+.5*kG0*4,1e-11,"Analytic ballistic position");
    near(s.vel_ned.z,initial.vel_ned.z+kG0*2,1e-11,"Analytic ballistic velocity");
    require(momentum<2e-5,"World angular momentum invariant");previous=error;
  }
  // Full engineering airframe with fixed actual surfaces and engine power:
  // derivative evaluation includes atmosphere, local omega cross r and force sites.
  Simulator sim;Controls controls;controls.gear01=0;
  State start;start.pos_ned.z=-5000;start.vel_ned={150,0,0};start.att=quatFromEuler(.05,4*kDeg2Rad,0);
  start.omega_body={.03,.01,.02};start.actuators_initialized=true;start.n1[0]=start.n1[1]=.6;
  start.fuel_mass=sim.config().initial_fuel;start.payload_mass=sim.config().initial_payload;
  auto flight=[&](double h){State s=start;for(int n=0;n<int(std::round(.64/h));++n)s=integrateContinuous(s,h,ContinuousIntegrator::RungeKutta4,[&](const State& snapshot){return sim.evaluateContinuous(snapshot,controls,{}).derivative;});return s;};
  const auto flightReference=flight(.64/16384);previous=0;
  for(double h:{.08,.04,.02,.01}) {
    const auto s=flight(h);const double pos=(s.pos_ned-flightReference.pos_ned).norm(),vel=(s.vel_ned-flightReference.vel_ned).norm(),omega=(s.omega_body-flightReference.omega_body).norm();
    const double att=std::hypot(std::hypot(s.att.w-flightReference.att.w,s.att.x-flightReference.att.x),std::hypot(s.att.y-flightReference.att.y,s.att.z-flightReference.att.z));
    const auto m=sim.massProperties(s);
    const auto energy=[&](const State& state){return .5*m.mass*state.vel_ned.norm2()-m.mass*kG0*state.pos_ned.z+.5*state.omega_body.dot(m.tensor().apply(state.omega_body));};
    const double error=vel+omega;
    std::printf("mathematical frozen-airframe RK4 h=%.5f position_error=%.9g velocity_error=%.9g attitude_error=%.9g omega_error=%.9g energy_difference_J=%.9g order=%.5f\n",h,pos,vel,att,omega,std::abs(energy(s)-energy(flightReference)),previous?std::log2(previous/error):0);
    if(previous)require(previous/error>12&&previous/error<20,"Airframe smooth continuous fourth-order convergence");
    previous=error;
  }

}
void allocation() {
  const std::array<Vec3,5> c{{{1,0,0},{1,0,0},{0,1,0},{0,0,1},{0,0,0}}};
  const std::array<double,5> weight{1,1,1,1,1},lower{0,0,-1,-1,0},upper{.1,1,1,1,0};
  const auto x=boundedControlAllocation(c,{.8,0,0},weight,lower,upper,1e-6);
  near(x[0],.1,1e-12,"First actuator at travel/rate limit");near(x[1],.7,1e-6,"Remaining actuator compensates for saturated column");
  for(std::size_t i=0;i<5;++i)require(x[i]>=lower[i]&&x[i]<=upper[i],"All allocation bounds honoured");
}
int main(int argc,char** argv) {try{const std::string_view which=argc>1?argv[1]:"";if(which=="inertia")inertia();else if(which=="interpolation")interpolation();else if(which=="purity")purity();else if(which=="convergence")convergence();else if(which=="allocation")allocation();else return 2;std::puts("PASS mathematical/invariant foundation");return 0;}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
