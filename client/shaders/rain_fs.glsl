// Rain seen from a moving aircraft.
//
// Drops fall at a few metres per second while the aircraft moves at a hundred,
// so what the pilot sees is streaks radiating from the point the aircraft is
// flying toward. The pattern is built in the frame of the rain's velocity
// relative to the camera: streaks are lines of constant angle around that axis.

$input v_uv
#include <bgfx_shader.sh>
#include "common.glsl"

SAMPLER2D(s_sceneRange, 13);
uniform mat4 u_ofsInvViewProj;
uniform vec4 u_rain;     // xyz rain velocity relative to the camera (unit), w relative speed
uniform vec4 u_rainSide; // xyz a unit vector perpendicular to u_rain.xyz, w intensity 0..1

// Sine-free hash (Hoskins), stable across GPU vendors for integer-like input.
float rainHash(vec2 p)
{
    vec3 p3 = fract(vec3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main()
{
    vec2 ndc = ofsUvToNdc(v_uv);
    vec4 farPoint = mul(u_ofsInvViewProj, vec4(ndc, 1.0, 1.0));
    vec4 nearPoint = mul(u_ofsInvViewProj, vec4(ndc, 0.0, 1.0));
    vec3 ray = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
    vec3 axis = u_rain.xyz;
    vec3 side = u_rainSide.xyz;
    vec3 third = cross(axis, side);
    float alongAxis = dot(ray, axis);
    float angle = atan2(dot(ray, third), dot(ray, side));
    // Distance from the streak axis in view: drops near the axis are seen end-on.
    float off = sqrt(max(1.0 - alongAxis * alongAxis, 1.0e-4));
    float sceneRange = texture2DLod(s_sceneRange, v_uv, 0.0).r * u_cameraForward.w;

    float streaks = 0.0;
    for (int layer = 0; layer < 3; ++layer) {
        float depth = 3.0 + float(layer) * 7.0;
        float count = 300.0 + float(layer) * 210.0;
        float column = angle / (2.0 * OFS_PI) * count;
        // Each angular column carries its own drops, with its own timing.
        vec2 seed = vec2(floor(column), float(layer) * 17.0);
        float h1 = rainHash(seed + vec2(0.0, 1.0));
        float h2 = rainHash(seed + vec2(0.0, 2.0));
        // Position along the fall line. Flying faster stretches each drop
        // into a longer streak and sweeps it past sooner.
        float fall = alongAxis / off * depth * (0.05 + 0.04 * h2)
                   + u_cameraPos.w * (1.6 + 1.2 * h1) * (1.0 + u_rain.w * 0.02) + h1 * 53.0;
        float along = fract(fall);
        float extent = clamp(0.07 + u_rain.w * 0.0035, 0.07, 0.6);
        float body = smoothstep(0.0, 0.02, along) * (1.0 - smoothstep(extent * 0.5, extent, along));
        float width = abs(fract(column) - 0.5);
        float streak = body * (1.0 - smoothstep(0.04, 0.16, width)) * step(0.45, h2);
        streaks += streak * step(depth, sceneRange) * (1.0 - float(layer) * 0.25);
    }
    // Drops are lit by the sky around them: bright against shadow, lost against cloud.
    vec3 light = ofsAmbientIrradiance(vec3(0.0, 1.0, 0.0)) / OFS_PI * 0.55;
    float opacity = ofsSaturate(streaks * 0.17 * u_rainSide.w) * smoothstep(0.02, 0.2, off);
    gl_FragColor = vec4(light, opacity);
}
