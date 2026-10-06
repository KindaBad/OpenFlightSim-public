// Unlit vertex-colour pass. Used for HUD world-space markers, tracer lines,
// particles and the optional developer grid: anything that must read as
// UI-in-world rather than as a lit surface.

$input a_position, a_color0
$output v_color, v_worldPos
#include <bgfx_shader.sh>

uniform mat4 u_ofsModel;
uniform mat4 u_ofsViewProj;


void main()
{
    v_color = a_color0;
    const vec4 world = mul(u_ofsModel, vec4(a_position, 1.0));
    v_worldPos = world.xyz;
    gl_Position = mul(u_ofsViewProj, world);
}
