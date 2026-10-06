// Sky dome fragment shader.

$input v_uv
#include <bgfx_shader.sh>
#include "common.glsl"

// Inverse view-projection is not needed: the ray is rebuilt from the camera
// basis vectors, which keeps this independent of the projection convention.
uniform mat4 u_ofsInvViewProj;
uniform vec4 u_worldOrigin;
uniform vec4 u_cameraPos;   // xyz used
uniform vec4 u_sunDirection;   // xyz: world, pointing from the sun toward the scene
uniform vec4 u_zenithColor;    // rgb used
uniform vec4 u_horizonColor;  // rgb used
uniform vec4 u_groundColor;   // rgb used
uniform vec4 u_sunColor;      // rgb used
uniform vec4 u_fogColor;      // rgb used
uniform vec4 u_sunIntensity;    // x: disc brightness multiplier
uniform vec4 u_horizonSharpness; // x
uniform vec4 u_exposure;     // x
uniform vec4 u_groundBlend;    // x: how far below the horizon the ground haze sits

// Reconstruct a world-space view ray through this pixel from an inverse
// view-projection matrix, which avoids threading the camera basis separately.
vec3 rayDirection(vec2 ndc)
{
    const vec4 far = mul(u_ofsInvViewProj, vec4(ndc, 1.0, 1.0));
    const vec4 near = mul(u_ofsInvViewProj, vec4(ndc, 0.0, 1.0));
    return normalize(far.xyz / far.w - near.xyz / near.w);
}

void main()
{
    const vec3 ray = rayDirection(v_uv);

    // Vertical gradient. The exponent keeps the horizon band tight while the
    // zenith stays saturated.
    const float up = ray.y;
    const float t = pow(clamp(up, 0.0, 1.0), u_horizonSharpness.x);
    const vec3 worldEye = u_cameraPos.xyz + u_worldOrigin.xyz;
    // Approximate optical air column, with a longer path toward the horizon.
    // This is an analytical scattering approximation, not radiative transfer.
    const float airColumn = exp(-max(worldEye.y, 0.0) / 8000.0);
    const float path = inversesqrt(max(up*up + .0025, .0025));
    const float scattering = 1.0-exp(-airColumn*path);
    const float seaLevelScattering = 1.0-exp(-path);
    vec3 color = mix(u_horizonColor.rgb, u_zenithColor.rgb, t)
               * scattering/max(seaLevelScattering,.001);


    // Sun disc plus a broad halo. The disc is deliberately soft-edged: a hard
    // edge aliases badly at low sun angles without MSAA. u_sunColor already
    // carries the sun's radiance, so u_sunIntensity only scales the disc.
    const float sunCos = dot(ray, -u_sunDirection.xyz);
    // Solar angular radius ~0.266 degrees; derivative AA preserves size.
    const float sunEdge = max(fwidth(sunCos), .0000005);
    const float disc = smoothstep(.9999892-sunEdge, .9999892+sunEdge, sunCos);
    const float halo = pow(clamp(sunCos, 0.0, 1.0), 220.0) * 0.55
                     + pow(clamp(sunCos, 0.0, 1.0), 12.0) * 0.16;
    // Forward Mie scatter gives a warm sunward haze; Rayleigh keeps the
    // opposite sky blue. Low suns produce a broad amber horizon band.
    const float lowSun=1.0-smoothstep(.05,.55,-u_sunDirection.y);
    const float horizonBand=exp(-abs(up)*7.0)*airColumn;
    color=mix(color,color*vec3(1.05,.88,.72),lowSun*horizonBand*.45);
    color+=u_sunColor.rgb*(disc*u_sunIntensity.x+halo*scattering);
    color+=u_sunColor.rgb*pow(max(sunCos,0.0),6.0)*horizonBand*.055;

    // Below the horizon the sky becomes ground haze, which must match the
    // world's fog colour or the horizon line shows as a hard seam.
    const float below = (1.0 - smoothstep(-max(u_groundBlend.x, .001), 0.0, up));
    color = mix(color, mix(u_fogColor.rgb, u_groundColor.rgb, 0.35), below);

    // Linear scene radiance; exposure, tone mapping and sRGB encoding happen
    // once in the shared HDR composite after sky, surfaces and effects.
    gl_FragData[0] = vec4(color, 1.0);
    gl_FragData[1] = vec4(u_weather.z,0.0,0.0,1.0);
}
