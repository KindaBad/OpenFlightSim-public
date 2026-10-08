#pragma once
// Aircraft configuration, controls, weather, state, instruments.
//
// Frames: world NED (X north, Y east, Z down), body FRD (X fwd, Y right,
// Z down). All SI, radians internally.

#include <array>
#include <cstddef>

#include "ofs/math.hpp"
#include "ofs/data_model.hpp"
#include "ofs/terrain.hpp"

namespace ofs {

enum class SurfaceRole { Wing, Pitch, Fin, Body };
struct AeroSurface {
  const char *name{"surface"};
  SurfaceRole role{SurfaceRole::Wing};
  Vec3 position{}; // loaded-reference body FRD
  double area_fraction{.5};
};
inline constexpr std::size_t surfaceCount = 6;
enum class FlightControlLaw { Direct, Transport, Fighter, Canard, Delta, VectorFighter };
// Laws that honour Controls::maneuver_mode; others ignore the replicated flag.
constexpr bool hasManeuverMode(FlightControlLaw law) {
  return law == FlightControlLaw::VectorFighter || law == FlightControlLaw::Canard;
}

struct EngineComponent {
  Vec3 position{}, direction{1, 0, 0};
  double dry_thrust{}, reheat_thrust{}, spool_seconds{1};
  double dry_tsfc{1.65e-5}, reheat_tsfc{4.5e-5};
  // Fixed canted hinge, angle/rate in rad and rad/s. Zero limit disables TV.
  Vec3 vector_axis{0,1,0};
  double vector_limit{}, vector_rate{1};
  // Exit position is rotated about this hinge when articulated_nozzle is set.
  Vec3 nozzle_pivot{};
  bool articulated_nozzle{};

};
enum class AeroModelKind { EngineeringAeroModel, DataDriven, AirlinerEngineering };
enum class JetModelKind { EngineeringJetEngineModel, EngineDeck };
struct AircraftConfig {
  AeroModelKind aero_kind{AeroModelKind::EngineeringAeroModel};
  JetModelKind jet_kind{JetModelKind::EngineeringJetEngineModel};
  std::shared_ptr<const AerodynamicModel> aerodynamic_model;
  std::shared_ptr<const PropulsionModel> propulsion_model;
  double table_aileron_sign{1}; // dataset deflection sign relative to right-roll stick
  const char* provenance_dataset{"engineering-surrogates"};
  // --- Wing / geometry ---
  double wing_area{122.6}; // m^2 (A320)
  double wing_span{35.8};  // m (sharklet span, matches visual model report)
  double mac{4.19};        // m mean aerodynamic chord
  double alpha0{-2.0 * kDeg2Rad}; // zero-lift alpha (camber/incidence)
  double cl_alpha{5.05};          // per rad, finite-wing lift slope
  double cl_max_clean{1.42};
  double cl_max_full_flap{2.25};
  double flap_lift{1.05}; // Delta CL at full travel; smaller on fighter flaperons.
  double alpha_crit_clean{14.5 * kDeg2Rad};
  double cd0_clean{0.022};
  double oswald_e{0.82};
  double cm0{0.06};
  double cm_alpha{-0.85}; // per rad (longitudinal stability)
  double cm_de{-1.45};    // per rad elevator effectiveness
  double cm_q{-12.0};     // pitch damping (per rad of q*mac/2V)
  double cl_beta{-0.14};  // dihedral effect per rad
  double cl_p{-0.50};     // roll damping
  double cl_da{0.16};     // aileron effectiveness per rad (per unit diff?)
  double cn_beta{0.13};   // weathervane per rad
  double cn_r{-0.16};     // yaw damping
  double cn_dr{-0.085};   // rudder effectiveness per rad
  double cy_beta{-0.55};  // sideforce per rad sideslip

  // --- Mass ---
  double mass{64000.0}; // kg (mid-weight A320)
  double ixx{1.35e6};   // kg m^2
  double iyy{3.55e6};
  double izz{4.60e6};

