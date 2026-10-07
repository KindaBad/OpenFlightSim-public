#include "atmosphere_model.hpp"

#include "procedural.hpp"

#include <algorithm>
#include <cmath>

namespace ofs::client {
namespace {

constexpr double kPiD = 3.14159265358979323846;
// Angular radius of the solar disc, radians.
constexpr double kSunAngularRadius = 0.004675;

struct Vec3d { double x, y, z; };

double smoothstep(double a, double b, double x) {
  const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
  return t * t * (3 - 2 * t);
}

Rgb expRgb(const Rgb& v) { return {std::exp(-v.r), std::exp(-v.g), std::exp(-v.b)}; }

double rayleighPhase(double cosTheta) { return 3.0 / (16.0 * kPiD) * (1.0 + cosTheta * cosTheta); }

// Cornette-Shanks: Henyey-Greenstein with the correct backward lobe for haze.
double miePhase(double cosTheta, double g) {
  const double g2 = g * g;
  return 3.0 / (8.0 * kPiD) * (1.0 - g2) * (1.0 + cosTheta * cosTheta) /
         ((2.0 + g2) * std::pow(std::max(1.0 + g2 - 2.0 * g * cosTheta, 1e-6), 1.5));
}

// Geometry for a point at `altitude` looking along a ray with zenith cosine mu.
struct Shell {
  double ground, top, r, rho2;  // rho2 = r^2 - ground^2, computed without cancellation
  Shell(const AtmosphereParameters& p, double altitude)
      : ground(p.planetRadius), top(p.planetRadius + p.atmosphereHeight) {
    const double h = std::clamp(altitude, 0.0, double(p.atmosphereHeight));
    r = ground + h;
    rho2 = h * (2 * ground + h);
  }
  double horizonCos() const { return -std::sqrt(rho2) / r; }
  bool hitsGround(double mu) const { return mu < 0 && r * r * mu * mu - rho2 >= 0; }
  double toTop(double mu) const {
    return -r * mu + std::sqrt(std::max(0.0, r * r * mu * mu + (top - r) * (top + r)));
  }
  double toGround(double mu) const { return -r * mu - std::sqrt(std::max(0.0, r * r * mu * mu - rho2)); }
  // Altitude after travelling t along the ray.
  double altitudeAt(double mu, double t) const {
    const double numerator = t * (t + 2 * r * mu) + rho2;
    return numerator / (std::sqrt(std::max(0.0, r * r + t * (t + 2 * r * mu))) + ground);
  }
};

}  // namespace

AtmosphereParameters AtmosphereParameters::fromWeather(float visibilityKm, float fogDensity,
                                                       float fogHeightM) {
  AtmosphereParameters p;
  const float visibility = std::clamp(visibilityKm, 2.f, 300.f) * 1000.f;
  // Koschmieder: total extinction at 550 nm for a 2 % contrast threshold.
  const float total = 3.912f / visibility;
  p.mieExtinction = std::max(total - p.rayleighScattering.g, 1.0e-6f);
  p.mieScattering = p.mieExtinction * .90f;
  const float fog = std::clamp(fogDensity, 0.f, 1.f);
  p.fogExtinction = fog * fog * .013f;
  p.fogScaleHeight = std::clamp(fogHeightM, 20.f, 2000.f);
  return p;
}

AtmosphereModel::AtmosphereModel(const AtmosphereParameters& parameters) { rebuild(parameters); }

AtmosphereModel::Medium AtmosphereModel::medium(float altitude) const {
  const auto& p = parameters_;
  const float h = std::max(altitude, 0.f);
  const float rayleigh = std::exp(-h / p.rayleighScaleHeight);
  const float mie = std::exp(-h / p.mieScaleHeight);
  const float fog = p.fogExtinction > 0 ? std::exp(-h / p.fogScaleHeight) : 0.f;
  const float ozone = std::max(0.f, 1.f - std::abs(h - 25000.f) / 15000.f);
  Medium m;
  m.rayleigh = rayleigh;
  m.mie = p.mieScattering * mie + p.fogExtinction * fog;
  const float aerosolExtinction = p.mieExtinction * mie + p.fogExtinction * fog;
  m.scattering = p.rayleighScattering * rayleigh + Rgb{m.mie, m.mie, m.mie};
  m.extinction = p.rayleighScattering * rayleigh + p.ozoneAbsorption * ozone +
                 Rgb{aerosolExtinction, aerosolExtinction, aerosolExtinction};
  return m;
}

Rgb AtmosphereModel::opticalDepthToSpace(float altitude, float cosZenith) const {
  const Shell shell(parameters_, altitude);
  const double distance = shell.toTop(cosZenith);
  // The path to space is hundreds of kilometres long, but ground fog is tens of
  // metres deep and haze about a kilometre. Quartic spacing puts the first
  // samples centimetres apart and still reaches the top in 64 steps, so every
  // layer is resolved from wherever the ray starts.
  constexpr int kSteps = 64;
  Rgb depth{};
  for (int i = 0; i < kSteps; ++i) {
    const double a = double(i) / kSteps, b = double(i + 1) / kSteps, m = (a + b) * .5;
    const double t = distance * m * m * m * m;
    const double dt = distance * (b * b * b * b - a * a * a * a);
    depth += medium(float(std::max(0.0, shell.altitudeAt(cosZenith, t)))).extinction * float(dt);
  }
  return depth;
}

void AtmosphereModel::rebuild(const AtmosphereParameters& parameters) {
  parameters_ = parameters;
  const auto& p = parameters_;
  const double ground = p.planetRadius, height = p.atmosphereHeight;
  const double bigH = std::sqrt(height * (2 * ground + height));

  transmittance_.assign(std::size_t(kTransmittanceWidth) * kTransmittanceHeight * 4, 1.f);
  procedural::parallelRows(kTransmittanceHeight, [&](int j) {
    const double rho = bigH * j / (kTransmittanceHeight - 1);
    const double altitude = rho * rho / (std::sqrt(rho * rho + ground * ground) + ground);
    const double r = ground + altitude;
    const double dMin = height - altitude, dMax = rho + bigH;
    for (int i = 0; i < kTransmittanceWidth; ++i) {
      const double d = dMin + (dMax - dMin) * i / (kTransmittanceWidth - 1);
      const double mu = d <= 0 ? 1.0 : std::clamp((bigH * bigH - rho * rho - d * d) / (2 * r * d), -1.0, 1.0);
      const Rgb t = expRgb(opticalDepthToSpace(float(altitude), float(mu)));
      float* texel = &transmittance_[(std::size_t(j) * kTransmittanceWidth + i) * 4];
      texel[0] = t.r; texel[1] = t.g; texel[2] = t.b;
    }
  });

  // Multiple scattering: the second-order radiance from an isotropic source,
  // extended to all orders as a geometric series (Hillaire 2020, section 5.5).
  multiScatter_.assign(std::size_t(kMultiScatterSize) * kMultiScatterSize * 4, 1.f);
  constexpr int kSqrtDirections = 8, kSteps = 20;
  procedural::parallelRows(kMultiScatterSize, [&](int j) {
    const float altitude = float(height * (j + .5) / kMultiScatterSize);
    const Shell shell(p, altitude);
    for (int i = 0; i < kMultiScatterSize; ++i) {
      const double sunCos = (i + .5) / kMultiScatterSize * 2 - 1;
      const Vec3d sun{std::sqrt(std::max(0.0, 1 - sunCos * sunCos)), sunCos, 0};
      Rgb second{}, transfer{};
      for (int a = 0; a < kSqrtDirections; ++a) for (int b = 0; b < kSqrtDirections; ++b) {
        const double cosTheta = 1 - 2 * (a + .5) / kSqrtDirections;
        const double sinTheta = std::sqrt(std::max(0.0, 1 - cosTheta * cosTheta));
        const double phi = 2 * kPiD * (b + .5) / kSqrtDirections;
        const Vec3d dir{sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi)};
        const double mu = dir.y, nu = dir.x * sun.x + dir.y * sun.y;
        const bool ground_ = shell.hitsGround(mu);
        const double end = ground_ ? shell.toGround(mu) : shell.toTop(mu);
        Rgb through{1, 1, 1};
        for (int s = 0; s < kSteps; ++s) {
          const double a = double(s) / kSteps, b = double(s + 1) / kSteps;
          const double t = end * (a * a + b * b) * .5, dt = end * (b * b - a * a);
          const double h = std::max(0.0, shell.altitudeAt(mu, t));
          const double rt = ground + h;
          const double sunCosHere = std::clamp((shell.r * sunCos + t * nu) / rt, -1.0, 1.0);
          const Medium m = medium(float(h));
          const Rgb stepT = expRgb(m.extinction * float(dt));
          const Rgb sunT = transmittanceToSpace(float(h), float(sunCosHere));
          // Analytic integral of a constant source over the step.
          const Rgb weight{(1 - stepT.r) / std::max(m.extinction.r, 1e-12f),
                           (1 - stepT.g) / std::max(m.extinction.g, 1e-12f),
                           (1 - stepT.b) / std::max(m.extinction.b, 1e-12f)};
          second += through * m.scattering * sunT * weight * float(1.0 / (4 * kPiD));
          transfer += through * m.scattering * weight;
          through = through * stepT;
        }
        if (ground_) {
          const double sunCosGround = std::clamp((shell.r * sunCos + end * nu) / ground, -1.0, 1.0);
          if (sunCosGround > 0)
            second += through * transmittanceToSpace(0, float(sunCosGround)) * p.groundAlbedo *
                      float(sunCosGround / kPiD);
        }
      }
      const float n = float(kSqrtDirections * kSqrtDirections);
      second = second / n;
      transfer = transfer / n;
      float* texel = &multiScatter_[(std::size_t(j) * kMultiScatterSize + i) * 4];
      texel[0] = second.r / std::max(1e-3f, 1 - transfer.r);
      texel[1] = second.g / std::max(1e-3f, 1 - transfer.g);
      texel[2] = second.b / std::max(1e-3f, 1 - transfer.b);
    }
  });
}

