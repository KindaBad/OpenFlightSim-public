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
      groundRotation |= clearanceAt(position) <= 0;
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
        (state_.att.inverseRotate(world().down(state_.pos_ned)).z - load);
    // Typhoon gameplay engineering law, sharing the Su-57 airspeed schedule
    // and gear-down inhibit. Without vectoring nozzles it only asks more of
    // the foreplanes and elevons, so its gains and AoA extension are smaller.
    const double maneuver = cfg_.control_law == FlightControlLaw::Canard &&
                            controls_.maneuver_mode && controls_.gear01 < .5
                                ? clamp((300. - V) / 90., 0., 1.) : 0.;
    const double responseTime = cfg_.response_time * (1. - .30 * maneuver);
    const double alphaLimit = cfg_.alpha_limit +
        maneuver * std::max(0., 50 * kDeg2Rad - cfg_.alpha_limit);
    double pitchCommand =
        (state_.pilot_pitch + controls_.elevator_trim - state_.trim_reference) *
            cfg_.max_pitch_rate * (1. + 1.2 * maneuver) +
        loadFeedback;
    // The canard law commands load as well as rate: past corner speed a full
    // stick asks only for the pitch rate that, with the share of gravity the
    // wing already carries, makes the g limit. Left to the soft limit below,
    // a full pull or push overshot to 14 g and -15 g and hunted round it.
    const double gravityLoad = state_.att.inverseRotate(world().down(state_.pos_ned)).z;
    // Load past a limit eases the rate asked for rather than the surfaces
    // directly: at 300 m/s a tenth of the travel is four g.
    const bool loadLaw = cfg_.control_law == FlightControlLaw::Canard;
    if (loadLaw) {
      // Manoeuvre mode flies past the wing's best lift on purpose, where the
      // rate no longer makes the load, so it lifts the cap with the rate.
      const double margin = 1.2 * maneuver * cfg_.max_pitch_rate;
      pitchCommand = clamp(pitchCommand, (cfg_.g_negative - gravityLoad) * kG0 / V - margin,
                           (cfg_.g_positive - gravityLoad) * kG0 / V + margin);
      if (load > cfg_.g_positive && pitchCommand > 0)
        pitchCommand = std::max(0., pitchCommand - .6 * (load - cfg_.g_positive) * kG0 / V);
      if (load < cfg_.g_negative && pitchCommand < 0)
        pitchCommand = std::min(0., pitchCommand + .6 * (cfg_.g_negative - load) * kG0 / V);
    }
    // A heavy transport pushes over to weightlessness at most, the zero-g
    // arc airliners are flown along for training; its -1 g limit is a
    // structural margin, not a manoeuvre. Held, the push only steepens the
    // dive towards vertical: the yoke alone carried the 747 through an
    // outside loop at -1 g and 700 kt.
    if (cfg_.control_law == FlightControlLaw::Transport) {
      const double pushLimit = std::max(0., cfg_.g_negative);
      pitchCommand = std::max(pitchCommand, (pushLimit - gravityLoad) * kG0 / V);
      if (load < pushLimit && pitchCommand < 0)
        pitchCommand = std::min(0., pitchCommand + .6 * (pushLimit - load) * kG0 / V);
    }
    double rollCommand=state_.pilot_roll*cfg_.max_roll_rate*(1.+.25*maneuver);
    if(cfg_.aero_kind==AeroModelKind::AirlinerEngineering) {
      // OpenFlightSim reconstruction, not the Airbus ELAC algorithm. Load
      // factor command, alpha command transition, bank and speed protections
      // all request actuator motion; none modifies the rigid-body state.
      double bank,pitch,heading;eulerFromQuat(world().toLocal(state_.pos_ned,state_.att),bank,pitch,heading);
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
                                (responseTime * pitchAuthority) -
                            target.elevator_trim;
    target.aileron_stick =
        (rollCommand - state_.omega_body.x) *
        mass.inertia.x / (responseTime * rollAuthority);
    // AoA protection and load command soft limits through surfaces, never rate
    // clamps.
    // The canard law holds its limit firmly, so a stick held back turns at
    // the wing's best lift instead of mushing past it and bleeding to a stall.
    if (alpha > alphaLimit && target.elevator_stick > 0)
      target.elevator_stick -= (cfg_.control_law == FlightControlLaw::Canard ? 14 : 3) * (alpha - alphaLimit);
    // Pushed, the wing stalls upside down well before it does the right way
    // up, and a stalled push only mushes. Hold the angle short of it.
    const double negativeAlphaLimit = -.45 * cfg_.alpha_limit;
    if (alpha < negativeAlphaLimit && target.elevator_stick < 0)
      target.elevator_stick += (cfg_.control_law == FlightControlLaw::Canard ? 14 : 3) * (negativeAlphaLimit - alpha);
    if (!loadLaw && load > cfg_.g_positive && target.elevator_stick > 0)
      target.elevator_stick -= .25 * (load - cfg_.g_positive);
    if (!loadLaw && load < cfg_.g_negative && target.elevator_stick < 0)
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
    ground |= clearanceAt(position) <= 0;
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
  const double alphaLimit = cfg_.alpha_limit + maneuver*std::max(0.,110*kDeg2Rad-cfg_.alpha_limit);
  double pitchCommand=(state_.pilot_pitch+controls_.elevator_trim-state_.trim_reference)*pitchRate;
  // Soft protections change the requested response; no attitude/rate clipping.
  if(pitchCommand>0) {
    pitchCommand-=.30*std::max(0.,load-cfg_.g_positive);
    pitchCommand-=2.5*std::max(0.,aero.alpha-alphaLimit);
  } else if(pitchCommand<0) pitchCommand+=.30*std::max(0.,cfg_.g_negative-load);
  // Past the normal limit the nose is only held up by the stick. Let go and
  // the law brings it back down by the shorter way round, which is what ends
  // a Cobra; hold it and the aircraft goes on over.
  if(maneuver>0) {
    // atan2 wraps alpha at +-180 deg, which is where a Cobra goes over the top.
    // Unwrapped, the brake stays nose-down from above the limit up to -90 deg,
    // so the command does not flip when alpha crosses the wrap.
    const double unwrapped=aero.alpha<0?aero.alpha+2*kPi:aero.alpha;
    const double lowerEdge=2*kPi-.5*cfg_.alpha_limit;
    const double excess=unwrapped>cfg_.alpha_limit&&unwrapped<1.5*kPi?unwrapped-cfg_.alpha_limit:
        unwrapped>=1.5*kPi&&unwrapped<lowerEdge?unwrapped-lowerEdge:0;
    const double released=1-clamp(std::abs(state_.pilot_pitch)/.25,0.,1.);
    pitchCommand-=maneuver*released*clamp(2.2*excess,-pitchRate,pitchRate);
  }
  double roll,pitch,yaw;eulerFromQuat(world().toLocal(state_.pos_ned,state_.att),roll,pitch,yaw);
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
  // Nozzles are pulled back to neutral when the moment is already balanced; the
  // surfaces are not, since their deflection carries the aerodynamic trim.
  const std::array<double,5> neutral{0,0,0,-currentPosition[3],-currentPosition[4]};
  const auto delta=boundedControlAllocation(columns,residual,weights,lower,upper,.0025,neutral);
  out.surfaces.elevator_stick=currentPosition[0]+delta[0];
  out.surfaces.aileron_stick=currentPosition[1]+delta[1];
  out.surfaces.rudder_pedal=currentPosition[2]+delta[2];
  for(unsigned e=0;e<2;++e)
    out.nozzle[e]=(currentPosition[3+e]+delta[3+e])*cfg_.engines[e].vector_limit;
  return out;
}


} // namespace ofs
