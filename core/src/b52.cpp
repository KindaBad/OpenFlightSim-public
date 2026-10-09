#include "ofs/aircraft_definition.hpp"
#include <array>

namespace ofs {
// Boeing B-52H Stratofortress with eight Pratt & Whitney TF33-P-3/103. The
// two engine slots of the simulation each stand for the four engines under one
// wing. Dimensions, weights and thrust are the published figures; aerodynamic
// derivatives, inertia, the thrust lapse and the control law are engineering
// estimates fitted to the published speed. See docs/AIRCRAFT_REFERENCE.md.
AircraftConfig b52Config() {
  AircraftConfig c;
  c.control_law=FlightControlLaw::Transport; c.pitch_arm=-21.5; c.pitch_span=4.5;
  c.max_pitch_rate=.12; c.max_roll_rate=.42; c.response_time=.60;
  c.g_positive=2.0; c.g_negative=-.5; c.alpha_limit=13*kDeg2Rad;
  c.actuator_rate=1.6;
  c.empty_mass=83250; c.fuel_capacity=141600;
  // A third of the tanks: enough for any sortie over this country.
  c.initial_fuel=45000; c.initial_payload=0; c.fuel_position={-.6,0,-.5}; c.payload_position={.5,0,.9};
  c.mass=c.empty_mass+c.initial_fuel;
  // Component inertia: the structure and, under each wing, four 1,770 kg
  // engines in two pods 11.4 and 18.6 m from the centreline.
  constexpr double length=48.5,span=56.4,height=12.4,podMass=4*1770.;
  const double structureMass=c.empty_mass-2*podMass;
  const Vec3 structureVariance{std::pow(.20*length,2),std::pow(.16*span,2),std::pow(.10*height,2)};
  const Vec3 fuelVariance{12,70,.2},podVariance{9,13,.3};
  c.fuel_inertia_per_kg={fuelVariance.y+fuelVariance.z,fuelVariance.x+fuelVariance.z,fuelVariance.x+fuelVariance.y};
  // Midway between a wing's two pods, a little behind their fans.
  const Vec3 left{3.3,-14.9,.03},right{3.3,14.9,.03};
  const Vec3 structureCg=-(left*podMass+right*podMass+c.fuel_position*c.initial_fuel)/structureMass;
  struct Part {double mass;Vec3 position,variance;};
  const std::array<Part,4> parts{{{structureMass,structureCg,structureVariance},
    {podMass,left,podVariance},{podMass,right,podVariance},{c.initial_fuel,c.fuel_position,fuelVariance}}};
  c.ixx=c.iyy=c.izz=c.ixz=0;
  for(const auto& part:parts) {
    const auto p=part.position,v=part.variance;
    c.ixx+=part.mass*(p.y*p.y+p.z*p.z+v.y+v.z);
    c.iyy+=part.mass*(p.x*p.x+p.z*p.z+v.x+v.z);
    c.izz+=part.mass*(p.x*p.x+p.y*p.y+v.x+v.y);
    c.ixz-=part.mass*p.x*p.z;
  }
  c.wing_area=370; c.wing_span=span; c.mac=7.0;
  // The wing is set nose-up on the fuselage, so the aircraft leaves the ground
  // and lands with its body nearly level. The real six degrees are halved
  // here to keep the fast, nose-down end of the envelope trimmable.
  c.alpha0=-3.5*kDeg2Rad; c.cl_alpha=4.6;
  c.cl_max_clean=1.30; c.cl_max_full_flap=1.95;
  c.flap_lift=.60;
  c.alpha_crit_clean=10*kDeg2Rad;
  c.cd0_clean=.016; c.oswald_e=.80;
  c.cm0=.02; c.cm_alpha=-.90; c.cm_de=-1.5; c.cm_q=-16;
  c.cl_beta=-.10; c.cl_p=-.55; c.cl_da=.15;
  c.cn_beta=.12; c.cn_r=-.20; c.cn_dr=-.07; c.cy_beta=-.60;
  // Wave drag fitted to the published 1,047 km/h (Mach 0.86) at altitude.
  c.mach_drag_onset=.78; c.mach_drag_peak=.075; c.mach_drag_supersonic=.05;
  // TF33-P-3/103: 75.6 kN each; TSFC 0.52 lb/(lbf h).
  c.engine_count=2; c.thrust_sl_static_each=4*75620; c.engine_tau=2.2;
  c.thrust_density_exponent=.80; c.dry_tsfc=1.47e-5;
  c.engine_pos_l=left; c.engine_pos_r=right;
  // The four main trucks stand in line under the fuselage. The forward pair
  // is the steered contact and the aft pair the main gear; the outrigger
  // wheels near the wingtips are the last two of the body contacts. The aft
  // contacts stand 3 m out, wider than the real 1.25 m, and the struts are
  // stiffly damped: together they steady the wings as the outriggers do.
  c.gear_nose={10.2,0,3.6};
  c.gear_main_l={-5.0,-3.0,3.6}; c.gear_main_r={-5.0,3.0,3.6};
  c.belly_contacts={{{16,0,2.17},{4,0,2.26},{-8,0,2.26},{-18,0,1.2},{-6.5,-22.6,3.25},{-6.5,22.6,3.25}}};
  c.oleo_stroke=.40; c.oleo_k=3.4e6; c.oleo_c=9.0e5;
  c.mu_brake_max=.50; c.mu_side=.60; c.mu_roll=.022;
  c.elev_min=-20*kDeg2Rad; c.elev_max=15*kDeg2Rad;
  c.ail_max=20*kDeg2Rad; c.rud_max=20*kDeg2Rad; c.flap_max_deg=35;
  configureSurfaces(c);
  return c;
}
} // namespace ofs
