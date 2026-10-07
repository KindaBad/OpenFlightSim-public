// Sky: a full-screen triangle on the far plane, so it is drawn only where no
// geometry has written depth.

$input a_position
$output v_uv
#include <bgfx_shader.sh>
#include "frame.glsl"

void main()
{
    v_uv = ofsNdcToUv(a_position.xy);
    gl_Position = vec4(a_position.xy, 1.0, 1.0);
}
