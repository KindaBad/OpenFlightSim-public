$input a_position
$output v_uv
#include <bgfx_shader.sh>
void main() {
    v_uv = a_position.xy;
    gl_Position = vec4(a_position.xy,0.0,1.0);
}
