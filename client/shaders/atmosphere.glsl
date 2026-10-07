// Physically based atmosphere: shared lookups for every pass.
//
// Mirrors client/src/atmosphere_model.cpp. The transmittance and multiple-
// scattering tables are built on the CPU; the sky-view and aerial-perspective
// tables are integrated on the GPU each frame by skyview_fs and aerial_fs.
// Altitudes are metres above sea level and cosines are relative to the zenith.

#ifndef OFS_ATMOSPHERE_GLSL
#define OFS_ATMOSPHERE_GLSL

#include "frame.glsl"

#define OFS_PI 3.14159265359
#define OFS_SUN_ANGULAR_RADIUS 0.004675
// Sky-view table and aerial-perspective atlas dimensions (see renderer.cpp).
#define OFS_SKY_WIDTH 192.0
#define OFS_SKY_HEIGHT 108.0
#define OFS_AERIAL_TILE 32.0
#define OFS_AERIAL_SLICES 32.0

SAMPLER2D(s_transmittance, 6);
SAMPLER2D(s_skyView, 7);
SAMPLER2D(s_aerial, 8);

float atmoRayleighDensity(float altitude) { return exp(-max(altitude, 0.0) / u_atmoGeometry.z); }

float atmoOzoneDensity(float altitude) { return max(0.0, 1.0 - abs(altitude - 25000.0) / 15000.0); }

// Aerosol scattering and extinction: the haze layer plus optional ground fog.
vec2 atmoAerosol(float altitude)
{
    float h = max(altitude, 0.0);
    float haze = exp(-h / u_atmoGeometry.w);
    float fog = u_mie.z * exp(-h / u_mie.w);
    return vec2(u_mie.x * haze + fog, u_mie.y * haze + fog);
}

float atmoPhaseRayleigh(float cosTheta) { return 3.0 / (16.0 * OFS_PI) * (1.0 + cosTheta * cosTheta); }

float atmoPhaseMie(float cosTheta, float g)
{
    float g2 = g * g;
    float denominator = max(1.0 + g2 - 2.0 * g * cosTheta, 1.0e-4);
    return 3.0 / (8.0 * OFS_PI) * (1.0 - g2) * (1.0 + cosTheta * cosTheta)
         / ((2.0 + g2) * denominator * sqrt(denominator));
}

// Cosine of the zenith angle of the geometric horizon at an altitude.
float atmoHorizonCos(float altitude)
{
    float h = clamp(altitude, 0.0, u_atmoGeometry.y);
    return -sqrt(h * (2.0 * u_atmoGeometry.x + h)) / (u_atmoGeometry.x + h);
}

// Transmittance to the top of the atmosphere. The products are arranged so the
// planet radius never cancels against itself in single precision.
vec3 atmoTransmittanceToSpace(float altitude, float mu)
{
    float ground = u_atmoGeometry.x;
    float h = clamp(altitude, 0.0, u_atmoGeometry.y);
    float r = ground + h;
    float top = ground + u_atmoGeometry.y;
    float rho = sqrt(h * (2.0 * ground + h));
    float bigH = sqrt(u_atmoGeometry.y * (2.0 * ground + u_atmoGeometry.y));
    float d = -r * mu + sqrt(max(r * r * mu * mu + (top - r) * (top + r), 0.0));
    float dMin = top - r;
    float dMax = rho + bigH;
    vec2 uv = vec2((d - dMin) / max(dMax - dMin, 1.0), rho / bigH);
    uv = (clamp(uv, 0.0, 1.0) * vec2(255.0, 63.0) + 0.5) / vec2(256.0, 64.0);
    return texture2DLod(s_transmittance, uv, 0.0).rgb;
}

// Direct sun irradiance on a surface facing the sun, with the disc setting
// gradually behind the horizon.
vec3 atmoSunIrradiance(float altitude, float sunCos)
{
    float horizon = atmoHorizonCos(altitude);
    float visible = smoothstep(horizon - OFS_SUN_ANGULAR_RADIUS, horizon + OFS_SUN_ANGULAR_RADIUS, sunCos);
    return u_solar.rgb * atmoTransmittanceToSpace(altitude, max(sunCos, horizon + 1.0e-4)) * visible;
}

// Sky plus ground-bounce irradiance arriving on a surface with normal n.
vec3 ofsAmbientIrradiance(vec3 n)
{
    vec3 e = u_ambient0.rgb + u_ambientX.rgb * n.x + u_ambientY.rgb * n.y + u_ambientZ.rgb * n.z;
    return max(e, u_ambient0.rgb * 0.06);
}

