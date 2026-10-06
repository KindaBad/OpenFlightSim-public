// Unlit fragment shader with optional distance and height fog, so combat
// markers and particles fade into the scene instead of floating on top of it.

$input v_color, v_worldPos
#include <bgfx_shader.sh>
#include "common.glsl"

uniform vec4 u_cameraPos;   // xyz used
uniform vec4 u_worldOrigin;
uniform vec4 u_fogColor;
uniform vec4 u_fogDensity;    // x
uniform vec4 u_fogHeightFalloff; // x
uniform vec4 u_fogGroundFade;  // x
uniform vec4 u_fogEnabled;   // x

void main()
{
    vec4 color = v_color;
    if (u_fogEnabled.x > 0.5) {
        const float viewDistance = length(u_cameraPos.xyz - v_worldPos);
        color.rgb = applyFog(color.rgb, viewDistance, v_worldPos.y+u_worldOrigin.y, u_cameraPos.y+u_worldOrigin.y, u_fogColor.rgb, u_fogDensity.x,
                             u_fogHeightFalloff.x, u_fogGroundFade.x);
    }
    gl_FragData[0] = color;
    gl_FragData[1] = vec4(min(length(u_cameraPos.xyz-v_worldPos),u_weather.z),0.0,0.0,color.a);
}
