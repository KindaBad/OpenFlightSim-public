$input v_uv
#include <bgfx_shader.sh>
SAMPLER2D(s_scene,0);
uniform vec4 u_postStep;
void main() {
    vec3 c=texture2D(s_scene,v_uv).rgb*.227027;
    c+=(texture2D(s_scene,v_uv+u_postStep.xy*1.384615).rgb+texture2D(s_scene,v_uv-u_postStep.xy*1.384615).rgb)*.316216;
    c+=(texture2D(s_scene,v_uv+u_postStep.xy*3.230769).rgb+texture2D(s_scene,v_uv-u_postStep.xy*3.230769).rgb)*.070270;
    if(u_postStep.z>.5) {
        float brightness=max(c.r,max(c.g,c.b));
        float knee=clamp(brightness-.6,0.0,.8);
        float contribution=max(brightness-1.0,knee*knee/.8);
        c*=contribution/max(brightness,.0001);
    }
    gl_FragColor=vec4(c,1.0);
}
