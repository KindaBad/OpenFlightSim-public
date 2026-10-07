// Depth-only pass for instanced trees; the instance transform matches tree_vs
// without the wind sway, which is far below a shadow texel.

$input a_position, a_texcoord0, i_data0, i_data1
$output v_uv
#include <bgfx_shader.sh>

uniform mat4 u_ofsModel;
uniform mat4 u_lightViewProj;

void main()
{
    float c = cos(i_data1.y);
    float s = sin(i_data1.y);
    vec3 local = a_position * vec3(i_data1.x * i_data0.w, i_data0.w, i_data1.x * i_data0.w);
    vec3 rotated = vec3(local.x * c + local.z * s, local.y, -local.x * s + local.z * c);
    v_uv = a_texcoord0;
    gl_Position = mul(u_lightViewProj, mul(u_ofsModel, vec4(i_data0.xyz + rotated, 1.0)));
}