  // Full symmetric matrix entries; positive aerospace products have opposite sign.
  double ixz{}, ixy{}, iyz{};
  double empty_mass{}; // zero derives basic mass from reference total
  double initial_fuel{8000}, initial_payload{14000};
  double fuel_capacity{}; // zero retains the historical unconstrained load model
  Vec3 payload_inertia_per_kg{}; // distributed payload radii, m2; shifted payload retains this shape
  Vec3 fuel_inertia_per_kg{}; // optional distributed-tank radius-of-gyration terms
  Vec3 fuel_position{-.4, 0, 0}, payload_position{.6, 0, 0};
  double dry_tsfc{1.65e-5}, reheat_tsfc{4.5e-5}; // kg/(N s), approximate
  double fuel_flow_scale{1}; // zero only for fixed-mass validation
  double payload_cd_per_kg{};

  std::array<AeroSurface, surfaceCount> surfaces{};
  bool surfaces_configured{};
  // Optional root leading-edge foreplanes; reference share redistributes wing
  // lift rather than adding free lift. Their local flow/force is resolved separately.
  Vec3 levcon_position[2]{};
  double levcon_lift_share{}, levcon_control_share{}, levcon_max{20*kDeg2Rad};
  double pitch_arm{-12}, pitch_span{3};
  double mach_drag_onset{.72}, mach_drag_peak{.08}, mach_drag_supersonic{.045};
  double thrust_density_exponent{.85}, thrust_ram_gain{};
  // Extra installed thrust per Mach number above 1, as a fraction of static
  // thrust: intake ram recovery that the subsonic lapse does not carry.
  double thrust_ram_supersonic{};
  bool variable_inlets{}; // optional mixed-compression propulsion approximation
  double inlet_spike_rate{.20}; // normalized travel / s; reconstructed actuator
  FlightControlLaw control_law{FlightControlLaw::Transport};
  double max_pitch_rate{.22}, max_roll_rate{.58}, response_time{.55};
  double g_positive{2.5}, g_negative{-1}, alpha_limit{15 * kDeg2Rad};
  double actuator_rate{2.5}, flap_rate{.25},
      spoiler_rate{1.7};         // normalized / s
  double control_q_limit{45000}; // reserved FCS dynamic-pressure scheduling anchor
  double unsteady_alpha_tau{}, unsteady_detach_tau{.18}, unsteady_attach_tau{.55}, unsteady_vortex_tau{.25};
  double unsteady_alpha_dot_gain{.12}, unsteady_beta_gain{.35};
  // Nose-down pitching moment coefficient that builds over the 23 degrees past
  // the stall. Airframes shaped to be flown there have far less of it.
  double poststall_pitch_break{.55};
  double vortex_lift{}; // estimated nonlinear leading-edge lift coefficient
  double input_deadzone{}, input_exponent{1}, input_tau{.035};

  // --- Propulsion: 2x CFM56-5B class ---
  double thrust_sl_static_each{120000.0}; // N
  double engine_tau{1.2};                 // spool lag, s
  // Optional reheat regime in the existing replicated 0..1 throttle/spool.
  // Zero keeps the historical dry-thrust curve exactly unchanged.
  double afterburner_thrust_each{};
  double afterburner_threshold{1};
  unsigned engine_count{
      2}; // Fixed two-slot State/Controls ABI; unused slot has no thrust.
  Vec3 engine_pos_l{0.0, -5.3, 1.1}; // body FRD rel CG (fwd, right, down)
  Vec3 engine_pos_r{0.0, 5.3, 1.1};

  std::array<EngineComponent, 2> engines{};
  bool engines_configured{}; // false materializes legacy definition fields once

