#include "ofs/simulator.hpp"
#include "ofs/water.hpp"
#include "ofs/aerodynamics.hpp"
#include "ofs/airliner.hpp"
#include "ofs/control_allocation.hpp"
#include "flight_model_detail.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ofs {
using namespace detail;
namespace {

// Boundary-only policy: malformed external commands become neutral inputs.
double sanitizedInput(double value,double lo,double hi) {
  return std::isfinite(value)?clamp(value,lo,hi):0;
}

} // namespace

Simulator::Simulator(const AircraftConfig &cfg, GroundModel ground) : cfg_(cfg), ground_model_(ground) {
  if (!cfg_.surfaces_configured)
    configureSurfaces(cfg_);
  if (!cfg_.engines_configured) {
    for (unsigned e = 0; e < 2; ++e)
      cfg_.engines[e] = {e == 0 ? cfg_.engine_pos_l : cfg_.engine_pos_r,
                         {1, 0, 0},
                         cfg_.thrust_sl_static_each,
                         cfg_.afterburner_thrust_each,
                         cfg_.engine_tau,
                         cfg_.dry_tsfc,
                         cfg_.reheat_tsfc};
    cfg_.engines_configured = true;
  }
  if (cfg.engine_count < 1 || cfg.engine_count > 2)
    throw std::invalid_argument("engine_count must be 1 or 2");
  auto positive = [](double x) { return std::isfinite(x) && x > 0; };
  if(!std::isfinite(cfg.levcon_lift_share) || cfg.levcon_lift_share<0 || cfg.levcon_lift_share>.20 ||
     !std::isfinite(cfg.levcon_control_share) || cfg.levcon_control_share<0 || cfg.levcon_control_share>1 ||
     !positive(cfg.levcon_max) || !std::isfinite(cfg.levcon_position[0].norm2()) ||
     !std::isfinite(cfg.levcon_position[1].norm2()) ||
     (cfg.levcon_control_share>0 && (cfg.levcon_lift_share==0 ||
       std::abs(.5*(cfg.levcon_position[0].x+cfg.levcon_position[1].x)-cfg_.surfaces[0].position.x)<.1)))
    throw std::invalid_argument("Invalid LEVCON geometry or allocation");
  if (!positive(cfg.mass) || !positive(cfg.ixx) || !positive(cfg.iyy) ||
      !positive(cfg.izz) || !positive(cfg.mac) || !positive(cfg.wing_span) ||
      !positive(cfg.oswald_e) || !std::isfinite(cfg.wing_area) ||
      cfg.wing_area < 0)
    throw std::invalid_argument(
        "Invalid aircraft mass, inertia or reference geometry");
  for (double value : {cfg.alpha0,
                       cfg.cl_alpha,
                       cfg.flap_lift,
                       cfg.cl_max_clean,
                       cfg.cl_max_full_flap,
                       cfg.alpha_crit_clean,
                       cfg.cd0_clean,
                       cfg.cm0,
                       cfg.cm_alpha,
                       cfg.cm_de,
                       cfg.cm_q,
                       cfg.cl_beta,
                       cfg.cl_p,
                       cfg.cl_da,
                       cfg.cn_beta,
                       cfg.cn_r,
                       cfg.cn_dr,
                       cfg.cy_beta,
                       cfg.thrust_sl_static_each,
                       cfg.engine_tau,
                       cfg.afterburner_thrust_each,
                       cfg.afterburner_threshold,
                       cfg.oleo_stroke,
                       cfg.oleo_k,
                       cfg.oleo_c,
                       cfg.mu_brake_max,
                       cfg.mu_side,
                       cfg.mu_roll,
                       cfg.elev_max,
                       cfg.elev_min,
                       cfg.ail_max,
                       cfg.rud_max,
                       cfg.flap_max_deg,
                       cfg.engine_pos_l.x,
                       cfg.engine_pos_l.y,
                       cfg.engine_pos_l.z,
                       cfg.engine_pos_r.x,
                       cfg.engine_pos_r.y,
                       cfg.engine_pos_r.z,
                       cfg.gear_nose.x,
                       cfg.gear_nose.y,
                       cfg.gear_nose.z,
                       cfg.gear_main_l.x,
                       cfg.gear_main_l.y,
                       cfg.gear_main_l.z,
                       cfg.gear_main_r.x,
                       cfg.gear_main_r.y,
                       cfg.gear_main_r.z})
    if (!std::isfinite(value))
      throw std::invalid_argument("Nonfinite aircraft coefficient or geometry");
  if (cfg.flap_lift<0 || cfg.cl_max_clean <= 0 || cfg.cl_max_full_flap <= 0 ||
      cfg.thrust_sl_static_each < 0 || cfg.engine_tau <= 0 ||
      cfg.oleo_stroke <= 0 || cfg.oleo_k < 0 || cfg.oleo_c < 0 ||
      cfg.mu_brake_max < 0 || cfg.mu_side < 0 || cfg.mu_roll < 0 ||
      cfg.elev_max <= 0 || cfg.elev_min >= 0)
    throw std::invalid_argument(
        "Invalid aircraft limits or contact parameters");
  for(const auto& point:cfg.belly_contacts)
    if(!std::isfinite(point.norm2()))throw std::invalid_argument("Nonfinite belly contact");
  for (auto &engine : cfg_.engines) {
    if (!std::isfinite(engine.position.norm2()) || !std::isfinite(engine.nozzle_pivot.norm2()) ||
        !std::isfinite(engine.direction.norm2()) ||
        engine.direction.norm() < 1e-6 || !std::isfinite(engine.dry_thrust) ||
        !std::isfinite(engine.reheat_thrust) || engine.dry_thrust < 0 ||
        engine.reheat_thrust < 0 || !std::isfinite(engine.vector_axis.norm2()) ||
        engine.vector_axis.norm()<1e-6 || !std::isfinite(engine.vector_limit) ||
        engine.vector_limit<0 || engine.vector_limit>30*kDeg2Rad || !positive(engine.vector_rate) ||
        !positive(engine.spool_seconds) ||
        !positive(engine.dry_tsfc) || !positive(engine.reheat_tsfc))
      throw std::invalid_argument("Invalid engine component");
    engine.direction = engine.direction.normalized();
    engine.vector_axis = engine.vector_axis.normalized();
  }
  for (double value : {cfg_.ixz, cfg_.ixy, cfg_.iyz, cfg_.table_aileron_sign,
                       cfg_.initial_fuel,
                       cfg_.initial_payload,
                       cfg_.empty_mass,
                       cfg_.fuel_flow_scale,
                       cfg_.payload_cd_per_kg,
                       cfg_.fuel_position.x,
                       cfg_.fuel_position.y,
                       cfg_.fuel_position.z,
                       cfg_.payload_position.x,
                       cfg_.payload_position.y,
                       cfg_.payload_position.z,
                       cfg_.pitch_arm,
                       cfg_.pitch_span,
                       cfg_.mach_drag_onset,
                       cfg_.mach_drag_peak,
                       cfg_.mach_drag_supersonic,
                       cfg_.max_pitch_rate,
                       cfg_.max_roll_rate,
                       cfg_.response_time,
                       cfg_.g_positive,
                       cfg_.g_negative,
                       cfg_.alpha_limit,
                       cfg_.actuator_rate,
                       cfg_.flap_rate,
                       cfg_.spoiler_rate,
                       cfg_.control_q_limit,
                       cfg_.input_deadzone,
                       cfg_.input_exponent,
                       cfg_.input_tau,
                       cfg_.thrust_density_exponent,
                       cfg_.thrust_ram_gain, cfg_.vortex_lift})
    if (!std::isfinite(value))
      throw std::invalid_argument("Nonfinite advanced configuration");
  if(!std::isfinite(cfg_.fuel_capacity) || cfg_.fuel_capacity<0 ||
     (cfg_.fuel_capacity>0 && cfg_.initial_fuel>cfg_.fuel_capacity) ||
     !std::isfinite(cfg_.payload_inertia_per_kg.norm2()) ||
     cfg_.payload_inertia_per_kg.x<0 || cfg_.payload_inertia_per_kg.y<0 || cfg_.payload_inertia_per_kg.z<0 ||
     !std::isfinite(cfg_.fuel_inertia_per_kg.norm2()) ||
     cfg_.fuel_inertia_per_kg.x<0 || cfg_.fuel_inertia_per_kg.y<0 || cfg_.fuel_inertia_per_kg.z<0 ||
     !positive(cfg_.inlet_spike_rate))
    throw std::invalid_argument("Invalid distributed fuel or inlet configuration");
  if (cfg_.initial_fuel < 0 || cfg_.initial_payload < 0 ||
      cfg_.empty_mass < 0 || cfg_.fuel_flow_scale < 0 ||
      cfg_.payload_cd_per_kg < 0 || cfg_.vortex_lift < 0 ||
      (cfg_.empty_mass == 0 &&
       cfg_.mass <= cfg_.initial_fuel + cfg_.initial_payload) ||
      cfg_.input_deadzone < 0 || cfg_.input_deadzone >= 1 ||
      !positive(cfg_.input_exponent) || !positive(cfg_.input_tau) ||
      !positive(cfg_.response_time) || !positive(cfg_.control_q_limit) ||
      !positive(cfg_.actuator_rate) || !positive(cfg_.flap_rate) ||
      !positive(cfg_.spoiler_rate) || !positive(cfg_.max_pitch_rate) ||
      !positive(cfg_.max_roll_rate) || cfg_.mach_drag_onset <= 0 ||
      cfg_.mach_drag_onset >= 1.05 || cfg_.mach_drag_peak < 0 ||
      cfg_.mach_drag_supersonic < 0 || std::abs(cfg_.pitch_arm) < .1)
    throw std::invalid_argument("Invalid advanced configuration limits");
  for (const auto &surface : cfg_.surfaces)
    if (!surface.name || !std::isfinite(surface.position.norm2()) ||
        !positive(surface.area_fraction))
      throw std::invalid_argument("Invalid aerodynamic surface");
  if(!std::isfinite(cfg_.unsteady_alpha_tau)||cfg_.unsteady_alpha_tau<0 ||
     !positive(cfg_.unsteady_detach_tau)||!positive(cfg_.unsteady_attach_tau)||!positive(cfg_.unsteady_vortex_tau)||
     !std::isfinite(cfg_.unsteady_alpha_dot_gain)||cfg_.unsteady_alpha_dot_gain<0||
     !std::isfinite(cfg_.poststall_pitch_break)||cfg_.poststall_pitch_break<0||cfg_.poststall_pitch_break>1||
     !std::isfinite(cfg_.unsteady_beta_gain)||cfg_.unsteady_beta_gain<0)
    throw std::invalid_argument("Invalid unsteady aerodynamic parameters");
  State empty = state_;
  empty.fuel_mass = 0;
  empty.payload_mass = 0;
  state_ = empty;
  const auto basicInertia = massProperties();
  if (!basicInertia.tensor().positiveDefinite())
    throw std::invalid_argument(
        "Mass distribution must have positive definite basic inertia");
  state_ = {};
  state_.fuel_mass = cfg_.initial_fuel;
  state_.payload_mass = cfg_.initial_payload;
  if (cfg.afterburner_thrust_each < 0 ||
      (cfg.afterburner_thrust_each > 0 &&
       (cfg.afterburner_thrust_each < cfg.thrust_sl_static_each ||
        cfg.afterburner_threshold <= 0 || cfg.afterburner_threshold >= 1)))
    throw std::invalid_argument("Invalid afterburner regime");
}
bool Simulator::setState(const State &s) {
  auto finite = [](const Vec3 &v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  };
  const double qnorm = s.att.w * s.att.w + s.att.x * s.att.x +
                       s.att.y * s.att.y + s.att.z * s.att.z;
  if (!finite(s.pos_ned) || !finite(s.vel_ned) || !finite(s.omega_body) ||
      !std::isfinite(qnorm) || qnorm < 1e-24 || !std::isfinite(s.time) ||
      !std::isfinite(s.n1[0]) || !std::isfinite(s.n1[1]) ||
      !std::isfinite(s.afterburner[0]) || !std::isfinite(s.afterburner[1]))
    return false;
  for (double value :
       {s.fuel_mass, s.payload_mass, s.payload_offset.x, s.payload_offset.y,
        s.payload_offset.z, s.trim_reference, s.pilot_pitch, s.pilot_roll,
        s.pilot_yaw, s.elevator, s.aileron, s.rudder, s.flap, s.spoiler,
        s.canard, s.elevon_l, s.elevon_r, s.engine_health[0],
        s.engine_health[1],s.inlet_spike[0],s.inlet_spike[1],s.nozzle_angle[0],s.nozzle_angle[1]})
    if (!std::isfinite(value))
      return false;
  for(unsigned i=0;i<2;++i)
    if(!std::isfinite(s.alpha_lag[i]) || std::abs(s.alpha_lag[i])>kPi ||
       !std::isfinite(s.separation[i]) || s.separation[i]<0 || s.separation[i]>1 ||
       !std::isfinite(s.vortex_state[i]) || s.vortex_state[i]<0 || s.vortex_state[i]>1)return false;
  for(double inlet:s.inlet_spike) if(inlet<0 || inlet>1) return false;
  for(unsigned e=0;e<2;++e) if(std::abs(s.nozzle_angle[e])>cfg_.engines[e].vector_limit+1e-12) return false;
  for (auto v : s.surface_health)
    if (!std::isfinite(v) || v < 0 || v > 1)
      return false;
  for (auto v : s.surface_drag)
    if (!std::isfinite(v) || v < 0 || v > 10)
      return false;
  if (s.fuel_mass < -1 || s.payload_mass < -1 || s.fuel_mass > 1e6 ||
      s.payload_mass > 1e6 || s.payload_offset.norm() > 20)
    return false;
  for (double v :
       {s.payload_inertia_correction.x, s.payload_inertia_correction.y,
        s.payload_inertia_correction.z, s.payload_products_correction.x,
        s.payload_products_correction.y, s.payload_products_correction.z})
    if (!std::isfinite(v) || std::abs(v) > 1e7)
      return false;
  if (!massProperties(s).tensor().positiveDefinite())
    return false;
  if(cfg_.fuel_capacity>0 && s.fuel_mass>cfg_.fuel_capacity) return false;
  for (double value : {s.trim_reference, s.pilot_pitch, s.pilot_roll,
                       s.pilot_yaw, s.canard, s.elevon_l, s.elevon_r})
    if (std::abs(value) > 1)
      return false;
  const auto candidateMass=massProperties(s);
  if(!std::isfinite(candidateMass.mass)||candidateMass.mass<=0 ||
     !std::isfinite(candidateMass.cg.norm2()) || !candidateMass.tensor().positiveDefinite())return false;
  state_ = s;
  ground_impact_ = {};
  state_.att = state_.att.normalized();
  for (auto &health : state_.engine_health)
    health = clamp(health, 0, 1);
  state_.elevator = clamp(state_.elevator, -1, 1);
  state_.aileron = clamp(state_.aileron, -1, 1);
  state_.rudder = clamp(state_.rudder, -1, 1);
  state_.flap = clamp(state_.flap, 0, 1);
  state_.spoiler = clamp(state_.spoiler, 0, 1);
  if(cfg_.flap_max_deg==0) state_.flap=0;
  if(cfg_.control_law==FlightControlLaw::Delta) state_.spoiler=0;
  state_.n1[0] = clamp(s.n1[0], 0, 1);
  state_.n1[1] = clamp(s.n1[1], 0, 1);
  for (unsigned e = 0; e < 2; ++e)
    state_.afterburner[e] =
        cfg_.engines[e].reheat_thrust > cfg_.engines[e].dry_thrust
            ? clamp(s.afterburner[e], 0, 1)
            : 0;
  if (state_.fuel_mass < 0)
    state_.fuel_mass = cfg_.initial_fuel;
  if (state_.payload_mass < 0)
    state_.payload_mass = cfg_.initial_payload;
  last_debug_ = {};
  last_total_force_world_ = {};
  return true;
}
void Simulator::setWeather(const Weather &w) {
  weather_ = w;
  weather_.wind_ned = {sanitizedInput(w.wind_ned.x, -150, 150),
                       sanitizedInput(w.wind_ned.y, -150, 150),
                       sanitizedInput(w.wind_ned.z, -150, 150)};
  weather_.turbulence01 = sanitizedInput(w.turbulence01, 0, 1);
  weather_.temp_offset_c = sanitizedInput(w.temp_offset_c, -100, 100);
}

