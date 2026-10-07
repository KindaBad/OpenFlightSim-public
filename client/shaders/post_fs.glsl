// Display transform: heat refraction, glare, exposure, tone curve and sRGB
// encoding, applied once to the fully composited linear scene.

$input v_uv
#include <bgfx_shader.sh>
#include "frame.glsl"

SAMPLER2D(s_scene, 0);
SAMPLER2D(s_bloom, 1);
SAMPLER2D(s_distortion, 2);
uniform vec4 u_postSettings; // x exposure, y glare fraction, z heat refraction on, w contrast
uniform vec4 u_postStep;     // x 1 / number of glare pyramid levels

float postDither(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// A filmic curve applied per channel in a slightly desaturated space, in the
// manner of AgX: bright colours roll off toward white instead of clipping to a
// saturated primary, and mid-tones keep unit contrast. 0.18 maps to 0.18.
vec3 displayTransform(vec3 color, float contrast)
{
    vec3 weights = vec3(0.30, 0.56, 0.14);
    color = max(color, vec3_splat(0.0));
    color = mix(color, vec3_splat(dot(color, weights)), 0.14);

    // Log2 exposure range: 12.5 stops below middle grey to 4.6 above.
    float range = 17.1;
    float pivotX = 12.5 / range;
    float pivotY = 0.4587; // 0.18 encoded with a 2.2 power
    float slope = 0.1445 * range * contrast;
    vec3 x = clamp((log2(max(color, vec3_splat(1.0e-7)) / 0.18) + 12.5) / range, 0.0, 1.0);
    vec3 d = x - vec3_splat(pivotX);
    vec3 isShoulder = step(vec3_splat(0.0), d);
    // Each side approaches its own asymptote, chosen so the curve reaches
    // exactly 0 and 1 at the ends of the range.
    float toeEnd = slope * pivotX;
    float shoulderEnd = slope * (1.0 - pivotX);
    float toeLimit = toeEnd / pow(max(pow(toeEnd / pivotY, 1.5) - 1.0, 1.0e-3), 1.0 / 1.5);
    float shoulderLimit = shoulderEnd / pow(max(pow(shoulderEnd / (1.0 - pivotY), 1.5) - 1.0, 1.0e-3), 1.0 / 1.5);
    vec3 limit = mix(vec3_splat(toeLimit), vec3_splat(shoulderLimit), isShoulder);
    vec3 scaled = slope * d;
    vec3 y = vec3_splat(pivotY) + scaled / pow(vec3_splat(1.0) + pow(abs(scaled) / limit, vec3_splat(1.5)), vec3_splat(1.0 / 1.5));
    vec3 display = pow(clamp(y, 0.0, 1.0), vec3_splat(2.2));
    // Return part of the saturation given up before the curve.
    display = (display - 0.08 * dot(display, weights)) / 0.92;
    return clamp(display, 0.0, 1.0);
}

vec3 encodeSrgb(vec3 linearColor)
{
    vec3 cutoff = step(linearColor, vec3_splat(0.0031308));
    return mix(1.055 * pow(max(linearColor, vec3_splat(0.0)), vec3_splat(1.0 / 2.4)) - 0.055,
               linearColor * 12.92, cutoff);
}

void main()
{
    vec2 uv = v_uv;
    if (u_postSettings.z > 0.5) {
        uv = clamp(uv + texture2DLod(s_distortion, v_uv, 0.0).xy, vec2_splat(0.001), vec2_splat(0.999));
    }
    vec3 scene = texture2DLod(s_scene, uv, 0.0).rgb;
    vec3 glare = texture2DLod(s_bloom, uv, 0.0).rgb * u_postStep.x;
    vec3 radiance = mix(scene, glare, u_postSettings.y);
    // Daylight white balance, as a camera set to about 5600 K: direct sun
    // stays slightly warm and open shade slightly blue, instead of the whole
    // frame taking on the yellow of sunlight filtered by the lower atmosphere.
    radiance *= vec3(0.93, 1.0, 1.14);
    vec3 encoded = encodeSrgb(displayTransform(radiance * u_postSettings.x, u_postSettings.w));
    // One least-significant bit of dither removes banding in the sky gradient.
    encoded += (postDither(gl_FragCoord.xy) - 0.5) / 255.0;
    // Luma in alpha feeds the edge filter that may follow.
    gl_FragColor = vec4(encoded, dot(encoded, vec3(0.299, 0.587, 0.114)));
}
