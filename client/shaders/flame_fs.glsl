// Afterburner plume and the hot exhaust behind it.
//
// Layers 0..2 are nested emissive shells with shock diamonds, layer 3 is the
// nozzle glow. Layer 4 is not light at all: it is the column of hot, less dense
// gas behind the engine, drawn into the refraction buffer so the scene seen
// through it shimmers.

$input v_color, v_uv, v_surfacePos
#include <bgfx_shader.sh>
#include "frame.glsl"

SAMPLER2D(s_sceneRange, 13);
uniform vec4 u_flame;
uniform vec4 u_effectParams; // y refraction strength, z target width, w target height

float flowNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    float a = fract(sin(dot(i, vec2(127.1, 311.7))) * 43758.5453);
    float b = fract(sin(dot(i + vec2(1, 0), vec2(127.1, 311.7))) * 43758.5453);
    float c = fract(sin(dot(i + vec2(0, 1), vec2(127.1, 311.7))) * 43758.5453);
    float d = fract(sin(dot(i + vec2(1, 1), vec2(127.1, 311.7))) * 43758.5453);
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

void main()
{
    if (u_flame.z > 3.5) {
        // Schlieren: rolling cells displace the image by a fraction of a pixel
        // row, strongest near the nozzle. Hidden where geometry is nearer.
        float t = clamp(v_uv.y, 0.0, 1.0);
        vec2 screenUv = gl_FragCoord.xy / u_effectParams.zw;
        float sceneRange = texture2DLod(s_sceneRange, screenUv, 0.0).r * u_cameraForward.w;
        float viewDistance = v_surfacePos.x;
        float visible = step(viewDistance, sceneRange + 0.5);
        vec2 cells = vec2(flowNoise(vec2(v_uv.x * 9.0 + u_flame.w, t * 17.0 - u_flame.y * 11.0)),
                          flowNoise(vec2(v_uv.x * 7.0 - u_flame.w, t * 13.0 - u_flame.y * 9.0 + 31.0))) - 0.5;
        float fade = smoothstep(0.0, 0.06, t) * (1.0 - smoothstep(0.35, 1.0, t));
        float strength = u_effectParams.y * u_flame.x * fade * visible / max(viewDistance, 4.0);
        gl_FragColor = vec4(cells * strength, 0.0, 1.0);
    } else if (u_flame.z > 2.5) {
        vec2 p = v_uv * 2.0 - 1.0; float r2 = dot(p, p);
        float core = exp(-8.0 * r2), halo = exp(-3.5 * r2);
        vec3 color = mix(vec3(.22, .38, 1.4), vec3(3.2, 3.6, 4.0), exp(-6.0 * r2));
        float flicker = .96 + .04 * sin(u_flame.y * 57.0 + u_flame.w);
        float opacity = (.58 * core + .20 * halo) * (1.0 - smoothstep(.55, 1.0, r2)) * u_flame.x * flicker;
        gl_FragColor = vec4(color * u_sunDirection.w, opacity);
    } else {
        float t = clamp(v_uv.y, 0.0, 1.0);
        float layer = u_flame.z;
        float turbulence = flowNoise(vec2(v_uv.x * 9.0 + u_flame.w, t * 13.0 - u_flame.y * 7.0));
        float streak = .78 + .22 * sin(v_uv.x * 37.7 + u_flame.w + t * 8.0);
        float cells = pow(.5 + .5 * cos(t * 31.0 + u_flame.w * .09 + sin(u_flame.y * 4.0) * .2), 8.0);
        float fade = pow(max(1.0 - t, 0.0), 1.45) * smoothstep(0.0, .025, t) * (1.0 - smoothstep(.85, 1.0, t));
        float flicker = .94 + .06 * sin(u_flame.y * 57.0 + u_flame.w);
        vec3 nearColor = layer < .5 ? vec3(.86, .91, 1.0) : vec3(.24, .30, .98);
        vec3 farColor = layer < .5 ? vec3(1.0, .76, .30) : vec3(1.0, .34, .04);
        vec3 hot = mix(nearColor, farColor, smoothstep(.14, .54, t));
        hot = mix(hot, vec3(1.0, .91, .72), cells * .30 * (1.0 - t));
        float opacity = mix(.28, .085, layer * .5) * fade * streak * flicker * (.82 + .18 * turbulence) * u_flame.x;
        gl_FragColor = vec4(hot * (1.55 + .30 * cells) * u_sunDirection.w, opacity);
    }
}
