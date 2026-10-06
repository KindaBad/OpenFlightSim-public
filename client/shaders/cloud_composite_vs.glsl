$input a_position
$output v_uv
#include <bgfx_shader.sh>
uniform vec4 u_postSettings;
void main() {
    gl_Position=vec4(a_position.xy,0.0,1.0);
    v_uv=vec2(a_position.x*.5+.5,mix(.5-a_position.y*.5,.5+a_position.y*.5,u_postSettings.z));
}
