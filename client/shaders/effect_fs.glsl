// Particles: contrails, vapour, smoke, dust, tracers, flashes and fire.
//
// The UV band selects the style. Smoke-like styles are lit by the sun and sky
// like any other participating medium; emissive styles carry their own
// radiance. Smoke, dust and fire are shaded as lumpy balls and not as flat
// discs: each has a sunlit side and a shaded one, and fire cools from white
// through orange to soot over its life (v_params.x), so a handful of them
// reads as one rolling body. v_params.y is the particle's own random number. Every particle fades where it meets solid geometry and is hidden
// behind cloud, so trails sit in the scene instead of on top of it.

$input v_color, v_uv, v_worldPos, v_params
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
    } else if (style > 3.5 && style < 5.5) {
        // A ball of smoke, dust or fire. Its lumps belong to the particle and
        // turn over slowly as it ages.
        float age = v_params.x;
        vec2 own = vec2(v_params.y * 7.31, v_params.y * 3.17);
        vec2 at = p * (0.15 - 0.03 * age) + own;
        vec4 coarse = texture2DLod(s_noise, at, 0.0);
        vec4 fineNoise = texture2DLod(s_noise, at * 2.3 + vec2(age * 0.08, 0.37), 0.0);
        float lump = coarse.b * 0.76 + fineNoise.b * 0.24;
        float radius = sqrt(r2);
        // The surface of the ball as the eye sees it, pushed about by the lumps.
        vec3 toward = -ray;
        vec3 side = normalize(cross(vec3(0.0, 1.0, 0.0), toward) + vec3(1.0e-5, 0.0, 0.0));
        vec3 upward = cross(toward, side);
        vec2 slope = (vec2(coarse.r, coarse.g) - 0.5) * 1.5 + (vec2(fineNoise.r, fineNoise.g) - 0.5) * 0.7;
        vec3 normal = normalize(side * (p.x + slope.x) + upward * (p.y + slope.y)
                                + toward * sqrt(max(1.0 - r2, 0.0) + 0.08));
        float outline = 1.0 - smoothstep(0.34 + lump * 0.50, 0.98, radius);
        // Smoke has no skin: it thins toward its edge, so puffs run together.
        float haze = (1.0 - smoothstep(0.10 + lump * 0.42, 1.0, radius)) * exp(-1.1 * r2);
        if (style < 4.5) {
            // Smoke and dust: the sun on one side, the sky on the rest, and
            // the hollows between lumps darker than their crowns.
            float facing = dot(normal, u_sunDirection.xyz);
            float sunlit = ofsSaturate(facing * 0.62 + 0.38);
            vec3 light = (atmoSunIrradiance(world.y, u_sunDirection.y) * ofsCloudShadow(world)
                          * (sunlit * 1.5 + 0.5 * pow(ofsSaturate(cosTheta), 6.0))
                          + ofsAmbientIrradiance(normal) * 1.15) / (4.0 * OFS_PI) * 2.6;
            radiance = v_color.rgb * light * mix(0.66, 1.08, smoothstep(0.25, 0.75, lump));
            falloff = haze * (0.70 + 0.45 * lump);
        } else {
            // Fire: it cools from the outside in and from the lumps inward,
            // so soot closes over a heart that is still alight.
            float heat = ofsSaturate((1.0 - age) * 1.35 + 0.15 - (1.0 - lump) * (0.35 + 0.95 * age) - radius * radius * 0.30 * (0.4 + age));
            vec3 flame = vec3(pow(heat, 1.3) * 9.0, pow(heat, 2.7) * 5.2, pow(heat, 5.5) * 3.4)
                       * mix(vec3_splat(1.0), v_color.rgb, 0.45) * u_sunDirection.w;
            vec3 soot = vec3(0.045, 0.040, 0.036) * lit * (0.5 + 0.8 * ofsSaturate(dot(normal, u_sunDirection.xyz)));
            radiance = soot + flame;
            falloff = outline * mix(0.80, 1.0, heat);
        }
    } else if (style > 7.5) {
        // A blast wave on the ground: a pale front, and behind it a band of
        // dust that the noise breaks into streaks.
        float radius = sqrt(r2);
        float front = exp(-(radius - 0.90) * (radius - 0.90) * 420.0);
        float streaks = texture2DLod(s_noise, world.xz * 0.011 + vec2(v_params.y, 0.0), 0.0).r;
        float dust = smoothstep(0.30, 0.84, radius) * (1.0 - smoothstep(0.86, 0.93, radius)) * (0.25 + 0.75 * streaks);
        falloff = (front * 0.75 + dust * 0.55) * (1.0 - smoothstep(0.95, 1.0, radius));
        radiance = v_color.rgb * lit * 1.3;
    } else if (style > 5.5 && style < 6.5) {
        // Shock ring: a thin bright band at the rim of the quad.
        float rim = sqrt(r2) - 0.86;
        falloff = exp(-rim * rim * 180.0) * (1.0 - smoothstep(0.92, 1.0, r2));
        radiance = v_color.rgb * 3.0 * u_sunDirection.w;
    } else if (style > 6.5 && style < 7.5) {
        // Flash: a hot core, a soft halo and a few uneven rays.
        float angle = atan2(p.y, p.x);
        float rays = pow(abs(cos(angle * 2.5)), 14.0) * 0.6 + pow(abs(cos(angle * 3.5 + 0.9)), 22.0) * 0.4;
        float radius = sqrt(r2);
        falloff = (exp(-22.0 * r2) + exp(-5.0 * r2) * 0.10 + rays * exp(-5.5 * radius) * 0.55)
                * (1.0 - smoothstep(0.5, 1.0, r2));
        radiance = mix(v_color.rgb, vec3_splat(1.0), exp(-30.0 * r2)) * 8.0 * u_sunDirection.w;
    }

    // Soft intersection with opaque geometry, from the resolved scene range.
    float sceneRange = texture2DLod(s_sceneRange, screenUv, 0.0).r * u_cameraForward.w;
    float softness = max(1.2, viewDistance * 0.004);
    float fade = ofsSaturate((sceneRange - viewDistance) / softness + 0.35);
    // Smoke and flame thin out right at the eye, so a camera following an
    // aircraft through its own trail is not blinded by one enormous sprite.
    if (style < 1.5 || (style > 3.5 && style < 6.5) || style > 7.5) {
        fade *= smoothstep(2.0, 14.0, viewDistance);
    }
    // Hidden by whatever cloud lies in front of it.
    if (u_effectParams.x > 0.5) {
        vec4 cloud = texture2DLod(s_cloudLayer, screenUv, 0.0);
        float cloudRange = texture2DLod(s_cloudDepth, screenUv, 0.0).r * u_cameraForward.w;
        fade *= 1.0 - cloud.a * smoothstep(0.7, 1.15, viewDistance / max(cloudRange, 1.0));
    }

    vec3 color = ofsApplyAerial(radiance, screenUv, ray, viewDistance);
    gl_FragColor = vec4(color, v_color.a * falloff * fade);
}
