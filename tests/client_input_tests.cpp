#include "input.hpp"
#include "ofs/trim.hpp"
#include <cstdio>
#include <imgui.h>
#include <stdexcept>
using namespace ofs;
using namespace ofs::client;
void check(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
void key(Input &input, SDL_Scancode code, bool down) {
  SDL_Event e{};
  e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
  e.key.scancode = code;
  input.event(e, nullptr);
}
int main() {
  try {
    SDL_Init(SDL_INIT_GAMEPAD);
    ImGui::CreateContext();
    Input input;
    Controls c;
    for (auto code : {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_RSHIFT}) {
      key(input, code, true);
      input.update(c, .5, false);
      key(input, code, false);
    }
    check(std::abs(c.throttle[0] - .72) < 1e-12 &&
              c.throttle[0] == c.throttle[1],
          "either Shift increases both throttles at dt-based rate");
    for (auto code : {SDL_SCANCODE_LCTRL, SDL_SCANCODE_RCTRL}) {
      key(input, code, true);
      input.update(c, .25, false);
      key(input, code, false);
    }
    check(std::abs(c.throttle[0] - .36) < 1e-12,
          "either Ctrl decreases throttle");
    key(input, SDL_SCANCODE_LSHIFT, true);
    key(input, SDL_SCANCODE_RCTRL, true);
    input.update(c, 1, false);
    check(std::abs(c.throttle[0] - .36) < 1e-12,
          "opposed throttle inputs cancel");
    key(input, SDL_SCANCODE_LSHIFT, false);
    key(input, SDL_SCANCODE_RCTRL, false);
    auto trim = solveTrim();
    Simulator sim;
    sim.setState(trim.state);
    c = trim.controls;
    key(input, SDL_SCANCODE_S, true);
    key(input, SDL_SCANCODE_D, true);
    key(input, SDL_SCANCODE_E, true);
    input.update(c, .01, false);
    sim.setControls(c);
    for (int tick = 0; tick < 30; ++tick)
      sim.step(1. / 120);
    check(sim.state().omega_body.x > 0 && sim.state().omega_body.y > 0 &&
              sim.state().omega_body.z > 0,
          "S/D/E produce physical pitch-up/right-roll/right-yaw");
    input.update(c, .1, true);
    check(c.elevator_stick == 0 && c.aileron_stick == 0 && c.rudder_pedal == 0,
          "captured keyboard releases flight axes");
    key(input, SDL_SCANCODE_S, false);
    key(input, SDL_SCANCODE_D, false);
    key(input, SDL_SCANCODE_E, false);
    key(input, SDL_SCANCODE_W, true);
    key(input, SDL_SCANCODE_A, true);
    key(input, SDL_SCANCODE_Q, true);
    input.update(c, .01, false);
    check(c.elevator_stick == -1 && c.aileron_stick == -1 &&
              c.rudder_pedal == -1,
          "W/A/Q opposite axes");
    ImGui::DestroyContext();
    std::puts("PASS new keyboard controls, both modifiers, dt throttle, UI "
              "capture and physical axis signs");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "INPUT FAIL: %s\n", e.what());
    return 1;
  }
}
