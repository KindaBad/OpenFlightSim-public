// Blends the accumulated cloud layer over the scene at full resolution.

$input v_uv
#include <bgfx_shader.sh>
#include "frame.glsl"

SAMPLER2D(s_cloudLayer, 0);
SAMPLER2D(s_cloudDepth, 1);
SAMPLER2D(s_sceneRange, 13);
uniform vec4 u_cloudResolve; // xy 1/size of the cloud buffer, z cloud march range

void main()
{
    // Depth-aware bilinear upsampling keeps reduced-resolution cloud pixels
    // from bleeding over a nearby aircraft silhouette or cockpit frame: a tap
    // marched past geometry that this pixel does not see is rejected.
    vec2 size = 1.0 / u_cloudResolve.xy;
    vec2 pixel = v_uv * size - 0.5;
    vec2 corner = floor(pixel);
    vec2 f = pixel - corner;
    float limited = texture2DLod(s_sceneRange, v_uv, 0.0).r * u_cameraForward.w < u_cloudResolve.z ? 1.0 : 0.0;
    vec4 total = vec4_splat(0.0);
    float weightSum = 0.0;
    vec4 fallback = vec4_splat(0.0);
    for (int i = 0; i < 4; ++i) {
        vec2 offset = vec2(float(i - 2 * (i / 2)), float(i / 2));
        vec2 uv = (corner + offset + 0.5) * u_cloudResolve.xy;
        vec4 tap = texture2DLod(s_cloudLayer, uv, 0.0);
        float tapLimited = texture2DLod(s_cloudDepth, uv, 0.0).g;
        vec2 bilinear = mix(vec2_splat(1.0) - f, f, offset);
        float w = bilinear.x * bilinear.y;
        fallback += tap * w;
        w *= abs(tapLimited - limited) < 0.5 ? 1.0 : 0.02;
        total += tap * w;
        weightSum += w;
    }
    gl_FragColor = weightSum > 1.0e-4 ? total / weightSum : fallback;
}
