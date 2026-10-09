// Unlit coloured quads: tracers, muzzle flashes, impacts, explosions, smoke,
// debris, contrails, vapour and engine heat markers.
//
// Deliberately does not depth-write, so effects never punch holes in the world,
// and does not cull, so a billboard is never culled from the wrong side.

$input a_position, a_color0, a_texcoord0, a_texcoord1
$output v_color, v_uv, v_worldPos, v_params
#include <bgfx_shader.sh>

uniform mat4 u_ofsModel;
uniform mat4 u_ofsViewProj;


void main()
{
    v_color = a_color0;
    v_uv = a_texcoord0;
    v_params = a_texcoord1;
    vec4 world = mul(u_ofsModel, vec4(a_position,1.0));
    v_worldPos = world.xyz;
    gl_Position = mul(u_ofsViewProj,world);
}
