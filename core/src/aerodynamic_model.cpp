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
Simulator::AeroResult Simulator::evalAero() const { return evalAero(state_,controls_,weather_); }
Simulator::AeroResult Simulator::evalAero(const State& state_, const Controls& controls_, const Weather& weather_) const {
  AeroResult r;
  if (cfg_.wing_area == 0)
    return r;
  const auto air = isaAtAltitude(-state_.pos_ned.z, weather_.temp_offset_c);
  const auto mass = massProperties(state_);
  const Vec3 v = state_.att.inverseRotate(state_.vel_ned -
                                          windAt(state_.pos_ned, state_.time, weather_));
  const double V = v.norm();
  r.vtas = V;
  r.alpha = V > 1e-9 ? std::atan2(v.z, v.x) : 0;
  r.beta = V > 1e-9 ? std::atan2(v.y, std::hypot(v.x, v.z)) : 0;
  r.qbar = .5 * air.rho * V * V;
  const auto c = state_.actuators_initialized
                     ? actualControls(state_, controls_)
                     : controls_;
  const double elev = clamp(c.elevator_stick + c.elevator_trim, -1, 1);
  const double de = elev >= 0 ? elev * cfg_.elev_min : -elev * cfg_.elev_max;
  const bool delta=cfg_.control_law==FlightControlLaw::Delta;
  const bool allocated = (cfg_.control_law == FlightControlLaw::Canard || delta) &&
                         state_.actuators_initialized;
  const double roll =
      allocated ? (delta?-1:1)*.5 * (state_.elevon_l - state_.elevon_r) : c.aileron_stick;
  const double da = roll * cfg_.ail_max, dr = -c.rudder_pedal * cfg_.rud_max;
  if(cfg_.aero_kind==AeroModelKind::DataDriven) {
    if(!cfg_.aerodynamic_model)throw std::logic_error("Missing aerodynamic dataset");
    const double rateScale=V>1e-9?1/(2*V):0;
    AeroInputs input{r.alpha,r.beta,V/air.sound,state_.omega_body.x*cfg_.wing_span*rateScale,
      state_.omega_body.y*cfg_.mac*rateScale,state_.omega_body.z*cfg_.wing_span*rateScale,
      de,da*cfg_.table_aileron_sign,dr,0,c.flap01*cfg_.flap_max_deg*kDeg2Rad,c.spoiler01,c.gear01};
    const auto coeff=cfg_.aerodynamic_model->coefficients(input);
    const double scale=r.qbar*cfg_.wing_area;
    r.force_body={coeff.cx*scale,coeff.cy*scale,coeff.cz*scale};
    r.moment_body={coeff.cl*scale*cfg_.wing_span,coeff.cm*scale*cfg_.mac,coeff.cn*scale*cfg_.wing_span};
    // Dataset moments are about the configured loaded reference. Shift to current CG.
    r.moment_body+=(-mass.cg).cross(r.force_body);
    r.cm=coeff.cm;r.cl=-coeff.cz*std::cos(r.alpha)+coeff.cx*std::sin(r.alpha);
    r.cd=-coeff.cx*std::cos(r.alpha)-coeff.cz*std::sin(r.alpha);
    return r;
  }
  const bool airliner=cfg_.aero_kind==AeroModelKind::AirlinerEngineering;
  const auto highlift=a320HighLift(c.flap01);
  const double clmax = airliner?highlift.clmax:lerp(cfg_.cl_max_clean, cfg_.cl_max_full_flap, c.flap01);
  const double acrit = airliner?highlift.alpha_critical:lerp(cfg_.alpha_crit_clean, 12 * kDeg2Rad, c.flap01);
  const double flapLift=airliner?highlift.lift_increment:cfg_.flap_lift*c.flap01;
  const bool unsteady=cfg_.unsteady_alpha_tau>0&&state_.aero_memory_initialized;
  auto sectionSeparation=[&](double alpha,unsigned i) {
    return unsteady?state_.separation[i]:aerodynamics::separation(alpha,acrit);
  };
  auto lift = [&](double alpha, double elevator, int section=-1) {
    const double linear = cfg_.cl_alpha * (alpha - cfg_.alpha0) +
                          flapLift - .55 * c.spoiler01 + (airliner?0:.32 * elevator);
    double cl = std::min(linear, clmax);
    if (alpha > acrit)
      cl = lerp(cl, std::max(.75, clmax - 3.1 * (alpha - acrit)),
                smooth((alpha - acrit) / kDeg2Rad));
    if (alpha < -8 * kDeg2Rad) {
      const double under = -8 * kDeg2Rad - alpha;
      const double onset = cfg_.cl_alpha * (-8 * kDeg2Rad - cfg_.alpha0) +
                           flapLift - .55 * c.spoiler01 + (airliner?0:.32 * elevator);
      cl = lerp(linear, lerp(onset, -.6, 1 - std::exp(-under * 2)),
                smooth(under / kDeg2Rad));
    }
    cl += aerodynamics::vortexLift(alpha,V/air.sound,cfg_.vortex_lift);
    if(unsteady && section>=0) {
      const unsigned i=unsigned(section);
      const double equilibrium=aerodynamics::separation(alpha,acrit);
      cl+=(equilibrium-state_.separation[i])*.65*cl;
      const double equilibriumVortex=aerodynamics::vortexLift(alpha,0,1);
      const double vortex=std::copysign(state_.vortex_state[i],alpha);
      cl+=cfg_.vortex_lift*(vortex-equilibriumVortex)*clamp(1-(V/air.sound-.6)/1.2,0,1);
    }
    return lerp(cl, std::sin(2 * alpha),
                smooth((std::abs(alpha) - 25 * kDeg2Rad) / (35 * kDeg2Rad)));
  };
  const double mach = V / air.sound;
  // Bounded approximation, no Prandtl-Glauert singularity at Mach 1.
  // Swept transport compressibility remains bounded at transonic speeds.
  const double liftMach = airliner ? 1+.08*smooth(clamp((mach-.55)/.27,0,1)) : aerodynamics::liftMach(mach);
  const double effectiveness = aerodynamics::controlMach(mach) *
      (unsteady?1-.94*.5*(state_.separation[0]+state_.separation[1]):aerodynamics::controlFlow(r.alpha,acrit));
  const Vec3 wingPosition=state_.pos_ned+state_.att.rotate(cfg_.surfaces[0].position-mass.cg);
  const double height=std::max(0.,groundHeightAt(wingPosition.x,wingPosition.y)-wingPosition.z);
  // Continuous at ground and altitude; finite asymptote rather than a cutoff.
  r.ground_effect = 1 - .28 * std::exp(-height / (cfg_.wing_span * .18));
  const double induced =
      cfg_.wing_area / (kPi * cfg_.wing_span * cfg_.wing_span * cfg_.oswald_e);
  r.profile_cd = cfg_.cd0_clean;
  r.wave_cd = machCurve(mach, {{0, 0},
                               {cfg_.mach_drag_onset, 0},
                               {1.05, cfg_.mach_drag_peak},
                               {1.4, cfg_.mach_drag_supersonic},
                               {3, cfg_.mach_drag_supersonic * .7}});
  r.device_cd = airliner?highlift.drag_increment+.025*c.gear01+.060*c.spoiler01:
      .028 * c.flap01 + .016 * c.gear01 + .028 * c.spoiler01;
  r.device_cd += cfg_.payload_cd_per_kg * std::max(0., state_.payload_mass);
  const double cmStatic =
      cfg_.cm0 + cfg_.cm_alpha * std::sin(r.alpha) + (airliner?highlift.pitching_increment:-.1*c.flap01) +
      .02 * c.spoiler01 -
      (r.alpha > acrit ? .55 * std::sin(clamp((r.alpha - acrit) * 3, 0, 1.2))
                       : 0);
  const double canardDe =
      allocated ? elevatorDeflection(state_.canard, cfg_) : de;
  const double trailingDe =
      allocated ? .5 * (elevatorDeflection(state_.elevon_l, cfg_) +
                        elevatorDeflection(state_.elevon_r, cfg_))
                : de;
  const double controlCm = cfg_.cm_de * (delta?trailingDe:canardDe) * effectiveness;
  const double trailingCm = cfg_.cm_de * trailingDe * effectiveness;
  // Typhoon splits pitch authority across foreplanes and trailing surfaces.
  const bool canard = cfg_.control_law == FlightControlLaw::Canard;
  const double wingX = cfg_.surfaces[0].position.x;
  const double pitchShare = canard ? .45 : 1;
  const double trailingLift =
      canard && std::abs(wingX) > 1e-6
          ? (1 - pitchShare) * trailingCm * cfg_.mac / wingX
          : 0;
  const double referenceLift=(unsteady?.5*(lift(r.alpha,de,0)+lift(r.alpha,de,1)):lift(r.alpha,de))*liftMach;
  const double levconBase=cfg_.levcon_lift_share*referenceLift;
  const double levconX=.5*(cfg_.levcon_position[0].x+cfg_.levcon_position[1].x);
  const double levconControl=cfg_.levcon_control_share>0?
      cfg_.levcon_control_share*controlCm*cfg_.mac/(levconX-wingX):0;
  const double pitchCL = ((cmStatic + pitchShare * controlCm * (1-cfg_.levcon_control_share)) * cfg_.mac -
                          wingX * (referenceLift-levconBase) - levconX*levconBase) /
                         (cfg_.pitch_arm - wingX);
  const double refWingCL = referenceLift - pitchCL - levconBase - levconControl;
  const double b = cfg_.wing_span, chord = cfg_.mac;
  for (std::size_t i = 0; i < surfaceCount; ++i) {
    const auto &surface = cfg_.surfaces[i];
    const Vec3 arm = surface.position - mass.cg;
    const Vec3 local = v + state_.omega_body.cross(arm);
    const double speed = local.norm(), qb = .5 * air.rho * speed * speed;
    const double alpha = speed > 1e-9 ? std::atan2(local.z, local.x) : 0;
    const double beta =
        speed > 1e-9 ? std::atan2(local.y, std::hypot(local.x, local.z)) : 0;
    r.local_alpha[i] = alpha;
    r.local_beta[i] = beta;
    r.local_qbar[i] = qb;
    Vec3 force{};
    double cl = 0, cd = 0;
    if (speed > 1e-9) {
      const Vec3 direction = local / speed,
                 liftAxis{std::sin(alpha), 0, -std::cos(alpha)};
      if (surface.role == SurfaceRole::Wing) {
        cl = (refWingCL + lift(alpha, de,int(i)) * liftMach -
              (unsteady?.5*(lift(r.alpha,de,0)+lift(r.alpha,de,1)):lift(r.alpha,de)) * liftMach + trailingLift) *
             surface.area_fraction;
        cl +=
            (i == 0 ? 1 : -1) * cfg_.cl_da * da * effectiveness *
            aerodynamics::controlFlow(alpha,acrit)/std::max(.06,aerodynamics::controlFlow(r.alpha,acrit)) * b / (.44 * b);
        const double separation = sectionSeparation(alpha,unsigned(i));
        const double sectionCl = lift(alpha, de,int(i)) * liftMach;
        cd = surface.area_fraction *
             (r.profile_cd * (.75-cfg_.levcon_lift_share) + r.wave_cd + r.device_cd +
              induced * sectionCl * sectionCl * r.ground_effect +
              (1-cfg_.levcon_lift_share)*1.8 * std::sin(alpha) * std::sin(alpha) * separation);
        const Vec3 lf = liftAxis * (qb * cfg_.wing_area * cl),
                   df = -direction *
                        (qb * cfg_.wing_area * cd * state_.surface_drag[i]);
        force = lf + df;
        r.lift_body += lf * state_.surface_health[i];
        r.drag_body += df * state_.surface_health[i];
        // Wings behind the CG require their steady lift moment in the trim
        // balance.
      } else if (surface.role == SurfaceRole::Pitch) {
        // Local angular airflow adds damping; baseline load sharing preserves
        // configured engineering lift.
        cl = .5 * pitchCL +
             surface.area_fraction * cfg_.cl_alpha * std::sin(alpha - r.alpha) *
                 aerodynamics::controlFlow(alpha,acrit);
        const Vec3 lf = liftAxis * (qb * cfg_.wing_area * cl);
        force = lf;
        r.lift_body += lf * state_.surface_health[i];
      } else if (surface.role == SurfaceRole::Fin) {
        const double armX = surface.position.x;
        const double cn =
            (cfg_.cn_beta * std::sin(beta) + cfg_.cn_dr * dr * effectiveness) *
            (1-.85*aerodynamics::separation(r.alpha,acrit));
        const double side = armX != 0 ? b * cn / armX : 0;
        force = direction.cross(liftAxis) * (qb * cfg_.wing_area * side);
        r.side_body += force * state_.surface_health[i];
      } else {
        const double side = cfg_.cy_beta * std::sin(beta) + .12 * dr -
                            b *
                                (cfg_.cn_beta * std::sin(beta) +
                                 cfg_.cn_dr * dr * effectiveness) /
                                cfg_.surfaces[4].position.x;
        const Vec3 sf =
            direction.cross(liftAxis) * (qb * cfg_.wing_area * side);
        const Vec3 df = -direction * (qb * cfg_.wing_area * r.profile_cd * .25 *
                                      state_.surface_drag[i]);
        force = sf + df;
        r.side_body += sf * state_.surface_health[i];
        r.drag_body += df * state_.surface_health[i];
      }
    }
    force = force * state_.surface_health[i];
    r.surfaces[i].pos_body = arm;
    r.surfaces[i].force_body = force;
    std::strncpy(r.surfaces[i].name, surface.name, 31);
    r.force_body += force;
    r.moment_body += arm.cross(force);
  }
  if(cfg_.levcon_lift_share>0) for(unsigned i=0;i<2;++i) {
    const Vec3 arm=cfg_.levcon_position[i]-mass.cg;
    const Vec3 local=v+state_.omega_body.cross(arm);
    const double speed=local.norm(),alpha=speed>1e-9?std::atan2(local.z,local.x):0;
    const double qb=.5*air.rho*speed*speed;
    // Leading-edge controls share the finite pitch actuator. A shorter wing
    // incidence range and local flow reduce their authority at separation.
    const double localFlow=aerodynamics::controlFlow(alpha,acrit);
    const double referenceFlow=std::max(.06,aerodynamics::controlFlow(r.alpha,acrit));
    const double cl=.5*(cfg_.levcon_lift_share*lift(alpha,de)*liftMach + levconControl*localFlow/referenceFlow);
    const Vec3 lf=Vec3{std::sin(alpha),0,-std::cos(alpha)}*(qb*cfg_.wing_area*cl);
    const Vec3 df=speed>1e-9?-local/speed*(qb*cfg_.wing_area*cfg_.levcon_lift_share*.5*
       (cfg_.cd0_clean+1.8*std::sin(alpha)*std::sin(alpha)*aerodynamics::separation(alpha,acrit))):Vec3{};
    const Vec3 force=(lf+df)*state_.surface_health[i];
    r.levcons[i].pos_body=arm;r.levcons[i].force_body=force;
    std::snprintf(r.levcons[i].name,32,"levcon_%c",i==0?'L':'R');
    r.lift_body+=lf*state_.surface_health[i];r.drag_body+=df*state_.surface_health[i];
    r.force_body+=force;r.moment_body+=arm.cross(force);
  }
  // Residual whole-airframe stability derivatives: local force moments are
  // retained. Use only residual roll damping (wing local flow already supplies
  // most Cl_p).
  const double rateDen = 2 * std::max(V, 1.);
  const double phat = state_.omega_body.x * b / rateDen,
               qhat = state_.omega_body.y * chord / rateDen,
               rhat = state_.omega_body.z * b / rateDen;
  const double wingDamping = -2 * cfg_.cl_alpha * .22 * .22 * liftMach;
  r.moment_body.x += r.qbar * cfg_.wing_area * b *
                     (cfg_.cl_beta * std::sin(r.beta) +
                      (cfg_.cl_p - wingDamping) * (1-.9*aerodynamics::separation(r.alpha,acrit)) * phat + .025 * rhat);
  r.moment_body.y += r.qbar * cfg_.wing_area * chord * cfg_.cm_q * (1-.7*aerodynamics::separation(r.alpha,acrit)) * qhat;
  r.moment_body.z +=
      r.qbar * cfg_.wing_area * b * (cfg_.cn_r * (1-.8*aerodynamics::separation(r.alpha,acrit)) * rhat - .015 * phat);
  if (r.qbar > 1e-12) {
    r.cl = r.lift_body.norm() / (r.qbar * cfg_.wing_area) *
           (r.lift_body.z <= 0 ? 1 : -1);
    // Wind-axis projection gives signed aggregate CL even in reverse/vertical
    // flow.
    r.cl = r.lift_body.dot({std::sin(r.alpha), 0, -std::cos(r.alpha)}) /
           (r.qbar * cfg_.wing_area);
    r.cd = r.drag_body.norm() / (r.qbar * cfg_.wing_area);
    r.cm = r.moment_body.y / (r.qbar * cfg_.wing_area * chord);
    r.induced_cd = induced * r.cl * r.cl * r.ground_effect;
  }
  return r;
}


} // namespace ofs