Rgb AtmosphereModel::transmittanceToSpace(float altitude, float cosZenith) const {
  const auto& p = parameters_;
  const Shell shell(p, altitude);
  if (shell.hitsGround(cosZenith)) return {};
  const double bigH = std::sqrt(double(p.atmosphereHeight) * (2.0 * p.planetRadius + p.atmosphereHeight));
  const double rho = std::sqrt(shell.rho2);
  const double d = shell.toTop(cosZenith);
  const double dMin = shell.top - shell.r, dMax = rho + bigH;
  const double x = std::clamp((d - dMin) / std::max(dMax - dMin, 1e-6), 0.0, 1.0) * (kTransmittanceWidth - 1);
  const double y = std::clamp(rho / bigH, 0.0, 1.0) * (kTransmittanceHeight - 1);
  const int x0 = std::min(int(x), kTransmittanceWidth - 2), y0 = std::min(int(y), kTransmittanceHeight - 2);
  const float fx = float(x - x0), fy = float(y - y0);
  const auto at = [&](int ix, int iy) {
    const float* texel = &transmittance_[(std::size_t(iy) * kTransmittanceWidth + ix) * 4];
    return Rgb{texel[0], texel[1], texel[2]};
  };
  return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) +
         (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
}

Rgb AtmosphereModel::sunIrradiance(float altitude, float sunCosZenith) const {
  const Shell shell(parameters_, altitude);
  const double horizon = shell.horizonCos();
  // The disc sets over about half a degree; near the horizon a change in the
  // zenith cosine is very nearly a change in angle.
  const double visible = smoothstep(horizon - kSunAngularRadius, horizon + kSunAngularRadius, sunCosZenith);
  if (visible <= 0) return {};
  const float cosine = float(std::max(double(sunCosZenith), horizon + 1e-4));
  return parameters_.solarIrradiance * transmittanceToSpace(altitude, cosine) * float(visible);
}

