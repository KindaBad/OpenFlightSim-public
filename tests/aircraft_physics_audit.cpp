#include "scenario.hpp"
#include "ofs/airliner.hpp"
#include "ofs/physical_geometry.hpp"
#include "ofs/geometry_debug.hpp"
#include "ofs/unsteady_aero.hpp"
#include "ofs/telemetry.hpp"
#include "gltf.hpp"
#include "animation.hpp"
#include "json.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <limits>
using namespace ofs;
using scenario::check;
namespace {
void close(double a,double b,double tolerance,const char* why){check(std::isfinite(a)&&std::abs(a-b)<=tolerance,why);}
void vectoring() {
  Simulator sim(su57Config());State s;s.pos_ned.z=-2000;s.n1[0]=s.n1[1]=1;
  s.fcs_enabled=false;Controls c;c.gear01=0;sim.setControls(c);
  for(double cg:{-1.,0.,1.})for(double thrust:{.3,1.})for(int active:{0,1,2})
    for(auto angles:{std::pair{0.,0.},std::pair{-10.,-10.},std::pair{-10.,10.}}) {
      s.payload_offset={cg,0,0};s.payload_mass=4000;
      s.n1[0]=s.n1[1]=thrust;s.engine_health[0]=active==1?0:1;s.engine_health[1]=active==0?0:1;
      s.nozzle_angle[0]=angles.first*kDeg2Rad;s.nozzle_angle[1]=angles.second*kDeg2Rad;
      check(sim.setState(s),"TV snapshot accepted");const auto t=sim.evalThrust();const auto m=sim.massProperties();
      Vec3 force,moment;
      for(unsigned e=0;e<2;++e){force+=t.force[e];moment+=(t.position[e]-m.cg).cross(t.force[e]);
        close(t.force[e].norm(),t.each[e],1e-8,"TV thrust magnitude preserved");}
      check((force-t.force_body).norm()<1e-8&&(moment-t.moment_body).norm()<1e-8,"TV sum mechanics about actual CG");
      std::printf("TV engines=%d power=%.1f cg=%.3f angles=%.0f/%.0f F_L=%.2f/%.2f/%.2f F_R=%.2f/%.2f/%.2f M=%.2f/%.2f/%.2f\n",active,thrust,m.cg.x,angles.first,angles.second,t.force[0].x,t.force[0].y,t.force[0].z,t.force[1].x,t.force[1].y,t.force[1].z,moment.x,moment.y,moment.z);
      if(active==2 && angles.first==-10 && angles.second==-10) {check(moment.y>0,"symmetric TV nose-up sign");close(moment.x,0,1e-8,"symmetric roll cancellation");close(moment.z,0,1e-8,"symmetric yaw cancellation");}
      if(active==2 && angles.first==-10 && angles.second==10) {check(moment.z>0&&moment.x<0,"differential TV yaw right and roll left coupling");}
      if(active==0 && angles.first==0)check(moment.z>0,"left engine yaw-right sign");
      if(active==1 && angles.first==0)check(moment.z<0,"right engine yaw-left sign");
      // Analytical articulated-force Jacobian checked by independent differences.
      const double h=1e-6;
      for(unsigned e=0;e<2;++e) {
        auto plus=s,minus=s;plus.nozzle_angle[e]+=h;minus.nozzle_angle[e]-=h;
        const auto finite=(sim.evalThrust(plus,{}).moment_body-sim.evalThrust(minus,{}).moment_body)/(2*h);
        const auto& engine=sim.config().engines[e];
        const auto exact=(t.position[e]-m.cg).cross(engine.vector_axis.cross(t.force[e]))+
          engine.vector_axis.cross(t.position[e]-engine.nozzle_pivot).cross(t.force[e]);
        check((finite-exact).norm()<.001,"articulated nozzle Jacobian");
      }
    }
}
void geometry(const std::filesystem::path& root) {
  for(auto type:{AircraftType::A320,AircraftType::Su57}) {
    const auto& d=aircraftDefinition(type);const auto& g=type==AircraftType::A320?a320Geometry():su57Geometry();
    Simulator sim(d.flight);
    check((d.visual.assetCg-g.asset_cg).norm()==0,"shared visual CG");
    for(unsigned e=0;e<2;++e) {
      check((d.visual.exhaust[e]-sim.config().engines[e].position).norm()==0,"shared visual/physics nozzle exits");
      check((g.toBody(g.toAsset(g.engines[e]))-g.engines[e]).norm()<1e-10,"asset/body inverse transform");
    }
    if(type==AircraftType::A320) {
      ofs::client::AircraftPose pose;auto state=sim.state();state.actuators_initialized=true;state.flap=.2;
      pose.update(state,sim.controls(),d,0);close(pose.channel("flap"),0,1e-12,"slat-only visual flap angle");
      close(pose.channel("slat"),-18*kDeg2Rad,1e-12,"visual slat schedule");
      state.flap=1;pose.update(state,sim.controls(),d,0);close(pose.channel("flap"),1,1e-12,"full visual flap angle");
      close(pose.channel("slat"),-27*kDeg2Rad,1e-12,"full visual slat angle");
    }
    const auto tensor=sim.massProperties().tensor();const auto axes=tensor.principalAxes();
    for(unsigned i=0;i<3;++i) {close(axes[i].norm(),1,1e-12,"principal axis unit length");
      check(tensor.apply(axes[i]).cross(axes[i]).norm()<1e-6,"principal axis eigenvector");
      for(unsigned j=i+1;j<3;++j)close(axes[i].dot(axes[j]),0,1e-12,"principal axes orthogonal");}
    const auto debug=geometryDebugLines(sim,type);check(debug.size()>30,"geometry debug includes hinges and force sites");
    const auto path=root/d.modelAsset;if(!std::filesystem::exists(path)){scenario::skipMissingAsset(root.string(),d.modelAsset);continue;}
    const auto mesh=ofs::client::loadGltf(path.string());check(mesh.valid(),"local geometry load");
    check(std::abs(mesh.boundsMax[0]-mesh.boundsMin[0]-g.length)<.15 &&
      std::abs(mesh.boundsMax[2]-mesh.boundsMin[2]-g.span)<.15 &&
      std::abs(mesh.boundsMax[1]-mesh.boundsMin[1]-g.height)<.15,"overall physical/visual dimensions");
    std::printf("Geometry %.*s length error=%.4f span error=%.4f height error=%.4f m\n",int(d.key.size()),d.key.data(),mesh.boundsMax[0]-mesh.boundsMin[0]-g.length,mesh.boundsMax[2]-mesh.boundsMin[2]-g.span,mesh.boundsMax[1]-mesh.boundsMin[1]-g.height);
    std::printf("  nozzle exits spacing=%.3f m, body stations %.3f/%.3f m; these are model estimates, not independent OEM measurements\n",
      (g.engines[1]-g.engines[0]).norm(),g.engines[0].x,g.engines[1].x);
    for(const auto& hinge:g.hinges) {
      double nearest=1e9;Vec3 matched;
      for(const auto& node:mesh.nodes)if(node.channel==hinge.channel) {
        const auto position=g.toBody({node.world[12],node.world[13],node.world[14]});
        const double distance=(position-hinge.position).norm();if(distance<nearest){nearest=distance;matched=position;}
      }
      std::printf("  hinge %s model=%.4f/%.4f/%.4f expected=%.4f/%.4f/%.4f error=%.4f m\n",hinge.name,matched.x,matched.y,matched.z,hinge.position.x,hinge.position.y,hinge.position.z,nearest);
      check(nearest<.15,"visual/physics hinge alignment exceeds 15 cm");
    }
    for(const auto& primitive:mesh.primitives)if(primitive.name=="Su57 | L-engine"||primitive.name=="Su57 | R-engine") {
      double rear=-1e9,low=1e9,high=-1e9;
      for(std::size_t k=0;k<primitive.vertices.size();k+=ofs::client::kGltfVertexFloats) {
        rear=std::max(rear,double(primitive.vertices[k]));low=std::min(low,double(primitive.vertices[k+2]));high=std::max(high,double(primitive.vertices[k+2]));
      }
      std::printf("  %s rear exit asset X=%.4f, lateral centre Z=%.4f; body exit X=%.4f\n",primitive.name.c_str(),rear,(low+high)*.5,g.asset_cg.x-rear);
      const auto e=primitive.name=="Su57 | L-engine"?0u:1u;
      close(g.asset_cg.x-rear,g.engines[e].x,.15,"visual nozzle longitudinal station");
      close(-.5*(low+high),g.engines[e].y,.15,"visual engine lateral centre");
    }
    // Wheel hubs constrain individual contact stations, not just the min-Y plane.
    for(unsigned i=0;i<3;++i) {
      double nearest=1e9;const double radius=i==0?d.visual.noseWheelRadius:d.visual.wheelRadius;
      for(const auto& node:mesh.nodes)if(node.channel==(i==0?"nose_wheel":"wheel")) {
        auto p=g.toBody({node.world[12],node.world[13],node.world[14]});p.z+=radius;
        // Paired wheels share an axle; lateral tire offsets are expected.
        const auto error=p-g.gear[i];nearest=std::min(nearest,std::hypot(error.x,error.z));
      }
      std::printf("  wheel contact %u longitudinal/vertical error %.4f m\n",i,nearest);check(nearest<.15,"wheel contact station alignment");
    }
    // Cross-sections constrain wing leading/trailing edge stations without
    // confusing imposed overall scale with accurate planform.
    for(double span:{.25*g.span,.4*g.span}) {
      double leading=1e9,trailing=-1e9;
      for(const auto& primitive:mesh.primitives)
        if(primitive.name.find("wing")!=std::string::npos||primitive.name.find("Wing")!=std::string::npos||primitive.name.find("Main_Body")!=std::string::npos)
          for(std::size_t k=0;k<primitive.vertices.size();k+=ofs::client::kGltfVertexFloats)
            if(std::abs(std::abs(primitive.vertices[k+2])-span)<.12) {
              leading=std::min(leading,double(primitive.vertices[k]));trailing=std::max(trailing,double(primitive.vertices[k]));
            }
      check(leading<trailing,"wing station mesh measurement exists");
      std::printf("  wing slice y=%.3f asset leading/trailing=%.3f/%.3f m (if absent, inspect supplied mesh names; no OEM planform validation)\n",span,leading,trailing);
    }
  }
  check(!validAircraftType(static_cast<AircraftType>(2)),"retired wire ID rejected");
  bool rejected=false;try{aircraftTypeFromName("fighter");}catch(const std::invalid_argument&){rejected=true;}check(rejected,"temporary selection rejected");
}
void performance(const std::filesystem::path& root) {
  std::ifstream input(root/"data/reference/a320/performance.json");check(bool(input),"independent references present");
  std::ostringstream contents;contents<<input.rdbuf();const auto text=contents.str();ofs::client::json::Parser parser(text);const auto& reference=parser.root();
  auto cfg=a320Config();cfg.initial_fuel=10000;cfg.mass=cfg.empty_mass+cfg.initial_payload+cfg.initial_fuel;
  Simulator sim(cfg);State s;s.pos_ned.z=-20;s.fcs_enabled=false;
  Controls c;c.flap01=1;c.gear01=1;sim.setControls(c);
  double maxLift=0;
  for(unsigned angle=0;angle<=250;++angle) {const double alpha=double(angle)*.1*kDeg2Rad;s.vel_ned={70*std::cos(alpha),0,70*std::sin(alpha)};sim.setState(s);maxLift=std::max(maxLift,sim.evalAero().cl);}
  const double stall=std::sqrt(2*reference["approach"]["mass_kg"].number()*kG0/(isaAtAltitude(0).rho*cfg.wing_area*maxLift));
  const double approach=reference["vls_stall_ratio"]["value"].number()*stall/.514444444444;
  std::printf("Independent A320 approach mass=%.0f kg expected=%.3f actual=%.3f error=%.3f kt; realized CLmax=%.4f\n",reference["approach"]["mass_kg"].number(),reference["approach"]["ias_knots"].number(),approach,approach-reference["approach"]["ias_knots"].number(),maxLift);
  close(approach,reference["approach"]["ias_knots"].number(),reference["approach"]["tolerance_knots"].number(),"held-out Airbus approach-speed point");
  for(double altitude:{1000.,11000.}) {
    TrimRequest request;request.altitude=altitude;request.tas=altitude==1000?110:.78*isaAtAltitude(altitude).sound;
    const auto trim=solveTrim(cfg,request);check(trim.converged,"transport steady cruise solution");sim.setState(trim.state);sim.setControls(trim.controls);
    const auto aero=sim.evalAero();
    const auto thrust=sim.evalThrust();
    std::printf("A320 cruise engineering check h=%.0f Mach=%.3f drag=%.1f thrust=%.1f fuel=%.4f kg/s; no independent thrust/drag point claimed\n",altitude,request.tas/isaAtAltitude(altitude).sound,aero.drag_body.norm(),thrust.force_body.norm(),thrust.fuel_flow[0]+thrust.fuel_flow[1]);
  }
  const auto clean=a320HighLift(0),full=a320HighLift(1),slats=a320HighLift(.2);
  check(slats.flap_degrees==0&&slats.slat_degrees>0,"slat-only setting separated");
  check(full.clmax>clean.clmax&&full.drag_increment>clean.drag_increment,"high-lift configuration effects");
  for(double travel:{0.,.2,.4,.6,.8,1.}) {c.flap01=travel;s.vel_ned={100,0,15};sim.setState(s);sim.setControls(c);
    const auto h=a320HighLift(travel);std::printf("A320 setting=%.1f flap=%.1f slat=%.1f CLmax=%.2f CDdevice=%.4f\n",travel,h.flap_degrees,h.slat_degrees,h.clmax,sim.evalAero().device_cd);}
}
// Operational diagnostics are deliberately separate from reference pass/fail.
// Certified runway curves cannot be validated with this all-engine test pilot.
void performanceReport(const std::filesystem::path& root) {
  std::ifstream input(root/"data/reference/a320/performance.json");std::ostringstream contents;contents<<input.rdbuf();
  const auto text=contents.str();ofs::client::json::Parser parser(text);const auto& reference=parser.root();
  auto cfg=a320Config();cfg.fuel_flow_scale=0;
  Simulator takeoff(cfg,Simulator::GroundModel::FlatRunway);State s;s.pos_ned.z=-3.55;s.fcs_enabled=false;takeoff.setState(s);
  Controls c;c.flap01=.4;c.throttle[0]=c.throttle[1]=1;takeoff.setControls(c);
  double rotation=0,screen=0;
  for(unsigned tick=0;tick<12000;++tick) {
    const auto n=takeoff.instruments();
    if(n.tas>78) {if(rotation==0)rotation=takeoff.state().pos_ned.x;c.elevator_stick=scenario::pitchInput(takeoff,11*kDeg2Rad);}
    takeoff.setControls(c);takeoff.step(1./120);check(scenario::finite(takeoff.state()),"takeoff diagnostic finite");
    if(takeoff.instruments().agl>15.24){screen=takeoff.state().pos_ned.x;break;}
  }
  check(screen>rotation&&rotation>0,"all-engine takeoff reaches screen height");
  std::printf("DIAGNOSTIC A32064t all-engine runway to CG15.24m=%.1f m, rotation at %.1f m /78m/s; planning curve %.1f +/-%.1f m; numerical difference %.1f m, NOT equivalent certified conditions\n",screen,rotation,reference["takeoff"]["planning_runway_m"].number(),reference["takeoff"]["digitization_tolerance_m"].number(),screen-reference["takeoff"]["planning_runway_m"].number());
  cfg.initial_fuel=10000;cfg.mass=66000;
  TrimRequest r;r.altitude=15.24;r.tas=reference["approach"]["ias_knots"].number()*.514444444444;r.flap01=1;r.gear01=1;r.gamma=-3*kDeg2Rad;
  const auto trim=solveTrim(cfg,r);check(trim.converged,"landing diagnostic trim");
  Simulator landing(cfg,Simulator::GroundModel::FlatRunway);s=trim.state;s.fcs_enabled=false;landing.setState(s);c=trim.controls;landing.setControls(c);
  bool touched=false,stopped=false;double touch=0,sink=0;
  for(unsigned tick=0;tick<18000;++tick) {
    const auto n=landing.instruments();
    if(!touched) {c.elevator_stick=scenario::flightPathInput(landing,1,-.9);c.throttle[0]=c.throttle[1]=.25;}
    else {c.elevator_stick=-c.elevator_trim;c.brake01=1;c.spoiler01=1;c.throttle[0]=c.throttle[1]=0;}
    landing.setControls(c);landing.step(1./120);check(scenario::finite(landing.state()),"landing diagnostic finite");
    if(!touched&&scenario::gearLoad(landing)>1000){touched=true;touch=landing.state().pos_ned.x;sink=-n.vs;}
    if(touched&&landing.instruments().tas<.05){stopped=true;break;}
  }
  check(stopped,"landing diagnostic completes rollout");
  const double expected=reference["landing"]["field_length_m"].number()*reference["landing"]["field_factor"].number();
  std::printf("DIAGNOSTIC A32066t CG15.24m to stop=%.1f m, flare=%.1f, rollout=%.1f, sink=%.2f m/s; planning unfactored %.1f +/-%.1f m; difference %.1f m; test pilot/brakes/reverse assumptions are NOT certified\n",landing.state().pos_ned.x,touch,landing.state().pos_ned.x-touch,sink,expected,reference["landing"]["digitization_tolerance_m"].number()*.6,landing.state().pos_ned.x-expected);
  // Aerodynamic descent and all-engine excess-thrust mechanisms, no independent
  // manufacturer polar/climb curve was retrieved for these operating points.
  Simulator airframe(cfg);s=State{};s.fcs_enabled=false;s.pos_ned.z=-3000;c=Controls{};c.gear01=0;airframe.setControls(c);
  double best=0,bestAlpha=0;
  for(unsigned i=1;i<150;++i){const double alpha=i*.1*kDeg2Rad;s.vel_ned={150*std::cos(alpha),0,150*std::sin(alpha)};airframe.setState(s);const auto a=airframe.evalAero();if(a.cl>0&&a.cd>0&&a.cl/a.cd>best){best=a.cl/a.cd;bestAlpha=alpha;}}
  std::printf("DIAGNOSTIC clean glide at150m/s,3000m: best L/D=%.2f alpha=%.2fdeg, sink=%.2fm/s (engineering polar, no independent flight validation)\n",best,bestAlpha*kRad2Deg,150/std::sqrt(1+best*best));
  c.flap01=.4;s.pos_ned.z=-1000;s.vel_ned={100,0,10};s.n1[0]=s.n1[1]=1;airframe.setState(s);airframe.setControls(c);
  const auto aero=airframe.evalAero();const auto thrust=airframe.evalThrust();
  std::printf("DIAGNOSTIC 1000m/100m/s all-engine excess-thrust grade=%.3f corresponding climb=%.2fm/s; no engine-out or independently sourced climb claim\n",(thrust.force_body.norm()-aero.drag_body.norm())/(cfg.mass*kG0),100*(thrust.force_body.norm()-aero.drag_body.norm())/(cfg.mass*kG0));
}
void unsteady(const std::filesystem::path& root) {
  auto cfg=su57Config();cfg.fuel_flow_scale=0;
  State s;s.pos_ned.z=-5000;s.vel_ned={120*std::cos(45*kDeg2Rad),4,120*std::sin(45*kDeg2Rad)};s.fcs_enabled=false;s.n1[0]=s.n1[1]=.6;
  Simulator sim(cfg);check(sim.setState(s),"high-alpha raw state");Controls c;c.gear01=0;c.throttle[0]=c.throttle[1]=.6;sim.setControls(c);
  std::filesystem::create_directories(root/"output/aircraft-audit");std::ofstream trace(root/"output/aircraft-audit/su57-unsteady.csv");telemetryHeader(trace);
  for(unsigned tick=0;tick<2400;++tick) {
    c.elevator_stick=tick<240?.3:tick<480?-.3:0;c.rudder_pedal=tick<360?.2:0;sim.setControls(c);sim.step(1./120);
    check(scenario::finite(sim.state()),"high-alpha full trajectory finite");
    for(unsigned i=0;i<2;++i)check(sim.state().separation[i]>=0&&sim.state().separation[i]<=1&&sim.state().vortex_state[i]>=0&&sim.state().vortex_state[i]<=1,"unsteady states bounded");
    if(tick%12==0)telemetryRow(trace,sim);
  }
  // Hysteresis and sideslip response at prescribed incidence, independent of
  // closed-loop maneuvers. The evaluator never advances its supplied state.
  State lag;lag.vel_ned={100,0,0};initializeUnsteady(cfg,lag,lag.vel_ned);
  const Vec3 up{80,10,70};lag.alpha_lag[0]=lag.alpha_lag[1]=35*kDeg2Rad;const auto departure=unsteadyDerivative(cfg,lag,up);
  check(departure.alpha[0]>0&&departure.separation[0]>0,"separation grows on alpha increase");
  check(departure.alpha[0]!=departure.alpha[1],"sideslip creates asymmetric lag targets");
  lag.separation[0]=lag.separation[1]=1;
  lag.alpha_lag[0]=lag.alpha_lag[1]=0;const auto recovery=unsteadyDerivative(cfg,lag,{100,0,0});check(recovery.separation[0]<0,"reattachment hysteresis");
  close(recovery.separation[0],-1/cfg.unsteady_attach_tau,1e-10,"reattachment rate uses documented time constant");
  const auto snapshot=sim.state();const auto a=sim.evalAero(snapshot,c,{});const auto b=sim.evalAero(snapshot,c,{});
  check((a.force_body-b.force_body).norm()==0&&sim.state().separation[0]==snapshot.separation[0],"raw pure unsteady evaluation");
  for(auto method:{ContinuousIntegrator::SemiImplicitEuler,ContinuousIntegrator::RungeKutta4}) {
    Simulator uninterrupted(cfg);uninterrupted.setIntegrator(method);uninterrupted.setState(s);uninterrupted.setControls(c);
    for(unsigned i=0;i<120;++i)uninterrupted.step(1./120);
    Simulator restored(cfg);restored.setIntegrator(method);check(restored.setState(uninterrupted.state()),"accept integrated unsteady checkpoint");restored.setControls(uninterrupted.controls());
    for(unsigned i=0;i<240;++i){uninterrupted.step(1./120);restored.step(1./120);}
    check((uninterrupted.state().pos_ned-restored.state().pos_ned).norm()<1e-8 &&
      std::abs(uninterrupted.state().separation[0]-restored.state().separation[0])<1e-10 &&
      std::abs(uninterrupted.state().vortex_state[1]-restored.state().vortex_state[1])<1e-10,"unsteady snapshot deterministic continuation for both integrators");
  }
  // Prescribed local-flow transient has an analytic exponential solution; this
  // checks continuous state integration against independent mathematics.
  State memory;memory.fuel_mass=cfg.initial_fuel;memory.payload_mass=cfg.initial_payload;
  memory.aero_memory_initialized=true;memory.alpha_lag[0]=memory.alpha_lag[1]=0;
  memory.separation[0]=memory.separation[1]=1;
  for(auto method:{ContinuousIntegrator::SemiImplicitEuler,ContinuousIntegrator::RungeKutta4}) {
    std::array<double,3> error{};
    for(unsigned resolution=0;resolution<3;++resolution) {
      auto transient=memory;const unsigned steps=60u<<resolution;const double h=.5/steps;
      for(unsigned i=0;i<steps;++i)transient=integrateContinuous(transient,h,method,[&](const State& st){ContinuousDerivative d;d.aerodynamic_memory=unsteadyDerivative(cfg,st,{100,0,0});return d;});
      error[resolution]=std::abs(transient.separation[0]-std::exp(-.5/cfg.unsteady_attach_tau));
    }
    check(error[1]<error[0]&&error[2]<error[1],"unsteady integration converges to analytic reattachment");
    check(error[0]/error[1]>(method==ContinuousIntegrator::RungeKutta4?12.:1.8),"unsteady integration expected order");
  }
  auto invalid=snapshot;invalid.separation[0]=std::numeric_limits<double>::quiet_NaN();check(!sim.setState(invalid),"reject malformed aerodynamic memory");
  std::printf("Raw Su57 20s trace written; alpha=%.3f beta=%.3f rates=%.4f/%.4f/%.4f speed=%.3f altitude=%.3f separation=%.3f/%.3f (architecture regression, no Su57 maneuver validation)\n",sim.instruments().alpha_deg,sim.instruments().beta_deg,sim.state().omega_body.x,sim.state().omega_body.y,sim.state().omega_body.z,sim.instruments().tas,sim.instruments().alt_msl,sim.state().separation[0],sim.state().separation[1]);
}
}
int main(int argc,char** argv){try{check(argc==3,"suite and repository root");const std::string suite=argv[1];const std::filesystem::path root=argv[2];if(suite=="vectoring")vectoring();else if(suite=="geometry")geometry(root);else if(suite=="performance")performance(root);else if(suite=="performance_report")performanceReport(root);else if(suite=="unsteady")unsteady(root);else throw std::runtime_error("unknown suite");return scenario::assetResult();}catch(const std::exception& e){std::fprintf(stderr,"FAIL aircraft physics audit: %s\n",e.what());return 1;}}
