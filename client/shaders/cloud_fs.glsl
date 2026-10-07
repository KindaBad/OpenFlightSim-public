// Volumetric clouds.
//
// A ray march through a cumulus layer whose shape comes from a Perlin-Worley
// volume carved by a weather map (Schneider 2015), with a thin cirrus sheet
// above it. Lighting follows Hillaire (2016): energy-conserving integration,
// a short secondary march toward the sun, and a few octaves of diminishing
// extinction standing in for multiple scattering. Rendered at reduced
// resolution with a per-frame jitter; cloud_resolve_fs accumulates the result.

$input v_uv
#include <bgfx_shader.sh>
#include "common.glsl"

SAMPLER3D(s_cloudShape, 11);
SAMPLER3D(s_cloudDetail, 12);
SAMPLER2D(s_sceneRange, 13);
uniform mat4 u_ofsInvViewProj;
uniform vec4 u_cloudRender; // x view steps, y light steps, z detail amount

#define SHAPE_TILE 4480.0
#define DETAIL_TILE 470.0

// Altitude above the curved Earth after travelling t along a ray. The world is
// flat for navigation, but the layer must still dip to the horizon.
float altitudeAlong(float eye, float rayY, float t)
{
    return eye + rayY * t + t * t / (2.0 * u_atmoGeometry.x);
}

// Both distances at which the ray is at `level`, or a negative pair if never.
vec2 levelCrossings(float eye, float rayY, float level)
{
    float discriminant = rayY * rayY - 2.0 * (eye - level) / u_atmoGeometry.x;
    if (discriminant < 0.0) {
        return vec2(-1.0, -1.0);
    }
    float root = sqrt(discriminant);
    return u_atmoGeometry.x * vec2(-rayY - root, -rayY + root);
}

float henyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    float d = max(1.0 + g2 - 2.0 * g * cosTheta, 1.0e-4);
    return (1.0 - g2) / (4.0 * OFS_PI * d * sqrt(d));
}

float cloudDensity(vec3 p, float heightFraction, vec4 weather, float lod, float detailAmount)
{
    if (heightFraction <= 0.0 || heightFraction >= 1.0) {
        return 0.0;
    }
    float cover = ofsCloudCover(weather);
    if (cover <= 0.001) {
        return 0.0;
    }
    // The shape drifts slightly faster than the weather and shears with height.
    vec3 q = vec3(p.x - u_cloudWeather.x * 1.25 - heightFraction * 180.0, p.y,
                  p.z - u_cloudWeather.y * 1.25);
    // Two scales of the same volume: the body of the cloud and the cauliflower
    // lumps on it.
    float shape = texture3DLod(s_cloudShape, q.xzy / SHAPE_TILE, lod).r * 0.70
                + texture3DLod(s_cloudShape, q.xzy / (SHAPE_TILE * 0.36) + vec3_splat(0.37), lod).r * 0.30;
    // Cloud type: flat stratocumulus that use the lower part of the layer, up
    // to towering cumulus that fill it. A cell is tallest at its centre, and
    // its top follows the noise, so tops are domed rather than a level deck.
    float kind = ofsSaturate(weather.g * 0.85 + u_cloudLayer.x * 0.25 + u_cloudShape.w);
    float reach = mix(0.40, 1.0, kind) * mix(0.30, 1.0, cover) * mix(0.55, 1.0, smoothstep(0.30, 0.72, shape));
    float h = heightFraction / reach;
    float profile = smoothstep(0.0, 0.10, h) * (1.0 - smoothstep(0.45, 1.0, h));
    // Coverage sets how much of the noise survives: at full cover the layer is
    // continuous, below it only the cores of the noise remain as separate cells.
    float keep = cover * 0.86;
    float base = ofsSaturate((shape * profile - (1.0 - keep)) / max(keep, 0.02));
    if (base <= 0.0) {
        return 0.0;
    }
    if (detailAmount > 0.0) {
        float fine = texture3DLod(s_cloudDetail, q.xzy / DETAIL_TILE, lod).r;
        // Wisps at the base, billows at the top.
        float erosion = mix(fine, 1.0 - fine, ofsSaturate(h * 3.0)) * 0.52 * detailAmount;
        base = ofsSaturate((base - erosion) / (1.0 - erosion));
    }
    return base * mix(0.7, 1.25, ofsSaturate(h)) * (0.65 + 0.5 * weather.a);
}