Rgb AtmosphereModel::multiScatter(float altitude, float sunCosZenith) const {
  const double x = std::clamp(double(sunCosZenith) * .5 + .5, 0.0, 1.0) * kMultiScatterSize - .5;
  const double y = std::clamp(double(altitude) / parameters_.atmosphereHeight, 0.0, 1.0) * kMultiScatterSize - .5;
  const int x0 = std::clamp(int(std::floor(x)), 0, kMultiScatterSize - 2);
  const int y0 = std::clamp(int(std::floor(y)), 0, kMultiScatterSize - 2);
  const float fx = float(std::clamp(x - x0, 0.0, 1.0)), fy = float(std::clamp(y - y0, 0.0, 1.0));
  const auto at = [&](int ix, int iy) {
    const float* texel = &multiScatter_[(std::size_t(iy) * kMultiScatterSize + ix) * 4];
    return Rgb{texel[0], texel[1], texel[2]};
  };
  return (at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx) * (1 - fy) +
         (at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx) * fy;
}

Rgb AtmosphereModel::skyRadiance(float altitude, const std::array<float, 3>& view,
                                 const std::array<float, 3>& sun, int steps) const {
  const auto& p = parameters_;
  const Shell shell(p, altitude);
  const double mu = view[1], sunCos = sun[1];
  const double nu = double(view[0]) * sun[0] + double(view[1]) * sun[1] + double(view[2]) * sun[2];
  const bool ground = shell.hitsGround(mu);
  const double end = ground ? shell.toGround(mu) : shell.toTop(mu);
  const float phaseR = float(rayleighPhase(nu)), phaseM = float(miePhase(nu, p.mieAnisotropy));
  Rgb radiance{}, through{1, 1, 1};
  for (int s = 0; s < steps; ++s) {
    // Quadratic spacing resolves the dense air next to the camera on the long
    // near-horizontal paths that set the horizon colour.
    const double a = double(s) / steps, b = double(s + 1) / steps;
    const double t = end * (a * a + b * b) * .5, dt = end * (b * b - a * a);
    const double h = std::max(0.0, shell.altitudeAt(mu, t));
    const double sunCosHere = std::clamp((shell.r * sunCos + t * nu) / (shell.ground + h), -1.0, 1.0);
    const Medium m = medium(float(h));
    const Rgb stepT = expRgb(m.extinction * float(dt));
    const Rgb sunLight = sunIrradiance(float(h), float(sunCosHere));
    const Rgb single = (p.rayleighScattering * (m.rayleigh * phaseR) + Rgb{m.mie, m.mie, m.mie} * phaseM) * sunLight;
    const Rgb multiple = m.scattering * multiScatter(float(h), float(sunCosHere)) * p.solarIrradiance;
    const Rgb source = single + multiple;
    radiance += through * Rgb{source.r * (1 - stepT.r) / std::max(m.extinction.r, 1e-12f),
                              source.g * (1 - stepT.g) / std::max(m.extinction.g, 1e-12f),
                              source.b * (1 - stepT.b) / std::max(m.extinction.b, 1e-12f)};
    through = through * stepT;
  }
  if (ground) {
    const double sunCosGround = std::clamp((shell.r * sunCos + end * nu) / shell.ground, -1.0, 1.0);
    const Rgb direct = sunIrradiance(0, float(sunCosGround)) * float(std::max(0.0, sunCosGround));
    const Rgb bounce = multiScatter(0, float(sunCosGround)) * p.solarIrradiance * float(kPiD);
    radiance += through * p.groundAlbedo * (direct + bounce) * float(1.0 / kPiD);
  }
  return radiance;
}

