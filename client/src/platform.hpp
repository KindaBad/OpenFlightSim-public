#pragma once
#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <string>
namespace ofs::client {
// Window size and title, fixed before the window is created. The graphics
// settings carry the same values, but the window must exist first, so the
// request travels through this.
struct PlatformOptions {
  int width{1280};
  int height{800};
  std::string title{"OpenFlightSim"};
};
class Platform {
 public:
  explicit Platform(PlatformOptions options = {});
  ~Platform();
  Platform(const Platform&) = delete;
  Platform& operator=(const Platform&) = delete;
  SDL_Window* window() const { return window_; }
  bgfx::SwapChain nativeHandles() const;
  bool toggleFullscreen();
  const PlatformOptions& options() const { return options_; }
 private:
  PlatformOptions options_;
  SDL_Window* window_{};
  bool fullscreen_{};
};
}  // namespace ofs::client
