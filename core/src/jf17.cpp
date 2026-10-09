#include "ofs/aircraft_definition.hpp"
#include <array>

namespace ofs {
// PAC/CAC JF-17 Thunder Block II with one Klimov RD-93. Dimensions, weights and
// thrust are the published figures; aerodynamic derivatives, inertia, the
// thrust lapse and the control law are engineering estimates fitted to the
// published speed and load limits. See docs/AIRCRAFT_REFERENCE.md.
AircraftConfig jf17Config() {
  AircraftConfig c;
  c.control_law=FlightControlLaw::Fighter; c.pitch_arm=-4.6; c.pitch_span=1.9;
  c.max_pitch_rate=.70; c.max_roll_rate=3.3; c.response_time=.30;
  c.g_positive=8; c.g_negative=-3; c.alpha_limit=26*kDeg2Rad;
  c.actuator_rate=5;
  c.empty_mass=6586; c.fuel_capacity=2330;
  c.initial_fuel=1500; c.initial_payload=0; c.fuel_position={-.2,0,-.1}; c.payload_position={0,0,.4};
  c.mass=c.empty_mass+c.initial_fuel;
  // Component inertia: 5,531 kg structure and one 1,055 kg engine.
  constexpr double length=14.93,span=9.44,height=4.77,engineMass=1055;
  const double structureMass=c.empty_mass-engineMass;
  const Vec3 structureVariance{std::pow(.20*length,2),std::pow(.16*span,2),std::pow(.10*height,2)};
  const Vec3 fuelVariance{3,.6,0},engine{-4.4,0,0};
  c.fuel_inertia_per_kg={fuelVariance.y+fuelVariance.z,fuelVariance.x+fuelVariance.z,fuelVariance.x+fuelVariance.y};
  const Vec3 structureCg=-(engine*engineMass+c.fuel_position*c.initial_fuel)/structureMass;
  struct Part {double mass;Vec3 position,variance;};
  const std::array<Part,3> parts{{{structureMass,structureCg,structureVariance},
    {engineMass,engine,{.9,.06,.06}},{c.initial_fuel,c.fuel_position,fuelVariance}}};
  c.ixx=c.iyy=c.izz=c.ixz=0;
  for(const auto& part:parts) {
    const auto p=part.position,v=part.variance;
    c.ixx+=part.mass*(p.y*p.y+p.z*p.z+v.y+v.z);
    c.iyy+=part.mass*(p.x*p.x+p.z*p.z+v.x+v.z);
    c.izz+=part.mass*(p.x*p.x+p.y*p.y+v.x+v.y);
    c.ixz-=part.mass*p.x*p.z;
  }
  c.wing_area=24.43; c.wing_span=9.44; c.mac=2.9;
  c.alpha0=-.5*kDeg2Rad; c.cl_alpha=3.1;
  c.cl_max_clean=1.45; c.cl_max_full_flap=1.80;
  c.flap_lift=.35;
  c.alpha_crit_clean=20*kDeg2Rad;
  c.cd0_clean=.021; c.oswald_e=.80;
  c.cm0=.015; c.cm_alpha=-.25; c.cm_de=-1.0; c.cm_q=-14;
  c.cl_beta=-.07; c.cl_p=-.45; c.cl_da=.18;
  c.cn_beta=.12; c.cn_r=-.25; c.cn_dr=-.10; c.cy_beta=-.60;
  c.mach_drag_onset=.88; c.mach_drag_peak=.042; c.mach_drag_supersonic=.018;
  // RD-93: 49.4 kN dry, 84.4 kN with reheat; TSFC 0.77 and 2.05 lb/(lbf h).
  c.engine_count=1; c.thrust_sl_static_each=49400; c.afterburner_thrust_each=84400;
  c.afterburner_threshold=.85; c.engine_tau=.9;
  c.thrust_density_exponent=.55; c.thrust_ram_gain=.22; c.thrust_ram_supersonic=.22;
  c.dry_tsfc=2.18e-5; c.reheat_tsfc=5.8e-5;
  // Anchors measured from the donor model; see assets/aircraft/jf17/README.md.
  c.engine_pos_l=c.engine_pos_r={-5.81,0,0};
  c.gear_nose={4.547,0,1.876};
  c.gear_main_l={-.55,-1.188,1.876}; c.gear_main_r={-.55,1.188,1.876};
  c.belly_contacts={{{4.5,0,.78},{-3.0,-.45,.76},{-3.0,.45,.76},{0,0,.84},{-1.9,-4.75,.33},{-1.9,4.75,.33}}};
  c.oleo_stroke=.25; c.oleo_k=330000; c.oleo_c=20000;
  c.mu_brake_max=.58; c.mu_side=.65; c.mu_roll=.023;
  c.elev_min=-25*kDeg2Rad; c.elev_max=15*kDeg2Rad;
  c.ail_max=20*kDeg2Rad; c.rud_max=25*kDeg2Rad; c.flap_max_deg=25;
  configureSurfaces(c);
  return c;
}
} // namespace ofs
