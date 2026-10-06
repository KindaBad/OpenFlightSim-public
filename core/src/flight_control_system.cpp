#include "ofs/simulator.hpp"
#include "ofs/aerodynamics.hpp"
#include "ofs/airliner.hpp"
#include "ofs/control_allocation.hpp"
#include "flight_model_detail.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace ofs {
using namespace detail;
void Simulator::primeActuators() {
  state_.actuators_initialized = true;
  state_.trim_reference = controls_.elevator_trim;
  state_.pilot_pitch = controls_.elevator_stick;
  state_.pilot_roll = controls_.aileron_stick;
  state_.pilot_yaw = controls_.rudder_pedal;
  state_.elevator =
      clamp(controls_.elevator_stick + controls_.elevator_trim, -1, 1);
  state_.aileron = controls_.aileron_stick;
  state_.rudder = controls_.rudder_pedal;
  state_.flap = controls_.flap01;
  state_.spoiler = controls_.spoiler01;
  const bool delta=cfg_.control_law==FlightControlLaw::Delta;
  state_.canard = delta?0:state_.elevator;
  state_.elevon_l = clamp(state_.elevator + (delta?-1:1)*state_.aileron, -1, 1);
  state_.elevon_r = clamp(state_.elevator - (delta?-1:1)*state_.aileron, -1, 1);
}
double Simulator::normalLoad() const {
  const auto a = evalAero();
  const auto t = evalThrust();
  return -(a.force_body.z + t.force_body.z) / (massProperties().mass * kG0);
}
double Simulator::equilibriumElevator() const {
  const auto a = evalAero();
  const auto t = evalThrust();
  const double authority = a.qbar * cfg_.wing_area * cfg_.mac *
                           std::abs(cfg_.cm_de * cfg_.elev_min);
  return authority > 1
             ? clamp(state_.elevator -
                         (a.moment_body.y + t.moment_body.y) / authority,
                     -1, 1)
             : controls_.elevator_trim;
}
Controls Simulator::controlTargets() const {
  Controls target = controls_;
  target.elevator_stick = state_.pilot_pitch;
  target.aileron_stick = state_.pilot_roll;
  target.rudder_pedal = state_.pilot_yaw;
  const auto air = airData();
  const Vec3 velocity = state_.att.inverseRotate(
      state_.vel_ned - windAt(state_.pos_ned, state_.time));
  const double V = velocity.norm(), qb = .5 * air.rho * V * V;
  // This heavy delta needs direct elevon authority against the main-gear
  // reaction during rotation. The airborne rate law otherwise cancels most
  // of the pitch-up command while wheels carry the weight. This is an
  // approximate ground mode, not a reconstruction of the SR-71 control law.
  bool groundRotation=false;
  if((cfg_.control_law==FlightControlLaw::Delta || cfg_.control_law==FlightControlLaw::Transport) && controls_.gear01>.5) {
    const auto cg=massProperties().cg;
    for(const Vec3 point:{cfg_.gear_nose,cfg_.gear_main_l,cfg_.gear_main_r}) {
      const auto position = state_.pos_ned+state_.att.rotate(point-cg);
      groundRotation |= position.z >= groundHeightAt(position.x, position.y);
    }
  }
  if (state_.fcs_enabled && !groundRotation && cfg_.control_law != FlightControlLaw::Direct &&
      V > 30 && qb > 50 && cfg_.wing_area > 0 && std::abs(cfg_.cm_de) > 1e-9 &&
      cfg_.cl_da > 1e-9) {
    const auto mass = massProperties();
    const double alpha = std::atan2(velocity.z, velocity.x);
    const double pitchAuthority = qb * cfg_.wing_area * cfg_.mac *
                                  std::abs(cfg_.cm_de * cfg_.elev_min);
    const double rollAuthority = qb * cfg_.wing_area * cfg_.wing_span *
                                 cfg_.cl_da * cfg_.ail_max;
    // Rate command plus aerodynamic damping. Trim remains a persistent bias.
    const double load = normalLoad();
    const double loadFeedback =
        (cfg_.control_law == FlightControlLaw::Transport ? .12 : .08) *
        (state_.att.inverseRotate({0, 0, 1}).z - load);
    double pitchCommand =
        (state_.pilot_pitch + controls_.elevator_trim - state_.trim_reference) *
            cfg_.max_pitch_rate +
        loadFeedback;
    double rollCommand=state_.pilot_roll*cfg_.max_roll_rate;
    if(cfg_.aero_kind==AeroModelKind::AirlinerEngineering) {
      // OpenFlightSim reconstruction, not the Airbus ELAC algorithm. Load
      // factor command, alpha command transition, bank and speed protections
      // all request actuator motion; none modifies the rigid-body state.
      double bank,pitch,heading;eulerFromQuat(state_.att,bank,pitch,heading);
      const double neutralLoad=std::cos(pitch)/std::max(.5,std::cos(bank));
      const double maxLoad=state_.flap>.2?2.0:cfg_.g_positive;
      const double minLoad=state_.flap>.2?0.0:cfg_.g_negative;
      const double desired=neutralLoad+state_.pilot_pitch*(state_.pilot_pitch>=0?maxLoad-neutralLoad:neutralLoad-minLoad);
      pitchCommand=.18*(clamp(desired,minLoad,maxLoad)-load);
      const auto configuration=a320HighLift(state_.flap);
      const double alphaProt=configuration.alpha_critical-4*kDeg2Rad;
      if(alpha>alphaProt) {
        const double alphaTarget=alphaProt+std::max(0.,state_.pilot_pitch)*2*kDeg2Rad;
        pitchCommand=1.2*(alphaTarget-alpha);
      }
      const double excessMach=V/air.sound-.82;
      const double excessCas=instruments().cas-350*.514444444444;
      if(excessMach>0||excessCas>0)
        pitchCommand=std::max(pitchCommand,std::min(.15,2*std::max(0.,excessMach)+.006*std::max(0.,excessCas)));
      if(std::abs(bank)>33*kDeg2Rad && std::abs(state_.pilot_roll)<.01)
        rollCommand=-.8*(bank-std::copysign(33*kDeg2Rad,bank));
      if(bank*rollCommand>0)
        rollCommand*=clamp((67*kDeg2Rad-std::abs(bank))/(10*kDeg2Rad),0,1);
    }
    target.elevator_stick = equilibriumElevator() +
                            (pitchCommand - state_.omega_body.y) *
                                mass.inertia.y /
                                (cfg_.response_time * pitchAuthority) -
                            target.elevator_trim;
    target.aileron_stick =
        (rollCommand - state_.omega_body.x) *
        mass.inertia.x / (cfg_.response_time * rollAuthority);
    // AoA protection and load command soft limits through surfaces, never rate
    // clamps.
    if (alpha > cfg_.alpha_limit && target.elevator_stick > 0)
      target.elevator_stick -= 3 * (alpha - cfg_.alpha_limit);
    if (load > cfg_.g_positive && target.elevator_stick > 0)
      target.elevator_stick -= .25 * (load - cfg_.g_positive);
    if (load < cfg_.g_negative && target.elevator_stick < 0)
      target.elevator_stick += .25 * (cfg_.g_negative - load);
    target.rudder_pedal -= .12 * state_.omega_body.z;
  }
  target.elevator_stick =
      clamp(target.elevator_stick + target.elevator_trim, -1, 1);
  target.elevator_trim = 0;
  target.aileron_stick = clamp(target.aileron_stick, -1, 1);
  target.rudder_pedal = clamp(target.rudder_pedal, -1, 1);
  return target;
}

