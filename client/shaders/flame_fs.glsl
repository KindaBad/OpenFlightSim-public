// Afterburner plume and the hot exhaust behind it.
//
// Layers 0..2 are nested emissive shells: a chain of shock diamonds, the flame
// around them and a faint outer sheath. Layer 3 is the glow at the nozzle and
// layer 5 the white-hot throat seen looking up the jet pipe. Layer 4 is not
// light at all: it is the column of hot, less dense gas behind the engine,
// drawn into the refraction buffer so the scene seen through it shimmers.

$input v_color, v_uv, v_surfacePos
#include <bgfx_shader.sh>
#include "frame.glsl"
#include "flame.glsl"

SAMPLER2D(s_sceneRange, 13);
uniform vec4 u_flame;
uniform vec4 u_effectParams; // x 1 for a rocket motor's palette, y refraction strength, z target width, w target height

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
    if (u_flame.z > 3.5 && u_flame.z < 4.5) {
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
    } else if (u_flame.z > 4.5) {
        // The throat: white-yellow in the middle, orange where it meets the petals.
        vec2 p = v_uv * 2.0 - 1.0; float r = length(p);
        vec3 color = mix(vec3(4.4, 3.5, 2.1), vec3(2.7, .86, .15), smoothstep(.30, 1.0, r));
        float flicker = .95 + .05 * sin(u_flame.y * 47.0 + u_flame.w);
        float opacity = (1.0 - smoothstep(.84, 1.0, r)) * sqrt(clamp(u_flame.x, 0.0, 1.0)) * flicker;
        gl_FragColor = vec4(color * flameAdaptation(u_worldOrigin.w) * u_sunDirection.w, opacity);
    } else if (u_flame.z > 2.5) {
        vec2 p = v_uv * 2.0 - 1.0; float r2 = dot(p, p);
        float core = exp(-8.0 * r2), halo = exp(-3.5 * r2);
        // Orange light spilling from the nozzle, white where it is hottest.
        vec3 color = mix(vec3(1.9, .60, .15), vec3(4.4, 3.5, 2.2), exp(-7.0 * r2));
        // A rocket's glare is white at the nozzle and orange around it.
        color = mix(color, mix(vec3(2.6, .95, .22), vec3(5.5, 5.0, 4.2), exp(-6.0 * r2)), u_effectParams.x);
        float flicker = .96 + .04 * sin(u_flame.y * 57.0 + u_flame.w);
        float opacity = (mix(.44, .58, u_effectParams.x) * core + mix(.15, .20, u_effectParams.x) * halo)
                      * (1.0 - smoothstep(.55, 1.0, r2)) * u_flame.x * flicker;
        color *= mix(flameAdaptation(u_worldOrigin.w), 1.0, u_effectParams.x);
        gl_FragColor = vec4(color * u_sunDirection.w, opacity);
    } else if (u_effectParams.x > 0.5) {
        // Solid propellant burns white-hot and yellow, with no blue flame and a
        // denser, brighter core than an afterburner.
        float t = clamp(v_uv.y, 0.0, 1.0);
        float layer = u_flame.z;
        float turbulence = flowNoise(vec2(v_uv.x * 9.0 + u_flame.w, t * 13.0 - u_flame.y * 7.0));
        float streak = .78 + .22 * sin(v_uv.x * 37.7 + u_flame.w + t * 8.0);
        float fade = pow(max(1.0 - t, 0.0), 1.45) * smoothstep(0.0, .025, t) * (1.0 - smoothstep(.85, 1.0, t));
        float flicker = .94 + .06 * sin(u_flame.y * 57.0 + u_flame.w);
        vec3 rocketNear = layer < .5 ? vec3(1.0, .97, .88) : vec3(1.0, .82, .42);
        vec3 rocketFar = layer < .5 ? vec3(1.0, .72, .30) : vec3(1.0, .40, .08);
        vec3 hot = mix(rocketNear, rocketFar, smoothstep(.10, .60, t));
        float opacity = 1.9 * mix(.28, .085, layer * .5) * fade * streak * flicker * (.82 + .18 * turbulence) * u_flame.x;
        gl_FragColor = vec4(hot * 2.6 * u_sunDirection.w, opacity);
    } else {
        float t = clamp(v_uv.y, 0.0, 1.0);
        float layer = u_flame.z;
        float reheat = clamp(u_flame.x, 0.0, 1.0);
        float along = t * flameLength(reheat) * flameReach(layer);
        float cell = flameCell(along);
        float turbulence = flowNoise(vec2(v_uv.x * 9.0 + u_flame.w, along * 2.4 - u_flame.y * 11.0));
        float flicker = .93 + .07 * sin(u_flame.y * 61.0 + u_flame.w);
        float decay = max(1.0 - t, 0.0);
        // No hard edge at the nozzle lip or where the shell ends.
        float ends = smoothstep(0.0, .02, t) * (1.0 - smoothstep(.80, 1.0, t));
        vec3 color;
        float opacity, gain;
        float depth = v_surfacePos.y;
        if (layer < .5) {
            // Shock diamonds: knots of burning gas, yellow near the nozzle and
            // pink as they weaken downstream, brightest at each Mach disc.
            color = mix(vec3(1.0, .66, .40), vec3(1.0, .38, .36), smoothstep(0.0, .55, t));
            opacity = .44 * cell * cell * pow(decay, 1.1) * depth;
            gain = 1.3;
        } else if (layer < 1.5) {
            // The flame: orange and brightest as it leaves the nozzle, then the
            // pale blue of burning kerosene, fading to violet.
            float root = 1.0 - smoothstep(0.0, .10, t);
            color = mix(vec3(.36, .48, 1.0), vec3(1.0, .56, .20), root);
            color = mix(color, vec3(.46, .40, .86), smoothstep(.45, 1.0, t));
            opacity = .19 * pow(decay, 1.25) * (.78 + .22 * turbulence) * (.88 + .24 * cell * decay);
            gain = 1.0 + 2.2 * root;
        } else {
            color = vec3(.30, .42, 1.0);
            opacity = .055 * pow(decay, .9) * (.65 + .35 * turbulence);
            gain = 1.0;
        }
        opacity *= ends * flicker * depth * sqrt(reheat);
        gain *= flameAdaptation(u_worldOrigin.w);
        gl_FragColor = vec4(color * gain * u_sunDirection.w, opacity);
    }
}
