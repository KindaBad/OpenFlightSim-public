#pragma once
// 6-DoF fixed-step simulator with render-origin rebasing helper.

#include "ofs/aircraft.hpp"
#include "ofs/atmosphere.hpp"
#include "ofs/inertia.hpp"
#include "ofs/dynamics.hpp"

namespace ofs {

// Double-precision sim position with a float-friendly render offset.
// The renderer draws at (pos - origin); origin jumps only by rebase().
class RenderOrigin {
public:
  Vec3 origin_ned{0, 0, 0};
  double threshold{5000.0}; // m

  Vec3 toRender(const Vec3 &pos_ned) const { return pos_ned - origin_ned; }
  // Returns true if a rebase happened.
  bool rebaseIfNeeded(const Vec3 &pos_ned) {
    if ((pos_ned - origin_ned).norm() > threshold) {
      origin_ned = pos_ned;
      return true;
    }
    return false;
  }
  void rebaseTo(const Vec3 &pos_ned) { origin_ned = pos_ned; }
};

class Simulator {
public:
  enum class GroundModel { Terrain, FlatRunway }; // FlatRunway is a reference test fixture.
  explicit Simulator(const AircraftConfig &cfg = a320Config(), GroundModel ground = GroundModel::Terrain);

  void setControls(const Controls &c) {
    controls_ = sanitized(c);
    if(cfg_.flap_max_deg==0) controls_.flap01=0;
    if(cfg_.control_law==FlightControlLaw::Delta) controls_.spoiler01=0;
  }
  const Controls &controls() const { return controls_; }
  void setWeather(const Weather &w);
  const Weather &weather() const { return weather_; }
  // Reject nonfinite external state; accepted spool values are clamped.
  bool setState(const State &s);
  const State &state() const { return state_; }
  const AircraftConfig &config() const { return cfg_; }

  RenderOrigin &origin() { return origin_; }
  const RenderOrigin &origin() const { return origin_; }

  // Advance by dt seconds (splits into <=1/240 s substeps internally).
  void step(double dt);
  struct GroundImpact {
    Vec3 position{}, normal{0,0,-1}, velocity{};
    double closingSpeed{}, damage{}, scrapeSpeed{};
    bool bodyContact{};
  };
  const GroundImpact& groundImpact() const { return ground_impact_; }

  AirData airData() const;
  Vec3 windAt(const Vec3 &pos_ned, double t) const;
  Instruments instruments() const;
  DebugFrame debugFrame() const { return last_debug_; }

  // Exposed for tests: single-physics evaluation at current state.
  struct AeroResult {
    Vec3 force_body{0, 0, 0};
    Vec3 moment_body{0, 0, 0};
    Vec3 lift_body{}, drag_body{}, side_body{};
    double cl{0}, cd{0}, cm{0}, alpha{0}, beta{0}, qbar{0}, vtas{0};
    std::array<DebugForce, surfaceCount> surfaces{};
    std::array<DebugForce,2> levcons{};
    std::array<double, surfaceCount> local_alpha{}, local_beta{}, local_qbar{};
    double ground_effect{1}, profile_cd{}, induced_cd{}, wave_cd{}, device_cd{};
  };
  AeroResult evalAero() const;
  // Pure evaluation: supplied snapshot and actual surfaces, no memory advancement.
  AeroResult evalAero(const State&, const Controls&, const Weather&) const;
  struct ThrustResult {
    Vec3 force_body{}, moment_body{};
    Vec3 position[2]{}; // actual articulated nozzle exits in reference body frame
    Vec3 direction[2]{{1,0,0},{1,0,0}}, force[2]{};
    double each[2]{}, fuel_flow[2]{};
  };
  ThrustResult evalThrust() const;
  ThrustResult evalThrust(const State&, const Weather&) const;
  struct ContinuousEvaluation { AeroResult aero; ThrustResult propulsion; ContinuousDerivative derivative; };
  ContinuousEvaluation evaluateContinuous(const State&, const Controls&, const Weather&) const;
  void setIntegrator(ContinuousIntegrator method) { integrator_=method; }
  ContinuousIntegrator integrator() const { return integrator_; }

  struct MassProperties {
    double mass{};
    Vec3 cg{}, inertia{};
    double ixz{}, ixy{}, iyz{};
    InertiaTensor tensor() const { return {inertia.x,inertia.y,inertia.z,ixy,ixz,iyz}; }
  };
  MassProperties massProperties() const;
  MassProperties massProperties(const State&) const;
  // Only used by offline trim/static coefficient inspection. Runtime uses
  // actuators.
  void primeActuators();
  double normalLoad() const;
  double equilibriumElevator() const;
  Controls controlTargets() const;
  struct ControlAllocation { Controls surfaces; double nozzle[2]{}; };
  ControlAllocation vectorFighterAllocation(double horizon_seconds = 1e9) const;

private:
  void substep(double dt);
  TerrainSample groundSurface(double north, double east) const {
    return ground_model_==GroundModel::FlatRunway ? TerrainSample{} : sampleTerrain(north,east);
  }
  double groundHeightAt(double north, double east) const { return groundSurface(north,east).heightNed; }

  Vec3 windAt(const Vec3&, double, const Weather&) const;
  ContinuousIntegrator integrator_{ContinuousIntegrator::SemiImplicitEuler};
  AircraftConfig cfg_;
  GroundModel ground_model_{GroundModel::Terrain}; // immutable for a simulation run
  State state_;
  Controls controls_;
  Weather weather_;
  RenderOrigin origin_;
  DebugFrame last_debug_;
  Vec3 last_total_force_world_{0, 0, 0};
  GroundImpact ground_impact_{}; // diagnostics aggregated over one public step

  static Controls sanitized(const Controls &c);
};

} // namespace ofs
