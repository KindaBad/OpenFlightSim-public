#pragma once
#include "ofs/simulator.hpp"
#include "ofs/airliner.hpp"
#include <iomanip>
#include <ostream>
namespace ofs {
// Explicit opt-in stream; no logging or allocation in the simulation tick.
inline void telemetryHeader(std::ostream &out) {
  out << "time,altitude,tas,ias,mach,alpha,beta,pitch,roll,heading,p,q,r,g,"
         "throttle_l,throttle_r,elevator,aileron,rudder,flap,spoiler,lift,drag,"
         "thrust_l,thrust_r,fuel,mass,cg_x,cg_y,cg_z,north,east,down,"
         "energy_specific,nozzle_l,nozzle_r,elevator_rad,aileron_rad,rudder_rad,slat_deg,normal_acceleration,"
         "alpha_lag_l,alpha_lag_r,separation_l,separation_r,vortex_l,vortex_r\n";
}
inline void telemetryRow(std::ostream &out, const Simulator &sim) {
  const auto &s = sim.state();
  const auto i = sim.instruments();
  const auto a = sim.evalAero();
  const auto t = sim.evalThrust();
  const auto m = sim.massProperties();
  out << std::setprecision(12);
  const double values[]{s.time,
                        i.alt_msl,
                        i.tas,
                        i.ias,
                        i.mach,
                        i.alpha_deg,
                        i.beta_deg,
                        i.pitch_deg,
                        i.roll_deg,
                        i.hdg_deg,
                        s.omega_body.x,
                        s.omega_body.y,
                        s.omega_body.z,
                        i.g_load,
                        sim.controls().throttle[0],
                        sim.controls().throttle[1],
                        s.elevator,
                        s.aileron,
                        s.rudder,
                        s.flap,
                        s.spoiler,
                        a.lift_body.norm(),
                        a.drag_body.norm(),
                        t.each[0],
                        t.each[1],
                        s.fuel_mass,
                        m.mass,
                        m.cg.x,
                        m.cg.y,
                        m.cg.z,
                        s.pos_ned.x,
                        s.pos_ned.y,
                        s.pos_ned.z,
                        kG0*i.alt_msl+.5*i.tas*i.tas,
                        s.nozzle_angle[0],s.nozzle_angle[1],
                        elevatorDeflection(s.elevator,sim.config()),s.aileron*sim.config().ail_max,-s.rudder*sim.config().rud_max,
                        sim.config().aero_kind==AeroModelKind::AirlinerEngineering?a320HighLift(s.flap).slat_degrees:0,
                        i.normal_acceleration,s.alpha_lag[0],s.alpha_lag[1],s.separation[0],s.separation[1],s.vortex_state[0],s.vortex_state[1]};
  for (std::size_t j = 0; j < std::size(values); ++j) {
    if (j)
      out << ',';
    out << values[j];
  }
  out << '\n';
}
} // namespace ofs
