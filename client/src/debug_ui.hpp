#pragma once
// Developer windows: simulation diagnostics, graphics settings and renderer
// statistics. Kept entirely separate from the gameplay HUD so a clean
// screenshot is one keypress away.

#include "camera.hpp"
#include "hud.hpp"
#include "ofs/fixed_step.hpp"
#include "ofs/simulator.hpp"
#include "settings.hpp"

#include <SDL3/SDL.h>

namespace ofs::client {

class Renderer;

class UiContext {
 public:
  explicit UiContext(SDL_Window* window, bool smokeFont = false);
  ~UiContext();
  UiContext(const UiContext&) = delete;
  UiContext& operator=(const UiContext&) = delete;
};

// Simulation state the debug window can request, plus the settings it edits.
struct UiSettings {
  bool multiplayer{false};
  bool botsAvailable{}, dogfight{}, toggleDogfight{};
  bool paused{false};
  bool parkingBrake{true};
  bool resetParked{false};
  bool resetAirborne{false};
  // Set by the settings panel when the user asks to save or reset.
  bool saveSettings{false};
  bool resetSettings{false};
  // HUD toggles, kept here so the settings panel owns them.
  HudSettings hud{};
  // Window/fullscreen requests.
  bool toggleFullscreen{false};
  // Camera control, applied by the caller.
  CameraMode cameraMode{CameraMode::Free};
  bool frameAircraft{false};
  float resetAirborneX{190}, resetAirborneY{645}; // Observed UI target for smoke automation.
};

// Draws every developer window. `settings` is edited in place and the caller
// applies it.
void debugUi(const Simulator& sim, Controls& controls, const Camera& camera,
             const FixedStepClock& clock, unsigned steps, double frameTime,
             double measuredTicks, const Renderer& renderer, const std::string& assetName,
             std::size_t aircraftCount, bool gamepad, UiSettings& ui, GraphicsSettings& settings);

}  // namespace ofs::client
