// Tree shading: foliage as a translucent, self-shadowing volume and bark as a
// plain rough surface. The crown's own depth is approximated from the height
// within the tree and the direction of the normal, since individual leaves are
// far below a pixel from the air.

$input v_worldPos, v_normal, v_uv, v_surfacePos
#include <bgfx_shader.sh>
#include "common.glsl"

void main()
{
    vec3 toEye = u_cameraPos.xyz - v_worldPos;
    float viewDistance = length(toEye);
    vec3 v = toEye / max(viewDistance, 1.0e-4);
    vec3 world = v_worldPos + u_worldOrigin.xyz;
    vec3 n = normalize(v_normal);
    float foliage = step(0.5, v_uv.x);
    float tint = v_surfacePos.x;
    float conifer = v_surfacePos.y;
    float crown = ofsSaturate(v_uv.y);

    // Leaf clusters: break up the smooth crown with the shared noise tile.
    vec2 plane = abs(n.y) > 0.7 ? world.xz : (abs(n.x) > abs(n.z) ? world.zy : world.xy);
    float cluster = texture2D(s_noise, plane * 0.21).b;
    float mottle = texture2D(s_noise, plane * 0.047).r;
    n = normalize(n + (vec3(cluster, mottle, 1.0 - cluster) - 0.5) * 0.55 * foliage);

    vec3 broadleaf = mix(vec3(0.030, 0.062, 0.016), vec3(0.068, 0.105, 0.026), tint);
    vec3 needles = mix(vec3(0.014, 0.036, 0.016), vec3(0.030, 0.058, 0.022), tint);
    vec3 leaf = mix(broadleaf, needles, conifer) * (0.62 + 0.76 * cluster);
    vec3 bark = vec3(0.105, 0.078, 0.055) * (0.7 + 0.6 * mottle);
    vec3 albedo = mix(bark, leaf, foliage);

    // The inside and underside of a crown see little sky.
    float occlusion = mix(1.0, (0.34 + 0.66 * crown) * (0.6 + 0.4 * ofsSaturate(n.y * 0.5 + 0.5)) * (0.7 + 0.3 * cluster), foliage);
    float nDotL = dot(n, u_sunDirection.xyz);
    // Wrapped diffuse: leaves scatter light around the terminator.
    float wrapped = ofsSaturate((nDotL + 0.35 * foliage) / (1.0 + 0.35 * foliage));
    float shadow = ofsSunShadow(v_worldPos, n, ofsSaturate(nDotL)) * ofsCloudShadow(world);
    vec3 sunLight = atmoSunIrradiance(world.y, u_sunDirection.y) * shadow;

    vec3 color = albedo * (sunLight * wrapped * mix(1.0, 0.45 + 0.55 * crown, foliage)
                           + ofsAmbientIrradiance(n) * occlusion) / OFS_PI;
    // Sun through the canopy: a bright, yellower rim when looking up-sun.
    float through = pow(ofsSaturate(dot(-v, u_sunDirection.xyz)), 4.0) * ofsSaturate(0.4 - nDotL);
    color += leaf * vec3(1.5, 1.35, 0.5) * sunLight * (through * 0.5 * foliage / OFS_PI);

    color = ofsApplyAerial(color, ofsScreenUv(gl_FragCoord), -v, viewDistance);
    gl_FragData[0] = vec4(color, 1.0);
    gl_FragData[1] = vec4(min(viewDistance / u_cameraForward.w, 65000.0), 0.0, 0.0, 1.0);
}
