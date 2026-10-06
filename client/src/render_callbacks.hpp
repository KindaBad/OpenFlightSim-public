#pragma once
// bgfx platform callbacks.
//
// Split from renderer.hpp so the graphical smoke test and any future tool can
// reuse them without pulling in the whole renderer.

#include <bgfx/bgfx.h>

#include <atomic>
#include <cstdint>
#include <string>

namespace ofs::client {

class Renderer;

// Writes a captured frame as a binary PPM. Screenshots are diagnostics, not a
// rendering path, so a single allocation here is acceptable.
class RenderCallbacks final : public bgfx::CallbackI {
 public:
  std::atomic<bool> screenshotWritten{false};

  void fatal(const char*, std::uint16_t, bgfx::Fatal::Enum, const char*) override;
  void traceVargs(const char*, std::uint16_t, const char*, va_list) override;
  void profilerBegin(const char*, std::uint32_t, const char*, std::uint16_t) override {}
  void profilerBeginLiteral(const char*, std::uint32_t, const char*, std::uint16_t) override {}
  void profilerEnd() override {}
  std::uint32_t cacheReadSize(std::uint64_t) override { return 0; }
  bool cacheRead(std::uint64_t, void*, std::uint32_t) override { return false; }
  void cacheWrite(std::uint64_t, const void*, std::uint32_t) override {}
  void screenShot(const char*, std::uint32_t, std::uint32_t, std::uint32_t,
                  bgfx::TextureFormat::Enum, const void*, std::uint32_t, bool) override;
  void captureBegin(std::uint32_t, std::uint32_t, std::uint32_t, bgfx::TextureFormat::Enum, bool) override {}
  void captureEnd() override {}
  void captureFrame(const void*, std::uint32_t) override {}
};

}  // namespace ofs::client
