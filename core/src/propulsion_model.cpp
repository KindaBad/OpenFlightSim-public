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
Simulator::ThrustResult Simulator::evalThrust() const { return evalThrust(state_,weather_); }
Simulator::ThrustResult Simulator::evalThrust(const State& state_, const Weather& weather_) const {
  const AirData air = isaAtAltitude(-state_.pos_ned.z, weather_.temp_offset_c);
  const double sigma = air.rho / 1.225;
  const double V =
      state_.att
          .inverseRotate(state_.vel_ned - windAt(state_.pos_ned, state_.time, weather_))
          .norm();
  const double mach = (air.sound > 1.0 && V > 0.0) ? V / air.sound : 0.0;
  const double lapse = cfg_.variable_inlets
      ? std::pow(std::max(sigma,.0001),cfg_.thrust_density_exponent)*
        machCurve(mach,{{0,1},{.8,.80},{1.2,.90},{1.6,1.20},{2,1.55},
                        {2.8,2.7},{3.2,3.1},{3.6,3.0},{4,2.3}})
      : std::pow(std::max(sigma, .02), cfg_.thrust_density_exponent) *
        (1.0 - .20 * std::min(mach, 2.5) +
         cfg_.thrust_ram_gain * mach * mach / (1 + mach * mach));
  ThrustResult result;
  {
    for (unsigned e = 0; e < cfg_.engine_count; ++e) {
      const auto &engine = cfg_.engines[e];
      const double tmax = engine.dry_thrust * std::max(lapse, .05);
      const double n1 = state_.n1[e];
      double t = tmax * (0.05 + 0.95 * n1 * n1 * std::sqrt(n1));
      if (engine.reheat_thrust > engine.dry_thrust &&
          cfg_.afterburner_threshold > 0 && cfg_.afterburner_threshold < 1) {
        const double dry = clamp(n1 / cfg_.afterburner_threshold, 0, 1);
        const double reheat = state_.afterburner[e];
        t = tmax * (.05 + .95 * dry * dry * std::sqrt(dry)) +
            (engine.reheat_thrust - engine.dry_thrust) *
                (lapse > .05 ? lapse : .05) * reheat;
      }
      if(cfg_.jet_kind==JetModelKind::EngineDeck) {
        if(!cfg_.propulsion_model)throw std::logic_error("Missing engine deck");
        // n1 is normalized deck power for this model, not compressor RPM.
        const auto deck=cfg_.propulsion_model->evaluate(-state_.pos_ned.z,mach,n1,
          state_.engine_health[e]>0 && state_.fuel_mass!=0);
        t=deck.thrust;
        result.fuel_flow[e]=deck.fuel_flow*state_.engine_health[e]*cfg_.fuel_flow_scale;
      }
      if(cfg_.variable_inlets) t*=inletPressureRecovery(mach,state_.inlet_spike[e]);
      t *= state_.engine_health[e] * (state_.fuel_mass == 0 ? 0 : 1);
      if(cfg_.jet_kind==JetModelKind::EngineeringJetEngineModel) result.fuel_flow[e] =
          t *
          (engine.dry_tsfc +
           (engine.reheat_tsfc - engine.dry_tsfc) * state_.afterburner[e]) *
          cfg_.fuel_flow_scale;
      const auto axis=engine.vector_axis;
      const double angle=state_.nozzle_angle[e], c=std::cos(angle), sn=std::sin(angle);
      const auto rotate=[&](Vec3 v) {return v*c+axis.cross(v)*sn+axis*(axis.dot(v)*(1-c));};
      const Vec3 direction=rotate(engine.direction);
      const Vec3 f = direction * t;
      result.direction[e]=direction;
      result.force[e]=f;
      const Vec3 rp = engine.articulated_nozzle?engine.nozzle_pivot+rotate(engine.position-engine.nozzle_pivot):engine.position;
      result.position[e]=rp;
      result.force_body += f;
      result.each[e] = t;
      result.moment_body += (rp - massProperties(state_).cg).cross(f);
    }
  }

  return result;
}


} // namespace ofs
