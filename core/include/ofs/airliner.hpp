#pragma once
#include "ofs/aircraft.hpp"
namespace ofs {
// Reconstructed schedules; only the detent names identify Airbus configurations.
// The numerical aerodynamic increments are engineering estimates.
struct HighLiftConfiguration {
  double flap_degrees{}, slat_degrees{}, lift_increment{}, clmax{}, alpha_critical{}, drag_increment{}, pitching_increment{};
};
const std::array<HighLiftConfiguration,6>& a320HighLiftTable();
HighLiftConfiguration a320HighLift(double travel);
class CFM565B4EngineeringDeck final : public PropulsionModel {
public:
  EngineDeckResult evaluate(double altitude,double mach,double power,bool running) const override;
};
}
