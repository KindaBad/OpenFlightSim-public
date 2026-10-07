// Unlit coloured lines for the developer grid, seen through the same haze as
// the scene so they sit in it rather than on top of it.

$input v_color, v_worldPos
#include <bgfx_shader.sh>
#include "atmosphere.glsl"

void main()
{
    vec3 toEye = u_cameraPos.xyz - v_worldPos;
    float viewDistance = length(toEye);
    vec3 color = v_color.rgb * ofsAmbientIrradiance(vec3(0.0, 1.0, 0.0)) / OFS_PI;
    color = ofsApplyAerial(color, ofsScreenUv(gl_FragCoord), -toEye / max(viewDistance, 1.0e-4), viewDistance);
    gl_FragData[0] = vec4(color, v_color.a);
    gl_FragData[1] = vec4(min(viewDistance / u_cameraForward.w, 65000.0), 0.0, 0.0, 1.0);
}
