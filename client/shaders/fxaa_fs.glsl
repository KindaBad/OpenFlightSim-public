// Post-process edge antialiasing after Lottes' FXAA 3.11 (public domain).
// MSAA resolves geometric edges before tone mapping, which leaves very bright
// edges, alpha-tested foliage and shader detail aliased; this pass works on the
// final display image and catches those.

$input v_uv
#include <bgfx_shader.sh>

SAMPLER2D(s_scene, 0);
uniform vec4 u_postStep; // xy texel size

float luma(vec2 uv) { return texture2DLod(s_scene, uv, 0.0).a; }

void main()
{
    vec2 t = u_postStep.xy;
    vec4 centre = texture2DLod(s_scene, v_uv, 0.0);
    float lumaM = centre.a;
    float lumaN = luma(v_uv + vec2(0.0, -t.y));
    float lumaS = luma(v_uv + vec2(0.0, t.y));
    float lumaW = luma(v_uv + vec2(-t.x, 0.0));
    float lumaE = luma(v_uv + vec2(t.x, 0.0));
    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(lumaW, lumaE)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(lumaW, lumaE)));
    float contrast = lumaMax - lumaMin;
    if (contrast < max(0.0312, lumaMax * 0.125)) {
        gl_FragColor = vec4(centre.rgb, 1.0);
        return;
    }
    float lumaNW = luma(v_uv + vec2(-t.x, -t.y));
    float lumaNE = luma(v_uv + vec2(t.x, -t.y));
    float lumaSW = luma(v_uv + vec2(-t.x, t.y));
    float lumaSE = luma(v_uv + vec2(t.x, t.y));

    // Edge orientation from the second derivatives across each axis.
    float edgeHorizontal = abs(lumaNW + lumaSW - 2.0 * lumaW) + 2.0 * abs(lumaN + lumaS - 2.0 * lumaM)
                         + abs(lumaNE + lumaSE - 2.0 * lumaE);
    float edgeVertical = abs(lumaNW + lumaNE - 2.0 * lumaN) + 2.0 * abs(lumaW + lumaE - 2.0 * lumaM)
                       + abs(lumaSW + lumaSE - 2.0 * lumaS);
    bool horizontal = edgeHorizontal >= edgeVertical;
    float luma1 = horizontal ? lumaN : lumaW;
    float luma2 = horizontal ? lumaS : lumaE;
    float gradient1 = abs(luma1 - lumaM);
    float gradient2 = abs(luma2 - lumaM);
    bool steepest1 = gradient1 >= gradient2;
    float gradientScaled = 0.25 * max(gradient1, gradient2);
    float stepLength = horizontal ? t.y : t.x;
    float lumaLocalAverage = 0.0;
    if (steepest1) {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaM);
    } else {
        lumaLocalAverage = 0.5 * (luma2 + lumaM);
    }
    vec2 edgeUv = v_uv;
    if (horizontal) {
        edgeUv.y += stepLength * 0.5;
    } else {
        edgeUv.x += stepLength * 0.5;
    }

    // Walk along the edge both ways to find where it ends.
    vec2 along = horizontal ? vec2(t.x, 0.0) : vec2(0.0, t.y);
    vec2 uv1 = edgeUv - along;
    vec2 uv2 = edgeUv + along;
    float end1 = luma(uv1) - lumaLocalAverage;
    float end2 = luma(uv2) - lumaLocalAverage;
    bool reached1 = abs(end1) >= gradientScaled;
    bool reached2 = abs(end2) >= gradientScaled;
    for (int i = 0; i < 8; ++i) {
        if (reached1 && reached2) {
            break;
        }
        float stride = i < 3 ? 1.5 : (i < 6 ? 2.5 : 5.0);
        if (!reached1) {
            uv1 -= along * stride;
            end1 = luma(uv1) - lumaLocalAverage;
            reached1 = abs(end1) >= gradientScaled;
        }
        if (!reached2) {
            uv2 += along * stride;
            end2 = luma(uv2) - lumaLocalAverage;
            reached2 = abs(end2) >= gradientScaled;
        }
    }
    float distance1 = horizontal ? v_uv.x - uv1.x : v_uv.y - uv1.y;
    float distance2 = horizontal ? uv2.x - v_uv.x : uv2.y - v_uv.y;
    bool nearer1 = distance1 < distance2;
    float nearest = min(distance1, distance2);
    float edgeLength = distance1 + distance2;
    float pixelOffset = -nearest / max(edgeLength, 1.0e-6) + 0.5;
    bool centreSmaller = lumaM < lumaLocalAverage;
    bool correct = ((nearer1 ? end1 : end2) < 0.0) != centreSmaller;
    float finalOffset = correct ? pixelOffset : 0.0;

    // Sub-pixel aliasing: isolated bright or dark pixels.
    float lumaAverage = (2.0 * (lumaN + lumaS + lumaW + lumaE) + lumaNW + lumaNE + lumaSW + lumaSE) / 12.0;
    float subPixel = clamp(abs(lumaAverage - lumaM) / contrast, 0.0, 1.0);
    subPixel = (-2.0 * subPixel + 3.0) * subPixel * subPixel;
    finalOffset = max(finalOffset, subPixel * subPixel * 0.6);

    vec2 finalUv = v_uv;
    if (horizontal) {
        finalUv.y += finalOffset * stepLength;
    } else {
        finalUv.x += finalOffset * stepLength;
    }
    gl_FragColor = vec4(texture2DLod(s_scene, finalUv, 0.0).rgb, 1.0);
}
