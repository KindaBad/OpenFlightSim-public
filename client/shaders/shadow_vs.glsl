// Depth-only pass for one cascade of the sun shadow atlas.

$input a_position, a_texcoord0
$output v_uv
#include <bgfx_shader.sh>

uniform mat4 u_ofsModel;
uniform mat4 u_lightViewProj;

void main()
{
    v_uv = a_texcoord0;
    gl_Position = mul(u_lightViewProj, mul(u_ofsModel, vec4(a_position, 1.0)));
}
