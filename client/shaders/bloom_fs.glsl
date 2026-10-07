// Glare. Light scattered inside a lens or eye spreads a small fraction of every
// bright source over a wide area; this builds that spread as a pyramid of
// progressively blurred images (Jimenez 2014) rather than thresholding and
// blurring highlights, so it stays stable and energy conserving.

$input v_uv
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);
SAMPLER2D(s_bloom, 1);
uniform vec4 u_postStep; // xy source texel size, z mode (0 first downsample, 1 downsample, 2 upsample)

vec3 fetch(vec2 uv) { return texture2DLod(s_scene, uv, 0.0).rgb; }

// Weights a 2x2 block by inverse brightness so one very bright pixel (the sun,
// a glint) cannot flicker through the whole pyramid.
vec3 stableAverage(vec3 a, vec3 b, vec3 c, vec3 d)
{
    vec4 w = vec4(1.0 / (1.0 + max(a.r, max(a.g, a.b))), 1.0 / (1.0 + max(b.r, max(b.g, b.b))),
                  1.0 / (1.0 + max(c.r, max(c.g, c.b))), 1.0 / (1.0 + max(d.r, max(d.g, d.b))));
    return (a * w.x + b * w.y + c * w.z + d * w.w) / (w.x + w.y + w.z + w.w);
}

void main()
{
    vec2 t = u_postStep.xy;
    vec3 result = vec3_splat(0.0);
    if (u_postStep.z < 1.5) {
        // 13-tap downsample: four overlapping 2x2 boxes around a centre box.
        vec3 a = fetch(v_uv + t * vec2(-2.0, -2.0));
        vec3 b = fetch(v_uv + t * vec2(0.0, -2.0));
        vec3 c = fetch(v_uv + t * vec2(2.0, -2.0));
        vec3 d = fetch(v_uv + t * vec2(-2.0, 0.0));
        vec3 e = fetch(v_uv);
        vec3 f = fetch(v_uv + t * vec2(2.0, 0.0));
        vec3 g = fetch(v_uv + t * vec2(-2.0, 2.0));
        vec3 h = fetch(v_uv + t * vec2(0.0, 2.0));
        vec3 i = fetch(v_uv + t * vec2(2.0, 2.0));
        vec3 j = fetch(v_uv + t * vec2(-1.0, -1.0));
        vec3 k = fetch(v_uv + t * vec2(1.0, -1.0));
        vec3 l = fetch(v_uv + t * vec2(-1.0, 1.0));
        vec3 m = fetch(v_uv + t * vec2(1.0, 1.0));
        if (u_postStep.z < 0.5) {
            result = stableAverage(j, k, l, m) * 0.5
                   + (stableAverage(a, b, d, e) + stableAverage(b, c, e, f)
                      + stableAverage(d, e, g, h) + stableAverage(e, f, h, i)) * 0.125;
        } else {
            result = (j + k + l + m) * 0.125 + (a + c + g + i) * 0.03125
                   + (b + d + f + h) * 0.0625 + e * 0.125;
        }
        // Bound the input so a single extreme or invalid texel cannot spread
        // through every level of the pyramid.
        result = clamp(result, vec3_splat(0.0), vec3_splat(4000.0));
    } else {
        // 3x3 tent upsample of the smaller level, added to this level.
        vec3 wide = fetch(v_uv + t * vec2(-1.0, -1.0)) + fetch(v_uv + t * vec2(1.0, -1.0))
                  + fetch(v_uv + t * vec2(-1.0, 1.0)) + fetch(v_uv + t * vec2(1.0, 1.0))
                  + 2.0 * (fetch(v_uv + t * vec2(0.0, -1.0)) + fetch(v_uv + t * vec2(-1.0, 0.0))
                           + fetch(v_uv + t * vec2(1.0, 0.0)) + fetch(v_uv + t * vec2(0.0, 1.0)))
                  + 4.0 * fetch(v_uv);
        result = wide / 16.0 + texture2DLod(s_bloom, v_uv, 0.0).rgb;
    }
    gl_FragColor = vec4(result, 1.0);
}
