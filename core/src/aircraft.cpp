#include "ofs/aircraft.hpp"
#include "ofs/airliner.hpp"
#include "ofs/physical_geometry.hpp"

namespace ofs {

double inletSpikeTarget(double mach) {
  return std::isfinite(mach) ? clamp((mach-1.6)/1.6,0,1) : 0;
}
double inletPressureRecovery(double mach, double normalizedSpike) {
  if (!std::isfinite(mach) || !std::isfinite(normalizedSpike)) return .20;
  const double mismatch=clamp(normalizedSpike,0,1)-inletSpikeTarget(mach);
  const double sensitivity=lerp(.4,3.2,clamp((mach-1)/2,0,1));
  return .20+.80*std::exp(-sensitivity*mismatch*mismatch);
}

void applyA320PhysicalConfig(AircraftConfig&);
AircraftConfig a320Config() {
  AircraftConfig c;
  applyA320PhysicalConfig(c);
  c.provenance_dataset="data/physics/a320.json";
  c.aero_kind=AeroModelKind::AirlinerEngineering;
  c.jet_kind=JetModelKind::EngineDeck;
  c.propulsion_model=std::make_shared<CFM565B4EngineeringDeck>();
  // Component inertia, with estimated mass allocations and distributed load.
  // Structure centroid balances reference fuel/payload and engine first moments.
  const auto& distribution=a320MassDistribution();
  const double engineMass=distribution.engine_mass,structureMass=c.empty_mass-2*engineMass;
  const auto structureCg=-(c.engine_pos_l*engineMass+c.engine_pos_r*engineMass+
      c.fuel_position*c.initial_fuel+c.payload_position*c.initial_payload)/structureMass;
  struct Part {double mass;Vec3 position,variance;};
  const std::array<Part,5> parts{{
    {structureMass,structureCg,distribution.structure_variance},
    {engineMass,c.engine_pos_l,distribution.engine_variance},
    {engineMass,c.engine_pos_r,distribution.engine_variance},
    {c.initial_fuel,c.fuel_position,distribution.fuel_variance},
    {c.initial_payload,c.payload_position,distribution.payload_variance}}};
  c.ixx=c.iyy=c.izz=c.ixz=0;
  for(const auto& p:parts) {
    c.ixx+=p.mass*(p.position.y*p.position.y+p.position.z*p.position.z+p.variance.y+p.variance.z);
    c.iyy+=p.mass*(p.position.x*p.position.x+p.position.z*p.position.z+p.variance.x+p.variance.z);
    c.izz+=p.mass*(p.position.x*p.position.x+p.position.y*p.position.y+p.variance.x+p.variance.y);
    c.ixz-=p.mass*p.position.x*p.position.z;
  }
  configureSurfaces(c);
  for(unsigned i=0;i<surfaceCount;++i)c.surfaces[i].position=a320Geometry().force_sites[i];
  return c;
}

void configureSurfaces(AircraftConfig &c) {
  c.surfaces = {
      {{"wing_L", SurfaceRole::Wing, {0, -.22 * c.wing_span, -.1}, .5},
       {"wing_R", SurfaceRole::Wing, {0, .22 * c.wing_span, -.1}, .5},
       {c.pitch_arm > 0 ? "canard_L" : "tail_L",
        SurfaceRole::Pitch,
        {c.pitch_arm, -c.pitch_span, 0},
        .04},
       {c.pitch_arm > 0 ? "canard_R" : "tail_R",
        SurfaceRole::Pitch,
        {c.pitch_arm, c.pitch_span, 0},
        .04},
       {"fin", SurfaceRole::Fin, {-std::abs(c.pitch_arm), 0, -1.5}, .05},
       {"body", SurfaceRole::Body, {0, 0, 0}, .10}}};
  if (c.control_law == FlightControlLaw::Canard) {
    c.surfaces[0].position.x = -1.2;
    c.surfaces[1].position.x = -1.2;
  }
  c.surfaces_configured = true;
}
} // namespace ofs