  bool contacts_enabled{true}; // false for airborne reference models without contact data
  // --- Landing gear (body FRD rel CG, ground plane handling in sim) ---
  // CG sits ~3.55 m above ground on extended oleos (A320 sits tall).
  // Body FRD: +Z is down/belly, so gear contact points are +Z (below CG).
  Vec3 gear_nose{10.44, 0.0, 3.55}; // measured visual/OEM wheelbase 12.64 m
  // Fuselage/nacelle contact samples in the reference frame; estimated from
  // exterior geometry. Resolve each with a compliant contact and friction.
  std::array<Vec3,6> belly_contacts{{{10,0,1.4},{-.2,-5.75,2.97},{-.2,5.75,2.97},{-10,0,1.4},{-6,-17.4,.5},{-6,17.4,.5}}};
  Vec3 gear_main_l{-2.2, -3.8, 3.55};
  Vec3 gear_main_r{-2.2, 3.8, 3.55};
  double oleo_stroke{0.45};  // m
  double oleo_k{1.35e6};     // N/m per gear
  double oleo_c{9.0e4};      // N s/m per gear
  double mu_brake_max{0.55}; // dry runway, anti-skid limited
  double mu_side{0.60};
  double mu_roll{0.025};

  // --- Control limits ---
  double elev_max{20.0 * kDeg2Rad};  // + = trailing-edge-down (nose-down)
  double elev_min{-25.0 * kDeg2Rad}; // - = pull / nose-up
  double ail_max{25.0 * kDeg2Rad};
  double rud_max{30.0 * kDeg2Rad};
  double flap_max_deg{35.0};
};

inline double elevatorDeflection(double normalized, const AircraftConfig &c) {
  return normalized >= 0 ? normalized * c.elev_min : -normalized * c.elev_max;
}
AircraftConfig a320Config();
void configureSurfaces(AircraftConfig &config);

// Pilot / autopilot inputs. Stick conventions chosen for the frontend:
// elevator_stick: +1 = full pull (nose up). aileron_stick: +1 = roll right.
// rudder_pedal: +1 = nose right. throttle: 0..1 per engine.
struct Controls {
  double elevator_stick{0.0}; // -1..1
  double aileron_stick{0.0};  // -1..1
  double rudder_pedal{0.0};   // -1..1
  double flap01{0.0};         // 0..1 maps to 0..35 deg
  double spoiler01{0.0};      // 0..1
  double gear01{1.0};         // 1 = down, 0 = up (continuous transit ok)
  double throttle[2]{0.0, 0.0};
  double brake01{0.0}; // 0..1 wheel brakes
  double steering{
      -0.0}; // -1..1 nosewheel (used at low speed); default straight
  double elevator_trim{0.0}; // persistent normalized offset, added to stick
  bool maneuver_mode{}; // Su-57/Typhoon low-speed high-AoA command; retains FCS/G limits
};

struct Weather {
  Vec3 wind_ned{0, 0, 0};   // steady wind, m/s (Z ~ 0 normally)
  double turbulence01{0.0}; // 0 calm .. 1 severe
  double temp_offset_c{0.0};
};

struct State {
  Vec3 pos_ned{0, 0, 0};           // m; Z down, so altitude MSL = -z
  Vec3 vel_ned{0, 0, 0};           // m/s
  Quat att;                        // body -> world
  Vec3 omega_body{0, 0, 0};        // p,q,r rad/s
  double n1[2]{0.0, 0.0};          // engineering: commanded power 0=running idle; health=0 stops. Deck: normalized power
  double afterburner[2]{0.0, 0.0}; // actual per-engine reheat ramp, 0..1
  double inlet_spike[2]{}; // independent normalized aft travel, replicated memory
  double nozzle_angle[2]{}; // independent physical hinge deflection, rad
  double time{0.0};
  double fuel_mass{-1},
      payload_mass{-1}; // -1 initializes configured load at setState
  Vec3 payload_offset{};
  // kg m2: distributed-load tensor correction to the existing payload centroid.
  // Products store symmetric matrix entries (xy,xz,yz), not unsigned products.
  Vec3 payload_inertia_correction{}, payload_products_correction{};
  double engine_health[2]{1, 1}; // 0 = off, independent output modifier
  std::array<double, surfaceCount> surface_health{1, 1, 1, 1, 1, 1};
  std::array<double, surfaceCount> surface_drag{1, 1, 1, 1, 1, 1};
  // Generic unsteady engineering states: lagged section incidence, separation,
  // vortex persistence. Su-57 parameters are estimates, not measured transients.
  bool aero_memory_initialized{};
  double alpha_lag[2]{}, separation[2]{}, vortex_state[2]{};
  // Complete dynamic control memory, replicated for prediction/replay.
  bool actuators_initialized{}, fcs_enabled{true};
  double trim_reference{};
  double pilot_pitch{}, pilot_roll{}, pilot_yaw{};
  double elevator{}, aileron{}, rudder{}, flap{}, spoiler{};
  double canard{}, elevon_l{},
      elevon_r{}; // actual normalized allocator outputs
};

// The body surface already carries replicated structural health, so terrain
// damage uses the existing deterministic replay and replication fields.
inline double airframeIntegrity(const State& state) { return state.surface_health[5]; }
inline bool aircraftCrashed(const State& state) { return airframeIntegrity(state) <= .001; }

// Actual CG relative to the reference body frame used by aircraft
// definitions/assets.
inline Vec3 loadedCg(const AircraftConfig &cfg, const State &state) {
  const double fuel = state.fuel_mass < 0 ? cfg.initial_fuel : state.fuel_mass;
  const double payload =
      state.payload_mass < 0 ? cfg.initial_payload : state.payload_mass;
  const double basic = cfg.empty_mass > 0
                           ? cfg.empty_mass
                           : cfg.mass - cfg.initial_fuel - cfg.initial_payload;
  return (cfg.fuel_position * (fuel - cfg.initial_fuel) +
          cfg.payload_position * (payload - cfg.initial_payload) +
          state.payload_offset * payload) /
         (basic + fuel + payload);
}

// Public 26-inch travel between Mach1.6 and3.2; interpolation and pressure
// recovery are approximations, not the operational inlet control algorithm.
double inletSpikeTarget(double mach);
double inletPressureRecovery(double mach, double normalizedSpike);

struct Instruments {
  double tas{0}; // m/s true airspeed
  double ias{
      0}; // m/s ideal pitot IAS, equals CAS without instrument/position error
  double cas{
      0}; // m/s calibrated using sea-level pitot inversion, including normal shock
  double mach{0};
  double alt_msl{0}; // m
  double agl{0}; // m above the local terrain surface
  double vs{0};  // m/s, up-positive
  double hdg_deg{0}; // 0..360, 0 = north
  double pitch_deg{0};
  double roll_deg{0};
  double alpha_deg{0};
  double beta_deg{0};
  double g_load{1.0};
  double normal_acceleration{}; // body up specific acceleration, m/s2
  double vstall{0}; // m/s current-config 1g stall speed (SL approx scaled by
                    // sqrt(rho0/rho))
  bool stall_warn{false};
};

struct DebugForce {
  char name[32]{0};
  Vec3 pos_body{0, 0, 0};
  Vec3 force_body{0, 0, 0};
};

struct DebugFrame {
  static constexpr std::size_t kMax = 24;
  DebugForce forces[kMax];
  std::size_t count{0};
  Vec3 total_force_body{0, 0, 0};
  Vec3 total_moment_body{0, 0, 0};
  double alpha_rad{0};
  double beta_rad{0};
  double qbar{0};
  double mach{}, ground_effect{1}, total_mass{}, fuel_mass{}, payload_mass{};
  Vec3 cg{}, inertia{}, wind{};
  double engine_thrust[2]{}, fuel_flow[2]{};
  double nozzle_angle[2]{};
  Vec3 aero_moment{}, thrust_moment{};
  double elevator{}, aileron{}, rudder{}, flap{}, spoiler{};

  double normal_load{}, normal_acceleration{};
  double contact_normal_force{0}; // exact sum of runway-normal loads, N
  double sample_time{0}; // forces evaluated before the final internal substep
};

} // namespace ofs
