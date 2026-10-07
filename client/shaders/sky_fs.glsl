// Sky and far horizon.
//
// A full-screen triangle behind all geometry. Above the horizon it reads the
// sky-view table and adds the solar disc. Below, the simulated world is an
// unbounded plane: the shader shades that plane with the scene's mean terrain
// albedo and the same aerial perspective the terrain mesh receives, so the land
// fades into the sky without a visible edge at any altitude.

$input v_uv
#include <bgfx_shader.sh>
#include "common.glsl"

uniform mat4 u_ofsInvViewProj;

void main()
{
    vec2 ndc = ofsUvToNdc(v_uv);
    vec4 farPoint = mul(u_ofsInvViewProj, vec4(ndc, 1.0, 1.0));
    vec4 nearPoint = mul(u_ofsInvViewProj, vec4(ndc, 0.0, 1.0));
    vec3 ray = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
    float eye = max(u_ozone.w, 1.0);
    vec3 sun = u_sunDirection.xyz;

    vec3 sky = atmoSkyRadiance(vec3(ray.x, max(ray.y, 0.0), ray.z));
    vec3 color = sky;
    if (ray.y < 0.0) {
        float range = eye / max(-ray.y, 1.0e-6);
        vec3 world = u_cameraPos.xyz + u_worldOrigin.xyz + ray * range;
        vec3 light = atmoSunIrradiance(0.0, sun.y) * max(sun.y, 0.0) * ofsCloudShadow(vec3(world.x, 0.0, world.z))
                   + ofsAmbientIrradiance(vec3(0.0, 1.0, 0.0));
        vec3 ground = u_groundAlbedo.rgb * light / OFS_PI;
        float reach = min(range, u_groundAlbedo.w);
        vec3 hazed = ofsApplyAerial(ground, v_uv, ray, reach);
        // Past the atlas the remaining air is filled with horizon sky.
        vec3 beyond = atmoAerialTransmittance(eye, ray.y, range) / max(atmoAerialTransmittance(eye, ray.y, reach), vec3_splat(1.0e-6));
        color = mix(sky, hazed, beyond);
    } else {
        // Solar disc with limb darkening, seen through the air along the view.
        float cosAngle = dot(ray, sun);
        float sunCos = cos(OFS_SUN_ANGULAR_RADIUS);
        float edge = max(fwidth(cosAngle), 1.0e-7);
        float disc = smoothstep(sunCos - edge, sunCos + edge, cosAngle);
        if (disc > 0.0) {
            float radial = ofsSaturate((1.0 - cosAngle) / (1.0 - sunCos));
            float limb = 1.0 - 0.6 * (1.0 - sqrt(max(1.0 - radial, 0.0)));
            vec3 irradiance = u_solar.rgb * atmoTransmittanceToSpace(eye, max(ray.y, atmoHorizonCos(eye) + 1.0e-4));
            // E / solid angle would be ~150 000; the target is 16-bit float and
            // the glare is produced optically by bloom, so cap the stored value.
            color += min(irradiance * (limb / 6.87e-5), vec3_splat(900.0)) * disc;
        }
    }

    gl_FragData[0] = vec4(color, 1.0);
    gl_FragData[1] = vec4(65000.0, 0.0, 0.0, 1.0);
}
