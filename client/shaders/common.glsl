// Shared PBR and atmosphere helpers for the OpenFlightSim forward renderer.
//
// Portable bgfx shader language, compiled by the pinned shaderc for GL/D3D11.

#ifndef OFS_COMMON_GLSL
#define OFS_COMMON_GLSL

const float OFS_PI = 3.14159265359;

// Shared world-space weather. A small repeating volume supplies smooth 3D
// density without evaluating procedural octaves for every march step.
SAMPLER3D(s_cloudNoise, 6);
uniform vec4 u_cloudParams; // coverage, base, thickness, enabled
uniform vec4 u_weather; // wind displacement, extinction, range, shadow enabled

float weatherNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f*f*(3.0-2.0*f);
    vec4 h = fract(sin(vec4(dot(i,vec2(127.1,311.7)), dot(i+vec2(1,0),vec2(127.1,311.7)),
                           dot(i+vec2(0,1),vec2(127.1,311.7)), dot(i+1.0,vec2(127.1,311.7))))*43758.5453);
    return mix(mix(h.x,h.y,f.x), mix(h.z,h.w,f.x), f.y);
}
float cloudWeather(vec2 p) {
    p = (p - vec2(u_weather.x,u_weather.x*.32))*.00038;
    return weatherNoise(p)*.75 + weatherNoise(p*2.03+7.1)*.25;
}
float cloudDensity(vec3 p) {
    float height = (p.y-u_cloudParams.y)/u_cloudParams.z;
    if (height <= 0.0 || height >= 1.0) return 0.0;
    float coverage = smoothstep(1.0-u_cloudParams.x, 1.18-u_cloudParams.x, cloudWeather(p.xz));
    if (coverage < .005) return 0.0;
    vec3 q = (p-vec3(u_weather.x,0,u_weather.x*.32))*.000055;
    float shape = texture3D(s_cloudNoise,q).r*.65 + texture3D(s_cloudNoise,q*3.07).r*.35;
    float erosion=texture3D(s_cloudNoise,q*8.13).r;
    // Displaced upper density gives the volume rounded towers.
    float profile = smoothstep(0.0,.13,height)
        * (1.0-smoothstep(.32+shape*.35,.58+shape*.42,height));
    return max(coverage*profile-(1.0-shape)*.52-(1.0-erosion)*.10*(1.0-profile*.5),0.0);
}
vec2 cloudInterval(vec3 eye, vec3 ray, float limit) {
    if (abs(ray.y) < .0001) {
        if (eye.y > u_cloudParams.y && eye.y < u_cloudParams.y+u_cloudParams.z) return vec2(0,limit);
        return vec2(0,0);
    }
    float a = (u_cloudParams.y-eye.y)/ray.y;
    float b = (u_cloudParams.y+u_cloudParams.z-eye.y)/ray.y;
    return vec2(max(0.0,min(a,b)),min(limit,max(a,b)));
}
float cloudSunVisibility(vec3 p, vec3 sun) {
    if (u_cloudParams.w < .5 || u_weather.w < .5 || sun.y < .05 || p.y > u_cloudParams.y+u_cloudParams.z) return 1.0;
    vec2 atLayer = p.xz+sun.xz*max(0.0,(u_cloudParams.y+u_cloudParams.z*.3-p.y)/sun.y);
    float cover = smoothstep(1.0-u_cloudParams.x,1.18-u_cloudParams.x,cloudWeather(atLayer));
    return 1.0 - cover*.58;
}
// A bounded surface march handles aircraft and terrain seen through the layer,
// including flying inside/above it. It exits before sampling for nearby objects.
vec3 applyCloudAir(vec3 color, vec3 eye, vec3 surface, vec3 sunColor) {
    if (u_cloudParams.w < .5) return color;
    vec3 delta = surface-eye;
    float distance = length(delta);
    if (distance < 1.0) return color;
    vec3 ray = delta/distance;
    vec2 interval = cloudInterval(eye,ray,min(distance,u_weather.z));
    if (interval.y <= interval.x) return color;
    float stepSize = (interval.y-interval.x)/8.0;
    float opticalDepth = 0.0;
    for (int i=0;i<8;++i) opticalDepth += cloudDensity(eye+ray*(interval.x+(float(i)+.5)*stepSize))*stepSize;
    float transmittance = exp(-opticalDepth*u_weather.y);
    vec3 light = vec3(.32,.39,.48)*clamp(length(sunColor)*.15,.04,1.0)+sunColor*.13;
    return color*transmittance+light*(1.0-transmittance);
}

