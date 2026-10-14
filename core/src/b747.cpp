#include "ofs/aircraft_definition.hpp"
#include <array>

namespace ofs {
// Boeing 747-206B with four Pratt & Whitney JT9D-7A turbofans. The two engine
// slots of the simulation each stand for the pair under one wing. Dimensions,
// weights and thrust are the published figures quoted in
// docs/AIRCRAFT_REFERENCE.md; the centre of gravity and the undercarriage come
// from the donor model; aerodynamic derivatives, inertia, the thrust lapse and
// the control law are engineering estimates.
AircraftConfig b747Config() {
  AircraftConfig c;
  c.control_law=FlightControlLaw::Transport; c.pitch_arm=-37.8; c.pitch_span=5.5;
  // The transport law eases the nose down as the load rises. At the B-52's
  // 0.12 rad/s a full pull then holds only about 1.25 g in a 58-degree bank,
  // the bank the instructor flies to the 747's 2.5 g, so every steep turn sank.
  c.max_pitch_rate=.22; c.max_roll_rate=.40; c.response_time=.7;
  c.g_positive=2.5; c.g_negative=-1.0; c.alpha_limit=14*kDeg2Rad;
  c.actuator_rate=1.5;
  c.empty_mass=174000; c.fuel_capacity=146700;
  // About two fifths of the tanks: a long sector without a full load.
  c.initial_fuel=60000; c.initial_payload=0; c.fuel_position={-.5,0,0}; c.payload_position={0,0,0};
  c.mass=c.empty_mass+c.initial_fuel;
  // Component inertia: the structure, four 4,000 kg engines in pairs 11.9 and
  // 21.1 m from the centreline, and the fuel spread along the wings.
  constexpr double length=70.66,span=59.64,height=19.33,engineMass=4000.;
  const double structureMass=c.empty_mass-4*engineMass;
  const Vec3 structureVariance{std::pow(.20*length,2),std::pow(.16*span,2),std::pow(.10*height,2)};
  const Vec3 fuelVariance{15,120,.3},engineVariance{4,4,.3};
  c.fuel_inertia_per_kg={fuelVariance.y+fuelVariance.z,fuelVariance.x+fuelVariance.z,fuelVariance.x+fuelVariance.y};
  const Vec3 innerLeft{6.21,-11.85,2.28},innerRight{6.21,11.85,2.28};
  const Vec3 outerLeft{-2.68,-21.06,1.62},outerRight{-2.68,21.06,1.62};
  const Vec3 structureCg=-((innerLeft+innerRight+outerLeft+outerRight)*engineMass+c.fuel_position*c.initial_fuel)/structureMass;
  struct Part {double mass;Vec3 position,variance;};
  const std::array<Part,6> parts{{{structureMass,structureCg,structureVariance},
    {engineMass,innerLeft,engineVariance},{engineMass,innerRight,engineVariance},
    {engineMass,outerLeft,engineVariance},{engineMass,outerRight,engineVariance},
    {c.initial_fuel,c.fuel_position,fuelVariance}}};
  c.ixx=c.iyy=c.izz=c.ixz=0;
  for(const auto& part:parts) {
    const auto p=part.position,v=part.variance;
    c.ixx+=part.mass*(p.y*p.y+p.z*p.z+v.y+v.z);
    c.iyy+=part.mass*(p.x*p.x+p.z*p.z+v.x+v.z);
    c.izz+=part.mass*(p.x*p.x+p.y*p.y+v.x+v.y);
    c.ixz-=part.mass*p.x*p.z;
  }
  c.wing_area=511; c.wing_span=span; c.mac=8.3;
  // The wing sits nose-up on the fuselage by about 3 degrees, an estimate of the same kind as the
  // B-52's. It lifts the slow and steep turns the instructor flies; the reversal still sinks, see
  // docs/AIRCRAFT_REFERENCE.md.
  c.alpha0=-3*kDeg2Rad; c.cl_alpha=4.5;
  c.cl_max_clean=1.40; c.cl_max_full_flap=2.30;
  c.flap_lift=.60;
  c.alpha_crit_clean=14*kDeg2Rad;
  c.cd0_clean=.0175; c.oswald_e=.85;
  c.cm0=.02; c.cm_alpha=-.55; c.cm_de=-1.5; c.cm_q=-12;
  // Roll power of the ailerons and the roll spoilers together.
  c.cl_beta=-.10; c.cl_p=-.45; c.cl_da=.18;
  c.cn_beta=.14; c.cn_r=-.20; c.cn_dr=-.09; c.cy_beta=-.60;
  // Wave drag, estimated: little below the 747's drag-divergence Mach of about
  // 0.86, so it cruises at Mach 0.85, rising steeply towards the 0.92 limit.
  c.mach_drag_onset=.84; c.mach_drag_peak=.05; c.mach_drag_supersonic=.05;
  // JT9D-7A: 46,950 lbf (208.9 kN) rating each, as quoted; two slots of two engines.
  c.engine_count=2; c.thrust_sl_static_each=2*208870.; c.engine_tau=2.5;
  // A bypass-ratio-5 fan loses thrust with speed: about 79 % of static at Mach
  // 0.5 and 62 % at Mach 0.8, about 52 kN an engine at Mach 0.85 at 35,000 ft.
  c.thrust_density_exponent=.75; c.thrust_ram_gain=-.55; c.dry_tsfc=1.70e-5;
  c.engine_pos_l={1.76,-16.46,1.95}; c.engine_pos_r={1.76,16.46,1.95};
  // Nose gear six metres aft of the nose; main trucks two metres aft of the
  // CG, 5.8 m either side. Contact depth is the CG height above the ground.
  c.gear_nose={24.87,0,5.12};
  c.gear_main_l={-2.0,-5.8,5.12}; c.gear_main_r={-2.0,5.8,5.12};
  // Fuselage underside at stations 4, 28, 44 and 54 m aft of the nose, then the
  // bottoms of the inboard nacelles, which hang 0.8 m below the belly: wheels
  // up, the 747 settles onto them. With the side pair on the centreline it
  // rolled onto a wing that nothing held up.
  c.belly_contacts={{{26.9,0,1.4},{2.9,0,2.8},{-13.1,0,2.5},{-23.1,0,1.0},{6.2,-11.85,3.58},{6.2,11.85,3.58}}};
  // Oleo damping about 0.4 of critical: near critical it throws a 4 g touchdown.
  c.oleo_stroke=.5; c.oleo_k=6.2e6; c.oleo_c=6.0e5;
  c.mu_brake_max=.50; c.mu_side=.60; c.mu_roll=.022;
  c.elev_min=-20*kDeg2Rad; c.elev_max=15*kDeg2Rad;
  c.ail_max=20*kDeg2Rad; c.rud_max=25*kDeg2Rad; c.flap_max_deg=30;
  configureSurfaces(c);
  return c;
}
} // namespace ofs