// --- Sky-view table -----------------------------------------------------------
// u: azimuth from the sun, 0..pi (the sky is symmetric about the sun's vertical).
// v: elevation, with a square-root mapping that concentrates texels at the horizon.
vec2 atmoSkyUv(vec3 dir)
{
    float elevation = asin(clamp(dir.y, -1.0, 1.0));
    vec2 flatDir = dir.xz;
    vec2 flatSun = u_sunDirection.xz;
    float lengths = length(flatDir) * length(flatSun);
    float cosAzimuth = lengths > 1.0e-6 ? dot(flatDir, flatSun) / lengths : 1.0;
    float u = acos(clamp(cosAzimuth, -1.0, 1.0)) / OFS_PI;
    float signedRoot = sqrt(abs(elevation) / (0.5 * OFS_PI));
    float v = 0.5 + 0.5 * (elevation < 0.0 ? -signedRoot : signedRoot);
    return (vec2(u, v) * vec2(OFS_SKY_WIDTH - 1.0, OFS_SKY_HEIGHT - 1.0) + 0.5) / vec2(OFS_SKY_WIDTH, OFS_SKY_HEIGHT);
}

vec3 atmoSkyRadiance(vec3 dir) { return texture2DLod(s_skyView, atmoSkyUv(dir), 0.0).rgb; }

// Sky radiance for a glossy reflection: sharp for mirrors, converging on the
// diffuse irradiance for rough surfaces, where the table would otherwise alias.
vec3 atmoSkyReflection(vec3 dir, float roughness)
{
    vec3 sharp = atmoSkyRadiance(dir);
    vec3 diffuse = ofsAmbientIrradiance(dir) / OFS_PI;
    return mix(sharp, diffuse, clamp(roughness * 1.35 - 0.1, 0.0, 1.0));
}

// --- Aerial perspective -------------------------------------------------------
// The atlas holds 32 depth slices of in-scattered light across the view frustum,
// slice k ending at range * ((k + 1) / 32)^2.
vec3 atmoAerialSlice(vec2 uv, float slice)
{
    vec2 local = (clamp(uv, 0.0, 1.0) * (OFS_AERIAL_TILE - 1.0) + 0.5) / OFS_AERIAL_TILE;
    vec2 tile = vec2(mod(slice, 8.0), floor(slice / 8.0));
    return texture2DLod(s_aerial, (tile + local) / vec2(8.0, 4.0), 0.0).rgb;
}

vec3 atmoAerialInscatter(vec2 uv, float pathLength)
{
    float s = sqrt(clamp(pathLength / u_groundAlbedo.w, 0.0, 1.0)) * OFS_AERIAL_SLICES - 1.0;
    float k0 = max(floor(s), 0.0);
    float k1 = min(k0 + 1.0, OFS_AERIAL_SLICES - 1.0);
    vec3 a = atmoAerialSlice(uv, k0);
    vec3 b = atmoAerialSlice(uv, k1);
    // Before the first slice the in-scatter grows from zero at the camera.
    return s < 0.0 ? a * (s + 1.0) : mix(a, b, s - k0);
}

// Closed-form transmittance along a straight path through the exponential
// layers. The simulated world is flat, so altitude varies linearly with range.
vec3 atmoAerialTransmittance(float eyeAltitude, float rayY, float pathLength)
{
    vec3 scale = vec3(u_atmoGeometry.z, u_atmoGeometry.w, u_mie.w);
    float eye = max(eyeAltitude, 0.0);
    vec3 startDensity = exp(-eye / scale);
    vec3 endDensity = exp(-max(eye + rayY * pathLength, 0.0) / scale);
    vec3 slope = rayY * pathLength / scale;
    // Integral of the exponential layer along the path; level rays use the limit.
    vec3 column = vec3(abs(slope.x) < 1.0e-3 ? startDensity.x : (startDensity.x - endDensity.x) / slope.x,
                       abs(slope.y) < 1.0e-3 ? startDensity.y : (startDensity.y - endDensity.y) / slope.y,
                       abs(slope.z) < 1.0e-3 ? startDensity.z : (startDensity.z - endDensity.z) / slope.z) * pathLength;
    vec3 depth = u_rayleigh.rgb * column.x + vec3_splat(u_mie.y * column.y + u_mie.z * column.z);
    return exp(-min(depth, vec3_splat(60.0)));
}

// Applies haze to a surface seen along `ray` at `pathLength` from the eye.
vec3 ofsApplyAerial(vec3 color, vec2 uv, vec3 ray, float pathLength)
{
    vec3 transmittance = atmoAerialTransmittance(u_ozone.w, ray.y, pathLength);
    return color * transmittance + atmoAerialInscatter(uv, pathLength);
}

#endif // OFS_ATMOSPHERE_GLSL