AtmosphereLighting AtmosphereModel::lighting(float altitude, const std::array<float, 3>& sun) const {
  AtmosphereLighting out;
  out.sunIrradiance = sunIrradiance(altitude, sun[1]);
  constexpr int kDirections = 96;
  const double golden = kPiD * (3 - std::sqrt(5.0));
  Rgb l00{}, lx{}, ly{}, lz{}, up{}, mean{};
  int upper = 0;
  for (int i = 0; i < kDirections; ++i) {
    const double y = 1 - 2 * (i + .5) / kDirections;
    const double radius = std::sqrt(std::max(0.0, 1 - y * y)), phi = golden * i;
    const std::array<float, 3> dir{float(radius * std::cos(phi)), float(y), float(radius * std::sin(phi))};
    const Rgb radiance = skyRadiance(altitude, dir, sun, 14);
    l00 += radiance;
    lx += radiance * dir[0];
    ly += radiance * dir[1];
    lz += radiance * dir[2];
    if (y > 0) { up += radiance * float(y); mean += radiance; ++upper; }
  }
  const float solidAngle = float(4 * kPiD / kDirections);
  // SH projection (Y00 = 0.282095, Y1 = 0.488603 * axis) followed by the cosine
  // convolution (pi and 2 pi / 3), folded into one factor per band.
  const float constant = solidAngle * .282095f * .282095f * float(kPiD);
  const float linear = solidAngle * .488603f * .488603f * float(2 * kPiD / 3);
  out.ambientConstant = l00 * constant;
  out.ambientX = lx * linear;
  out.ambientY = ly * linear;
  out.ambientZ = lz * linear;
  out.skyIrradianceUp = up * solidAngle;
  out.meanSkyRadiance = mean / float(std::max(upper, 1));
  // An incident-light meter held between the sun and the zenith: this keeps the
  // exposure steady as the sun climbs instead of tracking a horizontal card.
  out.meteredIrradiance = out.sunIrradiance.luminance() * (.35f + .65f * std::max(sun[1], 0.f)) +
                          out.skyIrradianceUp.luminance();
  return out;
}

float exposureFromIrradiance(float meteredIrradiance, float compensationStops) {
  // Reference: a clear mid-morning meters about 9.5 and exposes at 0.36, which
  // puts sunlit grass in the lower mid-tones and a white cloud just under clip.
  constexpr float kReference = 9.5f, kReferenceExposure = .36f;
  const float metered = std::max(meteredIrradiance, 1e-5f);
  // Partial adaptation: dusk is allowed to look dimmer than noon.
  const float automatic = kReferenceExposure * std::pow(kReference / metered, .85f);
  return std::clamp(automatic, .12f, 36.f) * std::exp2(std::clamp(compensationStops, -6.f, 6.f));
}

}  // namespace ofs::client