Controls Simulator::sanitized(const Controls &c) {
  Controls o = c;
  o.elevator_trim = sanitizedInput(o.elevator_trim, -1.0, 1.0);
  o.elevator_stick = sanitizedInput(o.elevator_stick, -1.0, 1.0);
  o.aileron_stick = sanitizedInput(o.aileron_stick, -1.0, 1.0);
  o.rudder_pedal = sanitizedInput(o.rudder_pedal, -1.0, 1.0);
  o.flap01 = sanitizedInput(o.flap01, 0.0, 1.0);
  o.spoiler01 = sanitizedInput(o.spoiler01, 0.0, 1.0);
  o.gear01 = sanitizedInput(o.gear01, 0.0, 1.0);
  o.throttle[0] = sanitizedInput(o.throttle[0], 0.0, 1.0);
  o.throttle[1] = sanitizedInput(o.throttle[1], 0.0, 1.0);
  o.brake01 = sanitizedInput(o.brake01, 0.0, 1.0);
  o.steering = sanitizedInput(o.steering, -1.0, 1.0);
  return o;
}

void Simulator::step(double dt) {
  ground_impact_ = {};
  if (!std::isfinite(dt) || !(dt > 0))
    return;
  // Advance the complete requested duration; reject unrepresentable work.
  if (dt > double(std::numeric_limits<int>::max()) / 240.0)
    throw std::invalid_argument("Requested step exceeds substep count range");
  constexpr double kSub = 1.0 / 240.0;
  int n = static_cast<int>(std::ceil(dt / kSub));
  if (n < 1)
    n = 1;
  const double h = dt / n;
  for (int i = 0; i < n; ++i)
    substep(h);
}

