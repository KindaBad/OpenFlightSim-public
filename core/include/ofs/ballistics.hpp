#pragma once
#include "ofs/aircraft_definition.hpp"
#include "ofs/terrain.hpp"
#include <limits>

namespace ofs {
// Shared deterministic shot dispersion and gravity for authority and solo play.
inline Vec3 gunShotDirection(const GunConfig& gun, std::uint64_t sequence) {
  const double phase = double(sequence % 10007) * 2.399963229728653;
  const auto side = gun.direction.cross(std::abs(gun.direction.z) < .9
      ? Vec3{0,0,1} : Vec3{0,1,0}).normalized();
  const auto up = side.cross(gun.direction).normalized();
  return (gun.direction + (side*std::cos(phase)+up*std::sin(phase))*gun.dispersion).normalized();
}
inline Vec3 ballisticDisplacement(Vec3 velocity, double dt) {
  return velocity*dt + Vec3{0,0,.5*kG0*dt*dt};
}
// Sweep against the same triangle terrain the renderer and aircraft use.
// Samples at <=10 m then bisects first contact; bounded by the gun speed/tick.
inline double bulletTerrainFraction(Vec3 start, Vec3 end) {
  const auto clearance = [](Vec3 p) { return groundHeightNed(p.x,p.y)-p.z; };
  if (clearance(start) <= 0) return 0;
  const int steps = std::clamp(int(std::ceil((end-start).norm()/10.)),1,128);
  double before = 0;
  for (int i=1;i<=steps;++i) {
    double after = double(i)/steps;
    if (clearance(start+(end-start)*after) <= 0) {
      for (int j=0;j<18;++j) {
        const double mid = (before+after)*.5;
        if (clearance(start+(end-start)*mid) > 0) before=mid; else after=mid;
      }
      return after;
    }
    before=after;
  }
  return std::numeric_limits<double>::infinity();
}
} // namespace ofs
