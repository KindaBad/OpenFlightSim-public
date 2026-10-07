// Particles: contrails, vapour, smoke, dust, tracers, flashes and fire.
//
// The UV band selects the style. Smoke-like styles are lit by the sun and sky
// like any other participating medium; emissive styles carry their own
// radiance. Every particle fades where it meets solid geometry and is hidden
// behind cloud, so trails sit in the scene instead of on top of it.

$input v_color, v_uv, v_worldPos
#include <bgfx_shader.sh>
#include "common.glsl"

SAMPLER2D(s_sceneRange, 13);
SAMPLER2D(s_cloudLayer, 14);
SAMPLER2D(s_cloudDepth, 15);
uniform vec4 u_effectParams; // x 1 when cloud occlusion is available

void main()
{
    float style = floor(v_uv.x * 0.5);
    vec2 uv = v_uv - vec2(style * 2.0, 0.0);
    vec2 p = uv * 2.0 - 1.0;
    float r2 = dot(p, p);
    vec3 world = v_worldPos + u_worldOrigin.xyz;
    vec3 toEye = u_cameraPos.xyz - v_worldPos;
    float viewDistance = length(toEye);
    vec3 ray = -toEye / max(viewDistance, 1.0e-4);
    vec2 screenUv = ofsScreenUv(gl_FragCoord);

    // Turbulent structure anchored in the air mass the particle sits in.
    float grain = texture2DLod(s_noise, world.xz * 0.043 + world.y * 0.017, 0.0).r * 0.6
                + texture2DLod(s_noise, world.xz * 0.19 + world.y * 0.071, 0.0).g * 0.4;

    // Sun and sky on a puff of scattering droplets or soot.
    float cosTheta = dot(ray, u_sunDirection.xyz);
    float forward = 0.55 + 1.6 * pow(ofsSaturate(cosTheta), 6.0) + 0.25 * ofsSaturate(-cosTheta);
    vec3 lit = (atmoSunIrradiance(world.y, u_sunDirection.y) * ofsCloudShadow(world) * forward
                + ofsAmbientIrradiance(vec3(0.0, 1.0, 0.0)) * 1.2) / (4.0 * OFS_PI) * 2.6;

    float falloff = exp(-4.5 * r2) * (1.0 - smoothstep(0.55, 1.0, r2));
    vec3 radiance = v_color.rgb * lit;
    if (style > 0.5 && style < 1.5) {
        // Thin feathered veil, with gentle density variation instead of puffs.
        falloff = exp(-5.5 * p.y * p.y - 1.8 * p.x * p.x)
                * (1.0 - smoothstep(0.35, 1.0, abs(p.y)))
                * (1.0 - smoothstep(0.65, 1.0, abs(p.x))) * (0.6 + grain * 0.5);
    } else if (style > 1.5 && style < 2.5) {
        // Narrow bright tube, tapered tail and a white-hot head.
        falloff = exp(-5.0 * p.y * p.y) * (1.0 - smoothstep(0.5, 1.0, abs(p.y)))
                * smoothstep(0.0, 0.25, uv.x) * (1.0 - smoothstep(0.93, 1.0, uv.x));
        radiance = v_color.rgb * 7.0 * u_sunDirection.w;
    } else if (style > 2.5 && style < 3.5) {
        radiance = v_color.rgb * 9.0 * u_sunDirection.w;
        falloff = exp(-6.0 * r2) * (1.0 - smoothstep(0.5, 1.0, r2));
    } else if (style > 3.5 && style < 4.5) {
        // Lumpy smoke and dust: denser cores stay darker than their sunlit rims.
        falloff = exp(-2.0 * r2) * (1.0 - smoothstep(0.25 + grain * 0.4, 1.0, r2)) * (0.55 + grain * 0.65);
        radiance *= mix(0.55, 1.25, grain);
    } else if (style > 4.5) {
        falloff = exp(-2.8 * r2) * (1.0 - smoothstep(0.22 + grain * 0.5, 1.0, r2));
        radiance = mix(vec3(1.5, 0.13, 0.015), vec3(8.0, 3.2, 0.6), ofsSaturate(grain * 1.4 - r2 * 0.5))
                 * v_color.rgb * u_sunDirection.w;
    }

    // Soft intersection with opaque geometry, from the resolved scene range.
    float sceneRange = texture2DLod(s_sceneRange, screenUv, 0.0).r * u_cameraForward.w;
    float softness = max(1.2, viewDistance * 0.004);
    float fade = ofsSaturate((sceneRange - viewDistance) / softness + 0.35);
    // Hidden by whatever cloud lies in front of it.
    if (u_effectParams.x > 0.5) {
        vec4 cloud = texture2DLod(s_cloudLayer, screenUv, 0.0);
        float cloudRange = texture2DLod(s_cloudDepth, screenUv, 0.0).r * u_cameraForward.w;
        fade *= 1.0 - cloud.a * smoothstep(0.7, 1.15, viewDistance / max(cloudRange, 1.0));
    }

    vec3 color = ofsApplyAerial(radiance, screenUv, ray, viewDistance);
    gl_FragColor = vec4(color, v_color.a * falloff * fade);
}
