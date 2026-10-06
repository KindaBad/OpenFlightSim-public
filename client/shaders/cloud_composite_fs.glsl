$input v_uv
#include <bgfx_shader.sh>
SAMPLER2D(s_cloudLayer,7);
SAMPLER2D(s_sceneDepth,8);
uniform vec4 u_cloudRender;
void main() {
    // Depth-aware bilinear upsampling keeps reduced-resolution cloud pixels
    // from bleeding over a nearby aircraft silhouette or cockpit panel.
    vec2 size=u_cloudRender.yz;
    vec2 pixel=v_uv*size-.5, base=floor(pixel), f=fract(pixel);
    float reference=texture2D(s_sceneDepth,v_uv).r;
    vec4 total=vec4_splat(0.0);
    float weightSum=0.0;
    for(int i=0;i<4;++i) {
        vec2 offset=vec2(float(i-2*(i/2)),float(i/2));
        vec2 uv=(base+offset+.5)/size;
        float depth=texture2D(s_sceneDepth,uv).r;
        float matching=1.0-smoothstep(max(20.0,reference*.02),max(40.0,reference*.06),abs(depth-reference));
        vec2 weight=mix(1.0-f,f,offset);
        float w=weight.x*weight.y*matching;
        total+=texture2D(s_cloudLayer,uv)*w;
        weightSum+=w;
    }
    gl_FragColor=weightSum>.001?total/weightSum:vec4_splat(0.0);
}
