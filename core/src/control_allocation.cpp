#include "ofs/control_allocation.hpp"
#include <stdexcept>
namespace ofs {
std::array<double,5> boundedControlAllocation(const std::array<Vec3,5>& c,const Vec3& request,
  const std::array<double,5>& weight,const std::array<double,5>& lower,const std::array<double,5>& upper,double regularization) {
  if(!std::isfinite(request.norm2()) || !std::isfinite(regularization) || regularization<=0)throw std::invalid_argument("Invalid allocation request");
  std::array<double,5> x{};Vec3 error=-request;
  for(std::size_t i=0;i<5;++i) {
    if(!std::isfinite(c[i].norm2())||!std::isfinite(weight[i])||weight[i]<=0||!std::isfinite(lower[i])||!std::isfinite(upper[i])||lower[i]>upper[i])throw std::invalid_argument("Invalid allocation constraints");
    x[i]=clamp(0,lower[i],upper[i]);error+=c[i]*x[i];
  }
  for(unsigned iteration=0;iteration<128;++iteration)for(std::size_t i=0;i<5;++i) {
    const double penalty=regularization/weight[i];
    const double next=clamp(x[i]-(c[i].dot(error)+penalty*x[i])/(c[i].norm2()+penalty),lower[i],upper[i]);
    error+=c[i]*(next-x[i]);x[i]=next;
  }
  return x;
}
}
