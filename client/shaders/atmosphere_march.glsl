// Ray marching through the atmosphere, for the passes that build the per-frame
// sky-view table and the aerial-perspective atlas.

#ifndef OFS_ATMOSPHERE_MARCH_GLSL
#define OFS_ATMOSPHERE_MARCH_GLSL

#include "atmosphere.glsl"

SAMPLER2D(s_multiScatter, 11);
uniform mat4 u_ofsInvViewProj;

vec3 atmoMultiScatter(float altitude, float sunCos)
{
    vec2 uv = vec2(sunCos * 0.5 + 0.5, clamp(altitude / u_atmoGeometry.y, 0.0, 1.0));
    return texture2DLod(s_multiScatter, uv, 0.0).rgb;
}

// Light scattered toward the viewer per metre at one point, and the local
// extinction. `phases` holds the Rayleigh and Mie phase values for this ray.
vec3 atmoSource(float altitude, float sunCos, vec2 phases, out vec3 extinction)
{
    float rayleigh = atmoRayleighDensity(altitude);
    vec2 aerosol = atmoAerosol(altitude);
    vec3 scattering = u_rayleigh.rgb * rayleigh + vec3_splat(aerosol.x);
    extinction = u_rayleigh.rgb * rayleigh + u_ozone.rgb * atmoOzoneDensity(altitude) + vec3_splat(aerosol.y);
    vec3 single = (u_rayleigh.rgb * (rayleigh * phases.x) + vec3_splat(aerosol.x * phases.y))
                * atmoSunIrradiance(altitude, sunCos);
    return single + scattering * atmoMultiScatter(altitude, sunCos) * u_solar.rgb;
}

// World-space view ray through a screen position.
vec3 atmoViewRay(vec2 uv)
{
    vec2 ndc = ofsUvToNdc(uv);
    vec4 farPoint = mul(u_ofsInvViewProj, vec4(ndc, 1.0, 1.0));
    vec4 nearPoint = mul(u_ofsInvViewProj, vec4(ndc, 0.0, 1.0));
    return normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
}

#endif // OFS_ATMOSPHERE_MARCH_GLSL
