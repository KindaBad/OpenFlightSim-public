// Shared surface shading for the OpenFlightSim forward renderer: the
// microfacet BRDF, image-based sky lighting, cascaded sun shadows and cloud
// shadows. Portable bgfx shader language, compiled by the pinned shaderc for
// OpenGL and Direct3D 11.

#ifndef OFS_COMMON_GLSL
#define OFS_COMMON_GLSL

#include "atmosphere.glsl"

SAMPLER2DSHADOW(s_shadowAtlas, 0);
SAMPLER2D(s_weatherMap, 9);
SAMPLER2D(s_noise, 10);
uniform mat4 u_shadowMatrix[3];

float ofsSaturate(float v) { return clamp(v, 0.0, 1.0); }

vec3 srgbToLinear(vec3 encoded)
{
    vec3 cutoff = step(encoded, vec3_splat(0.04045));
    return mix(pow((encoded + 0.055) / 1.055, vec3_splat(2.4)), encoded / 12.92, cutoff);
}

vec3 linearToSrgb(vec3 linearColor)
{
    vec3 cutoff = step(linearColor, vec3_splat(0.0031308));
    vec3 low = linearColor * 12.92;
    vec3 high = 1.055 * pow(max(linearColor, vec3_splat(0.0)), vec3_splat(1.0 / 2.4)) - 0.055;
    return mix(high, low, cutoff);
}

// Interleaved gradient noise (Jimenez 2014): a stable per-pixel dither value.
float ofsDither(vec2 pixel)
{
    return fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
}

// --- Clouds as seen from the surface -----------------------------------------
// Fraction of sky covered above a weather-map sample. The cloud march uses the
// same mapping, so shadows fall where the clouds are drawn.
float ofsCloudCover(vec4 weather)
{
    float low = 1.0 - u_cloudLayer.x * 1.25;
    return smoothstep(low, low + 0.25, weather.r);
}

vec2 ofsWeatherUv(vec2 worldXZ) { return (worldXZ - u_cloudWeather.xy) * u_cloudWeather.w; }

// Sunlight remaining under the cloud layer at an absolute world position.
float ofsCloudShadow(vec3 world)
{
    float top = u_cloudLayer.y + u_cloudLayer.z;
    if (u_cloudLayer.w < 0.5 || u_sunIrradiance.w <= 0.0 || u_sunDirection.y < 0.03 || world.y > top) {
        return 1.0;
    }
    // Where the ray toward the sun crosses the densest part of the layer.
    float rise = max(u_cloudLayer.y + u_cloudLayer.z * 0.35 - world.y, 0.0);
    vec2 crossing = world.xz + u_sunDirection.xz * (rise / u_sunDirection.y);
    vec4 weather = texture2DLod(s_weatherMap, ofsWeatherUv(crossing), 1.5);
    float cover = ofsCloudCover(weather);
    // Thick cumulus still passes diffuse light; never fully black.
    float lit = mix(1.0, 0.16, cover * cover * (3.0 - 2.0 * cover));
    // Inside the layer the shadow fades in with depth below the tops.
    float depth = ofsSaturate((top - world.y) / max(u_cloudLayer.z * 0.6, 1.0));
    return mix(1.0, lit, u_sunIrradiance.w * depth);
}

// --- Sun shadows --------------------------------------------------------------
// Three stable cascades packed side by side in one depth atlas. The first
// cascade containing the point is used, cross-fading into the next near its edge.
float ofsShadowTaps(vec2 local, float depth, float index)
{
    float texel = u_shadowParams.y;
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 uv = clamp(local + vec2(float(x), float(y)) * texel, vec2_splat(texel), vec2_splat(1.0 - texel));
            sum += shadow2D(s_shadowAtlas, vec3((uv.x + index) / 3.0, uv.y, depth));
        }
    }
    return sum / 9.0;
}

float ofsSunShadow(vec3 worldPos, vec3 n, float nDotL)
{
    if (u_shadowParams.w <= 0.0 || u_shadowParams.x < 0.5) {
        return 1.0;
    }
    float lit = 0.0;
    float remaining = 1.0;
    for (int i = 0; i < 3; ++i) {
        if (float(i) >= u_shadowParams.x || remaining < 0.01) {
            break;
        }
        // Offsetting along the normal by a couple of texels removes acne on
        // grazing surfaces without the peter-panning of a large depth bias.
        float texelWorld = i == 0 ? u_shadowTexel.x : (i == 1 ? u_shadowTexel.y : u_shadowTexel.z);
        vec3 biased = worldPos + n * texelWorld * (1.2 + 2.2 * (1.0 - nDotL));
        vec4 coord = mul(u_shadowMatrix[i], vec4(biased, 1.0));
        vec2 local = vec2(coord.x * 3.0 - float(i), coord.y);
        vec2 margin = min(local, vec2_splat(1.0) - local);
        float inside = min(margin.x, margin.y);
        if (inside <= 0.0 || coord.z <= 0.0 || coord.z >= 1.0) {
            continue;
        }
        float weight = remaining * ofsSaturate(inside / u_shadowTexel.w);
        lit += weight * ofsShadowTaps(local, coord.z - u_shadowParams.z, float(i));
        remaining -= weight;
    }
    return mix(1.0, lit + remaining, u_shadowParams.w);
}

