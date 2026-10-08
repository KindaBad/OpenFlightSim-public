#include "camera.hpp"
#include <glm/gtc/quaternion.hpp>

namespace ofs::client {
namespace {

// Body FRD offsets. The aircraft is 37.6 m long with the CG about 19.9 m aft of
// the nose (see docs/COORDINATES.md), so a chase camera sits ~45 m behind the
// nose, which frames the whole airframe at a 60 degree field of view.
constexpr CameraSettings kFree{};

constexpr CameraSettings kChase{
    /*distance*/ -68.0, /*height*/ 19.0, /*lateral*/ 16.0,
    /*positionSmoothing*/ 0.16, /*rotationSmoothing*/ 0.10,
    /*velocityLookAhead*/ 0.35, /*fov*/ 52,
    /*rigid*/ false, /*hideOwnAircraft*/ false};

constexpr CameraSettings kCloseChase{
    /*distance*/ -60.0, /*height*/ 13.0, /*lateral*/ 0.0,
    /*positionSmoothing*/ 0.09, /*rotationSmoothing*/ 0.06,
    /*velocityLookAhead*/ 0.28, /*fov*/ 68,
    /*rigid*/ false, /*hideOwnAircraft*/ false};

// Orbit is positioned by mouse input, so its body offset is only a fallback.
constexpr CameraSettings kOrbit{
    /*distance*/ -60.0, /*height*/ 12.0, /*lateral*/ 0.0,
    /*positionSmoothing*/ 0.10, /*rotationSmoothing*/ 0.10,
    /*velocityLookAhead*/ 0.0, /*fov*/ 55,
    /*rigid*/ false, /*hideOwnAircraft*/ false};

// The flight deck is rigidly attached: any smoothing would read as the pilot's
// head lagging behind the airframe, which is both wrong and nauseating.
constexpr CameraSettings kFirstPerson{
    /*distance*/ 13.6, /*height*/ 0.0, /*lateral*/ 0.0,
    /*positionSmoothing*/ 0.0, /*rotationSmoothing*/ 0.0,
    /*velocityLookAhead*/ 0.0, /*fov*/ 70,
    /*rigid*/ true, /*hideOwnAircraft*/ true};

// Pursuit holds its place behind the aircraft rigidly and only its direction is
// smoothed, so the body offset comes from the aircraft's size at run time and
// positionSmoothing is unused. rotationSmoothing applies without mouse aim.
constexpr CameraSettings kPursuit{
    /*distance*/ 0.0, /*height*/ 0.0, /*lateral*/ 0.0,
    /*positionSmoothing*/ 0.0, /*rotationSmoothing*/ 0.20,
    /*velocityLookAhead*/ 0.0, /*fov*/ 62,
    /*rigid*/ false, /*hideOwnAircraft*/ false};
// Under mouse aim the view follows the pointer almost directly.
constexpr double kPursuitAimSmoothing = 0.045;
// Eye position behind and above the aircraft, in visual radii.
constexpr double kPursuitBack = 2.5, kPursuitUp = 0.44;
// Rate, per second, at which the horizon returns to level in level flight.
constexpr double kPursuitLevelRate = 2.5;

}  // namespace

double pursuitFovOffset(double acceleration) {
  return acceleration >= 0 ? 13.0 * std::tanh(acceleration / 7.0) : 8.0 * std::tanh(acceleration / 9.0);
}

const CameraSettings& cameraSettings(CameraMode mode) {
  switch (mode) {
    case CameraMode::Chase: return kChase;
    case CameraMode::CloseChase: return kCloseChase;
    case CameraMode::Orbit: return kOrbit;
    case CameraMode::FirstPerson: return kFirstPerson;
    case CameraMode::Pursuit: return kPursuit;
    case CameraMode::Free:
    case CameraMode::Count: break;
  }
  return kFree;
}

namespace {

// Spherical interpolation between attitudes. The core quaternion type has no
// slerp and the renderer must stay independent of the authoritative core, so
// this is defined locally. Negates `b` when the quaternions are in opposite
// hemispheres, which otherwise takes the long way round.
Quat slerpAttitude(const Quat& a, Quat b, double t) {
  if (a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0)
    b = Quat{-b.w, -b.x, -b.y, -b.z};
  const double cosine = clamp(a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z, -1.0, 1.0);
  if (cosine > 0.9995) {
    // Nearly parallel: linear interpolation is numerically safer and visually
    // indistinguishable at these angular rates.
    return Quat{lerp(a.w, b.w, t), lerp(a.x, b.x, t), lerp(a.y, b.y, t),
                lerp(a.z, b.z, t)}
        .normalized();
  }
  const double theta = std::acos(cosine);
  const double sinTheta = std::sin(theta);
  const double wa = std::sin((1 - t) * theta) / sinTheta;
  const double wb = std::sin(t * theta) / sinTheta;
  return Quat{a.w * wa + b.w * wb, a.x * wa + b.x * wb, a.y * wa + b.y * wb,
              a.z * wa + b.z * wb}
      .normalized();
}

// Builds an attitude whose +X axis points along `forward`, keeping `up` as
// close to vertical as the direction allows. Used by the orbit camera to look
// back at the aircraft with a stable horizon.
Quat attitudeLookAt(const Vec3& forward, const Vec3& up) {
  const Vec3 f = forward.normalized();
  if (f.norm2() < 1e-12) return Quat{};
  Vec3 side = f.cross(up);
  if (side.norm2() < 1e-9) {
    // Looking straight up or down: pick any perpendicular reference.
    side = f.cross(Vec3{0, 0, -1});
    if (side.norm2() < 1e-9) side = f.cross(Vec3{0, 1, 0});
  }
  side = side.normalized();
  const Vec3 trueUp = side.cross(f).normalized();
  const Vec3 down = -trueUp;
  const glm::mat3 basis(glm::vec3(f.x, f.y, f.z), glm::vec3(side.x, side.y, side.z),
                        glm::vec3(down.x, down.y, down.z));
  const glm::quat q = glm::quat_cast(basis);
  return Quat{q.w, q.x, q.y, q.z}.normalized();
}

}  // namespace

void Camera::update(CameraMode next, const State& aircraft, double dt, bool firstFrame, AircraftType type,
                    const Vec3* aimView) {
  const bool aimed = aimView && (next == CameraMode::Chase || next == CameraMode::CloseChase ||
                                 next == CameraMode::Pursuit);
  // Pursuit turns smoothly onto the aim line, so engaging mouse aim does not cut.
  const bool changed = mode != next || (aimed != aimViewActive && next != CameraMode::Pursuit);
  mode = next;
  aimViewActive = aimed;
  const CameraSettings& settings = cameraSettings(mode);
  const auto& visual = aircraftDefinition(type).visual;
  const Vec3 referencePosition=aircraft.pos_ned-aircraft.att.rotate(loadedCg(aircraftDefinition(type).flight,aircraft));

  // Rate of change of speed, taken across simulation steps so it is exact
  // whatever the frame rate. A reset or respawn restarts the estimate.
  const double speed = aircraft.vel_ned.norm();
  const double stateDt = aircraft.time - lastStateTime;
  if (!speedPrimed || firstFrame || stateDt < 0 || stateDt > .5) {
    acceleration = 0;
    speedPrimed = true;
    lastSpeed = speed;
    lastStateTime = aircraft.time;
  } else if (stateDt > 1e-6) {
    const double measured = clamp((speed - lastSpeed) / stateDt, -40.0, 40.0);
    acceleration += (measured - acceleration) * (1.0 - std::exp(-stateDt / .35));
    lastSpeed = speed;
    lastStateTime = aircraft.time;
  }
  const double fovTarget = dynamicFov && mode == CameraMode::Pursuit ? pursuitFovOffset(acceleration) : 0.0;
  fovOffset = firstFrame ? fovTarget : fovOffset + (fovTarget - fovOffset) * (1.0 - std::exp(-dt / .45));

  if (mode == CameraMode::Free) {
    // The free camera keeps its own position and orientation exactly as in M0.
    rigidAttitude = false;
    eye = position;
    target = eye + orientation().rotate({1, 0, 0});
    fov = 60;
    return;
  }

  if (mode == CameraMode::Pursuit) {
    // Looks along the aim line, or along the nose without mouse aim, from a
    // fixed place behind and above the aircraft on that line. The aircraft is
    // therefore free to swing about in the frame while the view stays steady.
    const Vec3 forward = aimed ? *aimView : aircraft.att.rotate({1, 0, 0});
    const bool carry = smoothingPrimed && !firstFrame && !changed;
    const Vec3 worldUp{0, 0, -1};
    if (aimed || !carry) {
      pursuitUp = worldUp;
    } else {
      // Carrying the previous up through a loop keeps the view from flipping at
      // the vertical; it then eases back to a level horizon, faster the nearer
      // the view is to level.
      const Vec3 levelUp = worldUp - forward * worldUp.dot(forward);
      Vec3 carried = pursuitUp - forward * pursuitUp.dot(forward);
      if (carried.norm2() < 1e-6) carried = smoothedAtt.rotate({0, 0, -1});
      carried = carried.normalized();
      if (levelUp.norm2() > 1e-6) {
        const Vec3 level = levelUp.normalized();
        if (carried.dot(level) < -.98) carried += forward.cross(carried) * .05;  // inverted: pick a way round
        const double weight = 1.0 - std::exp(-dt * kPursuitLevelRate * levelUp.norm2());
        carried = carried * (1 - weight) + level * weight;
      }
      pursuitUp = carried.normalized();
    }
    const Quat desiredAtt = attitudeLookAt(forward, pursuitUp);
    if (carry) {
      const double smoothing = aimed ? kPursuitAimSmoothing : settings.rotationSmoothing;
      smoothedAtt = slerpAttitude(smoothedAtt, desiredAtt, 1.0 - std::exp(-dt / smoothing));
    } else {
      smoothedAtt = desiredAtt;
      smoothingPrimed = true;
    }
    smoothedPosition = referencePosition + aircraft.att.rotate(visual.chaseTarget) +
        smoothedAtt.rotate({-kPursuitBack * visual.radius, 0, -kPursuitUp * visual.radius});
    smoothedPosition.z = std::min(smoothedPosition.z, groundHeightNed(smoothedPosition.x, smoothedPosition.y) - .8);
    eye = smoothedPosition;
    target = eye + smoothedAtt.rotate({1000, 0, 0});
    fov = settings.fov + fovOffset;
    rigidAttitude = false;
    return;
  }

  // Desired eye position in world NED, derived from a body-FRD offset.
  Vec3 offsetBody;
  if (mode == CameraMode::FirstPerson) {
    offsetBody = firstPersonEye(type);
  } else if (mode == CameraMode::Orbit) {
    const double horizontal = orbitDistance * std::cos(orbitPitch);
    offsetBody = visual.orbitCenter + Vec3{-horizontal * std::cos(orbitYaw), horizontal * std::sin(orbitYaw),
                      -orbitDistance * std::sin(orbitPitch)};
  } else {
    // The vertical axis is the third body component: this frame has -Z up
    // (see localPosition, and firstPersonEye which returns a negative Z for
    // eye height). Distance is body X and lateral is body Y.
    offsetBody = mode == CameraMode::CloseChase ? visual.closeChaseOffset : visual.chaseOffset;
  }
  Quat offsetAtt = aircraft.att;
  if (mode == CameraMode::Orbit) {
    double roll, pitch, yaw;
    eulerFromQuat(aircraft.att, roll, pitch, yaw);
    offsetAtt = quatFromEuler(0, 0, yaw);
  }
  Vec3 desired = referencePosition + offsetAtt.rotate(offsetBody);
  if (!settings.rigid) desired.z = std::min(desired.z, groundHeightNed(desired.x, desired.y) - .8);
  const Vec3 lookTarget = referencePosition + aircraft.att.rotate(visual.chaseTarget) + aircraft.vel_ned * settings.velocityLookAhead;
  Quat desiredAtt = settings.rigid ? aircraft.att*quatFromEuler(0,visual.cockpitPitch,0)
      : attitudeLookAt(lookTarget - desired, Vec3{0, 0, -1});
  if (aimed) {
    // Behind and above the aircraft along the aim line, horizon level.
    desiredAtt = attitudeLookAt(*aimView, Vec3{0, 0, -1});
    desired = referencePosition + desiredAtt.rotate({offsetBody.x, 0, offsetBody.z});
    desired.z = std::min(desired.z, groundHeightNed(desired.x, desired.y) - .8);
  }

  if (firstFrame || changed || !smoothingPrimed) {
    smoothedPosition = desired;
    smoothedAtt = desiredAtt;
    smoothingPrimed = true;
  } else if (settings.rigid) {
    smoothedPosition = desired;
    smoothedAtt = desiredAtt;
  } else {
    // Exponential smoothing: framerate independent, never overshoots, and it
    // is what keeps simulation jitter out of the camera.
    const double positionAlpha = settings.positionSmoothing > 0
        ? 1.0 - std::exp(-dt / settings.positionSmoothing)
        : 1.0;
    const double rotationAlpha = settings.rotationSmoothing > 0
        ? 1.0 - std::exp(-dt / settings.rotationSmoothing)
        : 1.0;
    smoothedPosition = smoothedPosition * (1 - positionAlpha) + desired * positionAlpha;
    smoothedAtt = slerpAttitude(smoothedAtt, desiredAtt, rotationAlpha);
  }

  if (!settings.rigid)
    smoothedPosition.z = std::min(smoothedPosition.z, groundHeightNed(smoothedPosition.x, smoothedPosition.y) - .8);
  eye = smoothedPosition;
  // Look-ahead biases the look target along the velocity vector so a fast
  // aircraft sits slightly low in frame instead of drifting off-centre.
  target = settings.rigid || aimed ? eye + smoothedAtt.rotate({1000, 0, 0}) : lookTarget;
  fov = settings.fov;
  // A rigid camera inherits the airframe attitude so the horizon rolls with the
  // aircraft; the other modes keep a world-level horizon.
  rigidAttitude = settings.rigid;
}

void Camera::watch(const Vec3& subject, double heading, double distance, double lift, double dt) {
  const Vec3 back{-std::cos(heading), -std::sin(heading), 0};
  Vec3 desired = subject + back * distance + Vec3{0, 0, -distance * .18};
  desired.z = std::min(desired.z, groundHeightNed(desired.x, desired.y) - 1.5);
  const bool carry = mode == CameraMode::Chase && smoothingPrimed && (smoothedPosition - desired).norm() < 400;
  // The pilot is thrown about in the first moments; the camera follows smoothly.
  smoothedPosition = carry ? smoothedPosition + (desired - smoothedPosition) * (1.0 - std::exp(-dt / .35)) : desired;
  smoothedPosition.z = std::min(smoothedPosition.z, groundHeightNed(smoothedPosition.x, smoothedPosition.y) - 1.5);
  eye = smoothedPosition;
  target = subject + Vec3{0, 0, -lift};
  smoothedAtt = attitudeLookAt(target - eye, Vec3{0, 0, -1});
  smoothingPrimed = true;
  mode = CameraMode::Chase;
  aimViewActive = false;
  rigidAttitude = false;
  fov = 62;
}

}  // namespace ofs::client
