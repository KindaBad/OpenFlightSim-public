#include "ofs/geometry_debug.hpp"
#include "ofs/physical_geometry.hpp"
namespace ofs {
std::vector<GeometryDebugLine> geometryDebugLines(const Simulator& sim,AircraftType type) {
  const PhysicalGeometry* geometry=type==AircraftType::A320?&a320Geometry():type==AircraftType::Su57?&su57Geometry():nullptr;
  const auto mass=sim.massProperties();const auto& state=sim.state();
  const auto world=[&](Vec3 p){return state.pos_ned+state.att.rotate(p-mass.cg);};
  std::vector<GeometryDebugLine> lines;
  const auto marker=[&](Vec3 p,std::uint32_t color) {
    for(auto axis:{Vec3{.35,0,0},Vec3{0,.35,0},Vec3{0,0,.35}})
      lines.push_back({world(p-axis),world(p+axis),color});
  };
  marker(mass.cg,0xffff00ff); // magenta actual CG
  marker(geometry?geometry->aerodynamic_reference:Vec3{},0xffffff00); // cyan reference
  const auto axes=mass.tensor().principalAxes();
  for(unsigned i=0;i<3;++i)lines.push_back({state.pos_ned,state.pos_ned+state.att.rotate(axes[i]*4),
      std::array<std::uint32_t,3>{0xff4444ff,0xff44ff44,0xffff4444}[i]});
  const auto aero=sim.evalAero();const auto thrust=sim.evalThrust();
  for(const auto& force:aero.surfaces) {
    const Vec3 p=force.pos_body+mass.cg;
    marker(p,0xff44ff44);
    lines.push_back({world(p),world(p)+state.att.rotate(force.force_body)*.00003,0xff44ff44});
  }
  if(sim.config().levcon_lift_share>0)for(const auto& force:aero.levcons) {
    const Vec3 p=force.pos_body+mass.cg;marker(p,0xff44ff44);
    lines.push_back({world(p),world(p)+state.att.rotate(force.force_body)*.00003,0xff44ff44});
  }
  for(unsigned e=0;e<sim.config().engine_count;++e) {
    marker(thrust.position[e],0xff0088ff);
    lines.push_back({world(thrust.position[e]),world(thrust.position[e])+state.att.rotate(thrust.force[e])*.00005,0xff0088ff});
  }
  for(const auto& point:{sim.config().gear_nose,sim.config().gear_main_l,sim.config().gear_main_r})marker(point,0xffffffff);
  if(geometry)for(const auto& hinge:geometry->hinges)marker(hinge.position,0xffff00ff);
  return lines;
}
}
