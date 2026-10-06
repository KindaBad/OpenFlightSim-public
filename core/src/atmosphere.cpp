#include "ofs/atmosphere.hpp"

#include <cmath>
#include <algorithm>

namespace ofs {

AirData isaAtAltitude(double alt_msl_m, double temp_offset_c) {
  // Standard layers through47km. Retain the established lower-atmosphere
  // equations; altitude is the flat-earth model's geopotential approximation.
  constexpr double T0 = 288.15;
  constexpr double P0 = 101325.0;
  constexpr double L = 0.0065;
  constexpr double R = 287.05;
  constexpr double G = 9.80665;
  constexpr double GAMMA = 1.4;

  AirData a;
  double h = std::isfinite(alt_msl_m)?alt_msl_m:0;
  if(!std::isfinite(temp_offset_c)) temp_offset_c=0;
  if (h < -500.0) h = -500.0;
  if (h > 47000.0) h = 47000.0;

  double T;
  double P;
  if (h <= 11000.0) {
    T = T0 - L * h;
    P = P0 * std::pow(T / T0, G / (R * L));
  } else {
    const double T11 = T0 - L * 11000.0;
    const double P11 = P0 * std::pow(T11 / T0, G / (R * L));
    T = T11;
    P = P11 * std::exp(-G * (h - 11000.0) / (R * T11));
    if (h > 20000) {
      const double P20=P11*std::exp(-G*9000/(R*T11));
      T=T11+.001*(std::min(h,32000.)-20000);
      P=P20*std::pow(T/T11,-G/(R*.001));
      if(h>32000) {
        const double T32=T,P32=P;
        T=T32+.0028*(h-32000);
        P=P32*std::pow(T/T32,-G/(R*.0028));
      }
    }
  }
  T += temp_offset_c;
  if (T < 150.0) T = 150.0;
  const double rho = P / (R * T);
  a.temp = T;
  a.pressure = P;
  a.rho = rho;
  a.sound = std::sqrt(GAMMA * R * T);
  return a;
}

}  // namespace ofs
