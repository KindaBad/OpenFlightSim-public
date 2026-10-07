// One oversized triangle covering the viewport, shared by every screen-space
// pass. v_uv addresses render-target textures in this backend's orientation.

$input a_position
$output v_uv
#include <bgfx_shader.sh>
#include "frame.glsl"

void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    v_uv = ofsNdcToUv(a_position.xy);
}
