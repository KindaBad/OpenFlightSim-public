#include "platform.hpp"
#include "log.hpp"
#include <SDL3/SDL_main.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
namespace ofs::client {
Platform::Platform(PlatformOptions options) : options_(std::move(options)) {
  SDL_SetMainReady();
#ifdef __linux__
  // Initial Linux path: X11/XWayland, isolated here. Native Wayland is deferred.
  SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
#endif
  if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) throw std::runtime_error(SDL_GetError());
  const int width = options_.width > 0 ? options_.width : 1280;
  const int height = options_.height > 0 ? options_.height : 800;
  window_ = SDL_CreateWindow(options_.title.c_str(), width, height,
                            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if(!window_) { const std::string error = SDL_GetError(); SDL_Quit(); throw std::runtime_error(error); }
  log("CORE", "SDL3 window initialized at " + std::to_string(width) + "x" + std::to_string(height));
}
Platform::~Platform() { SDL_DestroyWindow(window_); SDL_Quit(); log("CORE", "SDL shutdown complete"); }
bgfx::SwapChain Platform::nativeHandles() const {
  bgfx::SwapChain data{};
  const auto properties = SDL_GetWindowProperties(window_);
#ifdef _WIN32
  data.nwh = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__linux__)
  if(std::strcmp(SDL_GetCurrentVideoDriver(), "x11") != 0)
    throw std::runtime_error("The Linux client requires X11 or XWayland");
  data.ndt = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
  data.nwh = reinterpret_cast<void*>(static_cast<std::uintptr_t>(
    SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
#else
#error The native client supports Linux and Windows
#endif
  if(!data.nwh) throw std::runtime_error("SDL3 did not provide a native window handle");
  return data;
}
bool Platform::toggleFullscreen() {
  const bool next = !fullscreen_;
  if(!SDL_SetWindowFullscreen(window_, next)) { log("CORE", SDL_GetError()); return false; }
  fullscreen_ = next; return true;
}
}  // namespace ofs::client
