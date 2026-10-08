// Tree shading: foliage as a translucent, self-shadowing volume and bark as a
// plain rough surface. The crown's own depth is approximated from the height
// within the tree and the direction of the normal. Close to, the smooth masses
// the mesh is built from are broken into sprays of leaves: the surface is cut
// away between them, most of all toward its outline, so a crown has a ragged
// edge and sky showing through it.

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
    vec3 facing = normalize(v_normal);
    vec2 plane = abs(facing.y) > 0.7 ? world.xz : (abs(facing.x) > abs(facing.z) ? world.zy : world.xy);
    float cluster = texture2D(s_noise, plane * 0.21).b;
    float mottle = texture2D(s_noise, plane * 0.047).r;
    float spray = texture2D(s_noise, plane * 0.53 + 0.37).b;
    // Sprays of leaves, gone by the distance at which they would only shimmer.
    float leafy = foliage * (1.0 - smoothstep(350.0, 700.0, viewDistance)) * step(0.5, u_quality.x);
    float outline = 1.0 - abs(dot(facing, v));
    float cover = smoothstep(-0.10, 0.10, cluster * 0.55 + spray * 0.45 - mix(0.20, 0.60, outline * outline));
    cover = mix(1.0, cover, leafy);
    if (cover < 0.3) {
        discard;
    }
    n = normalize(n + (vec3(cluster, mottle, 1.0 - spray) - 0.5) * mix(0.55, 1.1, leafy) * foliage);

    vec3 broadleaf = mix(vec3(0.040, 0.085, 0.020), vec3(0.100, 0.155, 0.034), tint);
    vec3 needles = mix(vec3(0.018, 0.046, 0.020), vec3(0.038, 0.072, 0.028), tint);
    vec3 leaf = mix(broadleaf, needles, conifer) * (0.62 + 0.76 * cluster);
    // Each spray stands out from the shade behind it, the sunlit ones yellower.
    leaf *= mix(vec3_splat(1.0), mix(vec3(0.50, 0.55, 0.60), vec3(1.30, 1.22, 0.85), smoothstep(0.25, 0.75, spray)), leafy);
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
    gl_FragData[0] = vec4(color, cover);
    gl_FragData[1] = vec4(min(viewDistance / u_cameraForward.w, 65000.0), 0.0, 0.0, 1.0);
}
