// Terrain, lakes and the paved airfield surfaces.

$input a_position, a_normal, a_texcoord0
$output v_worldPos, v_normal, v_uv, v_surfacePos
#include <bgfx_shader.sh>

uniform mat4 u_ofsModel;
uniform mat4 u_ofsViewProj;
uniform vec4 u_surface; // x surface kind, y decal layer

void main()
{
    vec4 world = mul(u_ofsModel, vec4(a_position, 1.0));
    v_worldPos = world.xyz;
    v_normal = a_normal;
    v_uv = a_texcoord0;
    v_surfacePos = a_position;
    gl_Position = mul(u_ofsViewProj, world);
    // Paving and paint lie in the ground plane. A constant offset in depth-
    // buffer units separates each layer from the one beneath it at every range,
    // which a geometric lift cannot do once the surfaces are kilometres away.
    gl_Position.z -= u_surface.y * 2.0e-6 * gl_Position.w;
}
