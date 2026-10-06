$input v_uv
#include <bgfx_shader.sh>
#include "common.glsl"
uniform mat4 u_ofsInvViewProj;
uniform vec4 u_worldOrigin, u_cameraPos, u_sunDirection, u_sunColor, u_fogColor;
uniform vec4 u_cloudRender; // steps, render width, render height
uniform vec4 u_postSettings;
SAMPLER2D(s_sceneDepth,8);
void main() {
    vec4 farPoint = mul(u_ofsInvViewProj,vec4(v_uv,1.0,1.0));
    vec4 nearPoint = mul(u_ofsInvViewProj,vec4(v_uv,0.0,1.0));
    vec3 ray = normalize(farPoint.xyz/farPoint.w-nearPoint.xyz/nearPoint.w);
    vec3 eye = u_cameraPos.xyz+u_worldOrigin.xyz;
    vec2 screenUv=vec2(v_uv.x*.5+.5,mix(.5-v_uv.y*.5,.5+v_uv.y*.5,u_postSettings.z));
    float sceneRange=texture2D(s_sceneDepth,screenUv).r;
    vec2 interval = cloudInterval(eye,ray,min(u_weather.z,sceneRange));
    vec3 lightSum = vec3_splat(0.0);
    float transmittance = 1.0;
    if (interval.y > interval.x) {

        // Midpoint samples remain stable when the camera moves, without noisy
        // dither or the memory/ghosting cost of temporal reprojection.
        float jitter = .5;
        vec3 sun = normalize(-u_sunDirection.xyz);
        float cosine = max(dot(ray,sun),0.0);
        float phase = .55 + 2.8*pow(cosine,18.0);
        for (int i=0;i<36;++i) {
            if (float(i) >= u_cloudRender.x || transmittance < .025) break;
            // Quadratic spacing gives nearby fly-through density more samples
            // while retaining the fixed work budget toward the horizon.
            float t0=float(i)/u_cloudRender.x,t1=(float(i)+1.0)/u_cloudRender.x;
            float start=interval.x+(interval.y-interval.x)*t0*t0;
            float stepSize=(interval.y-interval.x)*(t1*t1-t0*t0);
            float distance = start+jitter*stepSize;
            vec3 p = eye+ray*distance;
            float density = cloudDensity(p);
            if (density > .002) {
                // Two light probes provide internal shadows and silver linings.
                float sunDepth=cloudDensity(p+sun*55.0)*65.0
                    +cloudDensity(p+sun*160.0)*140.0
                    +cloudDensity(p+sun*420.0)*260.0;
                float shade = exp(-sunDepth*u_weather.y);
                float height = clamp((p.y-u_cloudParams.y)/u_cloudParams.z,0.0,1.0);
                vec3 ambient = mix(vec3(.23,.29,.38),vec3(.54,.63,.74),height)
                    *clamp(length(u_sunColor.rgb)*.18,.04,1.0);
                float powder=1.0-exp(-density*3.0);
                vec3 lighting=ambient*(.75+.25*powder)+u_sunColor.rgb*(.20*shade*phase);
                lighting+=u_sunColor.rgb*.045*powder*exp(-sunDepth*u_weather.y*.25);
                lighting = mix(lighting,u_fogColor.rgb,1.0-exp(-distance*.000025));
                float opacity = 1.0-exp(-density*stepSize*u_weather.y);
                lightSum += transmittance*opacity*lighting;
                transmittance *= 1.0-opacity;
            }
        }
    }
    gl_FragColor = vec4(lightSum,1.0-transmittance);
}
