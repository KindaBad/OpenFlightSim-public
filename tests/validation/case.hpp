#pragma once
#include "json.hpp"
#include "scenario.hpp"
#include <cstdio>
#include <string>

namespace validation {
using ofs::client::json::Value;
struct Comparison {
  double absoluteError{}, percentageError{};
  const char* status{};
};
// A non-comparable point is always WARN, even if its numbers happen to agree.
inline Comparison compare(double result,double low,double high,double tolerance,bool comparable) {
  scenario::check(std::isfinite(result)&&std::isfinite(low)&&std::isfinite(high)&&
                  std::isfinite(tolerance)&&low<=high&&tolerance>=0,"invalid validation comparison");
  const double nearest=std::clamp(result,low,high),error=std::abs(result-nearest);
  const double midpoint=(low+high)/2;
  return {error,midpoint==0?0:100*error/std::abs(midpoint),
    !comparable?"WARN":error<=tolerance?"PASS":"FAIL"};
}
inline void requireMetadata(const Value& c) {
  for(const char* key:{"id","aircraft","configuration","mass_kg","fuel_kg","payload_kg",
      "cg_assumption","altitude_m","atmosphere","runway","wind","flap01","gear01",
      "engine_mode","procedure","metric","reference_low","reference_high","unit",
      "source","locator","tolerance","tolerance_reason","authority","comparable","limitations","category"})
    scenario::check(c.has(key), (std::string("missing validation metadata: ")+key).c_str());
}
inline bool report(const Value& c,double value) {
  const auto low=c["reference_low"].number(),high=c["reference_high"].number();
  const auto r=compare(value,low,high,c["tolerance"].number(),c["comparable"].boolean());
  std::printf("%s [%.*s] %.*s simulated=%.6f reference=[%.6f,%.6f] %.*s absolute_error=%.6f percentage_error=%.4f%% tolerance=%.6f\n",
    r.status,int(c["category"].text().size()),c["category"].text().data(),int(c["id"].text().size()),c["id"].text().data(),
    value,low,high,int(c["unit"].text().size()),c["unit"].text().data(),r.absoluteError,r.percentageError,c["tolerance"].number());
  std::printf("  source=%.*s; locator=%.*s; authority=%.*s\n  limitations=%.*s\n",
    int(c["source"].text().size()),c["source"].text().data(),int(c["locator"].text().size()),c["locator"].text().data(),
    int(c["authority"].text().size()),c["authority"].text().data(),int(c["limitations"].text().size()),c["limitations"].text().data());
  return std::string_view(r.status)!="FAIL";
}
}
