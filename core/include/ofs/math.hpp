#pragma once
// OpenFlightSim core math: doubles, aerospace conventions.
//
// World frame: NED (X north, Y east, Z down). Gravity = +Z.
// Body frame:  FRD (X forward/nose, Y right/starboard, Z down/belly).
// Attitude quaternion `att` maps body -> world: v_world = att.rotate(v_body).
// Euler: yaw about world/body Z (nose-right), pitch about Y (nose-up positive), roll about X.
// We use the standard aerospace 3-2-1 (yaw-pitch-roll) sequence.

#include <cmath>
#include <stdexcept>

namespace ofs {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;
constexpr double kRad2Deg = 180.0 / kPi;
constexpr double kG0 = 9.80665;

inline double clamp(double v, double lo, double hi) {
  if (!std::isfinite(v)) throw std::domain_error("Nonfinite physics clamp input");
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

inline double lerp(double a, double b, double t) { return a + (b - a) * t; }

struct Vec3 {
  double x{0}, y{0}, z{0};
  Vec3() = default;
  Vec3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
  Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
  Vec3& operator+=(const Vec3& o) {
    x += o.x;
    y += o.y;
    z += o.z;
    return *this;
  }
  Vec3& operator-=(const Vec3& o) {
    x -= o.x;
    y -= o.y;
    z -= o.z;
    return *this;
  }
  Vec3 operator-() const { return {-x, -y, -z}; }
  double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
  Vec3 cross(const Vec3& o) const {
    return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
  }
  double norm2() const { return x * x + y * y + z * z; }
  double norm() const { return std::sqrt(norm2()); }
  Vec3 normalized() const {
    const double n = norm();
    if (n < 1e-12) return {0, 0, 0};
    return *this / n;
  }
};

inline Vec3 operator*(double s, const Vec3& v) { return v * s; }

struct Quat {
  double w{1}, x{0}, y{0}, z{0};
  Quat() = default;
  Quat(double w_, double x_, double y_, double z_) : w(w_), x(x_), y(y_), z(z_) {}
  Quat normalized() const {
    const double n = std::sqrt(w * w + x * x + y * y + z * z);
    if (n < 1e-12) return {1, 0, 0, 0};
    return {w / n, x / n, y / n, z / n};
  }
  Quat conj() const { return {w, -x, -y, -z}; }
  Quat operator*(const Quat& o) const {
    return {w * o.w - x * o.x - y * o.y - z * o.z, w * o.x + x * o.w + y * o.z - z * o.y,
            w * o.y - x * o.z + y * o.w + z * o.x, w * o.z + x * o.y - y * o.x + z * o.w};
  }
  Vec3 rotate(const Vec3& v) const {
    // q * (0,v) * q*
    Quat qv{0, v.x, v.y, v.z};
    Quat r = (*this) * qv * conj();
    return {r.x, r.y, r.z};
  }
  Vec3 inverseRotate(const Vec3& v) const { return conj().rotate(v); }
};

// Yaw (psi) about Z, pitch (theta) about Y, roll (phi) about X, 3-2-1.
// Pitch positive = nose up: right-handed +Y rotation maps forward +X to -Z.
inline Quat quatFromEuler(double roll_rad, double pitch_rad, double yaw_rad) {
  const double cr = std::cos(roll_rad * 0.5), sr = std::sin(roll_rad * 0.5);
  const double cp = std::cos(pitch_rad * 0.5), sp = std::sin(pitch_rad * 0.5);
  const double cy = std::cos(yaw_rad * 0.5), sy = std::sin(yaw_rad * 0.5);
  Quat qroll{cr, sr, 0, 0};
  Quat qpitch{cp, 0, sp, 0};
  Quat qyaw{cy, 0, 0, sy};
  return (qyaw * qpitch * qroll).normalized();
}

inline void eulerFromQuat(const Quat& q, double& roll_rad, double& pitch_rad, double& yaw_rad) {
  const Quat n = q.normalized();
  // Standard 3-2-1 extraction for right-hand rotations about X,Y,Z,
  // +Y is nose-up-positive in FRD.
  const double sinp = 2.0 * (n.w * n.y - n.z * n.x);
  const double pitch_rh = std::asin(clamp(sinp, -1.0, 1.0));
  const double roll_rh =
      std::atan2(2.0 * (n.w * n.x + n.y * n.z), 1.0 - 2.0 * (n.x * n.x + n.y * n.y));
  const double yaw_rh =
      std::atan2(2.0 * (n.w * n.z + n.x * n.y), 1.0 - 2.0 * (n.y * n.y + n.z * n.z));
  roll_rad = roll_rh;
  pitch_rad = pitch_rh;
  yaw_rad = yaw_rh;
}

}  // namespace ofs
