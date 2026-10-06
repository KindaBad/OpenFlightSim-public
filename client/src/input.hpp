#pragma once
// Input: keyboard, mouse and gamepad. M3.66 uses WASD flight controls,
// Q/E rudder and either Shift/Ctrl for throttle.

#include "camera.hpp"
#include "settings.hpp"

#include <SDL3/SDL.h>

#include <array>

namespace ofs::client {

struct Input {
  Input();
  ~Input();
  Input(const Input&) = delete;
  Input& operator=(const Input&) = delete;

  void event(const SDL_Event& event, SDL_Window* window);
  // Samples the aircraft controls. The camera is no longer driven here, so the
  // caller suppresses flight keys while WASD moves the developer camera.
  void update(Controls& controls, double dt, bool captureKeyboard);
  void release(SDL_Window* window);

  // A key-down transition this frame, for toggles the caller owns.
  bool pressed(SDL_Scancode code, bool captureKeyboard);
  // WASD/R/F/Shift, only in free-camera mode so the flight cameras are never
  // dragged around by leftover developer keys.
  void freeCamera(Camera& camera, double dt, bool captureKeyboard);
  // Held fire input: space, left mouse or the gamepad right trigger.
  bool firing(bool captureKeyboard, bool captureMouse) const;
  // Raw key state, for the free camera.
  bool key(SDL_Scancode code) const { return keys_[static_cast<std::size_t>(code)]; }

  bool connected() const { return pad_ != nullptr; }
  bool leftMouse() const { return leftMouse_; }
  bool rightMouse() const { return rightMouse_; }
  // Relative-mouse look is active; used for orbit and the free camera.
  bool looking() const { return looking_; }
  // Cycles through the camera modes, bound to Tab.
  CameraMode cycleCamera(CameraMode current) const;
  // Mouse-wheel orbit zoom accumulator, consumed by the camera.
  double orbitZoom() const { return orbitZoom_; }

 private:
  void connect(SDL_JoystickID id);

  SDL_Gamepad* pad_{};
  SDL_JoystickID padId_{};
  std::array<bool, SDL_SCANCODE_COUNT> keys_{};
  std::array<bool, SDL_SCANCODE_COUNT> previous_{};
  bool looking_{};
  bool leftMouse_{};
  bool rightMouse_{};
  double orbitZoom_{};
};

}  // namespace ofs::client
