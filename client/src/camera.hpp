#pragma once
// Camera rig.
//
// The free camera from M0 is preserved unchanged. The flight cameras are
// presentation only: they read the authoritative/predicted aircraft pose and
// smooth it visually. Nothing here ever writes to a State, so camera smoothing
// can never feed back into the simulation.

#include "coordinates.hpp"
#include "ofs/aircraft_definition.hpp"

#include <cstdint>

namespace ofs::client {

enum class CameraMode : std::uint8_t {
  Free = 0,       // M0 developer camera, independent of the aircraft
  Chase,          // standard follow, suitable for cruising
  CloseChase,     // tight follow for dogfighting
  Orbit,          // circles the aircraft, mouse-driven
  FirstPerson,    // flight-deck position
  Count
};

// Tuning for one camera mode. All distances are metres in body FRD.
struct CameraSettings {
  // Offset from the aircraft CG, body FRD.
  double distance{0};
  double height{0};
  double lateral{0};
  // Exponential smoothing time constants, seconds. 0 disables smoothing.
  double positionSmoothing{0};
  double rotationSmoothing{0};
  // Look-ahead: fraction of velocity added to the look target, seconds.
  double velocityLookAhead{0};
  // Vertical field of view in degrees.
  double fov{60};
  // True when the camera is rigidly attached (no smoothing, no look-ahead).
  bool rigid{};
  // Cockpit cameras sit inside the model, so the model is not drawn.
  bool hideOwnAircraft{};
};

const CameraSettings& cameraSettings(CameraMode mode);

struct Camera {
  // Free-camera state, preserved from M0.
  Vec3 position{-65, -70, -35};
  double yaw{.82}, pitch{-.30};
  CameraMode mode{CameraMode::Free};

  // Orbit state.
  double orbitYaw{0};
  double orbitPitch{0.25};
  double orbitDistance{60};

  // Smoothed flight-camera pose, kept across mode switches.
  Vec3 smoothedPosition{};
  Quat smoothedAtt{};
  bool smoothingPrimed{false};

  // Last resolved eye/target, used by the renderer for view-dependent effects.
  Vec3 eye{};
  Vec3 target{};
  double fov{60};
  // True when the view must use the airframe attitude directly (first person),
  // so the horizon rolls with the aircraft instead of staying world-level.
  bool rigidAttitude{false};

  // Free/orbit look direction, used by look() and move(). The flight cameras
  // carry their own smoothed attitude in smoothedAtt; reading yaw/pitch here
  // would give a free-camera orientation regardless of the active mode.
  Quat orientation() const { return quatFromEuler(0, pitch, yaw); }

  // Orientation the renderer should use: the smoothed flight attitude once a
  // frame has been resolved, and the free-camera orientation otherwise.
  Quat renderOrientation() const {
    return mode != CameraMode::Free && smoothingPrimed ? smoothedAtt : orientation();
  }

  void look(double dx, double dy) {
    yaw += dx * .0025;
    yaw = std::remainder(yaw, 2 * kPi);
    pitch = clamp(pitch - dy * .0025, -1.5, 1.5);
  }

  // Orbit cameras take mouse deltas; free cameras use look().
  void lookOrbit(double dx, double dy) {
    orbitYaw = std::remainder(orbitYaw - dx * .005, 2 * kPi);
    orbitPitch = clamp(orbitPitch + dy * .004, -1.4, 1.4);
  }

  void move(double forward, double right, double up, bool fast, double dt) {
    Vec3 direction = orientation().rotate({forward, right, 0}) + Vec3{0, 0, -up};
    if (direction.norm2() > 1) direction = direction.normalized();
    position += direction * ((fast ? 180.0 : 35.0) * dt);
  }

  void frameAircraft(const Vec3& aircraft) {
    smoothingPrimed = false;
    position = aircraft + Vec3{-65, -70, -32};
    yaw = .82;
    pitch = -.30;
  }

  void zoomOrbit(double delta) {
    orbitDistance = clamp(orbitDistance * (1.0 + delta * 0.12), 18.0, 600.0);
  }

  // Resolves the active camera for this frame.
  //
  // `dt` is the frame time; smoothing is exponential and framerate independent.
  // `velocity` is the aircraft's world velocity, used only for look-ahead.
  void update(CameraMode next, const State& aircraft, double dt, bool firstFrame,
              AircraftType type = AircraftType::A320);

  // True when the aircraft this camera belongs to should be skipped, because
  // the eye is inside its own model.
  bool hidesOwnAircraft() const { return cameraSettings(mode).hideOwnAircraft; }
};

// The body-FRD eye point of the flight deck, measured against the visual model:
// forward of the CG by the nose-to-CG distance, slightly above the cabin floor.
inline Vec3 firstPersonEye(AircraftType type = AircraftType::A320) {
  return aircraftDefinition(type).visual.cockpit;
}

}  // namespace ofs::client
