#pragma once
// Plain-C interface over the headless ofs::Simulator.
// No STL across the boundary; useful for tooling and external integrations.
#ifdef __cplusplus
extern "C" {
#endif

typedef struct OfsSim OfsSim;

OfsSim* ofs_create(void);
void ofs_destroy(OfsSim* sim);
void ofs_step(OfsSim* sim, double dt);
// Detectable variants: 1 accepted, 0 invalid input/error. Legacy void wrappers remain.
int ofs_try_step(OfsSim* sim, double dt);

// Controls: elevator/aileron/rudder sticks -1..1, flap/spoiler/brake 0..1,
// gear 0..1, throttle 0..1 each, steering -1..1.
void ofs_set_controls(OfsSim* sim, double elevator_stick, double aileron_stick,
                      double rudder_pedal, double flap01, double spoiler01, double gear01,
                      double throttle_l, double throttle_r, double brake01, double steering);
// Persistent normalized elevator offset; existing control calls retain it.
void ofs_set_elevator_trim(OfsSim* sim, double trim);
void ofs_set_weather(OfsSim* sim, double wind_n, double wind_e, double wind_d,
                     double turbulence01, double temp_offset_c);

// State snapshot (NED world, FRD body attitudes as yaw/pitch/roll degrees).
typedef struct OfsState {
  double pos_n, pos_e, pos_d;
  double vel_n, vel_e, vel_d;
  double yaw_deg, pitch_deg, roll_deg;
  double p, q, r;
  double n1_l, n1_r;
  double time;
} OfsState;

OfsState ofs_get_state(const OfsSim* sim);
void ofs_set_state(OfsSim* sim, const OfsState* s);
int ofs_try_set_state(OfsSim* sim, const OfsState* s);

// Supplemental M3.66 state. Pair with OfsState for complete physics save/restore.
// Legacy OfsState/control functions keep their ABI and configured initial loading.
typedef struct OfsPhysicsMemory {
  double fuel_mass,payload_mass,payload_x,payload_y,payload_z;
  double afterburner[2],engine_health[2],surface_health[6],surface_drag[6];
  double trim_reference,pilot_pitch,pilot_roll,pilot_yaw;
  double elevator,aileron,rudder,flap,spoiler,canard,elevon_l,elevon_r;
  int initialized,fcs_enabled;
  // M3.68 extension; rebuild callers using this supplemental struct.
  double inlet_spike[2],nozzle_angle[2];
  int aero_memory_initialized;
  double alpha_lag[2], separation[2], vortex_state[2];
} OfsPhysicsMemory;
OfsPhysicsMemory ofs_get_physics_memory(const OfsSim* sim);
void ofs_set_physics_memory(OfsSim* sim,const OfsPhysicsMemory* memory);

typedef struct OfsInstruments {
  double tas, ias, cas, mach;
  double alt_msl, agl, vs;
  double hdg_deg, pitch_deg, roll_deg;
  double alpha_deg, beta_deg;
  double g_load, vstall;
  int stall_warn;
} OfsInstruments;

OfsInstruments ofs_get_instruments(const OfsSim* sim);

// Debug forces: up to 16 entries, body-frame application points and forces.
typedef struct OfsDebugForce {
  char name[32];
  double px, py, pz;
  double fx, fy, fz;
} OfsDebugForce;

typedef struct OfsDebugFrame {
  OfsDebugForce forces[16];
  int count;
  double total_fx, total_fy, total_fz;
  double total_mx, total_my, total_mz;
  double alpha_rad, beta_rad, qbar;
} OfsDebugFrame;

OfsDebugFrame ofs_get_debug(const OfsSim* sim);

// Render-origin rebasing: render = sim - origin. Returns 1 if rebased.
int ofs_rebase_if_needed(OfsSim* sim, double threshold_m);
void ofs_get_origin(const OfsSim* sim, double* n, double* e, double* d);
void ofs_get_render_pos(const OfsSim* sim, double* x, double* y, double* z);

#ifdef __cplusplus
}
#endif
