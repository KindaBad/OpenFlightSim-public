// Temporal accumulation for the cloud layer. Each frame marches with a
// different sample offset; reprojecting the previous result and blending the
// new one in recovers a smooth, well-sampled image from few steps per frame.

$input v_uv
#include <bgfx_shader.sh>
#include "frame.glsl"

SAMPLER2D(s_cloudCurrent, 0);
SAMPLER2D(s_cloudDepth, 1);
SAMPLER2D(s_cloudHistory, 2);
SAMPLER2D(s_cloudHistoryDepth, 3);
uniform mat4 u_ofsInvViewProj;
uniform mat4 u_prevViewProj;
uniform vec4 u_cloudResolve; // xy 1/size, z history weight (0 disables), w unused

void main()
{
    vec4 current = texture2DLod(s_cloudCurrent, v_uv, 0.0);
    vec4 depthInfo = texture2DLod(s_cloudDepth, v_uv, 0.0);
    vec4 result = current;
    if (u_cloudResolve.z > 0.0) {
        // Where the cloud was last frame: reproject its mean depth.
        vec2 ndc = ofsUvToNdc(v_uv);
        vec4 farPoint = mul(u_ofsInvViewProj, vec4(ndc, 1.0, 1.0));
        vec4 nearPoint = mul(u_ofsInvViewProj, vec4(ndc, 0.0, 1.0));
        vec3 ray = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
        vec3 position = u_cameraPos.xyz + ray * (depthInfo.r * u_cameraForward.w);
        vec4 previous = mul(u_prevViewProj, vec4(position, 1.0));
        vec2 previousUv = ofsNdcToUv(previous.xy / max(previous.w, 1.0e-4));
        bool visible = previous.w > 0.0 && previousUv.x > 0.0 && previousUv.x < 1.0
                    && previousUv.y > 0.0 && previousUv.y < 1.0;
        if (visible) {
            vec4 history = texture2DLod(s_cloudHistory, previousUv, 0.0);
            vec4 historyDepth = texture2DLod(s_cloudHistoryDepth, previousUv, 0.0);
            // Bound the history by what this frame sees nearby, so stale light
            // cannot linger when the view or the cloud changes.
            vec4 low = current;
            vec4 high = current;
            for (int y = -1; y <= 1; ++y) {
                for (int x = -1; x <= 1; ++x) {
                    vec4 neighbour = texture2DLod(s_cloudCurrent, v_uv + vec2(float(x), float(y)) * u_cloudResolve.xy, 0.0);
                    low = min(low, neighbour);
                    high = max(high, neighbour);
                }
            }
            vec4 slack = (high - low) * 0.35 + vec4_splat(0.004);
            history = clamp(history, low - slack, high + slack);
            // Geometry moving in front of the cloud changes how far the march
            // went; history from the other state is not comparable.
            float weight = abs(historyDepth.g - depthInfo.g) > 0.5 ? 0.0 : u_cloudResolve.z;
            result = mix(current, history, weight);
        }
    }
    gl_FragData[0] = result;
    gl_FragData[1] = depthInfo;
}
