#include "ofs/aircraft_definition.hpp"
#include "ofs/simulator.hpp"
#include "ofs/trim.hpp"
#ifdef OFS_ENABLE_F16_REFERENCE
#include "ofs/f16_reference.hpp"
#endif
#include <string_view>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>
int main(int argc,char** argv) {
  const bool rk4=argc>1 && std::string_view(argv[1])=="--rk4";
  const auto method=rk4?ofs::ContinuousIntegrator::RungeKutta4:ofs::ContinuousIntegrator::SemiImplicitEuler;
  std::printf("continuous_integrator=%s (actuator/engine/contact split)\n",rk4?"RK4":"semi-implicit Euler");
  for(const auto& definition:ofs::aircraftDefinitions()) {
    ofs::Simulator sim(definition.flight);sim.setIntegrator(method);ofs::TrimRequest request;
    request.tas=definition.type==ofs::AircraftType::A320?110:180;
    const auto trim=ofs::solveTrim(definition.flight,request);
    if(!trim.converged) return 1;
    sim.setState(trim.state);sim.setControls(trim.controls);
    std::vector<double> samples;
    for(int i=0;i<3000;++i){const auto start=std::chrono::steady_clock::now();sim.step(1./120);
      if(i>=100)samples.push_back(std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count());}
    double mean=0;for(double value:samples)mean+=value;mean/=samples.size();std::sort(samples.begin(),samples.end());
    std::printf("type=%s mean_us=%.3f p95_us=%.3f p99_us=%.3f\n",std::string(definition.key).c_str(),mean,samples[samples.size()*95/100],samples[samples.size()*99/100]);
  }
  for (int count : {1, 8, 16, 32, 64}) {
    std::vector<ofs::Simulator> sims;
    for (int i = 0; i < count; ++i) {
      const auto& definition=ofs::aircraftDefinitions()[i % ofs::aircraftDefinitions().size()];
      auto cfg = definition.flight;
      ofs::TrimRequest r;
      r.tas = definition.type==ofs::AircraftType::A320 ? 110 : 180;
      auto t = ofs::solveTrim(cfg, r);
      sims.emplace_back(cfg);
      sims.back().setIntegrator(method);
      sims.back().setState(t.state);
      sims.back().setControls(t.controls);
    }
    std::vector<double> samples;
    for (int n = 0; n < 3000; ++n) {
      auto start = std::chrono::steady_clock::now();
      for (auto &s : sims)
        s.step(1. / 120);
      double us = std::chrono::duration<double, std::micro>(
                      std::chrono::steady_clock::now() - start)
                      .count();
      if (n >= 100)
        samples.push_back(us);
    }
    double mean = 0;
    for (auto us : samples)
      mean += us;
    mean /= samples.size();
    std::sort(samples.begin(), samples.end());
    std::printf("aircraft=%d mean_us=%.3f p95_us=%.3f p99_us=%.3f "
                "per_aircraft_us=%.3f\n",
                count, mean, samples[samples.size() * 95 / 100],
                samples[samples.size() * 99 / 100], mean / count);
  }
#ifdef OFS_ENABLE_F16_REFERENCE
  const auto cfg=ofs::f16ReferenceConfig();ofs::TrimRequest request;request.tas=150;request.gear01=0;
  const auto trim=ofs::solveTrim(cfg,request);if(!trim.converged)return 1;
  ofs::Simulator reference(cfg);reference.setIntegrator(method);reference.setState(trim.state);reference.setControls(trim.controls);
  const auto start=std::chrono::steady_clock::now();
  for(unsigned i=0;i<3000;++i)reference.step(1./120);
  const double time=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count();
  std::printf("reference=nasa-f16 table_aero_and_engine mean_us=%.3f\n",time/3000);
#endif

}