void Simulator::substep(double dt) {
  initializeUnsteady(cfg_,state_,state_.att.inverseRotate(state_.vel_ned-windAt(state_.pos_ned,state_.time)));
  const auto aeroMemory=unsteadyDerivative(cfg_,state_,state_.att.inverseRotate(state_.vel_ned-windAt(state_.pos_ned,state_.time)));
  // All control memory lives in State so snapshot replay reproduces the same
  // response.
  const double shapeLag = 1 - std::exp(-dt / std::max(.001, cfg_.input_tau));
  auto shape = [&](double x) {
    const double magnitude = std::max(0., std::abs(x) - cfg_.input_deadzone) /
                             (1 - cfg_.input_deadzone);
    return std::copysign(std::pow(magnitude, cfg_.input_exponent), x);
  };
  state_.pilot_pitch +=
      (shape(controls_.elevator_stick) - state_.pilot_pitch) * shapeLag;
  state_.pilot_roll +=
      (shape(controls_.aileron_stick) - state_.pilot_roll) * shapeLag;
  state_.pilot_yaw +=
      (shape(controls_.rudder_pedal) - state_.pilot_yaw) * shapeLag;
  if (!state_.actuators_initialized)
    state_.trim_reference = controls_.elevator_trim;
  state_.actuators_initialized = true;
  const auto allocation=cfg_.control_law==FlightControlLaw::VectorFighter
      ? vectorFighterAllocation(dt) : ControlAllocation{controlTargets(),{0,0}};
  const auto target = allocation.surfaces;
  auto actuator = [&](double &value, double request, double rate) {
    value += clamp(request - value, -rate * dt, rate * dt);
  };
  actuator(state_.elevator, target.elevator_stick, cfg_.actuator_rate);
  actuator(state_.aileron, target.aileron_stick, cfg_.actuator_rate);
  actuator(state_.rudder, target.rudder_pedal, cfg_.actuator_rate);
  actuator(state_.flap, controls_.flap01, cfg_.flap_rate);
  actuator(state_.spoiler, controls_.spoiler01, cfg_.spoiler_rate);
  for(unsigned e=0;e<2;++e)
    actuator(state_.nozzle_angle[e],allocation.nozzle[e],cfg_.engines[e].vector_rate);
  const bool delta=cfg_.control_law==FlightControlLaw::Delta;
  state_.canard = delta?0:state_.elevator;
  state_.elevon_l = clamp(state_.elevator + (delta?-1:1)*state_.aileron, -1, 1);
  state_.elevon_r = clamp(state_.elevator - (delta?-1:1)*state_.aileron, -1, 1);
  if(cfg_.variable_inlets) {
    const double mach=(state_.vel_ned-windAt(state_.pos_ned,state_.time)).norm()/airData().sound;
    for(auto& spike:state_.inlet_spike) actuator(spike,inletSpikeTarget(mach),cfg_.inlet_spike_rate);
  }
  const auto mass = massProperties();
  // Spool lag first (cheap, stable at any dt).
  for (int e = 0; e < 2; ++e) {
    const double tgt = state_.engine_health[e] > 0 && state_.fuel_mass != 0
                           ? controls_.throttle[e]
                           : 0;
    const double tau = std::max(.05, cfg_.engines[e].spool_seconds);
    state_.n1[e] += (tgt - state_.n1[e]) * (1 - std::exp(-dt / tau));
    state_.n1[e] = clamp(state_.n1[e], 0.0, 1.0);
    const bool supported =
        unsigned(e) < cfg_.engine_count &&
        cfg_.engines[e].reheat_thrust > cfg_.engines[e].dry_thrust &&
        cfg_.afterburner_threshold > 0 && cfg_.afterburner_threshold < 1;
    const double reheat =
        supported ? clamp((state_.n1[e] - cfg_.afterburner_threshold) /
                              (1 - cfg_.afterburner_threshold),
                          0, 1)
                  : 0;
    // Ignition follows spooled authority; cooling is responsive when commanded
    // off.
    const double target = tgt > cfg_.afterburner_threshold ? reheat : 0;
    state_.afterburner[e] +=
        (target - state_.afterburner[e]) * std::min(dt / .16, 1.0);
    if (state_.afterburner[e] < 1e-8)
      state_.afterburner[e] = 0;
  }

  const AeroResult aero = evalAero();
  // Lag memory advances once per fixed substep; force evaluation remains pure.
  // RK4 continuous flight advances the same memory within its stages below.


  // --- Propulsion ---
  const auto thrust = evalThrust();
  const Vec3 f_thr = thrust.force_body, m_thr = thrust.moment_body;

  // --- Landing gear ---
  Vec3 f_gear{0, 0, 0};
  Vec3 m_gear{0, 0, 0};
  double gear_n_load[3]{};
  // Contact work above a safe closing speed damages the body. The normal
  // impulse sets severity, so parked loads and ordinary landings cause none.
  const auto recordImpact = [&](const Vec3& position, const Vec3& normal,
                                const Vec3& velocity, double force, bool body) {
    const double closing = std::max(0., -velocity.dot(normal));
    const double slip = (velocity-normal*velocity.dot(normal)).norm();
    if (closing > ground_impact_.closingSpeed || (body && !ground_impact_.bodyContact)) {
      ground_impact_.position = position;
      ground_impact_.position.z = groundHeightAt(position.x, position.y);
      ground_impact_.normal = normal; ground_impact_.velocity = velocity;
      ground_impact_.closingSpeed = closing;
    }
    ground_impact_.bodyContact |= body;
    if (body) ground_impact_.scrapeSpeed = std::max(ground_impact_.scrapeSpeed, slip);
    const double safe = body ? 3.0 : 6.0;
    if (closing <= safe || force <= 0) return;
    if (aircraftCrashed(state_)) return;
    // A violent body strike or catastrophic gear impact is a total loss on
    // first contact. Ordinary landings and scrapes retain incremental damage.
    const bool catastrophic = closing >= (body ? 18.0 : 25.0);
    const double damage = catastrophic ? airframeIntegrity(state_) :
        std::min(1.,force*(closing-safe)*dt/(mass.mass*120.));
    ground_impact_.damage += damage;
    state_.surface_health[5] = std::max(0.,state_.surface_health[5]-damage);
    // Wing strikes reduce lift on that side; engines lose output after heavy
    // structural damage. All values are existing replicated state fields.
    const auto bodyPoint=state_.att.inverseRotate(position-state_.pos_ned);
    if (body && std::abs(bodyPoint.y)>cfg_.wing_span*.25) {
      const unsigned wing=bodyPoint.y<0?0:1;
      state_.surface_health[wing]=std::max(0.,state_.surface_health[wing]-damage*1.8);
      state_.surface_drag[wing]=std::min(4.,state_.surface_drag[wing]+damage);
    }
    if (airframeIntegrity(state_)<.4)
      for(auto& health:state_.engine_health) health=std::min(health,airframeIntegrity(state_)*2.5);
    if (aircraftCrashed(state_)) {
      for(auto& health:state_.surface_health)health=0;
      for(auto& health:state_.engine_health)health=0;
      state_.afterburner[0]=state_.afterburner[1]=0;
    }
  };
  Vec3 gear_f_body[3]{};
  const Vec3 gp[3] = {cfg_.gear_nose - mass.cg, cfg_.gear_main_l - mass.cg,
                      cfg_.gear_main_r - mass.cg};
  if (cfg_.contacts_enabled && controls_.gear01 > 0.5) {
    Vec3 rw[3]{}, forward[3]{}, side[3]{}, force_w[3]{};
    const double gs = std::hypot(state_.vel_ned.x, state_.vel_ned.y);
    const double steer_max =
        lerp(70 * kDeg2Rad, 6 * kDeg2Rad, clamp(gs / 60, 0, 1));
    for (int i = 0; i < 3; ++i) {
      rw[i] = state_.att.rotate(gp[i]);
      const Vec3 gw = state_.pos_ned + rw[i];
      const auto terrain = groundSurface(gw.x, gw.y);
      const Vec3 normal = terrain.normalNed;
      const double compression = (gw.z - terrain.heightNed) * -normal.z;
      if (compression <= 0)
        continue;
      const Vec3 wvel =
          state_.vel_ned + state_.att.rotate(state_.omega_body.cross(gp[i]));
      const double fn =
          std::max(0.0, cfg_.oleo_k * std::min(compression, cfg_.oleo_stroke) +
                            cfg_.oleo_c * -wvel.dot(normal));
      gear_n_load[i] = fn;
      recordImpact(gw, normal, wvel, fn, false);
      const double angle = i == 0 ? controls_.steering * steer_max : 0;
      Vec3 direction = state_.att.rotate({std::cos(angle), std::sin(angle), 0});
      direction -= normal * direction.dot(normal);
      forward[i] = direction.normalized();
      side[i] = forward[i].cross(normal).normalized();
      force_w[i] = normal * fn;
      // Rolling resistance remains active as brakes are progressively applied.
      force_w[i] += forward[i] * (-cfg_.mu_roll * fn *
                                  std::tanh(wvel.dot(forward[i]) / 1.5));
      const Vec3 fb = state_.att.inverseRotate(force_w[i]);
      f_gear += fb;
      m_gear += gp[i].cross(fb);
    }
    // Predict unconstrained velocities, then solve bounded tire impulses.
    // Coulomb friction can hold a stopped braked wheel against idle thrust;
    // a tanh-only model necessarily crept at every nonzero external force.
    const Vec3 external = aero.force_body + f_thr + f_gear;
    Vec3 velocity =
        state_.vel_ned +
        (state_.att.rotate(external) / mass.mass + Vec3{0, 0, kG0}) * dt;
    const Vec3 iw = inertiaMomentum(mass, state_.omega_body);
    const Vec3 torque =
        aero.moment_body + m_thr + m_gear - state_.omega_body.cross(iw);
    Vec3 omega = state_.omega_body + inverseInertia(mass, torque) * dt;
    double impulse[3][2]{};
    for (int iteration = 0; iteration < 8; ++iteration) {
      for (int i = 0; i < 3; ++i) {
        if (gear_n_load[i] <= 0)
          continue;
        for (int axis = 0; axis < 2; ++axis) {
          const double mu =
              axis == 1 ? cfg_.mu_side
                        : (i == 0 ? 0 : controls_.brake01 * cfg_.mu_brake_max);
          if (mu == 0)
            continue;
          const Vec3 direction = axis == 0 ? forward[i] : side[i];
          const Vec3 db = state_.att.inverseRotate(direction);
          const Vec3 arm = gp[i].cross(db);
          const double inverse_mass =
              1 / mass.mass + arm.dot(inverseInertia(mass, arm));
          const Vec3 point_velocity =
              velocity + state_.att.rotate(omega.cross(gp[i]));
          const double otherMu =
              axis == 0 ? cfg_.mu_side
                        : (i == 0 ? 0 : controls_.brake01 * cfg_.mu_brake_max);
          const double otherLimit = otherMu * gear_n_load[i] * dt;
          const double fraction =
              otherLimit > 0 ? clamp(impulse[i][1 - axis] / otherLimit, -1, 1)
                             : 0;
          // A shared friction ellipse bounds simultaneous braking/cornering.
          const double limit = mu * gear_n_load[i] * dt *
                               std::sqrt(std::max(0., 1 - fraction * fraction));
          const double next = clamp(
              impulse[i][axis] - point_velocity.dot(direction) / inverse_mass,
              -limit, limit);
          const double delta = next - impulse[i][axis];
          impulse[i][axis] = next;
          velocity += direction * (delta / mass.mass);
          omega += inverseInertia(mass, arm) * delta;
          force_w[i] += direction * (delta / dt);
        }
      }
    }
    // These forces feed the ordinary integrator and exact diagnostics.
    f_gear = {};
    m_gear = {};
    for (int i = 0; i < 3; ++i) {
      gear_f_body[i] = state_.att.inverseRotate(force_w[i]);
      f_gear += gear_f_body[i];
      m_gear += gp[i].cross(gear_f_body[i]);
    }
  }

  // --- Gravity ---
  const Vec3 f_grav_w{0, 0, mass.mass * kG0};
  const Vec3 f_grav_b = state_.att.inverseRotate(f_grav_w);

  // --- Distributed fuselage/nacelle contacts ---
  Vec3 f_belly_b{},m_belly_b{};
  // Include upper fuselage contacts so an inverted wreck remains supported.
  // Derive caps from the four longitudinal body points, excluding wing tips.
  std::array<Vec3,10> bodyContacts{};
  std::copy(cfg_.belly_contacts.begin(),cfg_.belly_contacts.end(),bodyContacts.begin());
  for(unsigned i=0;i<4;++i) {
    bodyContacts[6+i]=cfg_.belly_contacts[i];
    bodyContacts[6+i].z=-std::clamp(std::abs(bodyContacts[6+i].z)*.85,.4,2.2);
  }
  std::array<Vec3,10> bodyContactForces{};
  const double contactMass=mass.mass/cfg_.belly_contacts.size();
  const double stiffness=contactMass*kG0/.08;
  const double damping=1.4*std::sqrt(stiffness*contactMass);
  for(std::size_t contact=0;cfg_.contacts_enabled && contact<bodyContacts.size();++contact) {
    const auto& point=bodyContacts[contact];
    const Vec3 arm=point-mass.cg;
    const Vec3 position=state_.pos_ned+state_.att.rotate(arm);
    const auto terrain = groundSurface(position.x, position.y);
    const Vec3 surfaceNormal = terrain.normalNed;
    const double penetration=(position.z-terrain.heightNed)*-surfaceNormal.z;
    if(penetration<=0)continue;
    const Vec3 velocity=state_.vel_ned+state_.att.rotate(state_.omega_body.cross(arm));
    const double normal=std::max(0.,stiffness*penetration-damping*velocity.dot(surfaceNormal));
    recordImpact(position,surfaceNormal,velocity,normal,true);
    Vec3 forceWorld = surfaceNormal * normal;
    const Vec3 sliding = velocity - surfaceNormal * velocity.dot(surfaceNormal);
    const double slip=sliding.norm();
    if(slip>1e-9) {
      const Vec3 tangent = sliding / slip;
      const Vec3 tangentBody=state_.att.inverseRotate(tangent);
      const double effectiveInverseMass=1/mass.mass+tangentBody.dot(inverseInertia(mass,arm.cross(tangentBody)).cross(arm));
      const double friction=std::min(.50*normal,slip/(dt*cfg_.belly_contacts.size()*effectiveInverseMass));
      forceWorld-=tangent*friction;
    }
    const Vec3 force=state_.att.inverseRotate(forceWorld);
    bodyContactForces[contact]=force;
    f_belly_b+=force;m_belly_b+=arm.cross(force);
  }

  // --- Water ---
  // A lake is not ground: the airframe goes into it. Each of the points that
  // would bear on the ground is slowed by the water it is moving through and
  // lifted a little by what it displaces, so an aircraft that arrives slowly
  // wallows and settles, and one that arrives fast is stopped as if by a wall
  // and breaks up. Lakes lie at most 45 m over their beds, so nothing higher
  // above the ground than that looks for one.
  Vec3 f_water_b{}, m_water_b{};
  bool inWater=false;
  if(ground_model_==GroundModel::Terrain && cfg_.contacts_enabled &&
     state_.pos_ned.z+60>groundHeightAt(state_.pos_ned.x,state_.pos_ned.y)) {
    const double level=waterSurfaceElevation(state_.pos_ned.x,state_.pos_ned.y);
    if(level>kNoWater+1) {
      const double surface=-level,share=mass.mass/bodyContacts.size();
      // Wetted drag area of the whole airframe, shared between the points.
      const double dragArea=.10*cfg_.wing_area/bodyContacts.size();
      for(const auto& contact:bodyContacts) {
        const Vec3 arm=contact-mass.cg;
        const Vec3 point=state_.pos_ned+state_.att.rotate(arm);
        const double depth=point.z-surface;
        if(depth<=0) continue;
        inWater=true;
        const double wet=std::min(1.,depth/1.2);
        const Vec3 velocity=state_.vel_ned+state_.att.rotate(state_.omega_body.cross(arm));
        const double speed=velocity.norm();
        // Quadratic drag, taken implicitly so one step can stop but never reverse.
        const double k=.5*1000.*dragArea*wet/share;
        const Vec3 force=velocity*(-share*k*speed/(1+k*speed*dt))+Vec3{0,0,-.88*share*kG0*wet};
        const Vec3 body=state_.att.inverseRotate(force);
        f_water_b+=body;m_water_b+=arm.cross(body);
        // What the water does to the airframe: the rate of sinking into it,
        // and a share of the speed along it.
        const double arrival=std::max(0.,velocity.z)+.08*speed;
        const bool first=!ground_impact_.bodyContact || arrival>ground_impact_.closingSpeed;
        recordImpact({point.x,point.y,surface},{0,0,-1},{velocity.x,velocity.y,arrival},force.norm(),true);
        if(first) {ground_impact_.position.z=surface;ground_impact_.water=true;}
      }
      // Engines drown, and an airframe that has gone under is lost.
      if(state_.pos_ned.z-surface>.5) for(auto& health:state_.engine_health) health=0;
      if(state_.pos_ned.z-surface>2.5 && !aircraftCrashed(state_))
        recordImpact({state_.pos_ned.x,state_.pos_ned.y,surface},{0,0,-1},{0,0,30},1,true);
    }
  }

  const Vec3 Fb = aero.force_body + f_thr + f_gear + f_grav_b + f_belly_b + f_water_b;
  const Vec3 Mb = aero.moment_body + m_thr + m_gear + m_belly_b + m_water_b;
  const Vec3 Fw = state_.att.rotate(Fb);

  last_total_force_world_ = Fw;

  // Contact impulses/compliance stay on the original split solver. RK stages
  // evaluate continuous free-flight forces only, with frozen actuator/engine memory.
  bool nearContact=false;
  if(cfg_.contacts_enabled) for(const auto& p:bodyContacts) {
    const auto point=state_.pos_ned+state_.att.rotate(p-mass.cg);
    nearContact |= point.z + state_.vel_ned.norm()*dt >= groundHeightAt(point.x,point.y);
  }
  if(cfg_.contacts_enabled && controls_.gear01>.5) for(const auto& p:gp) {
    const auto point=state_.pos_ned+state_.att.rotate(p);
    nearContact |= point.z + state_.vel_ned.norm()*dt >= groundHeightAt(point.x,point.y);
  }
  nearContact |= inWater;
  if(integrator_==ContinuousIntegrator::RungeKutta4 && !nearContact && f_gear.norm2()==0 && f_belly_b.norm2()==0) {
    const double time=state_.time;
    state_=integrateContinuous(state_,dt,integrator_,[&](const State& s){return evaluateContinuous(s,controls_,weather_).derivative;});
    state_.time=time; // public clock advances once below
  } else {
  // --- Integrate translation ---
  state_.vel_ned += Fw * (dt / mass.mass);
  state_.pos_ned += state_.vel_ned * dt;

  // --- Integrate rotation ---
  const Vec3 Iw = inertiaMomentum(mass, state_.omega_body);
  const Vec3 alpha_b = inverseInertia(mass, Mb - state_.omega_body.cross(Iw));
  state_.omega_body += alpha_b * dt;
  // Quaternion derivative.
  const Quat wq{0, state_.omega_body.x, state_.omega_body.y,
                state_.omega_body.z};
  Quat dq = state_.att * wq;
  state_.att.w += 0.5 * dq.w * dt;
  state_.att.x += 0.5 * dq.x * dt;
  state_.att.y += 0.5 * dq.y * dt;
  state_.att.z += 0.5 * dq.z * dt;
  state_.att = state_.att.normalized();
  for(unsigned i=0;i<2;++i) {
    state_.alpha_lag[i]=std::remainder(state_.alpha_lag[i]+dt*aeroMemory.alpha[i],2*kPi);
    state_.separation[i]+=dt*aeroMemory.separation[i];
    state_.vortex_state[i]+=dt*aeroMemory.vortex[i];
  }
  }

  // Resolve deep impacts against the actual face after integration. This
  // prevents a fast strike from sinking through a hillside or storing enough
  // spring energy to launch the wreck. Shallow contacts retain oleo/skid forces.
  for(int iteration=0;cfg_.contacts_enabled && iteration<3;++iteration) {
    bool resolved=false;
    for(const auto& contact:bodyContacts) {
      const Vec3 arm=contact-mass.cg;
      const Vec3 worldArm=state_.att.rotate(arm);
      const Vec3 point=state_.pos_ned+worldArm;
      const auto terrain=groundSurface(point.x,point.y);
      const double penetration=(point.z-terrain.heightNed)*-terrain.normalNed.z;
      if(penetration<.25)continue;
      resolved=true;
      const Vec3 velocity=state_.vel_ned+state_.att.rotate(state_.omega_body.cross(arm));
      const double closing=-velocity.dot(terrain.normalNed);
      if(closing>0) {
        const Vec3 direction=state_.att.inverseRotate(terrain.normalNed);
        const Vec3 moment=arm.cross(direction);
        const double inverseMass=1/mass.mass+moment.dot(inverseInertia(mass,moment));
        const double impulse=closing/inverseMass; // inelastic collision
        recordImpact(point,terrain.normalNed,velocity,impulse/dt,true);
        state_.vel_ned+=terrain.normalNed*(impulse/mass.mass);
        state_.omega_body+=inverseInertia(mass,moment)*impulse;
      }
      state_.pos_ned+=terrain.normalNed*(penetration-.12);
    }
    if(!resolved)break;
  }

  if (state_.fuel_mass < 0)
    state_.fuel_mass = cfg_.initial_fuel;
  if (state_.payload_mass < 0)
    state_.payload_mass = cfg_.initial_payload;
  state_.fuel_mass = std::max(
      0., state_.fuel_mass - dt * (thrust.fuel_flow[0] + thrust.fuel_flow[1]));
  state_.time += dt;

  // --- Debug frame ---
  DebugFrame dbg;
  auto push = [&](const char *name, const Vec3 &p, const Vec3 &f) {
    if (dbg.count >= DebugFrame::kMax)
      return;
    DebugForce &d = dbg.forces[dbg.count++];
    std::snprintf(d.name, sizeof(d.name), "%.*s", int(sizeof(d.name)-1), name);
    d.pos_body = p;
    d.force_body = f;
  };
  for (const auto &surface : aero.surfaces)
    push(surface.name, surface.pos_body, surface.force_body);
  if(cfg_.levcon_lift_share>0) for(const auto& surface:aero.levcons)
    push(surface.name,surface.pos_body,surface.force_body);
  {
    for (int e = 0; e < 2; ++e)
      push(e == 0 ? "thrust_L" : "thrust_R", thrust.position[e] - mass.cg,
           thrust.force[e]);
  }
  {
    const Vec3 gp[3] = {cfg_.gear_nose - mass.cg, cfg_.gear_main_l - mass.cg,
                        cfg_.gear_main_r - mass.cg};
    const char *nm[3] = {"gear_N", "gear_ML", "gear_MR"};
    for (int i = 0; i < 3; ++i)
      if (gear_n_load[i] > 0.0)
        push(nm[i], gp[i], gear_f_body[i]);
  }
  push("gravity", {0, 0, 0}, f_grav_b);
  for(std::size_t contact=0;contact<bodyContactForces.size();++contact)
    if(bodyContactForces[contact].norm2()>0)
      push("body_contact",bodyContacts[contact]-mass.cg,bodyContactForces[contact]);
  dbg.total_force_body = Fb;
  dbg.total_moment_body = Mb;
  dbg.aero_moment=aero.moment_body;dbg.thrust_moment=m_thr;
  for(unsigned e=0;e<2;++e) dbg.nozzle_angle[e]=state_.nozzle_angle[e];
  dbg.alpha_rad = aero.alpha;
  dbg.beta_rad = aero.beta;
  dbg.qbar = aero.qbar;
  dbg.mach = aero.vtas / airData().sound;
  dbg.ground_effect = aero.ground_effect;
  dbg.total_mass = mass.mass;
  dbg.cg = mass.cg;
  dbg.inertia = mass.inertia;
  dbg.fuel_mass = state_.fuel_mass;
  dbg.payload_mass = state_.payload_mass;
  dbg.wind = windAt(state_.pos_ned, state_.time);
  for (int e = 0; e < 2; ++e) {
    dbg.engine_thrust[e] = thrust.each[e];
    dbg.fuel_flow[e] = thrust.fuel_flow[e];
  }
  dbg.elevator = state_.elevator;
  dbg.aileron = state_.aileron;
  dbg.rudder = state_.rudder;
  dbg.flap = state_.flap;
  dbg.spoiler = state_.spoiler;
  dbg.normal_acceleration =
      -(aero.force_body + f_thr + f_gear + f_belly_b).z / mass.mass;
  dbg.normal_load = dbg.normal_acceleration / kG0;
  dbg.contact_normal_force = gear_n_load[0] + gear_n_load[1] + gear_n_load[2];
  dbg.sample_time = state_.time - dt;
  last_debug_ = dbg;
}

