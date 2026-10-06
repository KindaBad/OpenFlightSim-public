#pragma once
#include "gltf.hpp"
#include "mesh.hpp"
#include "ofs/aircraft.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace ofs::client {
// Proper right-handed rotation: NED -> east/up/south. Subtract doubles first.
inline glm::vec3 localPosition(const Vec3& p, const Vec3& origin) {
  const Vec3 d = p - origin;
  return {static_cast<float>(d.y), static_cast<float>(-d.z), static_cast<float>(-d.x)};
}
inline glm::vec3 renderDirection(const Vec3& d) { return localPosition(d, {}); }
inline State interpolate(const State& previous, const State& current, double alpha) {
  State result = current;
  result.pos_ned = previous.pos_ned * (1-alpha) + current.pos_ned * alpha;
  for(unsigned e=0;e<2;++e) {
    result.n1[e]=lerp(previous.n1[e],current.n1[e],alpha);
    result.afterburner[e]=lerp(previous.afterburner[e],current.afterburner[e],alpha);
    result.inlet_spike[e]=lerp(previous.inlet_spike[e],current.inlet_spike[e],alpha);
    result.nozzle_angle[e]=lerp(previous.nozzle_angle[e],current.nozzle_angle[e],alpha);
  }
  for(auto pair:{std::pair{&result.elevator,std::pair{previous.elevator,current.elevator}},
    {&result.aileron,{previous.aileron,current.aileron}},{&result.rudder,{previous.rudder,current.rudder}},
    {&result.flap,{previous.flap,current.flap}},{&result.spoiler,{previous.spoiler,current.spoiler}},
    {&result.canard,{previous.canard,current.canard}},{&result.elevon_l,{previous.elevon_l,current.elevon_l}},
    {&result.elevon_r,{previous.elevon_r,current.elevon_r}}}) *pair.first=lerp(pair.second.first,pair.second.second,alpha);
  Quat b = current.att; const Quat& a = previous.att;
  if(a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z < 0) b = {-b.w,-b.x,-b.y,-b.z};
  result.att = Quat{lerp(a.w,b.w,alpha), lerp(a.x,b.x,alpha),
                    lerp(a.y,b.y,alpha), lerp(a.z,b.z,alpha)}.normalized();
  return result;
}

// Body FRD pose -> render-space 4x4. Columns are the body basis vectors through
// the attitude quaternion, then the NED/render rotation; the translation is the
// aircraft position relative to the shared render origin.
inline glm::mat4 aircraftMatrix(const State& s, const Vec3& origin) {
  glm::mat4 m{1};
  m[0] = glm::vec4(renderDirection(s.att.rotate({1,0,0})), 0);
  m[1] = glm::vec4(renderDirection(s.att.rotate({0,1,0})), 0);
  m[2] = glm::vec4(renderDirection(s.att.rotate({0,0,1})), 0);
  m[3] = glm::vec4(localPosition(s.pos_ned, origin), 1);
  return m;
}

// ---------------------------------------------------------------------------
// Visual model placement
// ---------------------------------------------------------------------------
//
// The A320 asset is authored in Blender Z-up with the nose at X=0 and the tail
// at X=37.57. The glTF export converts that to Y-up, so in asset space +X runs
// aft, +Y is up and +Z is to port. Mapping to body FRD (X fwd, Y right, Z down)
// is therefore a pure rotation:
//
//     body.x = -(asset.x - cg.x)      +X asset is aft, so forward negates
//     body.y = -(asset.z - cg.z)      +Z asset is port, right negates
//     body.z = -(asset.y - cg.y)      +Y asset is up, down negates
//
// The CG anchor (cg) is NOT in the asset; it was measured against the A320 and
// then verified against the validated sim geometry:
//
//   cg.x = 15.51 m   main wheels sit at asset X=17.71 and the sim places the
//                     main gear 2.2 m *aft* of the CG. Aft is body -X, and
//                     body.x = -(asset.x - cg.x), so the wheel landing at
//                     -2.2 needs cg.x = 17.71 - 2.2 = 15.51. (Adding instead
//                     of subtracting puts the gear 2.2 m *forward* of the CG,
//                     which mirrors the airframe about the gear.)
//                     Nose contact is body +10.44 m, giving the measured
//                     12.64 m wheelbase; M3.68 corrected the prior 9.5 m station.
//   cg.y = 3.55 m     matches AircraftConfig::gear_nose.z, the sim's contact
//                     height below the CG. The model's tyres touch asset Y=0,
//                     so this puts the wheels exactly on the runway.
//   cg.z = 0          the asset is laterally symmetric about its centreline.
//
// The rotation above is applied by the renderer; this header only documents it
// and provides the constants for tests and the HUD.
struct ModelAnchor {
  static constexpr double cgX = 15.51;
  static constexpr double cgY = 3.55;
  static constexpr double cgZ = 0.0;

  // Maps a point from asset space to body FRD.
  static Vec3 assetToBody(const Vec3& asset) {
    return {-(asset.x - cgX), -(asset.z - cgZ), -(asset.y - cgY)};
  }
  // Maps a direction (no translation).
  static Vec3 assetDirectionToBody(const Vec3& asset) {
    return {-asset.x, -asset.z, -asset.y};
  }
};

// Asset-space key points, measured from the loaded model, for muzzle flashes,
// contrails and engine markers. Stored as asset space so they are independent
// of the CG anchor.
namespace assetPoints {
  // Engine exhaust centres (nacelle aft ends), asset space.
  inline Vec3 engineL() { return {16.6, 1.5, 5.75}; }
  inline Vec3 engineR() { return {16.6, 1.5, -5.75}; }
  // Wingtip sharklet trailing edge, asset space.
  inline Vec3 wingtipL() { return {22.8, 4.6, 17.2}; }
  inline Vec3 wingtipR() { return {22.8, 4.6, -17.2}; }
  // Nose radome, for the first-person eye point.
  inline Vec3 cockpit() { return {3.6, 4.1, 0.0}; }
}

}  // namespace ofs::client
