$input v_uv
#include <bgfx_shader.sh>
#include "common.glsl"
SAMPLER2D(s_scene,0);
SAMPLER2D(s_bloom,1);
uniform vec4 u_postSettings;
void main() {
    vec3 radiance=texture2D(s_scene,v_uv).rgb+u_postSettings.y*texture2D(s_bloom,v_uv).rgb;
    gl_FragColor=vec4(linearToSrgb(tonemap(max(radiance,vec3_splat(0.0))*u_postSettings.x)),1.0);
}