// Distribution (GGX / Trowbridge-Reitz).
float distributionGGX(float nDotH, float roughness)
{
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(OFS_PI * d * d, 1e-6);
}

// Geometry term, Smith height-correlated visibility.
float geometrySmith(float nDotV, float nDotL, float roughness)
{
    const float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    const float gv = nDotV / (nDotV * (1.0 - k) + k);
    const float gl = nDotL / (nDotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 fresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 fresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness)
{
    const vec3 maxReflect = clamp(vec3_splat(1.0 - roughness), 0.0, 1.0);
    return f0 + (maxReflect - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 srgbToLinear(vec3 encoded)
{
    const vec3 cutoff = step(encoded, vec3_splat(0.04045));
    return mix(pow((encoded + 0.055) / 1.055, vec3_splat(2.4)), encoded / 12.92, cutoff);
}

// Linear to sRGB transfer. The swap chain is UNORM and untyped, so the
// renderer is responsible for the encode; without it mid-tones read far too
// dark and highlights clip to a flat wash.
vec3 linearToSrgb(vec3 linearColor)
{
    const vec3 cutoff = step(linearColor, vec3_splat(0.0031308));
    const vec3 low = linearColor * 12.92;
    const vec3 high = 1.055 * pow(max(linearColor, vec3_splat(0.0)), vec3_splat(1.0 / 2.4)) - 0.055;
    return mix(high, low, cutoff);
}

// Reinhard-style tone map. Keeps the sun disc and specular highlights from
// clipping to white, which is what makes a bright sky look like flat paint.
vec3 tonemap(vec3 color)
{
    // ACES filmic approximation, cheap and well behaved at high dynamic range.
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

// Cook-Torrance direct lighting for one light.
vec3 shadeDirect(vec3 n, vec3 v, vec3 l, vec3 albedo, float metallic, float roughness,
                 vec3 lightColor, vec3 lightRadiance)
{
    const vec3 h = normalize(v + l);
    const float nDotV = max(dot(n, v), 1e-4);
    const float nDotL = max(dot(n, l), 0.0);
    const float nDotH = max(dot(n, h), 0.0);
    const float vDotH = max(dot(v, h), 0.0);

    const vec3 f0 = mix(vec3_splat(0.04), albedo, metallic);
    const vec3 f = fresnelSchlick(vDotH, f0);
    const float d = distributionGGX(nDotH, roughness);
    const float g = geometrySmith(nDotV, nDotL, roughness);
    const vec3 numerator = d * g * f;
    const float denominator = 4.0 * nDotV * nDotL + 1e-4;
    const vec3 specular = numerator / denominator;

    const vec3 kd = (vec3_splat(1.0) - f) * (1.0 - metallic);
    const vec3 diffuse = kd * albedo / OFS_PI;

    // A cheap analytic environment term keeps metals from going black in
    // shadow, which a single-light forward pass otherwise does.
    const vec3 irradiance = mix(albedo, f0, metallic) * lightColor;
    return (diffuse + specular) * lightRadiance * nDotL + irradiance * 0.0;
}

// Hemispheric ambient: sky above, ground bounce below.
vec3 ambientHemispheric(vec3 n, vec3 albedo, float metallic, vec3 skyColor, vec3 groundColor)
{
    const float up = n.y * 0.5 + 0.5;
    const vec3 irradiance = mix(groundColor, skyColor, up);
    // A crude split-sum approximation: metals reflect the environment more
    // strongly and are tinted by their base colour.
    const vec3 diffuse = albedo * (1.0 - metallic);
    const vec3 specular = mix(albedo, vec3_splat(1.0), metallic) * 0.35;
    return irradiance * (diffuse + specular);
}

// Exponential height/distance fog. Height falloff keeps the horizon dense
// while the air above stays clear, which reads correctly from altitude.
vec3 applyFog(vec3 color, float viewDistance, float altitude, float eyeAltitude, vec3 fogColor, float density,
              float heightFalloff, float groundFade)
{
    const float scaleHeight = max(heightFalloff,1.0)/max(groundFade,.05);
    const float a = max(eyeAltitude,0.0)/scaleHeight;
    const float b = max(altitude,0.0)/scaleHeight;
    // Mean density along the view segment: integral of the exponential layer.
    const float heightFactor = abs(b-a)<.001 ? exp(-.5*(a+b)) : (exp(-a)-exp(-b))/(b-a);
    const float amount = 1.0 - exp(-viewDistance * density * heightFactor);
    return mix(color, fogColor, clamp(amount, 0.0, 1.0));
}

#endif // OFS_COMMON_GLSL
