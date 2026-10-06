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
AirData Simulator::airData() const {
  const double alt_msl = -state_.pos_ned.z;
  return isaAtAltitude(alt_msl, weather_.temp_offset_c);
}

Vec3 Simulator::windAt(const Vec3 &pos_ned, double t) const { return windAt(pos_ned,t,weather_); }
Vec3 Simulator::windAt(const Vec3 &pos_ned, double t, const Weather& weather_) const {
  Vec3 w = weather_.wind_ned;
  const double tb = weather_.turbulence01;
  if (tb > 1e-6) {
    // Deterministic harmonic gust approximation; NOT a Dryden stochastic process.
    // Amplitudes scale with turbulence intensity; vertical is weaker.
    const double uh = tb * 5.0;
    const double uv = tb * 2.2;
    w.x += uh * (0.55 * std::sin(gustPhase(0.31, pos_ned, t)) +
                 0.30 * std::sin(gustPhase(0.83, pos_ned, t) + 1.7) +
                 0.15 * std::sin(gustPhase(2.10, pos_ned, t) + 4.1));
    w.y += uh * (0.55 * std::sin(gustPhase(0.27, pos_ned, t) + 2.3) +
                 0.30 * std::sin(gustPhase(0.91, pos_ned, t) + 0.6) +
                 0.15 * std::sin(gustPhase(1.93, pos_ned, t) + 2.9));
    w.z += uv * (0.60 * std::sin(gustPhase(0.43, pos_ned, t) + 4.4) +
                 0.40 * std::sin(gustPhase(1.17, pos_ned, t) + 1.2));
  }
  return w;
}


} // namespace ofs
