#include "ofs/airliner.hpp"
#include "ofs/atmosphere.hpp"
#include <stdexcept>
namespace ofs {
HighLiftConfiguration a320HighLift(double travel) {
  // CLEAN, 1 (slats), 1+F, 2, 3, FULL. Continuous travel represents transit.
  // Angles are estimates pending a publicly licensed variant-specific schedule.
  const auto& schedule=a320HighLiftTable();
  const double index=clamp(travel,0,1)*5;
  const auto low=std::min(std::size_t(index),std::size_t(4));
  const auto& a=schedule[low];const auto& b=schedule[low+1];const double t=index-double(low);
  return {lerp(a.flap_degrees,b.flap_degrees,t),lerp(a.slat_degrees,b.slat_degrees,t),
    lerp(a.lift_increment,b.lift_increment,t),lerp(a.clmax,b.clmax,t),
    lerp(a.alpha_critical,b.alpha_critical,t),lerp(a.drag_increment,b.drag_increment,t),
    lerp(a.pitching_increment,b.pitching_increment,t)};
}
EngineDeckResult CFM565B4EngineeringDeck::evaluate(double altitude,double mach,double power,bool running) const {
  if(!std::isfinite(altitude)||!std::isfinite(mach)||!std::isfinite(power))
    throw std::invalid_argument("Nonfinite CFM56 deck input");
  if(!running)return {};
  const auto air=isaAtAltitude(altitude);
  const double delta=air.pressure/101325,theta=air.temp/288.15;
  // Pressure/temperature corrected installed high-bypass surrogate. Rated
  // takeoff thrust is published; every off-design map exponent is ESTIMATE.
  const double m=clamp(mach,0,1.2),p=clamp(power,0,1);
  const double lapse=std::pow(delta,.75)/std::sqrt(theta)*std::max(.25,1-.45*m+.10*m*m);
  const double thrust=120100*lapse*(.04+.96*std::pow(p,2.5));
  const double tsfc=1.65e-5*(1+.20*(1-p));
  return {thrust,.12+thrust*tsfc,true,p<.05?EngineRegime::Idle:EngineRegime::Dry};
}
}
