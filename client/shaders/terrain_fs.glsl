// Terrain surface: land cover, lakes and the paved airfield.
//
// The mesh is the simulation's collision surface and is never displaced. All
// extra detail is shading: a per-pixel normal from the same analytic height
// function, tiling material layers blended by slope, altitude and the baked
// land-cover map, lake water where a basin's level stands above the bed, and
// long shadows cast by the relief itself.

$input v_worldPos, v_normal, v_uv, v_surfacePos
#include <bgfx_shader.sh>
#include "common.glsl"

SAMPLER2DARRAY(s_terrainAlbedo, 1);
SAMPLER2DARRAY(s_terrainNormal, 2);
SAMPLER2D(s_landMap, 3);
SAMPLER2D(s_lakeMap, 4);
SAMPLER2D(s_waterNormal, 5);
uniform vec4 u_surface;   // x kind: 0 land, 1 asphalt, 2 paint, 3 concrete; y decal layer
uniform vec4 u_baseColor; // tint for paved kinds

#define LAYER_GRASS 0.0
#define LAYER_SOIL 1.0
#define LAYER_ROCK 2.0
#define LAYER_FOREST 3.0
#define LAYER_SNOW 4.0
#define LAYER_ASPHALT 5.0
#define LAND_EXTENT 96000.0

// Must match ofs::terrainElevation (core/include/ofs/terrain.hpp).
float terrainHeight(vec2 eastSouth)
{
    float east = eastSouth.x;
    float south = eastSouth.y;
    float radius = length(eastSouth);
    float t = clamp((radius - 3500.0) / 5000.0, 0.0, 1.0);
    float ramp = t * t * (3.0 - 2.0 * t);
    float mt = clamp((radius - 11000.0) / 9000.0, 0.0, 1.0);
    float mountainRamp = mt * mt * (3.0 - 2.0 * mt);
    float ridge = 1.0 - abs(sin(east * 0.00031 + sin(south * 0.00022) * 1.4));
    float peaks = mountainRamp * (500.0 + 1450.0 * ridge * ridge);
    return peaks + ramp * (380.0 + 240.0 * sin(east * 0.0008) * cos(south * 0.00065)
        + 140.0 * sin(south * 0.0013 + east * 0.0004)
        + 65.0 * sin(east * 0.0027 + south * 0.0011) * cos(south * 0.0023));
}

