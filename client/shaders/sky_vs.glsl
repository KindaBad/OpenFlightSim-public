// Sky dome.
//
// Drawn as a full-screen triangle before the world with depth writes disabled.
// The gradient is an analytic fit to a clear day: zenith blue, a lighter band
// near the horizon, a sun disc with a wide forward-scattering halo, and a
// ground haze layer that fades into the fog colour used by the world shader so
// the horizon line is seamless.

$input a_position
$output v_uv
#include <bgfx_shader.sh>

// bgfx resolves vertex attribute locations by name, looking for its own
// reserved names (a_position, a_normal, a_color0, ...) after glGetAttribLocation.
// Any other name returns -1, the attribute is never bound, and the draw is
// discarded. The vertex buffer supplies a full vec3 here; only x and y are read.

void main()
{
    v_uv = a_position.xy;
    gl_Position = vec4(a_position.xy, 1.0, 1.0);
}
