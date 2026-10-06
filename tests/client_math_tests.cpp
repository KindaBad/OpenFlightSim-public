#include "coordinates.hpp"
#include "camera.hpp"
#include <cmath>
#include <iostream>
int main() {
  using namespace ofs; using namespace ofs::client;
  const auto local = localPosition({1e9+.125,1e9+.25,-10},{1e9,1e9,0});
  if(local.x != .25f || local.y != 10 || local.z != -.125f) return 1;
  const auto east = renderDirection({0,1,0}), up = renderDirection({0,0,-1}), north = renderDirection({1,0,0});
  if(glm::length(glm::cross(east,up)+north) > 1e-6f) return 2;
  State a,b; a.pos_ned={1e9,0,0}; b.pos_ned={1e9+2,0,0}; b.att={-1,0,0,0};
  const auto midpoint = interpolate(a,b,.5);
  if(midpoint.pos_ned.x != 1e9+1 || std::abs(midpoint.att.w) != 1) return 3;
  b.att=quatFromEuler(0, 30*kDeg2Rad, 0); const auto m=aircraftMatrix(b,b.pos_ned);
  if(m[0].y <= 0) return 4;
  // The aircraft body basis must stay a proper rotation (det +1) for any
  // attitude, or the render pipeline would mirror the model.
  for (double r = -80; r <= 80; r += 40)
    for (double p = -80; p <= 80; p += 40)
      for (double y = -180; y <= 180; y += 90) {
        State s; s.att = quatFromEuler(r*kDeg2Rad, p*kDeg2Rad, y*kDeg2Rad);
        const auto t = aircraftMatrix(s, {});
        const float det = glm::determinant(glm::mat3(t));
        if (std::abs(det - 1.0f) > 1e-4f) return 5;
        if (std::abs(glm::length(glm::vec3(t[0])) - 1.0f) > 1e-4f) return 6;
      }
  // Asset-to-body mapping: +X asset is aft so it must become -X body (forward
  // is the model nose direction), and the map must preserve handedness.
  const Vec3 assetForward{-1, 0, 0};
  const Vec3 bodyForward = ModelAnchor::assetDirectionToBody(assetForward);
  if (std::abs(bodyForward.x - 1.0) > 1e-9) return 7;   // nose points +X body
  if (std::abs(ModelAnchor::assetDirectionToBody({0, 0, 1}).y + 1.0) > 1e-9) return 8;
  if (std::abs(ModelAnchor::assetDirectionToBody({0, 1, 0}).z + 1.0) > 1e-9) return 9;
  // CG anchor: the model's main wheels sit 2.2 m aft of the CG (sim value),
  // and the anchor is chosen so that relationship holds.
  const Vec3 mainWheel = ModelAnchor::assetToBody({17.71, 0.0, 3.85});
  if (std::abs(mainWheel.x + 2.2) > 1e-6) return 10;   // 2.2 m aft
  if (std::abs(mainWheel.z - 3.55) > 1e-6) return 11;  // 3.55 m below CG
  // The main gear must line up laterally with the sim's +-3.8 m track.
  if (std::abs(std::abs(mainWheel.y) - 3.8) > 0.06) return 12;
  // The nose wheels must sit on the ground when the CG is at its rest height.
  const Vec3 noseWheel = ModelAnchor::assetToBody({5.07, 0.0, 0.0});
  if (std::abs(noseWheel.z - 3.55) > 1e-6) return 13;
  // Handedness of the asset-to-body map: an orthogonal determinant +1 basis.
  const Vec3 bx = ModelAnchor::assetDirectionToBody({1, 0, 0});
  const Vec3 by = ModelAnchor::assetDirectionToBody({0, 1, 0});
  const Vec3 bz = ModelAnchor::assetDirectionToBody({0, 0, 1});
  const double det = bx.x * (by.y * bz.z - by.z * bz.y)
                   - by.x * (bx.y * bz.z - bx.z * bz.y)
                   + bz.x * (bx.y * by.z - bx.z * by.y);
  if (std::abs(det - 1.0) > 1e-9) return 14;
  // Camera regressions: cockpit must face forward, orbit pitch must change
  // altitude, and mode switches must not carry an old camera's orientation.
  State parked; parked.pos_ned={0,0,-3.4};
  Camera camera;
  camera.update(CameraMode::FirstPerson,parked,.016,true);
  if ((camera.target-camera.eye).dot(parked.att.rotate({1,0,0})) < 999) return 15;
  camera.orbitYaw=.7; camera.orbitPitch=.2;
  camera.update(CameraMode::Orbit,parked,.016,false);
  const double lowEye=camera.eye.z;
  const double radius=(camera.eye-parked.pos_ned).norm();
  camera.orbitPitch=.6;
  camera.update(CameraMode::Orbit,parked,1.0,false);
  if (camera.eye.z >= lowEye-5 || std::abs(radius-camera.orbitDistance)>1e-6) return 16;
  const Vec3 direction=(camera.target-camera.eye).normalized();
  if ((camera.renderOrientation().rotate({1,0,0})-direction).norm()>.01) return 17;
  if (camera.renderOrientation().rotate({0,0,-1}).z>=0) return 18;
  camera.yaw=.4; camera.pitch=.2;
  camera.update(CameraMode::Free,parked,.016,false);
  if ((camera.renderOrientation().rotate({1,0,0})-camera.orientation().rotate({1,0,0})).norm()>1e-6) return 19;
  parked.att=quatFromEuler(100*kDeg2Rad,0,0);
  camera.update(CameraMode::Chase,parked,.016,false);
  if (camera.eye.z>-.8) return 20;
  const double zoomBefore=camera.orbitDistance;
  camera.zoomOrbit(-1);
  if (camera.orbitDistance>=zoomBefore*.95) return 21;
  std::cout << "PASS render coordinates, hemisphere interpolation, asset/CG mapping\n";
}
