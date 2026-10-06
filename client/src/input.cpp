#include "input.hpp"

#include "log.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace ofs::client {

Input::Input() {
  int count = 0;
  auto* ids = SDL_GetGamepads(&count);
  if (count > 0) {
    connect(ids[0]);
    padId_ = ids[0];
  }
  SDL_free(ids);
}

Input::~Input() {
  if (pad_) SDL_CloseGamepad(pad_);
}

void Input::connect(SDL_JoystickID id) {
  if (!pad_) {
    pad_ = SDL_OpenGamepad(id);
    if (pad_) {
      padId_ = id;
      log("INPUT", "Gamepad connected");
    }
  }
}

void Input::release(SDL_Window* window) {
  keys_.fill(false);
  previous_.fill(false);
  looking_ = false;
  leftMouse_ = false;
  rightMouse_ = false;
  orbitZoom_ = 0;
  SDL_SetWindowRelativeMouseMode(window, false);
}

void Input::event(const SDL_Event& e, SDL_Window* window) {
  if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
    const auto sc = e.key.scancode;
    if (sc >= 0 && sc < SDL_SCANCODE_COUNT) keys_[static_cast<std::size_t>(sc)] = e.type == SDL_EVENT_KEY_DOWN;
  }
  if (e.type == SDL_EVENT_WINDOW_FOCUS_LOST) release(window);
  if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT &&
      !ImGui::GetIO().WantCaptureMouse) {
    looking_ = SDL_SetWindowRelativeMouseMode(window, true);
  }
  if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) {
    looking_ = false;
    SDL_SetWindowRelativeMouseMode(window, false);
  }
  if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT)
    leftMouse_ = !ImGui::GetIO().WantCaptureMouse;
  if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT)
    leftMouse_ = false;
  if (e.type == SDL_EVENT_MOUSE_WHEEL) {
    orbitZoom_ -= static_cast<double>(e.wheel.y);
  }
  if (e.type == SDL_EVENT_GAMEPAD_ADDED) connect(e.gdevice.which);
  if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad_ && SDL_GetGamepadID(pad_) == e.gdevice.which) {
    SDL_CloseGamepad(pad_);
    pad_ = nullptr;
    log("INPUT", "Gamepad disconnected");
    int count = 0;
    auto* ids = SDL_GetGamepads(&count);
    if (count > 0) connect(ids[0]);
    SDL_free(ids);
  }
}

bool Input::pressed(SDL_Scancode code, bool captureKeyboard) {
  if (captureKeyboard) return false;
  const auto index = static_cast<std::size_t>(code);
  if (index >= keys_.size()) return false;
  const bool down = keys_[index];
  const bool was = previous_[index];
  previous_[index] = down;
  return down && !was;
}

CameraMode Input::cycleCamera(CameraMode current) const {
  switch (current) {
    case CameraMode::Free: return CameraMode::Chase;
    case CameraMode::Chase: return CameraMode::CloseChase;
    case CameraMode::CloseChase: return CameraMode::Orbit;
    case CameraMode::Orbit: return CameraMode::FirstPerson;
    case CameraMode::FirstPerson: return CameraMode::Free;
    case CameraMode::Count: break;
  }
  return CameraMode::Free;
}

void Input::freeCamera(Camera& camera, double dt, bool captureKeyboard) {
  auto key = [&](SDL_Scancode sc) {
    return !captureKeyboard && keys_[static_cast<std::size_t>(sc)] ? 1 : 0;
  };
  camera.move(key(SDL_SCANCODE_W) - key(SDL_SCANCODE_S), key(SDL_SCANCODE_D) - key(SDL_SCANCODE_A),
              key(SDL_SCANCODE_R) - key(SDL_SCANCODE_F), key(SDL_SCANCODE_LSHIFT) != 0, dt);
}

bool Input::firing(bool captureKeyboard, bool captureMouse) const {
  const bool space = !captureKeyboard && keys_[static_cast<std::size_t>(SDL_SCANCODE_SPACE)];
  const bool mouse = !captureMouse && leftMouse_;
  const bool trigger = pad_ && SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000;
  return space || mouse || trigger;
}

void Input::update(Controls& c, double dt, bool captureKeyboard) {
  auto key = [&](SDL_Scancode sc) { return !captureKeyboard && keys_[static_cast<std::size_t>(sc)]; };
  c.elevator_stick = key(SDL_SCANCODE_S) - key(SDL_SCANCODE_W);
  c.aileron_stick = key(SDL_SCANCODE_D) - key(SDL_SCANCODE_A);
  c.rudder_pedal = key(SDL_SCANCODE_E) - key(SDL_SCANCODE_Q);
  const double throttle = c.throttle[0] + ((key(SDL_SCANCODE_LSHIFT) || key(SDL_SCANCODE_RSHIFT)) - (key(SDL_SCANCODE_LCTRL) || key(SDL_SCANCODE_RCTRL))) * dt * .72;
  c.throttle[0] = c.throttle[1] = clamp(throttle, 0, 1);
  // Keyboard brakes are momentary; the parking brake is a UI control.
  if (key(SDL_SCANCODE_B)) c.brake01 = 1;
  if (pad_ && !captureKeyboard) {
    const auto axis = [&](SDL_GamepadAxis a) {
      const double v = SDL_GetGamepadAxis(pad_, a) / 32767.0;
      return std::abs(v) < .12 ? 0.0 : std::copysign((std::min(1.0, std::abs(v)) - .12) / .88, v);
    };
    c.aileron_stick = clamp(c.aileron_stick + axis(SDL_GAMEPAD_AXIS_LEFTX), -1, 1);
    c.elevator_stick = clamp(c.elevator_stick + axis(SDL_GAMEPAD_AXIS_LEFTY), -1, 1);
    c.rudder_pedal = clamp(c.rudder_pedal + axis(SDL_GAMEPAD_AXIS_RIGHTX), -1, 1);
  }
  c.steering = c.rudder_pedal;
}

}  // namespace ofs::client
