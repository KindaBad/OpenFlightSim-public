// Forward-shaded PBR surface used by the aircraft and airfield structures.

$input a_position, a_normal, a_texcoord0, a_tangent
$output v_worldPos, v_normal, v_uv, v_surfacePos, v_tangent
#include <bgfx_shader.sh>

uniform mat4 u_ofsModel;
uniform mat4 u_ofsViewProj;
uniform mat3 u_normalMatrix;

void main()
{
    vec4 world = mul(u_ofsModel, vec4(a_position, 1.0));
    v_worldPos = world.xyz;
    v_normal = mul(u_normalMatrix, a_normal);
    v_uv = a_texcoord0;
    v_tangent = vec4(mul(u_ofsModel, vec4(a_tangent.xyz, 0.0)).xyz, a_tangent.w);
    v_surfacePos = a_position;
    gl_Position = mul(u_ofsViewProj, world);
}
