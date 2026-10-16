// The round Earth's buildings: the PBR surface shader's untextured path, with
// each part's look from its vertices instead of uniforms. v_color is the
// linear albedo and glass, v_params metallic, roughness, detail (1 weathered,
// 2 clad) and emissive strength.

$input v_worldPos, v_normal, v_color, v_params
#include <bgfx_shader.sh>
#include "common.glsl"

void main()
{
    vec3 geometric = normalize(v_normal);
    vec3 n = geometric;
    vec3 toEye = u_cameraPos.xyz - v_worldPos;
    float viewDistance = length(toEye);
    vec3 v = toEye / max(viewDistance, 1.0e-4);
    float glass = v_color.a;
    if (glass > 0.001 && dot(n, v) < 0.0) {
        n = -n;
        geometric = -geometric;
    }

    vec3 albedo = v_color.rgb;
    float metallic = v_params.x;
    float roughness = clamp(v_params.y, 0.045, 1.0);
    float detail = v_params.z;
    vec3 world = v_worldPos + u_worldOrigin.xyz;
    float occlusion = 1.0;
    if (detail > 0.5) {
        // Weathering from the shared noise tile, projected on the dominant
        // axis so it does not stretch.
        vec3 weight = abs(geometric);
        vec2 plane = weight.y > 0.6 ? world.xz : (weight.x > weight.z ? world.zy : world.xy);
        float broad = texture2D(s_noise, plane * 0.013).r;
        float fine = texture2D(s_noise, plane * 0.19).g;
        float streaks = texture2D(s_noise, vec2(plane.x * 0.31, plane.y * 0.017)).a;
        float grime = mix(1.0, 0.72 + 0.42 * streaks, weight.y > 0.6 ? 0.25 : 0.8);
        albedo *= (0.80 + 0.26 * broad + 0.14 * fine) * grime;
        roughness = clamp(roughness + (fine - 0.5) * 0.18, 0.08, 1.0);
        if (detail > 1.5) {
            // Cladding: panel seams every few metres on walls.
            vec2 panel = abs(fract(plane / vec2(6.0, 3.6)) - 0.5);
            float footprint = max(length(dFdx(plane)), length(dFdy(plane)));
            float seam = 1.0 - smoothstep(0.488 - footprint * 0.2, 0.497, max(panel.x, panel.y));
            albedo *= mix(1.0, 0.62 + 0.38 * seam, (1.0 - step(0.6, weight.y)) * exp(-footprint * 2.0));
        }
        // Wall bases are sheltered from the sky by the ground.
        occlusion *= mix(1.0, 0.78 + 0.22 * smoothstep(0.0, 2.5, world.y), 1.0 - step(0.6, weight.y));
    }

    roughness = ofsSpecularAntialias(n, roughness);
    float nDotL = dot(n, u_sunDirection.xyz);
    float shadow = ofsSunShadow(v_worldPos, geometric, ofsSaturate(dot(geometric, u_sunDirection.xyz)))
                 * ofsCloudShadow(world);
    vec3 sunLight = atmoSunIrradiance(world.y, u_sunDirection.y) * shadow;
    vec3 color = ofsShadeSurface(n, v, albedo, metallic, roughness, occlusion, sunLight, 1.0);

    if (glass > 0.001) {
        // Window glass: an opaque pane with the sky and sun mirrored on it.
        float nDotV = max(dot(n, v), 0.0);
        float fresnel = (0.045 + 0.955 * pow(ofsSaturate(1.0 - nDotV), 5.0)) * glass;
        vec3 reflected = reflect(-v, n);
        vec3 h = normalize(v + u_sunDirection.xyz + vec3(0.0, 1.0e-5, 0.0));
        float glint = ofsDistributionGGX(ofsSaturate(dot(n, h)), 0.0036)
                    * ofsVisibilitySmith(max(nDotV, 1.0e-3), ofsSaturate(nDotL), 0.0036);
        color += atmoSkyRadiance(reflected) * fresnel + sunLight * (glint * fresnel * ofsSaturate(nDotL));
    }
    color += albedo * v_params.w * u_sunDirection.w;

    color = ofsApplyAerial(color, ofsScreenUv(gl_FragCoord), -v, viewDistance);
    gl_FragData[0] = vec4(color, 1.0);
    gl_FragData[1] = vec4(min(viewDistance / u_cameraForward.w, 65000.0), 0.0, 0.0, 1.0);
}