// --- Microfacet BRDF ----------------------------------------------------------
float ofsDistributionGGX(float nDotH, float alpha)
{
    float a2 = alpha * alpha;
    float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(OFS_PI * d * d, 1.0e-7);
}

// Height-correlated Smith visibility (Heitz 2014), including the 1/(4 NL NV).
float ofsVisibilitySmith(float nDotV, float nDotL, float alpha)
{
    float a2 = alpha * alpha;
    float gv = nDotL * sqrt(nDotV * nDotV * (1.0 - a2) + a2);
    float gl = nDotV * sqrt(nDotL * nDotL * (1.0 - a2) + a2);
    return 0.5 / max(gv + gl, 1.0e-5);
}

vec3 ofsFresnel(float cosTheta, vec3 f0)
{
    float f = pow(ofsSaturate(1.0 - cosTheta), 5.0);
    return f0 + (vec3_splat(1.0) - f0) * f;
}

// Split-sum environment BRDF, analytic fit (Karis 2014).
vec3 ofsEnvironmentBrdf(vec3 f0, float roughness, float nDotV)
{
    vec4 r = roughness * vec4(-1.0, -0.0275, -0.572, 0.022) + vec4(1.0, 0.0425, 1.04, -0.04);
    float a004 = min(r.x * r.x, exp2(-9.28 * nDotV)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return f0 * ab.x + vec3_splat(ab.y);
}

// Widens roughness where the shading normal varies faster than a pixel, which
// is what makes thin highlights crawl on curved panels (Kaplanyan 2016).
float ofsSpecularAntialias(vec3 n, float roughness)
{
    vec3 dx = dFdx(n);
    vec3 dy = dFdy(n);
    float variance = 0.25 * (dot(dx, dx) + dot(dy, dy));
    float kernel = min(2.0 * variance, 0.18) * u_quality.z;
    return sqrt(ofsSaturate(roughness * roughness + kernel));
}

// Full surface response: direct sun, sky diffuse and sky specular.
//   sunLight is the sun's irradiance at the surface, already shadowed.
//   occlusion scales ambient light; cavity additionally scales direct light.
vec3 ofsShadeSurface(vec3 n, vec3 v, vec3 albedo, float metallic, float roughness, float occlusion,
                     vec3 sunLight, float skyVisibility)
{
    vec3 l = u_sunDirection.xyz;
    // The offset keeps the half vector defined when the eye looks straight down-sun.
    vec3 h = normalize(v + l + vec3(0.0, 1.0e-5, 0.0));
    float nDotV = max(dot(n, v), 1.0e-4);
    float nDotL = ofsSaturate(dot(n, l));
    float nDotH = ofsSaturate(dot(n, h));
    float vDotH = ofsSaturate(dot(v, h));
    // The solar disc is half a degree wide, so even a perfect mirror has a
    // highlight of finite size.
    float alpha = max(roughness * roughness, 0.0022);
    vec3 f0 = mix(vec3_splat(0.04), albedo, metallic);
    vec3 diffuseColor = albedo * (1.0 - metallic);

    vec3 fresnel = ofsFresnel(vDotH, f0);
    vec3 specular = fresnel * (ofsDistributionGGX(nDotH, alpha) * ofsVisibilitySmith(nDotV, nDotL, alpha));
    vec3 environment = ofsEnvironmentBrdf(f0, roughness, nDotV);
    // Energy lost to single-scattering between microfacets is returned as a
    // brighter, more saturated lobe on rough metals (Fdez-Aguera 2019).
    vec3 energy = vec3_splat(1.0) + f0 * (1.0 / max(environment.x + environment.y, 0.05) - 1.0) * 0.5;
    vec3 direct = (diffuseColor * (vec3_splat(1.0) - fresnel) / OFS_PI + specular * energy) * sunLight * nDotL;

    vec3 ambientDiffuse = diffuseColor * ofsAmbientIrradiance(n) / OFS_PI * occlusion;
    vec3 reflected = reflect(-v, n);
    // A reflection that points into the surface would see the surface itself.
    float horizon = ofsSaturate(1.0 + dot(reflected, n));
    float specularOcclusion = ofsSaturate(pow(nDotV + occlusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + occlusion);
    vec3 ambientSpecular = atmoSkyReflection(reflected, roughness) * environment * energy
                         * (specularOcclusion * horizon * horizon);
    return direct + (ambientDiffuse + ambientSpecular) * skyVisibility;
}

#endif // OFS_COMMON_GLSL
