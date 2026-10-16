// The round Earth's land and water (client/src/renderer_earth.cpp). Vertices
// are ECEF offsets from their chunk's centre; the model matrix turns them into
// render space.

$input a_position, a_normal, a_texcoord0, a_texcoord1, a_color0
$output v_worldPos, v_normal, v_uv, v_color, v_params, v_surfacePos
#include <bgfx_shader.sh>
#include "earth_pattern.glsl"

uniform mat4 u_ofsModel;
uniform mat4 u_ofsViewProj;
// xy the pattern coordinates of this chunk's texture origin.
uniform vec4 u_earthChunk;

void main()
{
    vec4 world = mul(u_ofsModel, vec4(a_position, 1.0));
    v_worldPos = world.xyz;
    v_normal = mul(u_ofsModel, vec4(a_normal, 0.0)).xyz;
    v_uv = a_texcoord1.xy;
    v_color = a_color0;
    // Towns change slowly enough across the ground to be worked out here.
    vec4 town = earthTown(world.xyz, a_texcoord1.xy + u_earthChunk.xy);
    v_params = vec4(a_texcoord0.x, a_texcoord0.y, town.z, 0.0);
    v_surfacePos = vec3(town.xy, town.w);
    gl_Position = mul(u_ofsViewProj, world);
}
