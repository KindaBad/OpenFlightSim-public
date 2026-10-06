// Depth-only pass for the directional shadow map. Nothing is written to colour;
// the fragment shader only exists because OpenGL requires one.

$input a_position, a_texcoord0
$output v_uv
#include <bgfx_shader.sh>

uniform mat4 u_ofsModel;
uniform mat4 u_shadowMatrix;

void main()
{
    v_uv=a_texcoord0;
    gl_Position = mul(u_shadowMatrix, mul(u_ofsModel, vec4(a_position, 1.0)));
}
