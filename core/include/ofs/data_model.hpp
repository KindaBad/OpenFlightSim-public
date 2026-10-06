#pragma once
#include "ofs/math.hpp"
#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace ofs {
enum class Provenance { REFERENCE, DERIVED, ESTIMATE, CALIBRATED_APPROXIMATION };
struct DataProvenance {
  std::string parameter, unit, source, revision, configuration, envelope, validation, notes;
  Provenance origin{Provenance::ESTIMATE};
};
enum class BoundaryPolicy { Clamp, Reject };
// Tensor grid: final dimension varies fastest. Immutable after construction.
class GridTable {
  std::vector<std::vector<double>> axes_;
  std::vector<double> values_;
  BoundaryPolicy boundary_;
public:
  GridTable(std::vector<std::vector<double>> axes,std::vector<double> values,
            BoundaryPolicy boundary=BoundaryPolicy::Clamp);
  double evaluate(std::span<const double> point) const;
  std::size_t dimensions() const { return axes_.size(); }
};
struct AeroInputs {
  double alpha{}, beta{}, mach{}, phat{}, qhat{}, rhat{};
  double elevator{}, aileron{}, rudder{}, leading_edge{}, trailing_edge{}, speed_brake{}, configuration{};
};
struct AeroCoefficients { double cx{},cy{},cz{},cl{},cm{},cn{}; };
class AerodynamicModel {
public:
  virtual ~AerodynamicModel()=default;
  virtual AeroCoefficients coefficients(const AeroInputs&) const=0;
};
enum class AeroVariable { Alpha, Beta, Mach, RollRate, PitchRate, YawRate, Elevator, Aileron, Rudder, LeadingEdge, TrailingEdge, SpeedBrake, Configuration };
struct AeroTableTerm {
  std::size_t coefficient{}; // CX CY CZ Cl Cm Cn
  GridTable table;
  std::vector<AeroVariable> variables;
  // Rate/control terms multiply a coefficient table by the selected input.
  bool multiply{};
  AeroVariable multiplier{AeroVariable::Alpha};
};
class TableAeroModel final : public AerodynamicModel {
  std::vector<AeroTableTerm> terms_;
public:
  explicit TableAeroModel(std::vector<AeroTableTerm> terms);
  AeroCoefficients coefficients(const AeroInputs&) const override;
};
enum class EngineRegime { Stopped, Idle, Dry, Afterburning };
struct EngineDeckResult { double thrust{}, fuel_flow{}; bool fuel_flow_available{}; EngineRegime regime{EngineRegime::Stopped}; };
class PropulsionModel {
public:
  virtual ~PropulsionModel()=default;
  virtual EngineDeckResult evaluate(double altitude,double mach,double power,bool running) const=0;
};
class EngineDeckModel final : public PropulsionModel {
  GridTable thrust_;
  std::shared_ptr<const GridTable> fuel_flow_;
  double idle_power_,dry_power_;
public:
  EngineDeckModel(GridTable thrust,std::shared_ptr<const GridTable> flow={},double idle_power=0,double dry_power=.5);
  EngineDeckResult evaluate(double altitude,double mach,double power,bool running) const override;
};
}
