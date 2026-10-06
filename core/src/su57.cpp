#include "ofs/aircraft_definition.hpp"
#include "ofs/physical_geometry.hpp"


namespace ofs {
void applySU57PhysicalConfig(AircraftConfig&);
AircraftConfig su57Config() {
  AircraftConfig c;
  c.provenance_dataset="data/physics/su57.json";
  // AL-41F1 / izdeliye 117 engineering configuration. All numerical maps,
  // dimensions, mass distribution, aerodynamic derivatives and FCS are estimates.
  c.control_law=FlightControlLaw::VectorFighter;
  applySU57PhysicalConfig(c);
  for(unsigned e=0;e<2;++e) {
    auto& engine=c.engines[e];
    engine.position=e==0?c.engine_pos_l:c.engine_pos_r;
    engine.nozzle_pivot=su57Geometry().nozzle_pivots[e];engine.articulated_nozzle=true;
    engine.dry_thrust=c.thrust_sl_static_each;engine.reheat_thrust=c.afterburner_thrust_each;
    engine.spool_seconds=c.engine_tau;engine.dry_tsfc=c.dry_tsfc;engine.reheat_tsfc=c.reheat_tsfc;
    // Axis, limits and rate come from the reviewed estimated JSON records.
  }
  c.engines_configured=true;
  // Reference inertia comes from a component mass distribution, independently
  // of response-rate tuning. Covariances are squared metre radii of gyration.
  struct Part {double mass;Vec3 position,variance;};
  const auto& distribution=su57MassDistribution();
  const double engineMass=distribution.engine_mass,structureMass=c.empty_mass-2*engineMass;
  const auto massL=su57Geometry().nozzle_pivots[0],massR=su57Geometry().nozzle_pivots[1];
  const Vec3 structureCg=-(massL*engineMass+massR*engineMass+
      c.fuel_position*c.initial_fuel+c.payload_position*c.initial_payload)/structureMass;
  const std::array<Part,5> parts{{
      {structureMass,structureCg,distribution.structure_variance},
      {engineMass,massL,distribution.engine_variance},
      {engineMass,massR,distribution.engine_variance},
      {c.initial_fuel,c.fuel_position,distribution.fuel_variance},
      {c.initial_payload,c.payload_position,distribution.payload_variance}
  }};
  c.ixx=c.iyy=c.izz=c.ixz=0;
  for(const auto& part:parts) {
    const auto p=part.position,v=part.variance;
    c.ixx+=part.mass*(p.y*p.y+p.z*p.z+v.y+v.z);
    c.iyy+=part.mass*(p.x*p.x+p.z*p.z+v.x+v.z);
    c.izz+=part.mass*(p.x*p.x+p.y*p.y+v.x+v.y);
    c.ixz-=part.mass*p.x*p.z;
  }
  configureSurfaces(c);
  for(unsigned i=0;i<surfaceCount;++i)c.surfaces[i].position=su57Geometry().force_sites[i];
  c.surfaces[4].name="twin_canted_fins";
  return c;
}
} // namespace ofs