Instruments Simulator::instruments() const {
  Instruments ins;
  const auto mass = massProperties();
  const AirData air = airData();
  const Vec3 wind = windAt(state_.pos_ned, state_.time);
  const Vec3 v_air_w = state_.vel_ned - wind;
  const double tas = v_air_w.norm();
  ins.tas = tas;
  // Ideal pitot pressure (Rayleigh relation above Mach 1), calibrated at sea
  // level.
  auto pitotRatio = [](double m) {
    return m <= 1 ? std::pow(1 + .2 * m * m, 3.5)
                  : std::pow(1.2 * m * m, 3.5) *
                        std::pow(2.4 / (2.8 * m * m - .4), 2.5);
  };
  const double impact = air.pressure * (pitotRatio(tas / air.sound) - 1);
  double low = 0, high = std::max(5., tas / air.sound * 2);
  for (int i = 0; i < 40; ++i) {
    const double mid = (low + high) * .5;
    if (101325 * (pitotRatio(mid) - 1) < impact)
      low = mid;
    else
      high = mid;
  }
  ins.cas = (low + high) * .5 * isaAtAltitude(0).sound;
  ins.ias = ins.cas; // ideal instrument, no installation/calibration error
  ins.mach = (air.sound > 1.0) ? tas / air.sound : 0.0;
  ins.alt_msl = -state_.pos_ned.z;
  ins.agl =
      -state_.pos_ned.z + groundHeightAt(state_.pos_ned.x, state_.pos_ned.y);
  ins.vs = -state_.vel_ned.z;
  double roll, pitch, yaw;
  eulerFromQuat(state_.att, roll, pitch, yaw);
  ins.roll_deg = roll * kRad2Deg;
  ins.pitch_deg = pitch * kRad2Deg;
  double hdg = yaw * kRad2Deg;
  while (hdg < 0)
    hdg += 360.0;
  while (hdg >= 360.0)
    hdg -= 360.0;
  ins.hdg_deg = hdg;
  const AeroResult aero = evalAero();
  ins.alpha_deg = aero.alpha * kRad2Deg;
  ins.beta_deg = aero.beta * kRad2Deg;
  // Load factor from non-gravity body-Z force.
  ins.g_load = last_debug_.count ? last_debug_.normal_load : normalLoad();
  ins.normal_acceleration = ins.g_load * kG0;
  const double flap =
      state_.actuators_initialized ? state_.flap : controls_.flap01;
  const double clmax = cfg_.aero_kind==AeroModelKind::AirlinerEngineering?a320HighLift(flap).clmax:lerp(cfg_.cl_max_clean, cfg_.cl_max_full_flap, flap);
  ins.vstall = cfg_.wing_area > 0
                   ? std::sqrt(2.0 * mass.mass * kG0 /
                               (air.rho * cfg_.wing_area * clmax))
                   : 0;
  const double acrit = cfg_.aero_kind==AeroModelKind::AirlinerEngineering?a320HighLift(flap).alpha_critical:lerp(cfg_.alpha_crit_clean, 12.0 * kDeg2Rad, flap);
  ins.stall_warn = (aero.alpha > acrit - 3.0 * kDeg2Rad && tas > 5.0) ||
                   (ins.tas < 1.13 * ins.vstall && tas > 5.0);
  return ins;
}

} // namespace ofs
