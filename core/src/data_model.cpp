#include "ofs/data_model.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace ofs {
GridTable::GridTable(std::vector<std::vector<double>> axes,std::vector<double> values,BoundaryPolicy boundary)
  :axes_(std::move(axes)),values_(std::move(values)),boundary_(boundary) {
  if(axes_.empty()||axes_.size()>13)throw std::invalid_argument("Grid requires 1..13 axes");
  std::size_t n=1;
  for(const auto& axis:axes_) {
    if(axis.empty() || n>std::numeric_limits<std::size_t>::max()/axis.size())throw std::invalid_argument("Invalid grid size");
    n*=axis.size();
    for(std::size_t i=0;i<axis.size();++i)
      if(!std::isfinite(axis[i])||(i&&axis[i]<=axis[i-1]))throw std::invalid_argument("Grid axes must be finite and strictly increasing");
  }
  if(n!=values_.size())throw std::invalid_argument("Grid shape mismatch");
  for(double v:values_)if(!std::isfinite(v))throw std::invalid_argument("Nonfinite grid value");
}
double GridTable::evaluate(std::span<const double> point) const {
  if(point.size()!=axes_.size())throw std::invalid_argument("Grid coordinate dimension mismatch");
  std::array<std::size_t,13> lo{},hi{},stride{};std::array<double,13> fraction{};
  std::size_t next=1;
  for(std::size_t k=axes_.size();k-->0;) {
    const auto& axis=axes_[k];double x=point[k];
    if(!std::isfinite(x))throw std::domain_error("Nonfinite grid coordinate");
    if(boundary_==BoundaryPolicy::Reject&&(x<axis.front()||x>axis.back()))throw std::out_of_range("Coordinate outside published envelope");
    x=std::clamp(x,axis.front(),axis.back());stride[k]=next;next*=axis.size();
    auto upper=std::upper_bound(axis.begin(),axis.end(),x);
    hi[k]=upper==axis.end()?axis.size()-1:std::size_t(upper-axis.begin());
    lo[k]=hi[k]?hi[k]-1:0;
    fraction[k]=lo[k]==hi[k]?0:(x-axis[lo[k]])/(axis[hi[k]]-axis[lo[k]]);
  }
  double result=0;
  for(std::size_t corner=0;corner<(std::size_t{1}<<axes_.size());++corner) {
    double weight=1;std::size_t index=0;
    for(std::size_t k=0;k<axes_.size();++k) {
      const bool high=corner&(std::size_t{1}<<k);weight*=high?fraction[k]:1-fraction[k];index+=(high?hi[k]:lo[k])*stride[k];
    }
    if(weight!=0)result+=weight*values_[index];
  }
  if(!std::isfinite(result))throw std::overflow_error("Nonfinite interpolated coefficient");
  return result;
}
namespace {
double input(AeroVariable v,const AeroInputs& i) {
  switch(v) {
    case AeroVariable::Alpha:return i.alpha;case AeroVariable::Beta:return i.beta;case AeroVariable::Mach:return i.mach;
    case AeroVariable::RollRate:return i.phat;case AeroVariable::PitchRate:return i.qhat;case AeroVariable::YawRate:return i.rhat;
    case AeroVariable::Elevator:return i.elevator;case AeroVariable::Aileron:return i.aileron;case AeroVariable::Rudder:return i.rudder;
    case AeroVariable::LeadingEdge:return i.leading_edge;case AeroVariable::TrailingEdge:return i.trailing_edge;
    case AeroVariable::SpeedBrake:return i.speed_brake;case AeroVariable::Configuration:return i.configuration;
  }
  throw std::invalid_argument("Unknown aero variable");
}
}
TableAeroModel::TableAeroModel(std::vector<AeroTableTerm> terms):terms_(std::move(terms)) {
  for(const auto& term:terms_)if(term.coefficient>5||term.variables.size()!=term.table.dimensions())throw std::invalid_argument("Invalid aero table term");
}
AeroCoefficients TableAeroModel::coefficients(const AeroInputs& i) const {
  std::array<double,6> c{};std::array<double,13> point{};
  for(const auto& t:terms_) {
    for(std::size_t k=0;k<t.variables.size();++k)point[k]=input(t.variables[k],i);
    const double value=t.table.evaluate(std::span(point.data(),t.variables.size()))*(t.multiply?input(t.multiplier,i):1);
    if(!std::isfinite(value))throw std::domain_error("Nonfinite aero term");
    c[t.coefficient]+=value;
  }
  for(double value:c)if(!std::isfinite(value))throw std::overflow_error("Nonfinite summed aerodynamic coefficient");
  return {c[0],c[1],c[2],c[3],c[4],c[5]};
}
EngineDeckModel::EngineDeckModel(GridTable thrust,std::shared_ptr<const GridTable> flow,double idle,double dry)
 :thrust_(std::move(thrust)),fuel_flow_(std::move(flow)),idle_power_(idle),dry_power_(dry) {
  if(thrust_.dimensions()!=3||(fuel_flow_&&fuel_flow_->dimensions()!=3)||!std::isfinite(idle)||!std::isfinite(dry)||dry<=idle)
    throw std::invalid_argument("Engine deck requires altitude/Mach/power axes and ordered regimes");
}
EngineDeckResult EngineDeckModel::evaluate(double altitude,double mach,double power,bool running) const {
  for(double v:{altitude,mach,power})if(!std::isfinite(v))throw std::domain_error("Nonfinite engine input");
  if(!running)return {};
  const std::array point{altitude,mach,power};EngineDeckResult out;
  out.thrust=thrust_.evaluate(point);out.fuel_flow_available=bool(fuel_flow_);
  out.fuel_flow=fuel_flow_?fuel_flow_->evaluate(point):0;
  out.regime=power<=idle_power_?EngineRegime::Idle:power<=dry_power_?EngineRegime::Dry:EngineRegime::Afterburning;
  if(out.fuel_flow<0)throw std::domain_error("Negative fuel flow");
  return out;
}
}
