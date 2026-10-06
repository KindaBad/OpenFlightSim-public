#pragma once
#include "ofs/math.hpp"
#include <array>
#include <span>
namespace ofs {
struct GeometryHinge {
  const char* name{};
  const char* channel{};
  Vec3 position{}; // reference body FRD, metres
};
struct PhysicalGeometry {
  double length{}, span{}, height{}, wing_area{}, mac{}, sweep{};
  Vec3 asset_cg{}; // glTF: aft X, up Y, port Z; nose X=0, wheels Y=0
  Vec3 aerodynamic_reference{};
  std::array<Vec3,2> engines{}, nozzle_pivots{};
  std::array<Vec3,3> gear{};
  std::span<const GeometryHinge> hinges;
  std::array<Vec3,6> force_sites{};
  Vec3 toAsset(Vec3 body) const { return {asset_cg.x-body.x,asset_cg.y-body.z,asset_cg.z-body.y}; }
  Vec3 toBody(Vec3 asset) const { return {asset_cg.x-asset.x,asset_cg.z-asset.z,asset_cg.y-asset.y}; }
};
struct ComponentMassDistribution {
  double engine_mass{};
  Vec3 structure_variance{},engine_variance{},fuel_variance{},payload_variance{};
};
const ComponentMassDistribution& a320MassDistribution();
const ComponentMassDistribution& su57MassDistribution();
const PhysicalGeometry& a320Geometry();
const PhysicalGeometry& su57Geometry();
}
