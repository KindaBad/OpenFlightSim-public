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
  // Pursuit: behind and above the aircraft on the view line, horizon level,
  // and the field of view follows the rate of change of speed.
  {
    State flying; flying.pos_ned={0,0,-2000}; flying.vel_ned={200,0,0};
    flying.att=quatFromEuler(60*kDeg2Rad,0,0);
    Camera pursuit;
    pursuit.update(CameraMode::Pursuit,flying,.016,true);
    const Vec3 back=pursuit.eye-flying.pos_ned;
    if (back.x>-10 || back.z>-2 || std::abs(back.y)>1e-6) return 22;
    if (std::abs(pursuit.renderOrientation().rotate({0,1,0}).z)>1e-6 || std::abs(pursuit.fov-cameraSettings(CameraMode::Pursuit).fov)>1e-9) return 23;
    // With mouse aim the eye swings behind the aircraft on the aim line.
    const Vec3 aim=Vec3{1,1,0}.normalized();
    for (int i=0;i<200;++i) pursuit.update(CameraMode::Pursuit,flying,.016,false,AircraftType::A320,&aim);
    if (((pursuit.target-pursuit.eye).normalized()-aim).norm()>1e-3 || (pursuit.eye-flying.pos_ned).dot(aim)>-10) return 24;
    const auto settle=[&](double acceleration) {
      for (int i=0;i<600;++i) {
        flying.time+=1./120; flying.vel_ned.x+=acceleration/120;
        pursuit.update(CameraMode::Pursuit,flying,1./120,false);
      }
      return pursuit.fov-cameraSettings(CameraMode::Pursuit).fov;
    };
    const double wide=settle(8), steady=settle(0), narrow=settle(-8);
    if (wide<5 || wide>13 || std::abs(steady)>.2 || narrow>-3 || narrow<-8) return 25;
    if (std::abs(wide-pursuitFovOffset(8))>.2 || pursuitFovOffset(0)!=0) return 26;
    pursuit.dynamicFov=false;
    if (std::abs(settle(8))>.05) return 27;
    // The other views keep their fixed field of view.
    pursuit.dynamicFov=true; settle(8);
    pursuit.update(CameraMode::Chase,flying,.016,false);
    if (pursuit.fov!=cameraSettings(CameraMode::Chase).fov) return 28;
    // Through the vertical the view carries on round instead of flipping.
    Camera loop; State climbing=flying; Vec3 lastUp{};
    for (int i=0;i<=360;++i) {
      climbing.att=quatFromEuler(0,i*kDeg2Rad,0); climbing.time+=1./60;
      loop.update(CameraMode::Pursuit,climbing,1./60,i==0);
      const Vec3 up=loop.renderOrientation().rotate({0,0,-1});
      if (i && (up-lastUp).norm()>.25) return 29;
      lastUp=up;
    }
    climbing.att=quatFromEuler(0,0,0);
    for (int i=0;i<300;++i) loop.update(CameraMode::Pursuit,climbing,1./60,false);
    if (loop.renderOrientation().rotate({0,0,-1}).z>-.999) return 30;
  }
  std::cout << "PASS render coordinates, hemisphere interpolation, asset/CG mapping\n";
}
