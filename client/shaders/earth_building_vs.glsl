// The round Earth's buildings, a tile's worth in one draw: each vertex
// carries its part's look (EarthBuildingVertex in earth_objects.hpp).

$input a_position, a_normal, a_color0, a_texcoord1
$output v_worldPos, v_normal, v_color, v_params
#include <bgfx_shader.sh>

uniform mat4 u_ofsModel;
uniform mat4 u_ofsViewProj;
uniform mat3 u_normalMatrix;

void main()
{
    vec4 world = mul(u_ofsModel, vec4(a_position, 1.0));
    v_worldPos = world.xyz;
    v_normal = mul(u_normalMatrix, a_normal);
    // Colours are stored as their square roots.
    v_color = vec4(a_color0.rgb * a_color0.rgb, a_color0.a);
    v_params = vec4(a_texcoord1.x, a_texcoord1.y, a_texcoord1.z * 2.0, a_texcoord1.w * 4.0);
    gl_Position = mul(u_ofsViewProj, world);
}
