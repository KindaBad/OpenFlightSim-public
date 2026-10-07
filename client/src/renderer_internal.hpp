#pragma once
// Constants and small helpers shared by the renderer's translation units.

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstdint>

namespace ofs::client {

inline constexpr std::uint64_t kOpaqueState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
    BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CCW | BGFX_STATE_FRONT_CCW;

inline constexpr std::uint64_t kBlendState =
    BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL |
    BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA);

// Position(3) + packed colour, used by the screen triangle and the developer grid.
struct UnlitVertex {
  float x, y, z;
  std::uint32_t color;
};

// Cloud layer constants shared between the frame constants and the passes.
// The weather map repeats over this distance.
inline constexpr float kWeatherMapExtent = 62720.f;
// Extinction of dense cumulus per metre of path. Real cloud is several times
// more opaque; this value keeps internal structure visible at march step sizes.
inline constexpr float kCloudExtinction = .026f;
inline constexpr float kCirrusAltitude = 9800.f;

// Atmosphere table sizes; they must match client/shaders/atmosphere.glsl.
inline constexpr int kSkyTableWidth = 192, kSkyTableHeight = 108;
inline constexpr int kAerialTile = 32, kAerialColumns = 8, kAerialRows = 4;

inline glm::mat4 makeOrtho(float l, float r, float b, float t, float n, float f) {
  return bgfx::getCaps()->homogeneousDepth ? glm::orthoRH_NO(l, r, b, t, n, f)
                                           : glm::orthoRH_ZO(l, r, b, t, n, f);
}

}  // namespace ofs::client