void main()
{
    vec2 ndc = ofsUvToNdc(v_uv);
    vec4 farPoint = mul(u_ofsInvViewProj, vec4(ndc, 1.0, 1.0));
    vec4 nearPoint = mul(u_ofsInvViewProj, vec4(ndc, 0.0, 1.0));
    vec3 ray = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
    vec3 eye = u_cameraPos.xyz + u_worldOrigin.xyz;
    vec3 sun = u_sunDirection.xyz;

    float sceneRange = texture2DLod(s_sceneRange, v_uv, 0.0).r * u_cameraForward.w;
    float limit = min(sceneRange, u_cloudShape.z);
    float base = u_cloudLayer.y;
    float thickness = u_cloudLayer.z;
    float top = base + thickness;

    // The layer is the region above `base` and below `top`; altitude along the
    // ray is a parabola, so that is the top's interval minus the base's.
    vec2 topCross = levelCrossings(eye.y, ray.y, top);
    vec2 baseCross = levelCrossings(eye.y, ray.y, base);
    float enter = 0.0;
    float leave = -1.0;
    if (topCross.y > 0.0) {
        enter = max(topCross.x, 0.0);
        leave = topCross.y;
        if (baseCross.y > 0.0) {
            if (baseCross.x > enter) {
                leave = baseCross.x;
            } else {
                enter = max(baseCross.y, enter);
            }
        }
    }
    leave = min(leave, limit);

    float cosTheta = dot(ray, sun);
    vec3 sunLight = atmoSunIrradiance(base + thickness * 0.5, sun.y);
    vec3 skyAbove = ofsAmbientIrradiance(vec3(0.0, 1.0, 0.0)) / OFS_PI;
    vec3 skyBelow = ofsAmbientIrradiance(vec3(0.0, -1.0, 0.0)) / OFS_PI;
    // Forward peak, a broad lobe and a little back-scatter for silver linings.
    float phase0 = mix(henyeyGreenstein(cosTheta, 0.82), henyeyGreenstein(cosTheta, -0.28), 0.32);
    float phase1 = henyeyGreenstein(cosTheta, 0.40);
    float phase2 = 1.0 / (4.0 * OFS_PI);
    float extinction = u_cloudWeather.z;

    vec3 radiance = vec3_splat(0.0);
    float through = 1.0;
    float depthSum = 0.0;
    float weightSum = 0.0;

    if (u_cloudLayer.w > 0.5 && leave > enter) {
        float steps = u_cloudRender.x;
        float span = leave - enter;
        // Samples bunch toward the camera on long grazing paths and stay even
        // on short steep ones.
        float curve = max(log(1.0 + span / 9000.0), 0.02);
        float normaliser = 1.0 / (exp(curve) - 1.0);
        float jitter = fract(ofsDither(gl_FragCoord.xy) + u_misc.x);
        for (int i = 0; i < 96; ++i) {
            if (float(i) >= steps || through < 0.012) {
                break;
            }
            float a = enter + span * (exp(curve * float(i) / steps) - 1.0) * normaliser;
            float b = enter + span * (exp(curve * float(i + 1) / steps) - 1.0) * normaliser;
            float t = mix(a, b, jitter);
            float stepLength = b - a;
            float altitude = altitudeAlong(eye.y, ray.y, t);
            float heightFraction = (altitude - base) / thickness;
            vec3 p = vec3(eye.x + ray.x * t, altitude, eye.z + ray.z * t);
            vec4 weather = texture2DLod(s_weatherMap, ofsWeatherUv(p.xz), 0.0);
            if (ofsCloudCover(weather) < 0.005) {
                continue;
            }
            float lod = clamp(log2(max(t, 1.0) / 9000.0), 0.0, 4.0);
            float density = cloudDensity(p, heightFraction, weather, lod, u_cloudRender.z);
            if (density < 0.002) {
                continue;
            }

            // Optical depth toward the sun, with steps that lengthen as they go.
            float sunDepth = 0.0;
            float reach = thickness * 0.045;
            float travelled = 0.0;
            for (int j = 0; j < 6; ++j) {
                if (float(j) >= u_cloudRender.y) {
                    break;
                }
                travelled += reach * 0.5;
                vec3 lp = p + sun * travelled;
                float lightFraction = (lp.y - base) / thickness;
                if (lightFraction < 1.0 && lightFraction > 0.0) {
                    vec4 lightWeather = texture2DLod(s_weatherMap, ofsWeatherUv(lp.xz), 1.0);
                    sunDepth += cloudDensity(lp, lightFraction, lightWeather, lod + 1.0, j < 2 ? u_cloudRender.z : 0.0) * reach;
                }
                travelled += reach * 0.5;
                reach *= 1.9;
            }
            sunDepth *= extinction;
            // Single scattering, then two broader, far less attenuated terms
            // standing in for the higher orders. Water droplets absorb almost
            // nothing, so inside a dense cloud the diffuse field carries most of
            // the energy: a sunlit cumulus face returns close to E / pi, which
            // is what makes it white rather than grey. Thin wisps have no
            // multiple scattering to speak of, so that term grows with density.
            float diffusion = 1.0 - exp(-density * 5.0);
            vec3 direct = sunLight * (phase0 * exp(-sunDepth)
                                      + 0.55 * phase1 * exp(-sunDepth * 0.30)
                                      + (3.4 * diffusion) * phase2 * exp(-sunDepth * 0.17));
            // Sky light reaches the tops and sides; the bases see the ground.
            float open = exp(-density * 2.6) * 0.62 + 0.38;
            vec3 ambient = mix(skyBelow * 0.8, skyAbove, ofsSaturate(heightFraction * 1.2)) * open
                         * (0.35 + 0.65 * ofsSaturate(heightFraction + 0.25));
            float sigma = density * extinction;
            float stepThrough = exp(-sigma * stepLength);
            float weight = through * (1.0 - stepThrough);
            radiance += (direct + ambient) * weight;
            depthSum += weight * t;
            weightSum += weight;
            through *= stepThrough;
        }
    }

    // ---- Cirrus --------------------------------------------------------------
    if (u_cloudShape.x > 0.005) {
        vec2 sheet = levelCrossings(eye.y, ray.y, u_cloudShape.y);
        float t = eye.y < u_cloudShape.y ? sheet.y : sheet.x;
        if (t > 0.0 && t < min(sceneRange, 260000.0)) {
            vec2 at = vec2(eye.x + ray.x * t, eye.z + ray.z * t) - u_cloudWeather.xy * 2.2;
            float fibres = texture2DLod(s_weatherMap, at / 71000.0, 0.0).b;
            float patches = texture2DLod(s_weatherMap, at / 173000.0 + 0.37, 0.0).a;
            float wisps = texture2DLod(s_noise, at / 9100.0, 0.0).g;
            float cover = smoothstep(1.0 - u_cloudShape.x * 1.15, 1.25 - u_cloudShape.x, patches * 0.6 + fibres * 0.5);
            float depth = cover * (0.25 + 0.75 * fibres) * (0.55 + 0.45 * wisps) * 0.42;
            // A grazing ray passes through more ice.
            float slant = 1.0 / max(abs(ray.y + t / u_atmoGeometry.x), 0.16);
            float opacity = (1.0 - exp(-depth * slant)) * (1.0 - smoothstep(140000.0, 250000.0, t));
            vec3 light = atmoSunIrradiance(u_cloudShape.y, sun.y)
                       * (henyeyGreenstein(cosTheta, 0.72) * 0.55 + 0.045) + skyAbove * 0.55;
            if (weightSum > 0.0 && t < depthSum / weightSum) {
                radiance = light * opacity + radiance * (1.0 - opacity);
            } else {
                radiance += light * opacity * through;
            }
            depthSum += t * opacity * through;
            weightSum += opacity * through;
            through *= 1.0 - opacity;
        }
    }

    float opacity = 1.0 - through;
    float meanDepth = weightSum > 1.0e-5 ? depthSum / weightSum : u_cloudShape.z;
    // Haze between the eye and the cloud, applied to the cloud's own light.
    vec3 airThrough = atmoAerialTransmittance(eye.y, ray.y, meanDepth);
    radiance = radiance * airThrough + atmoAerialInscatter(v_uv, meanDepth) * opacity;

    gl_FragData[0] = vec4(radiance, opacity);
    gl_FragData[1] = vec4(meanDepth / u_cameraForward.w, sceneRange < u_cloudShape.z ? 1.0 : 0.0, 0.0, 1.0);
}