Simulator::ControlAllocation Simulator::vectorFighterAllocation(double horizon_seconds) const {
  if(!std::isfinite(horizon_seconds)||horizon_seconds<=0)throw std::invalid_argument("Allocation horizon must be positive");
  ControlAllocation out{controls_,{0,0}};
  out.surfaces.elevator_stick=clamp(state_.pilot_pitch+controls_.elevator_trim,-1,1);
  out.surfaces.elevator_trim=0;
  out.surfaces.aileron_stick=state_.pilot_roll;
  out.surfaces.rudder_pedal=state_.pilot_yaw;
  bool ground=false;
  const auto mass=massProperties();
  if(controls_.gear01>.5) for(const auto point:{cfg_.gear_nose,cfg_.gear_main_l,cfg_.gear_main_r}) {
    const auto position = state_.pos_ned+state_.att.rotate(point-mass.cg);
    ground |= position.z >= groundHeightAt(position.x, position.y);
  }
  if(!state_.fcs_enabled || ground || cfg_.wing_area<=0) return out;
  const auto aero=evalAero();
  const auto thrust=evalThrust();
  const double mach=aero.vtas/airData().sound;
  const double eff=aerodynamics::controlMach(mach)*
      aerodynamics::controlFlow(aero.alpha,cfg_.alpha_crit_clean);
  const double load=-(aero.force_body.z+thrust.force_body.z)/(mass.mass*kG0);
  // Gameplay engineering law: retain rate feedback and physical allocation,
  // blending back to normal protection as airspeed rises. Gear-down inhibits it.
  const double maneuver = controls_.maneuver_mode && controls_.gear01 < .5
      ? clamp((300.-aero.vtas)/90.,0.,1.) : 0.;
  const double pitchRate = cfg_.max_pitch_rate*(1.+1.8*maneuver);
  const double rollRate = cfg_.max_roll_rate*(1.+.35*maneuver);
  const double responseTime = cfg_.response_time*(1.-.35*maneuver);
  const double alphaLimit = cfg_.alpha_limit + maneuver*std::max(0.,80*kDeg2Rad-cfg_.alpha_limit);
  double pitchCommand=(state_.pilot_pitch+controls_.elevator_trim-state_.trim_reference)*pitchRate;
  // Soft protections change the requested response; no attitude/rate clipping.
  if(pitchCommand>0) {
    pitchCommand-=.30*std::max(0.,load-cfg_.g_positive);
    pitchCommand-=2.5*std::max(0.,aero.alpha-alphaLimit);
  } else if(pitchCommand<0) pitchCommand+=.30*std::max(0.,cfg_.g_negative-load);
  double roll,pitch,yaw;eulerFromQuat(state_.att,roll,pitch,yaw);
  const double coordinatedR=load*kG0*std::sin(roll)*std::cos(pitch)/std::max(60.,aero.vtas);
  const Vec3 commanded{state_.pilot_roll*rollRate,pitchCommand,
                       state_.pilot_yaw*.45+coordinatedR+.65*aero.beta};
  const Vec3 desired=(commanded-state_.omega_body)/responseTime;
  const Vec3 gyroscopic=state_.omega_body.cross(inertiaMomentum(mass,state_.omega_body));
  const Vec3 current=inverseInertia(mass,aero.moment_body+thrust.moment_body-gyroscopic);
  const Vec3 residual=desired-current;
  const double scale=aero.qbar*cfg_.wing_area*eff;
  std::array<Vec3,5> columns{{
    inverseInertia(mass,{0,scale*cfg_.mac*std::abs(cfg_.cm_de*cfg_.elev_min),0}),
    inverseInertia(mass,{scale*cfg_.wing_span*cfg_.cl_da*cfg_.ail_max,0,0}),
    inverseInertia(mass,{0,0,scale*cfg_.wing_span*std::abs(cfg_.cn_dr)*cfg_.rud_max}),{},{}
  }};
  // Independent canted nozzle columns come from the actual force Jacobian:
  // dM/dtheta = (engine-CG) cross (hinge_axis cross F).
  for(unsigned e=0;e<2;++e) {
    const auto& engine=cfg_.engines[e];
    Vec3 momentDerivative=(thrust.position[e]-mass.cg).cross(engine.vector_axis.cross(thrust.force[e]));
    if(engine.articulated_nozzle)
      momentDerivative+=engine.vector_axis.cross(thrust.position[e]-engine.nozzle_pivot).cross(thrust.force[e]);
    columns[3+e]=inverseInertia(mass,momentDerivative)*engine.vector_limit;
  }
  // Weighted minimum-motion allocator. Dynamic pressure makes surfaces cheap
  // at speed; separation and low pressure progressively favour vector thrust.
  const double vectorPreference=.02+.98/(1+std::pow(aero.qbar/7000,2))+maneuver*(.65+1.-eff);
  const std::array<double,5> weights{1,1,1,vectorPreference,vectorPreference};
  const std::array<double,5> currentPosition{state_.elevator,state_.aileron,state_.rudder,
    cfg_.engines[0].vector_limit>0?state_.nozzle_angle[0]/cfg_.engines[0].vector_limit:0,
    cfg_.engines[1].vector_limit>0?state_.nozzle_angle[1]/cfg_.engines[1].vector_limit:0};
  std::array<double,5> lower{},upper{};
  for(unsigned i=0;i<5;++i) {
    double travel=cfg_.actuator_rate*horizon_seconds;
    if(i>=3) {
      const auto& engine=cfg_.engines[i-3];
      travel=engine.vector_limit>0?engine.vector_rate*horizon_seconds/engine.vector_limit:0;
    }
    lower[i]=std::max(-1-currentPosition[i],-travel);
    upper[i]=std::min(1-currentPosition[i],travel);
  }
  const auto delta=boundedControlAllocation(columns,residual,weights,lower,upper);
  out.surfaces.elevator_stick=currentPosition[0]+delta[0];
  out.surfaces.aileron_stick=currentPosition[1]+delta[1];
  out.surfaces.rudder_pedal=currentPosition[2]+delta[2];
  for(unsigned e=0;e<2;++e)
    out.nozzle[e]=(currentPosition[3+e]+delta[3+e])*cfg_.engines[e].vector_limit;
  return out;
}


} // namespace ofs
