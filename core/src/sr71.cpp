#include "ofs/aircraft_definition.hpp"
#include "ofs/units.hpp"

namespace ofs {
AircraftConfig sr71Config() {
  AircraftConfig c;
  // NASA/TP-2002-210718 table2 zero-fuel baseline, reference geometry and
  // inertias. Fuel loading and distributed tank radii are reconstructed.
  constexpr double pounds = units::kgPerPound, slugFootSquared = units::kgMetreSquaredPerSlugFootSquared;
  c.empty_mass = 60728 * pounds;
  c.initial_fuel = 20000;
  c.initial_payload = 0;
  c.fuel_capacity = 80000 * pounds;
  c.mass = c.empty_mass + c.initial_fuel;
  c.fuel_position = {-.45, 0, .05};
  c.fuel_inertia_per_kg = {9, 49, 58};
  c.ixx = 220660 * slugFootSquared + c.initial_fuel * (9 + .05 * .05);
  c.iyy =
      954850 * slugFootSquared + c.initial_fuel * (49 + .45 * .45 + .05 * .05);
  c.izz = 1172039 * slugFootSquared + c.initial_fuel * (58 + .45 * .45);
  // NASA equations (13)/(14) use -Ixz in the matrix X/Z entry.
  c.ixz = -19200 * slugFootSquared + c.initial_fuel * .45 * .05;
  const Vec3 basicCg=-(c.fuel_position*c.initial_fuel)/c.empty_mass;
  c.ixx+=c.empty_mass*(basicCg.y*basicCg.y+basicCg.z*basicCg.z);
  c.iyy+=c.empty_mass*(basicCg.x*basicCg.x+basicCg.z*basicCg.z);
  c.izz+=c.empty_mass*(basicCg.x*basicCg.x+basicCg.y*basicCg.y);
  c.ixz-=c.empty_mass*basicCg.x*basicCg.z;
  c.wing_area = units::squareFeetToSquareMetres(1605);
  c.wing_span = units::feetToMetres(55.5833333333);
  c.mac = units::feetToMetres(37.7);
  // Stable equivalent augmented delta/chine model; coefficients are inferred
  // and validated in this simulator, not claimed to be a measured SR71 deck.
  c.alpha0 = 0;
  c.cl_alpha = 3.3;
  c.cl_max_clean = 1.18;
  c.cl_max_full_flap = 1.18;
  c.alpha_crit_clean = 23 * kDeg2Rad;
  c.cd0_clean = .016;
  c.oswald_e = .72;
  c.cm0 = .005;
  c.cm_alpha = -.20;
  c.cm_de = -.65;
  c.cm_q = -12;
  c.cl_beta = -.08;
  c.cl_p = -.48;
  c.cl_da = .10;
  c.cn_beta = .10;
  c.cn_r = -.22;
  c.cn_dr = -.09;
  c.cy_beta = -.50;
  c.pitch_arm = -10.25;
  c.pitch_span = 2.25;
  c.control_law = FlightControlLaw::Delta;
  c.max_pitch_rate = .14;
  c.max_roll_rate = .55;
  c.response_time = .80;
  c.g_positive = 2.5;
  c.g_negative = -.5;
  c.alpha_limit = 16 * kDeg2Rad;
  c.actuator_rate = .85;
  c.control_q_limit = 75000;
  c.elev_min = -35 * kDeg2Rad;
  c.elev_max = 20 * kDeg2Rad;
  c.ail_max = 20 * kDeg2Rad;
  c.rud_max = 20 * kDeg2Rad;
  c.flap_max_deg = 0;
  c.mach_drag_onset = .87;
  c.mach_drag_peak = .045;
  c.mach_drag_supersonic = .012;
  c.engine_count = 2;
  c.thrust_sl_static_each = 106000;
  c.afterburner_thrust_each = units::poundsForceToNewtons(34000);
  c.afterburner_threshold = .82;
  c.engine_tau = 1.7;
  c.thrust_density_exponent = .60;
  c.variable_inlets = true;
  c.inlet_spike_rate = .20;
  c.dry_tsfc = 2.65e-5;
  c.reheat_tsfc = 5.4e-5;
  c.engine_pos_l = {-5.38, -4.14, -.15};
  c.engine_pos_r = {-5.38, 4.14, -.15};
  c.gear_nose = {10.42, 0, 2.55};
  c.belly_contacts={{{12,0,.75},{-2,-4.14,1.05},{-2,4.14,1.05},{-9,0,.7},{-8.4,-8.38,.07},{-8.4,8.38,.07}}};
  c.gear_main_l = {-1.75, -2.78, 2.55};
  c.gear_main_r = {-1.75, 2.78, 2.55};
  c.oleo_stroke = .32;
  c.oleo_k = 1.45e6;
  c.oleo_c = 75000;
  c.mu_roll = .021;
  c.mu_side = .64;
  c.mu_brake_max = .48;
  c.surfaces = {{{"delta_L", SurfaceRole::Wing, {-1.8, -3.73, -.1}, .5},
                 {"delta_R", SurfaceRole::Wing, {-1.8, 3.73, -.1}, .5},
                 {"elevons_L", SurfaceRole::Pitch, {-10.25, -2.25, .1}, .05},
                 {"elevons_R", SurfaceRole::Pitch, {-10.25, 2.25, .1}, .05},
                 {"canted_rudders", SurfaceRole::Fin, {-7.6, 0, -1.7}, .06},
                 {"chine_body", SurfaceRole::Body, {0, 0, 0}, .15}}};
  c.surfaces_configured = true;
  return c;
}
} // namespace ofs
