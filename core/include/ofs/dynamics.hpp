#pragma once
#include "ofs/aircraft.hpp"
#include <functional>
#include "ofs/unsteady_aero.hpp"
namespace ofs {
struct ContinuousDerivative {
  Vec3 position{}, velocity{}, angular_velocity{};
  Quat attitude{0,0,0,0};
  UnsteadyDerivative aerodynamic_memory;
};
enum class ContinuousIntegrator { SemiImplicitEuler, RungeKutta4 };
// Advances rigid-body variables, aerodynamic lag states and evaluation time. Other
// State memory is held fixed by this function. No actuator/event/contact calls.
State integrateContinuous(const State&, double dt, ContinuousIntegrator,
                          const std::function<ContinuousDerivative(const State&)>&);
}
