#pragma once
// Physically based clear-sky atmosphere, CPU side.
//
// One description of the air drives every lighting quantity the renderer uses:
// the sun's colour at any altitude, the sky's ambient irradiance, the haze that
// fades distant terrain, and the camera exposure. The same parameters are sent
// to the GPU, where the sky-view and aerial-perspective lookups are integrated
// each frame (see client/shaders/atmosphere.glsl, which mirrors this file).
//
// Units are SI: metres, and scattering coefficients in 1/m. Radiometric values
// use one scene unit = 10 000 cd/m^2, so irradiance is in units of 10 000 lux.
// A clear noon sun is therefore close to 10 and a sunlit white cloud close to 3.
//
// The model follows Bruneton & Neyret (2008) for the transmittance table and
// Hillaire (2020) for the multiple-scattering table. It has no GPU, window or
// GLM dependency so the headless test suite can check it.

#include <array>
#include <cstddef>
#include <vector>

namespace ofs::client {

struct Rgb {
  float r{}, g{}, b{};
  constexpr Rgb operator+(const Rgb& o) const { return {r + o.r, g + o.g, b + o.b}; }
  constexpr Rgb operator-(const Rgb& o) const { return {r - o.r, g - o.g, b - o.b}; }
  constexpr Rgb operator*(const Rgb& o) const { return {r * o.r, g * o.g, b * o.b}; }
  constexpr Rgb operator*(float s) const { return {r * s, g * s, b * s}; }
  constexpr Rgb operator/(float s) const { return {r / s, g / s, b / s}; }
  Rgb& operator+=(const Rgb& o) { r += o.r; g += o.g; b += o.b; return *this; }
  // Rec. 709 relative luminance.
  constexpr float luminance() const { return .2126f * r + .7152f * g + .0722f * b; }
};

struct AtmosphereParameters {
  float planetRadius{6'360'000.f};
  float atmosphereHeight{100'000.f};
  // Molecular (Rayleigh) scattering at sea level, exponential with altitude.
  Rgb rayleighScattering{5.802e-6f, 13.558e-6f, 33.1e-6f};
  float rayleighScaleHeight{8000.f};
  // Aerosols (Mie). Scattering and extinction at sea level, grey.
  float mieScattering{4.0e-5f};
  float mieExtinction{4.44e-5f};
  float mieScaleHeight{1200.f};
  float mieAnisotropy{.80f};
  // A second, shallow aerosol layer for ground fog and mist. Shares the Mie
  // phase function; zero extinction disables it.
  float fogExtinction{0.f};
  float fogScaleHeight{150.f};
  // Ozone absorbs in a tent-shaped layer centred on 25 km. It is what keeps the
  // zenith blue at twilight.
  Rgb ozoneAbsorption{.650e-6f, 1.881e-6f, .085e-6f};
  // Mean terrain reflectance used for light bounced back into the sky.
  Rgb groundAlbedo{.11f, .12f, .085f};
  // Solar irradiance above the atmosphere, about 128 000 lux.
  Rgb solarIrradiance{13.2f, 12.7f, 12.2f};

  // Builds the aerosol load from meteorological quantities. `visibilityKm` is
  // the Koschmieder visual range at sea level (a 2 % contrast threshold), and
  // `fogDensity` is 0..1 with 1 a dense ground fog of roughly 300 m visibility.
  static AtmosphereParameters fromWeather(float visibilityKm, float fogDensity, float fogHeightM);
};

// Lighting derived from the atmosphere for one camera altitude and sun position.
struct AtmosphereLighting {
  // Direct sun irradiance on a surface facing the sun at the given altitude.
  Rgb sunIrradiance{};
  // Sky plus ground-bounce irradiance as L1 spherical harmonics, already
  // convolved with the cosine lobe: E(n) = constant + x*n.x + y*n.y + z*n.z in
  // the supplied axis convention (the up axis is whichever one `sunDirection` used).
  Rgb ambientConstant{}, ambientX{}, ambientY{}, ambientZ{};
  // Irradiance on an upward-facing surface from the sky alone.
  Rgb skyIrradianceUp{};
  // Mean radiance of the upper sky, for metering.
  Rgb meanSkyRadiance{};
  // Illuminance an incident-light meter would read (scene units).
  float meteredIrradiance{};
};

class AtmosphereModel {
 public:
  static constexpr int kTransmittanceWidth = 256, kTransmittanceHeight = 64;
  static constexpr int kMultiScatterSize = 32;

  explicit AtmosphereModel(const AtmosphereParameters& parameters = {});
  // Rebuilds both tables. Cheap enough (a few milliseconds) to call when the
  // weather sliders move, but not every frame.
  void rebuild(const AtmosphereParameters& parameters);

  const AtmosphereParameters& parameters() const { return parameters_; }
  // RGBA float texels, row-major. Alpha is unused and set to one.
  const std::vector<float>& transmittanceTable() const { return transmittance_; }
  const std::vector<float>& multiScatterTable() const { return multiScatter_; }

  // Transmittance from `altitude` to the top of the atmosphere along a ray whose
  // cosine with the local zenith is `cosZenith`. Zero when the planet blocks it.
  Rgb transmittanceToSpace(float altitude, float cosZenith) const;
  // Sun irradiance at an altitude, including the gradual disc occlusion at the
  // horizon.
  Rgb sunIrradiance(float altitude, float sunCosZenith) const;
  // Isotropic multiple-scattering radiance per unit solar irradiance.
  Rgb multiScatter(float altitude, float sunCosZenith) const;
  // Radiance arriving at `altitude` from direction `view`, for a sun toward
  // `sun`. Both are unit vectors with +Y up. The sun disc itself is excluded;
  // rays that reach the ground return the lit, haze-attenuated ground.
  Rgb skyRadiance(float altitude, const std::array<float, 3>& view,
                  const std::array<float, 3>& sun, int steps = 24) const;
  // Integrates the sky into the quantities the surface shaders need.
  AtmosphereLighting lighting(float altitude, const std::array<float, 3>& sun) const;

  // Densities relative to sea level at an altitude: Rayleigh, aerosol (scatter),
  // aerosol (extinction) and ozone. Exposed for the tests.
  struct Medium { Rgb scattering, extinction; float rayleigh, mie; };
  Medium medium(float altitude) const;

 private:
  Rgb opticalDepthToSpace(float altitude, float cosZenith) const;
  AtmosphereParameters parameters_;
  std::vector<float> transmittance_, multiScatter_;
};

// Camera exposure multiplier for a metered irradiance, with a compensation in
// photographic stops. Bright scenes meter down and twilight meters up, within
// limits that keep night dark instead of grey.
float exposureFromIrradiance(float meteredIrradiance, float compensationStops);

}  // namespace ofs::client
