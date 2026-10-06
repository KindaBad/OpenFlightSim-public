#include "ofs/c_api.h"

#include "ofs/simulator.hpp"
#include <new>

using ofs::Simulator;

struct OfsSim {
  Simulator sim;
};

extern "C" {

OfsSim* ofs_create(void) { return new (std::nothrow) OfsSim(); }
void ofs_destroy(OfsSim* sim) { delete sim; }
int ofs_try_step(OfsSim* sim, double dt) {
  if(!sim||!std::isfinite(dt)||dt<=0)return 0;
  try {sim->sim.step(dt);return 1;}catch(...){return 0;}
}
void ofs_step(OfsSim* sim, double dt) { (void)ofs_try_step(sim,dt); }

void ofs_set_controls(OfsSim* sim, double elevator_stick, double aileron_stick,
                      double rudder_pedal, double flap01, double spoiler01, double gear01,
                      double throttle_l, double throttle_r, double brake01, double steering) {
  if (!sim) return;
  ofs::Controls c = sim->sim.controls();
  c.elevator_stick = elevator_stick;
  c.aileron_stick = aileron_stick;
  c.rudder_pedal = rudder_pedal;
  c.flap01 = flap01;
  c.spoiler01 = spoiler01;
  c.gear01 = gear01;
  c.throttle[0] = throttle_l;
  c.throttle[1] = throttle_r;
  c.brake01 = brake01;
  c.steering = steering;
  sim->sim.setControls(c);
}

void ofs_set_elevator_trim(OfsSim* sim, double trim) {
  if (!sim) return;
  auto c=sim->sim.controls(); c.elevator_trim=trim; sim->sim.setControls(c);
}

void ofs_set_weather(OfsSim* sim, double wind_n, double wind_e, double wind_d,
                     double turbulence01, double temp_offset_c) {
  if (!sim) return;
  ofs::Weather w;
  w.wind_ned = {wind_n, wind_e, wind_d};
  w.turbulence01 = turbulence01;
  w.temp_offset_c = temp_offset_c;
  sim->sim.setWeather(w);
}

OfsState ofs_get_state(const OfsSim* sim) {
  OfsState s{};
  if (!sim) return s;
  const ofs::State& st = sim->sim.state();
  s.pos_n = st.pos_ned.x;
  s.pos_e = st.pos_ned.y;
  s.pos_d = st.pos_ned.z;
  s.vel_n = st.vel_ned.x;
  s.vel_e = st.vel_ned.y;
  s.vel_d = st.vel_ned.z;
  double roll, pitch, yaw;
  ofs::eulerFromQuat(st.att, roll, pitch, yaw);
  s.roll_deg = roll * ofs::kRad2Deg;
  s.pitch_deg = pitch * ofs::kRad2Deg;
  s.yaw_deg = yaw * ofs::kRad2Deg;
  s.p = st.omega_body.x;
  s.q = st.omega_body.y;
  s.r = st.omega_body.z;
  s.n1_l = st.n1[0];
  s.n1_r = st.n1[1];
  s.time = st.time;
  return s;
}

int ofs_try_set_state(OfsSim* sim, const OfsState* s) {
  if (!sim || !s) return 0;
  ofs::State st;
  st.pos_ned = {s->pos_n, s->pos_e, s->pos_d};
  st.vel_ned = {s->vel_n, s->vel_e, s->vel_d};
  st.att = ofs::quatFromEuler(s->roll_deg * ofs::kDeg2Rad, s->pitch_deg * ofs::kDeg2Rad,
                              s->yaw_deg * ofs::kDeg2Rad);
  st.omega_body = {s->p, s->q, s->r};
  st.n1[0] = s->n1_l;
  st.n1[1] = s->n1_r;
  st.time = s->time;
  return sim->sim.setState(st)?1:0;
}
void ofs_set_state(OfsSim* sim, const OfsState* s) { (void)ofs_try_set_state(sim,s); }

OfsPhysicsMemory ofs_get_physics_memory(const OfsSim* sim) {
  OfsPhysicsMemory m{};if(!sim) return m;
  const auto& s=sim->sim.state();
  m.fuel_mass=s.fuel_mass;m.payload_mass=s.payload_mass;m.payload_x=s.payload_offset.x;m.payload_y=s.payload_offset.y;m.payload_z=s.payload_offset.z;
  for(int e=0;e<2;++e){m.afterburner[e]=s.afterburner[e];m.engine_health[e]=s.engine_health[e];m.inlet_spike[e]=s.inlet_spike[e];m.nozzle_angle[e]=s.nozzle_angle[e];}
  m.aero_memory_initialized=s.aero_memory_initialized;
  for(int i=0;i<2;++i){m.alpha_lag[i]=s.alpha_lag[i];m.separation[i]=s.separation[i];m.vortex_state[i]=s.vortex_state[i];}
  for(int i=0;i<6;++i){m.surface_health[i]=s.surface_health[i];m.surface_drag[i]=s.surface_drag[i];}
  m.trim_reference=s.trim_reference;m.pilot_pitch=s.pilot_pitch;m.pilot_roll=s.pilot_roll;m.pilot_yaw=s.pilot_yaw;
  m.elevator=s.elevator;m.aileron=s.aileron;m.rudder=s.rudder;m.flap=s.flap;m.spoiler=s.spoiler;m.canard=s.canard;m.elevon_l=s.elevon_l;m.elevon_r=s.elevon_r;
  m.initialized=s.actuators_initialized;m.fcs_enabled=s.fcs_enabled;return m;
}
void ofs_set_physics_memory(OfsSim* sim,const OfsPhysicsMemory* m) {
  if(!sim || !m || (m->initialized!=0 && m->initialized!=1) || (m->fcs_enabled!=0 && m->fcs_enabled!=1) || (m->aero_memory_initialized!=0 && m->aero_memory_initialized!=1)) return;
  auto s=sim->sim.state();s.fuel_mass=m->fuel_mass;s.payload_mass=m->payload_mass;s.payload_offset={m->payload_x,m->payload_y,m->payload_z};
  for(int e=0;e<2;++e){s.afterburner[e]=m->afterburner[e];s.engine_health[e]=m->engine_health[e];s.inlet_spike[e]=m->inlet_spike[e];s.nozzle_angle[e]=m->nozzle_angle[e];}
  s.aero_memory_initialized=m->aero_memory_initialized;
  for(int i=0;i<2;++i){s.alpha_lag[i]=m->alpha_lag[i];s.separation[i]=m->separation[i];s.vortex_state[i]=m->vortex_state[i];}
  for(int i=0;i<6;++i){s.surface_health[i]=m->surface_health[i];s.surface_drag[i]=m->surface_drag[i];}
  s.trim_reference=m->trim_reference;s.pilot_pitch=m->pilot_pitch;s.pilot_roll=m->pilot_roll;s.pilot_yaw=m->pilot_yaw;
  s.elevator=m->elevator;s.aileron=m->aileron;s.rudder=m->rudder;s.flap=m->flap;s.spoiler=m->spoiler;s.canard=m->canard;s.elevon_l=m->elevon_l;s.elevon_r=m->elevon_r;
  s.actuators_initialized=m->initialized;s.fcs_enabled=m->fcs_enabled;sim->sim.setState(s);
}

OfsInstruments ofs_get_instruments(const OfsSim* sim) {
  OfsInstruments o{};
  if (!sim) return o;
  const ofs::Instruments i = sim->sim.instruments();
  o.tas = i.tas;
  o.ias = i.ias;
  o.cas = i.cas;
  o.mach = i.mach;
  o.alt_msl = i.alt_msl;
  o.agl = i.agl;
  o.vs = i.vs;
  o.hdg_deg = i.hdg_deg;
  o.pitch_deg = i.pitch_deg;
  o.roll_deg = i.roll_deg;
  o.alpha_deg = i.alpha_deg;
  o.beta_deg = i.beta_deg;
  o.g_load = i.g_load;
  o.vstall = i.vstall;
  o.stall_warn = i.stall_warn ? 1 : 0;
  return o;
}

OfsDebugFrame ofs_get_debug(const OfsSim* sim) {
  OfsDebugFrame o{};
  o.count = 0;
  if (!sim) return o;
  const ofs::DebugFrame d = sim->sim.debugFrame();
  int n = static_cast<int>(d.count);
  if (n > 16) n = 16;
  o.count = n;
  for (int i = 0; i < n; ++i) {
    for (int k = 0; k < 32; ++k) o.forces[i].name[k] = d.forces[i].name[k];
    o.forces[i].px = d.forces[i].pos_body.x;
    o.forces[i].py = d.forces[i].pos_body.y;
    o.forces[i].pz = d.forces[i].pos_body.z;
    o.forces[i].fx = d.forces[i].force_body.x;
    o.forces[i].fy = d.forces[i].force_body.y;
    o.forces[i].fz = d.forces[i].force_body.z;
  }
  o.total_fx = d.total_force_body.x;
  o.total_fy = d.total_force_body.y;
  o.total_fz = d.total_force_body.z;
  o.total_mx = d.total_moment_body.x;
  o.total_my = d.total_moment_body.y;
  o.total_mz = d.total_moment_body.z;
  o.alpha_rad = d.alpha_rad;
  o.beta_rad = d.beta_rad;
  o.qbar = d.qbar;
  return o;
}

int ofs_rebase_if_needed(OfsSim* sim, double threshold_m) {
  if (!sim) return 0;
  if (threshold_m > 0) sim->sim.origin().threshold = threshold_m;
  return sim->sim.origin().rebaseIfNeeded(sim->sim.state().pos_ned) ? 1 : 0;
}

void ofs_get_origin(const OfsSim* sim, double* n, double* e, double* d) {
  if (!sim) return;
  if (n) *n = sim->sim.origin().origin_ned.x;
  if (e) *e = sim->sim.origin().origin_ned.y;
  if (d) *d = sim->sim.origin().origin_ned.z;
}

void ofs_get_render_pos(const OfsSim* sim, double* x, double* y, double* z) {
  if (!sim) return;
  const ofs::Vec3 r = sim->sim.origin().toRender(sim->sim.state().pos_ned);
  if (x) *x = r.x;
  if (y) *y = r.y;
  if (z) *z = r.z;
}

}  // extern "C"