// Sine-free hash (Hoskins), stable across GPU vendors for integer-like input.
float cellHash(vec2 p)
{
    vec3 p3 = fract(vec3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 layerAlbedo(vec2 uv, float layer) { return texture2DArray(s_terrainAlbedo, vec3(uv, layer)).rgb; }

// A layer sampled a few mip levels coarser than the screen needs. The far tile
// only supplies broad mottling; its fine grain, enlarged fifteen times, would
// read as streaks.
vec3 layerBroad(vec2 uv, float layer, float texelsPerPixel)
{
    return texture2DArrayLod(s_terrainAlbedo, vec3(uv, layer), max(log2(max(texelsPerPixel, 1.0e-4)), 0.0) + 2.6).rgb;
}

// Fine grain only: the tile divided by a blurred copy of itself. What is left
// has no variation at the scale of the tile, so its repeat cannot be seen, and
// it fades to one on its own as the tile shrinks below a pixel.
vec3 layerGrain(vec2 uv, float layer)
{
    vec3 sharp = texture2DArray(s_terrainAlbedo, vec3(uv, layer)).rgb;
    vec3 blurred = texture2DArrayLod(s_terrainAlbedo, vec3(uv, layer), 5.0).rgb;
    return sharp / max(blurred, vec3_splat(0.004));
}

// A layer's mean colour, from the top of its mip chain.
vec3 layerMean(float layer) { return texture2DArrayLod(s_terrainAlbedo, vec3(0.5, 0.5, layer), 9.0).rgb; }

// Rotates tile coordinates off the world axes, so the repeat of one layer
// never lines up with another's or with the field boundaries.
vec2 turned(vec2 p, vec2 basis) { return vec2(p.x * basis.x - p.y * basis.y, p.x * basis.y + p.y * basis.x); }

// Tangent-space detail: xy slope, z roughness, w cavity occlusion.
vec4 layerDetail(vec2 uv, float layer)
{
    vec4 texel = texture2DArray(s_terrainNormal, vec3(uv, layer));
    return vec4(texel.xy * 2.0 - 1.0, texel.z, texel.w);
}

// Long shadows from the relief: walk toward the sun over the height function
// and keep the tightest clearance, which gives a soft edge for free.
float reliefShadow(vec2 p, float altitude)
{
    vec3 sun = u_sunDirection.xyz;
    float flatLength = length(sun.xz);
    if (u_quality.w < 0.5 || sun.y < 0.0 || flatLength < 1.0e-3) {
        return 1.0;
    }
    vec2 heading = sun.xz / flatLength;
    float climb = sun.y / flatLength;
    float clearance = 1.0;
    float reach = 90.0;
    for (int i = 0; i < 10; ++i) {
        if (float(i) >= u_quality.w) {
            break;
        }
        float rayHeight = altitude + climb * reach + 4.0;
        clearance = min(clearance, (rayHeight - terrainHeight(p + heading * reach)) / reach);
        reach *= 1.75;
    }
    return smoothstep(-0.004, 0.022, clearance);
}

void main()
{
    vec2 p = v_surfacePos.xz;
    float altitude = v_surfacePos.y;
    vec3 toEye = u_cameraPos.xyz - v_worldPos;
    float viewDistance = length(toEye);
    vec3 v = toEye / max(viewDistance, 1.0e-4);
    vec3 world = v_worldPos + u_worldOrigin.xyz;
    float detailLevel = u_quality.x;
    // Screen-space footprint of the ground, taken before any branching.
    vec2 dpx = dFdx(p);
    vec2 dpy = dFdy(p);
    float footprint = max(length(dpx), length(dpy));

    vec3 n = normalize(v_normal);
    vec3 albedo = vec3_splat(0.1);
    float roughness = 0.9;
    float occlusion = 1.0;
    float waterDepth = -1.0;
    float waterLevel = -10000.0;
    float macroShadow = 1.0;
    vec2 slopeDetail = vec2_splat(0.0);

    if (u_surface.x < 0.5) {
        // ---- Natural land ----------------------------------------------------
        float bed = terrainHeight(p);
        if (detailLevel > 0.5) {
            float d = 3.0;
            float hx = terrainHeight(p + vec2(d, 0.0));
            float hz = terrainHeight(p + vec2(0.0, d));
            n = normalize(vec3(bed - hx, d, bed - hz));
        }
        vec4 land = texture2D(s_landMap, p / LAND_EXTENT + 0.5);
        vec4 macro = texture2D(s_noise, p / 1730.0);
        vec4 meso = texture2D(s_noise, p / 171.0);
        float steep = 1.0 - n.y;

        float rock = smoothstep(0.20, 0.40, steep + (land.a - 0.5) * 0.20 + (meso.r - 0.5) * 0.12
                                 + smoothstep(1500.0, 2200.0, altitude) * 0.22);
        float snow = smoothstep(1620.0, 1930.0, altitude + (macro.g - 0.5) * 520.0 + (meso.b - 0.5) * 120.0)
                   * (1.0 - smoothstep(0.42, 0.68, steep));
        float forest = smoothstep(0.34, 0.62, land.r + (meso.g - 0.5) * 0.34) * (1.0 - rock) * (1.0 - snow);
        float farm = land.g;
        float dry = smoothstep(0.30, 0.72, macro.r * 0.55 + (1.0 - land.b) * 0.55 + smoothstep(700.0, 1500.0, altitude) * 0.35);

        // Two scales per layer hide the tile repeat: a near tile for the
        // ground under the aircraft and a far tile that carries to the horizon.
        vec2 nearUv = p / 6.3;
        vec2 farUv = turned(p, vec2(0.7986, 0.6018)) / 97.0;
        vec2 stoneUv = turned(p, vec2(0.9455, -0.3256)) / 143.0;
        vec2 canopyUv = turned(p, vec2(0.6157, 0.7880)) / 61.0;
        float nearWeight = detailLevel > 0.5 ? exp(-viewDistance / 520.0) : 0.0;
        // Past a few kilometres a tile is smaller than a pixel and only its
        // repeat would show; hand over to the layer's mean colour and let the
        // land-cover noise carry the variation.
        float distant = smoothstep(1500.0, 7000.0, viewDistance);
        float farTexels = footprint * 512.0 / 97.0;
        vec3 grass = mix(layerBroad(farUv, LAYER_GRASS, farTexels), layerMean(LAYER_GRASS), distant);
        vec3 stone = mix(layerAlbedo(stoneUv, LAYER_ROCK), layerMean(LAYER_ROCK), distant * 0.6);
        if (detailLevel > 0.5) {
            grass *= mix(vec3_splat(1.0), layerGrain(nearUv, LAYER_GRASS), nearWeight);
            stone *= mix(vec3_splat(1.0), layerGrain(p / 11.0, LAYER_ROCK), nearWeight);
        }
        vec3 soil = mix(layerBroad(p / 23.0, LAYER_SOIL, footprint * 512.0 / 23.0), layerMean(LAYER_SOIL), distant);
        vec3 canopy = mix(layerAlbedo(canopyUv, LAYER_FOREST), layerMean(LAYER_FOREST) * 1.15,
                          smoothstep(900.0, 4500.0, viewDistance));

        // Meadow: lush to dry with moisture, never one flat green.
        vec3 meadow = grass * mix(vec3(0.98, 1.16, 0.86), vec3(1.65, 1.30, 0.80), dry);
        meadow *= 0.80 + 0.46 * macro.a;
        // Mown and unmown patches, clover and bare earth at walking scale.
        meadow *= 0.88 + 0.24 * texture2D(s_noise, p / 37.0).g;
        meadow = mix(meadow, soil, smoothstep(0.62, 0.86, meso.a + dry * 0.2) * 0.55);

        // Farmland: an irregular patchwork with a crop, a lay and a boundary
        // hedge per field.
        if (farm > 0.01) {
            vec2 warp = (macro.rg - 0.5) * 260.0;
            vec2 fieldPos = (p + warp) / vec2(410.0, 290.0);
            vec2 cell = floor(fieldPos);
            float crop = cellHash(cell);
            float lay = cellHash(cell + 17.0);
            // Mostly pasture and green crops, some ripening grain, the odd
            // ploughed field: muted, as fields are from the air.
            vec3 cropColor = crop < 0.42 ? grass * vec3(0.98, 1.12, 0.84)
                           : (crop < 0.66 ? vec3(0.058, 0.094, 0.030)
                           : (crop < 0.86 ? vec3(0.185, 0.160, 0.078) : soil * vec3(0.80, 0.84, 0.86)));
            vec2 along = lay < 0.5 ? vec2(1.0, 0.18) : vec2(0.22, 1.0);
            float furrow = sin(dot(p + warp, along) * 1.9);
            cropColor *= 1.0 + furrow * 0.09 * exp(-footprint * 1.4);
            cropColor *= 0.86 + 0.28 * meso.a;
            vec2 border = min(fract(fieldPos), 1.0 - fract(fieldPos)) * vec2(410.0, 290.0);
            float hedge = 1.0 - smoothstep(2.5, 6.0 + footprint * 1.5, min(border.x, border.y));
            cropColor = mix(cropColor, canopy * 0.9, hedge * 0.85);
            meadow = mix(meadow, cropColor, farm);
        }

        albedo = meadow;
        albedo = mix(albedo, canopy * (0.74 + 0.52 * macro.b) * (0.86 + 0.28 * meso.a), forest);
        albedo = mix(albedo, stone * (0.80 + 0.40 * macro.a), rock);
        vec3 snowColor = vec3(0.86, 0.89, 0.93) * (0.94 + 0.08 * meso.r);
        albedo = mix(albedo, snowColor, snow);
        roughness = mix(mix(mix(0.92, 0.88, forest), 0.86, rock), 0.55, snow);

        if (detailLevel > 0.5) {
            vec4 grassDetail = layerDetail(nearUv, LAYER_GRASS);
            vec4 stoneFar = layerDetail(stoneUv, LAYER_ROCK);
            vec4 stoneNear = layerDetail(p / 11.0, LAYER_ROCK);
            vec4 canopyDetail = layerDetail(canopyUv, LAYER_FOREST);
            canopyDetail.xy *= 1.0 - smoothstep(900.0, 4500.0, viewDistance);
            vec4 snowDetail = layerDetail(p / 14.0, LAYER_SNOW);
            vec4 detail = vec4(grassDetail.xy * nearWeight, 0.0, mix(1.0, grassDetail.w, nearWeight));
            detail = mix(detail, vec4(canopyDetail.xy * 1.2, 0.0, canopyDetail.w), forest);
            vec4 stone4 = vec4(stoneFar.xy * 1.4 + stoneNear.xy * nearWeight, 0.0, stoneFar.w * mix(1.0, stoneNear.w, nearWeight));
            detail = mix(detail, stone4, rock);
            detail = mix(detail, vec4(snowDetail.xy * 0.45, 0.0, 1.0), snow);
            slopeDetail = detail.xy;
            occlusion = detail.w;
        }

        // ---- Lakes -----------------------------------------------------------
        vec2 lakeUv = p / LAND_EXTENT + 0.5;
        if (u_quality.y > 0.5 && lakeUv.x > 0.0 && lakeUv.x < 1.0 && lakeUv.y > 0.0 && lakeUv.y < 1.0) {
            waterLevel = texture2DLod(s_lakeMap, lakeUv, 0.0).r;
            waterDepth = waterLevel - bed;
            // Damp shore just above the waterline.
            float damp = 1.0 - smoothstep(0.0, 1.6, -waterDepth);
            albedo *= mix(1.0, 0.55, damp * step(-9000.0, waterLevel));
            roughness = mix(roughness, 0.45, damp * step(-9000.0, waterLevel));
        }
        macroShadow = reliefShadow(p, altitude);
    } else {
        // ---- Paved surfaces --------------------------------------------------
        vec4 macro = texture2D(s_noise, p / 310.0);
        vec4 meso = texture2D(s_noise, p / 31.0);
        vec3 tarmac = layerAlbedo(p / 4.2, LAYER_ASPHALT);
        vec4 tarmacDetail = layerDetail(p / 4.2, LAYER_ASPHALT);
        float variation = 0.74 + 0.36 * macro.r + 0.16 * meso.g;
        if (u_surface.x < 1.5) {
            albedo = tarmac * u_baseColor.rgb * 40.0 * variation;
            // Rubber laid down by the main wheels in both touchdown zones.
            // Squares are written out: pow() with a negative base is undefined
            // in GLSL and NaN in HLSL.
            float offTrack = (abs(p.x) - 3.8) / 1.5;
            float offCentre = p.x / 9.0;
            float offZone = (abs(p.y) - 880.0) / 190.0;
            float tracks = exp(-offTrack * offTrack) + 0.45 * exp(-offCentre * offCentre);
            float touchdown = exp(-offZone * offZone);
            albedo *= 1.0 - 0.52 * min(tracks, 1.0) * touchdown * step(abs(p.x), 22.5) * (0.6 + 0.4 * meso.a);
            // Paving lanes and the occasional sealed crack line.
            vec2 lane = abs(fract(p / vec2(7.5, 60.0)) - 0.5);
            float joint = 1.0 - smoothstep(0.494 - footprint * 0.05, 0.499, max(lane.x, lane.y));
            albedo *= 1.0 - 0.28 * joint * exp(-footprint * 0.9);
            roughness = clamp(tarmacDetail.z - 0.08 * macro.g, 0.5, 1.0);
        } else if (u_surface.x < 2.5) {
            // Paint: worn through to the tarmac where the aggregate stands proud.
            float wear = smoothstep(0.55, 0.95, meso.r * 0.6 + macro.a * 0.5 + (1.0 - tarmacDetail.w) * 0.25);
            albedo = mix(u_baseColor.rgb * (0.86 + 0.2 * meso.g), tarmac * 1.6, wear * 0.55);
            roughness = 0.62;
        } else {
            // Concrete apron: cast slabs with dark joints and tyre scuffing.
            vec2 slab = abs(fract(p / 7.5) - 0.5);
            float joint = 1.0 - smoothstep(0.490 - footprint * 0.05, 0.498, max(slab.x, slab.y));
            float slabTone = 0.92 + 0.16 * cellHash(floor(p / 7.5));
            albedo = u_baseColor.rgb * variation * slabTone * (0.9 + tarmac.g * 2.2);
            albedo *= 1.0 - 0.55 * joint * exp(-footprint * 0.9);
            roughness = 0.82;
        }
        slopeDetail = tarmacDetail.xy * 0.6 * exp(-viewDistance / 160.0);
        occlusion = mix(1.0, tarmacDetail.w, exp(-viewDistance / 160.0));
        // Standing water after rain darkens paving and turns it glossy.
        float wet = u_misc.w * smoothstep(0.25, 0.75, macro.b + meso.b * 0.3);
        albedo *= 1.0 - 0.45 * wet;
        roughness = mix(roughness, 0.12, wet);
    }

    n = normalize(n + vec3(slopeDetail.x, 0.0, slopeDetail.y));
    float nDotL = ofsSaturate(dot(n, u_sunDirection.xyz));
    float shadow = ofsSunShadow(v_worldPos, n, nDotL) * ofsCloudShadow(world) * macroShadow;
    vec3 sunLight = atmoSunIrradiance(altitude, u_sunDirection.y) * shadow;
    vec3 color = ofsShadeSurface(n, v, albedo, 0.0, ofsSpecularAntialias(n, roughness), occlusion, sunLight, 1.0);

    if (waterDepth > 0.0) {
        // ---- Lake surface ----------------------------------------------------
        float time = u_solar.w;
        vec2 drift = u_wind.xz * 0.035 + vec2(0.31, 0.17);
        // Explicit gradients: this branch is taken per pixel along the shore.
        vec2 ripple = texture2DGrad(s_waterNormal, p / 19.0 + drift * time * 0.05, dpx / 19.0, dpy / 19.0).xy * 2.0 - 1.0;
        ripple += (texture2DGrad(s_waterNormal, p / 5.7 - drift.yx * time * 0.085, dpx / 5.7, dpy / 5.7).xy * 2.0 - 1.0) * 0.6;
        vec2 swell = texture2DGrad(s_waterNormal, p / 157.0 + drift * time * 0.012, dpx / 157.0, dpy / 157.0).xy * 2.0 - 1.0;
        // Gusts roughen the surface in patches; distant water averages toward
        // a flat mirror with a wider highlight.
        float gust = 0.35 + 0.65 * texture2DGrad(s_noise, p / 900.0 + drift * time * 0.01, dpx / 900.0, dpy / 900.0).r;
        float calm = exp(-viewDistance / 2600.0);
        vec3 wn = normalize(vec3((ripple.x * calm + swell.x * 0.35) * 0.16 * gust, 1.0,
                                 (ripple.y * calm + swell.y * 0.35) * 0.16 * gust));
        float nDotV = max(dot(wn, v), 1.0e-3);
        float fresnel = 0.02 + 0.98 * pow(ofsSaturate(1.0 - nDotV), 5.0);
        vec3 reflected = reflect(-v, wn);
        reflected.y = abs(reflected.y);
        vec3 mirror = atmoSkyRadiance(reflected);
        float waterShadow = ofsSunShadow(v_worldPos, vec3(0.0, 1.0, 0.0), ofsSaturate(u_sunDirection.y))
                          * ofsCloudShadow(world) * macroShadow;
        vec3 waterSun = atmoSunIrradiance(waterLevel, u_sunDirection.y) * waterShadow;
        vec3 h = normalize(v + u_sunDirection.xyz + vec3(0.0, 1.0e-5, 0.0));
        float alpha = mix(0.11, 0.022, calm) * (0.6 + 0.8 * gust);
        float sunDot = ofsSaturate(dot(wn, u_sunDirection.xyz));
        vec3 glitter = waterSun * (ofsDistributionGGX(ofsSaturate(dot(wn, h)), alpha)
                                   * ofsVisibilitySmith(nDotV, sunDot, alpha) * sunDot * fresnel);
        // Light scattered back out of the water column, and the bed seen
        // through it where the lake is shallow.
        vec3 illumination = waterSun * ofsSaturate(u_sunDirection.y) + ofsAmbientIrradiance(vec3(0.0, 1.0, 0.0));
        vec3 body = vec3(0.0030, 0.0125, 0.0165) * illumination * (1.0 - exp(-waterDepth * 0.35));
        vec3 bedColor = color * exp(-waterDepth * vec3(0.55, 0.16, 0.11) / max(nDotV, 0.25));
        vec3 water = mix(body + bedColor, mirror, fresnel) + glitter;
        color = mix(color, water, smoothstep(0.0, 0.35, waterDepth));
    }

    color = ofsApplyAerial(color, ofsScreenUv(gl_FragCoord), -v, viewDistance);
    gl_FragData[0] = vec4(color, 1.0);
    gl_FragData[1] = vec4(min(viewDistance / u_cameraForward.w, 65000.0), 0.0, 0.0, 1.0);
}
