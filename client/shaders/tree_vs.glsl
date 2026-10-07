// Instanced trees. One mesh per species and level of detail; each instance
// carries its position, size, heading and tint, so a forest costs a few bytes
// per tree instead of a copy of the geometry.

$input a_position, a_normal, a_texcoord0, i_data0, i_data1
$output v_worldPos, v_normal, v_uv, v_surfacePos
#include <bgfx_shader.sh>
#include "frame.glsl"

uniform mat4 u_ofsModel;    // translation of the floating origin
uniform mat4 u_ofsViewProj;

void main()
{
    // i_data0: xyz base position, w height. i_data1: x spread, y yaw, z tint, w species.
    float c = cos(i_data1.y);
    float s = sin(i_data1.y);
    vec3 local = a_position * vec3(i_data1.x * i_data0.w, i_data0.w, i_data1.x * i_data0.w);
    // Wind: the crown sways with height, each tree out of phase with the next.
    float phase = u_cameraPos.w * 1.1 + i_data0.x * 0.07 + i_data0.z * 0.05;
    float sway = a_position.y * a_position.y * 0.02 * length(u_wind.xz) * i_data0.w * 0.04;
    vec3 rotated = vec3(local.x * c + local.z * s, local.y, -local.x * s + local.z * c);
    rotated.xz += normalize(u_wind.xz + vec2_splat(1.0e-4)) * sway * (0.6 + 0.4 * sin(phase));
    vec4 world = mul(u_ofsModel, vec4(i_data0.xyz + rotated, 1.0));
    vec3 normal = normalize(a_normal / vec3(i_data1.x, 1.0, i_data1.x));
    v_worldPos = world.xyz;
    v_normal = vec3(normal.x * c + normal.z * s, normal.y, -normal.x * s + normal.z * c);
    // u: wood (0) or foliage (1); v: height within the tree, for crown shading.
    v_uv = a_texcoord0;
    v_surfacePos = vec3(i_data1.z, i_data1.w, a_position.y);
    gl_Position = mul(u_ofsViewProj, world);
}
