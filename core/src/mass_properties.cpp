#include "ofs/simulator.hpp"
#include "ofs/aerodynamics.hpp"
#include "ofs/control_allocation.hpp"
#include "flight_model_detail.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
namespace ofs {
using namespace detail;
Simulator::MassProperties Simulator::massProperties() const { return massProperties(state_); }
Simulator::MassProperties Simulator::massProperties(const State& state_) const {
  const double fuel =
      state_.fuel_mass < 0 ? cfg_.initial_fuel : state_.fuel_mass;
  const double payload =
      state_.payload_mass < 0 ? cfg_.initial_payload : state_.payload_mass;
  const double basic = cfg_.empty_mass > 0 ? cfg_.empty_mass
                                           : cfg_.mass - cfg_.initial_fuel -
                                                 cfg_.initial_payload;
  MassProperties m;
  m.mass = basic + fuel + payload;
  m.cg = loadedCg(cfg_, state_);
  m.inertia = {cfg_.ixx, cfg_.iyy, cfg_.izz};
  m.inertia+=cfg_.fuel_inertia_per_kg*(fuel-cfg_.initial_fuel);
  m.inertia+=cfg_.payload_inertia_per_kg*(payload-cfg_.initial_payload);
  m.ixz = cfg_.ixz; m.ixy = cfg_.ixy; m.iyz = cfg_.iyz;
  auto point = [&](double dm, Vec3 pos) {
    m.inertia.x += dm * (pos.y * pos.y + pos.z * pos.z);
    m.inertia.y += dm * (pos.x * pos.x + pos.z * pos.z);
    m.inertia.z += dm * (pos.x * pos.x + pos.y * pos.y);
    m.ixz -= dm * pos.x * pos.z;
    m.ixy -= dm * pos.x * pos.y;
    m.iyz -= dm * pos.y * pos.z;
  };
  point(fuel - cfg_.initial_fuel, cfg_.fuel_position);
  point(-cfg_.initial_payload, cfg_.payload_position);
  point(payload, cfg_.payload_position + state_.payload_offset);
  m.inertia += state_.payload_inertia_correction;
  m.ixy += state_.payload_products_correction.x;
  m.ixz += state_.payload_products_correction.y;
  m.iyz += state_.payload_products_correction.z;
  // Parallel-axis correction to the actual CG.
  point(-m.mass, m.cg);
  return m;
}

} // namespace ofs
