// Forward PBR surface: aircraft, airfield buildings and other authored geometry.
// Lit by the atmosphere's sun and sky, cascaded sun shadows and cloud shadows,
// then seen through aerial perspective.

$input v_worldPos, v_normal, v_uv, v_surfacePos, v_tangent
#include <bgfx_shader.sh>
#include "common.glsl"

uniform vec4 u_baseColor;         // rgb + alpha
uniform vec4 u_metallicRoughness; // x metallic, y roughness, z procedural detail mode
uniform vec4 u_emissive;          // rgb
uniform vec4 u_doubleSided;       // x
SAMPLER2D(s_baseColor, 1);
SAMPLER2D(s_metallicRoughness, 2);
SAMPLER2D(s_emissive, 3);
SAMPLER2D(s_normal, 4);
SAMPLER2D(s_occlusion, 5);
uniform vec4 u_normalSettings;    // x normal scale, y glass reflectance, z occlusion strength
uniform vec4 u_textureFlags;      // base, metallic-roughness, emissive, normal present
uniform vec4 u_alphaSettings;     // x alpha mask, y cutoff, z alpha blended

void main()
{
    vec3 geometric = normalize(v_normal);
    vec3 n = geometric;
    // Authored tangent frame takes precedence; derivative frame is the fallback.
    if (u_textureFlags.w > 0.5) {
        vec3 t = vec3_splat(0.0);
        float hand = 1.0;
        if (dot(v_tangent.xyz, v_tangent.xyz) > 1.0e-10) {
            t = v_tangent.xyz;
            hand = v_tangent.w;
        } else {
            vec3 dx = dFdx(v_worldPos);
            vec3 dy = dFdy(v_worldPos);
            vec2 ux = dFdx(v_uv);
            vec2 uy = dFdy(v_uv);
            float determinant = ux.x * uy.y - ux.y * uy.x;
            if (abs(determinant) > 1.0e-10) {
                t = (dx * uy.y - dy * ux.y) / determinant;
                hand = sign(determinant);
            }
        }
        t = t - n * dot(n, t);
        if (dot(t, t) > 1.0e-10) {
            t = normalize(t);
            vec3 b = normalize(cross(n, t)) * hand;
            vec3 mapped = texture2D(s_normal, v_uv).xyz * 2.0 - 1.0;
            mapped.xy *= u_normalSettings.x;
            n = normalize(t * mapped.x + b * mapped.y + n * mapped.z);
        }
    }
    // Reverse the complete mapped normal: this is equivalent to reversing
    // all three TBN basis vectors for the back face, including tangential tilt.
    if (u_doubleSided.x > 0.5 && !gl_FrontFacing) {
        n = -n;
        geometric = -geometric;
    }
    vec3 toEye = u_cameraPos.xyz - v_worldPos;
    float viewDistance = length(toEye);
    vec3 v = toEye / max(viewDistance, 1.0e-4);
    // The same thin canopy is visible from either side. Orient its normal
    // toward the viewer so an interior view does not become a grazing mirror.
    if (u_normalSettings.y > 0.001 && dot(n, v) < 0.0) {
        n = -n;
        geometric = -geometric;
    }

    vec4 mrTexel = texture2D(s_metallicRoughness, v_uv);
    float metallic = ofsSaturate(u_metallicRoughness.x * mix(1.0, mrTexel.b, u_textureFlags.y));
    float roughness = clamp(u_metallicRoughness.y * mix(1.0, mrTexel.g, u_textureFlags.y), 0.045, 1.0);
    vec4 baseTexel = texture2D(s_baseColor, v_uv);
    vec3 albedo = u_baseColor.rgb * mix(vec3_splat(1.0), srgbToLinear(baseTexel.rgb), u_textureFlags.x);
    float alpha = u_baseColor.a * mix(1.0, baseTexel.a, u_textureFlags.x);
    if (u_alphaSettings.x > 0.5 && alpha < u_alphaSettings.y) {
        discard;
    }

    vec3 world = v_worldPos + u_worldOrigin.xyz;
    float occlusion = mix(1.0, texture2D(s_occlusion, v_uv).r, u_normalSettings.z);
    if (u_metallicRoughness.z > 0.5) {
        // Untextured airfield structures: weathering from the shared noise
        // tile, projected on the dominant axis so it does not stretch.
        vec3 weight = abs(geometric);
        vec2 plane = weight.y > 0.6 ? world.xz : (weight.x > weight.z ? world.zy : world.xy);
        float broad = texture2D(s_noise, plane * 0.013).r;
        float fine = texture2D(s_noise, plane * 0.19).g;
        float streaks = texture2D(s_noise, vec2(plane.x * 0.31, plane.y * 0.017)).a;
        float grime = mix(1.0, 0.72 + 0.42 * streaks, weight.y > 0.6 ? 0.25 : 0.8);
        albedo *= (0.80 + 0.26 * broad + 0.14 * fine) * grime;
        roughness = clamp(roughness + (fine - 0.5) * 0.18, 0.08, 1.0);
        if (u_metallicRoughness.z > 1.5) {
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

    if (u_normalSettings.y > 0.001) {
        // Canopy and sensor glass: a thin dielectric. The reflection is added
        // on top of the transmitted scene, and opacity rises toward grazing
        // angles as the reflection takes over.
        float nDotV = max(dot(n, v), 0.0);
        float fresnel = (0.045 + 0.955 * pow(ofsSaturate(1.0 - nDotV), 5.0)) * u_normalSettings.y;
        vec3 reflected = reflect(-v, n);
        vec3 h = normalize(v + u_sunDirection.xyz + vec3(0.0, 1.0e-5, 0.0));
        float glint = ofsDistributionGGX(ofsSaturate(dot(n, h)), 0.0036)
                    * ofsVisibilitySmith(max(nDotV, 1.0e-3), ofsSaturate(nDotL), 0.0036);
        vec3 mirror = atmoSkyRadiance(reflected) * fresnel + sunLight * (glint * fresnel * ofsSaturate(nDotL));
        float opacity = alpha + (1.0 - alpha) * ofsSaturate(fresnel);
        color = (color * alpha + mirror) / max(opacity, 1.0e-3);
        alpha = opacity;
    }
    color += u_emissive.rgb * mix(vec3_splat(1.0), srgbToLinear(texture2D(s_emissive, v_uv).rgb), u_textureFlags.z)
           * u_sunDirection.w;

    color = ofsApplyAerial(color, ofsScreenUv(gl_FragCoord), -v, viewDistance);

    // Linear scene radiance; exposure and the display transform are applied
    // once, after the whole frame has been composited.
    gl_FragData[0] = vec4(color, alpha);
    // Blended surfaces leave the opaque range behind them untouched.
    gl_FragData[1] = vec4(min(viewDistance / u_cameraForward.w, 65000.0), 0.0, 0.0, u_alphaSettings.z > 0.5 ? 0.0 : 1.0);
}
