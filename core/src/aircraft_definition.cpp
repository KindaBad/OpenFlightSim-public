#include "ofs/aircraft_definition.hpp"
#include "ofs/physical_geometry.hpp"
#include <array>
#include <stdexcept>

namespace ofs {

AircraftConfig typhoonConfig() {
  AircraftConfig c;
  c.initial_fuel=3000; c.initial_payload=0; c.fuel_position={-.25,0,0}; c.payload_position={0,0,.5};
  c.control_law=FlightControlLaw::Canard; c.pitch_arm=3.1; c.pitch_span=1.6;
  c.max_pitch_rate=.75; c.max_roll_rate=3.5; c.response_time=.25;
  c.g_positive=9; c.g_negative=-3; c.alpha_limit=28*kDeg2Rad;
  c.actuator_rate=6; c.mach_drag_onset=.90; c.mach_drag_peak=.035; c.mach_drag_supersonic=.018;
  c.thrust_density_exponent=.50; c.thrust_ram_gain=.22; c.dry_tsfc=2.2e-5; c.reheat_tsfc=4.8e-5; // engineering TSFC approximation; no verified manufacturer curve
  c.mass=14000; // Representative fueled clean aircraft, not empty weight.
  // Reconstruct inertia from estimated component geometry, independently of FCS gains.
  // Basic mass 11000 kg: 9000 structure + two 1000 kg installed engines.
  c.empty_mass=11000;
  constexpr double length=15.96,span=10.95,height=5.28;
  const Vec3 structureVariance{std::pow(.20*length,2),std::pow(.16*span,2),std::pow(.10*height,2)};
  const Vec3 fuelVariance{6,2.5,0};
  c.fuel_inertia_per_kg={fuelVariance.y+fuelVariance.z,fuelVariance.x+fuelVariance.z,fuelVariance.x+fuelVariance.y};
  const Vec3 left{-4.45,-.63,.05},right{-4.45,.63,.05};
  const Vec3 structureCg=-(left*1000+right*1000+c.fuel_position*c.initial_fuel)/9000;
  struct Part {double mass;Vec3 position,variance;};
  const std::array<Part,4> parts{{{9000,structureCg,structureVariance},
    {1000,left,{.64,.09,.09}},{1000,right,{.64,.09,.09}},
    {c.initial_fuel,c.fuel_position,fuelVariance}}};
  c.ixx=c.iyy=c.izz=c.ixz=0;
  for(const auto& part:parts) {
    const auto p=part.position,v=part.variance;
    c.ixx+=part.mass*(p.y*p.y+p.z*p.z+v.y+v.z);
    c.iyy+=part.mass*(p.x*p.x+p.z*p.z+v.x+v.z);
    c.izz+=part.mass*(p.x*p.x+p.y*p.y+v.x+v.y);
    c.ixz-=part.mass*p.x*p.z;
  }
  c.wing_area=50; c.wing_span=10.95; c.mac=4.3;
  c.alpha0=-.5*kDeg2Rad; c.cl_alpha=3.8;
  c.cl_max_clean=1.50; c.cl_max_full_flap=1.85;
  c.flap_lift=.35;
  c.alpha_crit_clean=21*kDeg2Rad;
  c.cd0_clean=.024; c.oswald_e=.72;
  // Stable equivalent to augmented canard/delta response. Not raw unstable FCS.
  c.cm0=.018; c.cm_alpha=-.15; c.cm_de=-1.10; c.cm_q=-18;
  c.cl_beta=-.055; c.cl_p=-.55; c.cl_da=.24;
  c.cn_beta=.11; c.cn_r=-.26; c.cn_dr=-.115; c.cy_beta=-.60;
  c.engine_count=2; c.thrust_sl_static_each=60000; c.engine_tau=.65;
  c.afterburner_thrust_each=90000; c.afterburner_threshold=.85;
  c.engine_pos_l={-4.45,-.63,.05}; c.engine_pos_r={-4.45,.63,.05};
  c.gear_nose={3.97,0,2.05};
  c.belly_contacts={{{3,0,.8},{-4.3,-.63,.95},{-4.3,.63,.95},{0,0,1.3},{-2.2,-5.4,.15},{-2.2,5.4,.15}}};
  c.gear_main_l={-1.0,-1.45,2.05}; c.gear_main_r={-1.0,1.45,2.05};
  c.oleo_stroke=.32; c.oleo_k=560000; c.oleo_c=33000;
  c.mu_brake_max=.58; c.mu_side=.65; c.mu_roll=.023;
  c.elev_min=-23*kDeg2Rad; c.elev_max=18*kDeg2Rad;
  c.ail_max=20*kDeg2Rad; c.rud_max=25*kDeg2Rad; c.flap_max_deg=15;
  configureSurfaces(c);
  return c;
}

std::span<const AircraftDefinition> aircraftDefinitions() {
  static const auto definitions = [] {
    GunConfig typhoonGun;
    typhoonGun.rpm=1700; typhoonGun.muzzleVelocity=1000; typhoonGun.ammo=150;
    typhoonGun.muzzle={4.0,.82,.29}; // Starboard intake shoulder, body FRD.
    typhoonGun.damage=34; // Gameplay damage, not a ballistic lethality claim.
    const std::array<AircraftDefinition::CollisionSphere,17> typhoonBoxes{{
      {{7.85,0,-.52},.40},{{6.35,0,-.62},.68},{{4.85,0,-.54},.74},{{3.35,0,-.47},.85},
      {{1.35,0,-.30},1.02},{{-.65,0,-.20},1.13},{{-2.65,0,-.13},1.16},{{-4.65,0,.05},.90},
      {{.85,-1.8,.05},1.02},{{-.95,-3.0,.05},.90},{{-2.55,-4.1,.05},.76},{{-2.9,-5.0,.05},.52},
      {{.85,1.8,.05},1.02},{{-.95,3.0,.05},.90},{{-2.55,4.1,.05},.76},{{-2.9,5.0,.05},.52},
      {{-4.25,0,-1.95},.90}}};
    const std::array<AircraftDefinition::CollisionSphere,17> sr71Boxes{{
      {{19,0,.2},.5},{{16,0,.1},.9},{{12,0,0},1.3},{{8,0,0},1.5},
      {{4,0,0},1.7},{{0,0,0},1.9},{{-4,0,0},1.7},{{-9,0,0},1.3},
      {{2,-4.14,-.15},1},{{-1.5,-4.14,-.15},1.2},{{-5.5,-4.14,-.15},1.2},{{-8.2,-7,0},1.4},
      {{2,4.14,-.15},1},{{-1.5,4.14,-.15},1.2},{{-5.5,4.14,-.15},1.2},{{-8.2,7,0},1.4},
      {{-8,0,-1.2},1.8}}};
    GunConfig su57Gun;
    su57Gun.rpm=1500;su57Gun.muzzleVelocity=860;su57Gun.ammo=150;
    su57Gun.muzzle={5.2,1.15,-.12};su57Gun.damage=34;
    const std::array<AircraftDefinition::CollisionSphere,17> su57Boxes{{
      {{9.2,0,.20},.35},{{7.6,0,.18},.70},{{5.8,0,-.20},.90},{{3.8,0,.15},1.10},
      {{1.6,0,.2},1.25},{{-.6,0,.25},1.35},{{-2.8,0,.20},1.40},{{-5.8,0,.16},1.35},
      {{-.8,-2.1,.10},1.20},{{-2.3,-3.6,.10},1.05},{{-3.3,-5.0,.10},.80},{{-3.5,-6.3,.10},.65},
      {{-.8,2.1,.10},1.20},{{-2.3,3.6,.10},1.05},{{-3.3,5.0,.10},.80},{{-3.5,6.3,.10},.65},
      {{-5.1,0,-1.15},1.15}}};
    auto definitions = std::array{
      AircraftDefinition
      {AircraftType::A320, "a320", "Airbus A320-214 | CFM56-5B4/P reference", "output/Airbus_A320.glb", a320Config(),
       {{15.51, 3.55, 0}, {13.6, 0, -.55}, {-68, 16, -19}, {-60, 0, -13}, {}, {},
        {{-.2, -5.75, 1.71}, {-.2, 5.75, 1.71}}, {{-6.0, -17.6, -.5}, {-6.0, 17.6, -.5}},
        26, 5, 4, .61, .39, .025}, {1, 1, 1}, std::nullopt},
      AircraftDefinition{AircraftType::Typhoon, "typhoon", "Eurofighter Typhoon | Austria 7L-WA", "assets/typhoon/typhoon_lod0.glb", typhoonConfig(),
       {{9.35,2.05,0}, {5.12,0,-1.40}, {-29,5,-8}, {-20,0,-5}, {-1,0,0}, {-1,0,0},
        {{-5.46,-.63,.05},{-5.46,.63,.05}}, {{-2.92,-5.40,.04},{-2.92,5.40,.04}},
        9.5,4,2,.46,.31,.045,true}, {.46,.33,.55}, typhoonGun,
        {"assets/typhoon/typhoon_lod1.glb","assets/typhoon/typhoon_lod2.glb","assets/typhoon/typhoon_lod3.glb"},typhoonBoxes},
      AircraftDefinition{AircraftType::SR71,"sr71","Lockheed SR-71A | USAF 61-7972",
        "assets/aircraft/sr71/sr71_lod0.glb",sr71Config(),
        {{20.62,2.55,0},{14.68,0,-.92},{-68,11,-18},{-42,0,-8},{-1.5,0,0},{3.5,0,0},
          {{-9.43,-4.14,-.15},{-9.43,4.14,-.15}},{{-8.4,-8.38,.07},{-8.4,8.38,.07}},
          20.8,7,2,.34925,.3175,.020,true,1.9,1.6,-10*kDeg2Rad}, {1,1,1},std::nullopt,
        {"assets/aircraft/sr71/sr71_lod1.glb","assets/aircraft/sr71/sr71_lod2.glb","assets/aircraft/sr71/sr71_lod3.glb"},sr71Boxes},
      AircraftDefinition{AircraftType::Su57,"su57","Sukhoi Su-57 | AL-41F1 approximation",
        "assets/aircraft/su57/su57_lod0.glb",su57Config(),
        {{10.95,2.45,0},{6.70,0,-.98},{-37,6,-10},{-26,0,-6},{-.5,0,0},{-.5,0,0},
          {{-7.85,-1.34,.16},{-7.85,1.34,.16}},{{-3.2,-6.95,.10},{-3.2,6.95,.10}},
          13,5,2,.515,.33,.030,true,1.35,1.1}, {1,1,1},su57Gun,
        {"assets/aircraft/su57/su57_lod1.glb","assets/aircraft/su57/su57_lod2.glb","assets/aircraft/su57/su57_lod3.glb"},su57Boxes}
    };
    // A320/Su-57 renderer and physics consume the same geometry anchors.
    for(auto& d:definitions) {
      const PhysicalGeometry* g=d.type==AircraftType::A320?&a320Geometry():
          d.type==AircraftType::Su57?&su57Geometry():nullptr;
      if(g) {d.visual.assetCg=g->asset_cg;
        for(unsigned e=0;e<2;++e)d.visual.exhaust[e]=g->engines[e];}
    }
    return definitions;
  }();
  return definitions;
}

bool validAircraftType(AircraftType type) {
  for (const auto& definition : aircraftDefinitions())
    if (definition.type == type) return true;
  return false;
}

const AircraftDefinition& aircraftDefinition(AircraftType type) {
  for (const auto& definition : aircraftDefinitions())
    if (definition.type == type) return definition;
  throw std::invalid_argument("unknown aircraft type ID");
}

AircraftType aircraftTypeFromName(std::string_view name) {
  for (const auto& definition : aircraftDefinitions())
    if (definition.key == name) return definition.type;
  throw std::invalid_argument("unknown --aircraft key; see aircraft registry");
}
} // namespace ofs
