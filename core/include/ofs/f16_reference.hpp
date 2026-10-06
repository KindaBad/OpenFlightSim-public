#pragma once
#include "ofs/aircraft.hpp"
namespace ofs {
// Available when OFS_ENABLE_F16_REFERENCE is configured with imported NASA data.
AircraftConfig f16ReferenceConfig();
std::shared_ptr<const AerodynamicModel> f16ReferenceAero();
std::shared_ptr<const PropulsionModel> f16ReferenceEngine();
}
