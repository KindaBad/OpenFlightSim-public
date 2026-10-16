// The round Earth's land and water (client/src/renderer_earth.cpp).
//
// What covers the land comes from Natural Earth II's colour, carried on the
// vertices: farmland, woods, arid ground or ice, each drawn with the valley's
// own ground textures. Over it lie the patterns of client/src/earth_pattern.hpp
// (fields with hedges and lanes, the clearings in woods, the streets of towns),
// which the trees and buildings of earth_objects.cpp stand on: the functions
// here are the same as those there, and must stay so. Steep ground shows rock
// and high ground scree and snow. Where the water's level stands above the
// ground the pixel is sea or lake, shaded as the valley's lakes are.

$input v_worldPos, v_normal, v_uv, v_color, v_params, v_surfacePos
#include <bgfx_shader.sh>
#include "common.glsl"
#include "earth_pattern.glsl"

SAMPLER2DARRAY(s_terrainAlbedo, 1);
SAMPLER2DARRAY(s_terrainNormal, 2);
SAMPLER2D(s_waterNormal, 5);
// The nearest runways, as ends in render space: .w half width, then length.
uniform vec4 u_earthRunways[16];
// Their airports, for a runway an airport is laid along: side, taxiway
// offset, taxiway half width, kind; then the apron's centre along the runway,
// its half length and depth, and 1.
uniform vec4 u_earthAirports[16];
// xy the pattern coordinates of this chunk's texture origin, z how far
// buildings are drawn, w the camera's latitude in degrees.
uniform vec4 u_earthChunk;

#define LAYER_GRASS 0.0
#define LAYER_SOIL 1.0
#define LAYER_ROCK 2.0
#define LAYER_SNOW 3.0
#define LAYER_ASPHALT 4.0
#define LAYER_PASTURE 5.0
#define LAYER_CROP 6.0
#define LAYER_STUBBLE 7.0
#define LAYER_PLOUGH 8.0
#define LAYER_WOODLAND 9.0
// As in earth_pattern.hpp. Texture tiles are powers of two no larger than
// 16384 m, so they run on unbroken from chunk to chunk.
#define FIELD_SIZE 512.0
#define LANE_SIZE 2048.0
#define FIELD_WANDER 55.0
#define LOT 24.0

// A layer read with the caller's own screen-space gradients.
#if BGFX_SHADER_LANGUAGE_GLSL
#   define layerGrad(_coord, _dx, _dy) textureGrad(s_terrainAlbedo, _coord, _dx, _dy)
#else
#   define layerGrad(_coord, _dx, _dy) s_terrainAlbedo.m_texture.SampleGrad(s_terrainAlbedo.m_sampler, _coord, _dx, _dy)
#endif

// A layer's colour: its mean far off, the texture itself close to.
vec3 layerColour(vec2 uv, float layer, float closeness)
{
    vec3 mean = texture2DArrayLod(s_terrainAlbedo, vec3(0.5, 0.5, layer), 9.0).rgb;
    if (closeness < 0.01) return mean;
    return mix(mean, texture2DArray(s_terrainAlbedo, vec3(uv, layer)).rgb, closeness);
}
vec3 layerMean(float layer) { return texture2DArrayLod(s_terrainAlbedo, vec3(0.5, 0.5, layer), 9.0).rgb; }
// The same with the caller's gradients, for use where only some pixels of a
// quad take the branch it is read in.
vec3 layerColourGrad(vec2 uv, float layer, float closeness, vec2 dx, vec2 dy)
{
    vec3 mean = layerMean(layer);
    if (closeness < 0.01) return mean;
    return mix(mean, layerGrad(vec3(uv, layer), dx, dy).rgb, closeness);
}

// earthFieldEdge.
float fieldEdge(vec2 q, out vec2 own, out vec2 neighbour)
{
    vec2 home = floor(q);
    float nearest = 1.0e9;
    float second = 1.0e9;
    vec2 ownSite = home;
    vec2 otherSite = vec2_splat(0.0);
    own = home;
    neighbour = home;
    for (int j = -1; j <= 1; ++j) {
        for (int i = -1; i <= 1; ++i) {
            vec2 cell = home + vec2(float(i), float(j));
            vec2 site = cell + vec2(0.15, 0.15) + 0.7 * vec2(cellHash(cell + 5.3), cellHash(cell + 91.7));
            float away = length(q - site);
            if (away < nearest) {
                second = nearest;
                neighbour = own;
                otherSite = ownSite;
                nearest = away;
                own = cell;
                ownSite = site;
            } else if (away < second) {
                second = away;
                neighbour = cell;
                otherSite = site;
            }
        }
    }
    vec2 between = otherSite - ownSite;
    return dot((ownSite + otherSite) * 0.5 - q, between / max(length(between), 1.0e-4));
}

// earthFieldWander.
vec2 fieldWander(vec2 p)
{
    return p + FIELD_WANDER * vec2(sin(p.y / 340.0) + 0.5 * sin(p.x / 190.0 + 1.7) + 0.3 * sin(p.y / 95.0 + 4.1),
                                   sin(p.x / 410.0 + 0.6) + 0.5 * sin(p.y / 230.0 + 2.1) + 0.3 * sin(p.x / 83.0 + 0.9));
}

// Rotates tile coordinates off the world axes.
vec2 turned(vec2 p, vec2 basis) { return vec2(p.x * basis.x - p.y * basis.y, p.x * basis.y + p.y * basis.x); }

void main()
{
    float ground = v_params.x;
    float water = v_params.y;
    vec3 toEye = u_cameraPos.xyz - v_worldPos;
    float viewDistance = length(toEye);
    vec3 v = toEye / max(viewDistance, 1.0e-4);
    vec3 world = v_worldPos + u_worldOrigin.xyz;
    // The local vertical, away from the centre of the planet below the origin.
    vec3 up = normalize(v_worldPos + vec3(0.0, u_atmoGeometry.x + u_planet.y, 0.0));
    vec3 n = normalize(v_normal);
    float steep = 1.0 - dot(n, up);
    vec2 p = v_uv;
    vec2 pf = v_uv + u_earthChunk.xy;
    vec2 dpx = dFdx(p);
    vec2 dpy = dFdy(p);
    float footprint = max(length(dpx), length(dpy));
    float closeness = exp(-viewDistance / 9000.0);

    // ---- Land ----------------------------------------------------------------
    // Natural Earth's colour says what covers the ground (earthCoverClass).
    vec3 cover = v_color.rgb;
    float light = dot(cover, vec3(0.30, 0.59, 0.11));
    float plants = smoothstep(0.05, 0.11, cover.g - 0.5 * (cover.r + cover.b));
    float ice = (1.0 - plants) * smoothstep(-0.02, 0.04, cover.b - cover.r) * smoothstep(0.84, 0.9, light);
    float arid = (1.0 - plants) * (1.0 - ice);
    float wood = plants * smoothstep(0.775, 0.72, light);
    float fields = plants - wood;
    // Steep farmland is wooded (earthSteepWoods).
    float steepWoods = smoothstep(0.05, 0.10, steep) * fields;
    wood += steepWoods;
    fields -= steepWoods;
    vec4 macro = texture2D(s_noise, p / 2048.0);
    vec4 meso = texture2D(s_noise, p / 256.0 + 0.37);
    float mottle = macro.r;

    vec3 grass = layerColour(p / 32.0, LAYER_GRASS, closeness);
    vec3 soil = layerColour(p / 32.0, LAYER_SOIL, closeness);
    vec3 sward = mix(layerMean(LAYER_PASTURE), grass, 0.5) * vec3(1.02, 1.06, 0.92);
    // Woods (earthWoodland): crowns, with clearings of rough grass. What is
    // only seen where there are woods, rock or snow is worked out only there.
    float woodland = 0.0;
    if (wood > 0.001) woodland = wood * smoothstep(0.30, 0.46, valueNoise(pf / 1100.0) * 0.6 + valueNoise(pf / 330.0 + 17.0) * 0.4);
    vec3 meadow = sward * (0.86 + 0.3 * macro.a);
    meadow = mix(meadow, soil, smoothstep(0.66, 0.9, meso.a) * 0.35);
    float forest = woodland;

    // Farmland: irregular fields a few hundred metres across, most of them
    // grass and green crops, some cut or under the plough, with hedgerows and
    // belts of trees between them, a wood here and there and lanes winding
    // through, as in the valley. Far off, only their mean colour.
    vec3 farm = mix(mix(layerMean(LAYER_PASTURE), layerMean(LAYER_CROP), smoothstep(0.4, 0.6, meso.g)), sward, 0.3);
    float fieldDetail = 1.0 - smoothstep(9000.0, 15000.0, viewDistance);
    if (fields > 0.004 && fieldDetail > 0.0) {
        vec2 wandered = fieldWander(pf);
        vec2 field;
        vec2 nextField;
        float fieldGap = fieldEdge(wandered / FIELD_SIZE, field, nextField) * FIELD_SIZE;
        // Lanes and what grows along the boundaries are only made out close to.
        float nearFields = 1.0 - smoothstep(u_quality.x > 1.5 ? 6000.0 : 3500.0, u_quality.x > 1.5 ? 8000.0 : 5000.0, viewDistance);
        vec2 block = vec2_splat(0.0);
        vec2 nextBlock;
        float laneGap = 1000.0;
        if (nearFields > 0.0) laneGap = fieldEdge(wandered / LANE_SIZE + 37.5, block, nextBlock) * LANE_SIZE;
        vec2 plot = field;
        float crop = cellHash(plot + 11.0);
        float tone = cellHash(plot * 1.7 + 3.0);
        float lay = cellHash(plot + 23.0) * 3.14159265;
        vec2 along = vec2(cos(lay), sin(lay));
        float coverLayer = LAYER_PASTURE;
        float tile = 512.0;
        vec3 tint = mix(vec3(0.80, 0.90, 0.92), vec3(1.16, 1.10, 0.96), tone);
        if (crop >= 0.44) {
            tile = 256.0;
            if (crop < 0.68) {
                coverLayer = LAYER_CROP;
                tint = mix(vec3(0.74, 0.92, 1.05), vec3(1.25, 1.12, 0.85), tone);
            } else if (crop < 0.77) {
                coverLayer = LAYER_CROP;
                tint = mix(vec3(0.52, 0.70, 0.80), vec3(0.66, 0.80, 0.78), tone);
            } else if (crop < 0.89) {
                coverLayer = LAYER_STUBBLE;
                tint = mix(vec3(0.78, 0.80, 0.78), vec3(1.02, 1.00, 0.92), tone);
            } else if (crop < 0.94) {
                coverLayer = LAYER_CROP;
                tint = mix(vec3(2.40, 1.25, 3.00), vec3(2.90, 1.45, 3.80), tone);
            } else {
                coverLayer = LAYER_PLOUGH;
                tint = mix(vec3(0.85, 0.85, 0.85), vec3(1.25, 1.20, 1.15), tone);
            }
        }
        // Turned about the pattern's origin, so a field's texture runs on
        // unbroken across the chunks it lies in.
        vec2 coverUv = turned(pf, along) / tile + vec2(crop, tone) * 7.0;
        vec3 cropMean = texture2DArrayLod(s_terrainAlbedo, vec3(0.5, 0.5, coverLayer), 9.0).rgb;
        vec3 cropTexture = layerGrad(vec3(coverUv, coverLayer), turned(dpx, along) / tile, turned(dpy, along) / tile).rgb;
        vec3 cropColor = mix(cropMean, cropTexture, closeness) * tint * (0.90 + 0.22 * macro.a);
        // What grows on the boundary: a strip of rough grass, a hedge or a belt of trees.
        float boundary = cellHash(field + nextField + 40.0);
        float reach = boundary < 0.75 ? 4.0 : 11.0;
        float hedge = step(0.30, boundary) * (1.0 - smoothstep(reach * 0.45, reach + footprint * 1.2, fieldGap)) * nearFields;
        float margin = 1.0 - smoothstep(1.2, 3.2 + footprint * 1.2, fieldGap);
        cropColor = mix(cropColor, sward * vec3(1.15, 1.08, 0.95), margin * 0.7);
        // One field in a dozen was never cleared.
        float wooded = step(cellHash(field + 61.0), 0.085) * smoothstep(-3.0, 5.0, fieldGap);
        // Lanes: tarmac between grass verges.
        float verge = 1.0 - smoothstep(4.5, 8.0 + footprint * 1.5, laneGap);
        float lane = 1.0 - smoothstep(2.4, 3.2 + footprint * 1.2, laneGap);
        cropColor = mix(cropColor, sward * vec3(1.20, 1.10, 0.95), verge * 0.8);
        cropColor = mix(cropColor, vec3(0.150, 0.146, 0.138) * (0.9 + 0.2 * meso.g), lane);
        float farmed = smoothstep(0.3, 0.5, fields);
        forest = max(forest, max(hedge, wooded) * (1.0 - verge) * farmed);
        farm = mix(farm, cropColor, fieldDetail);
    }

    vec3 sand = srgbToLinear(cover) * 0.42 * mix(vec3_splat(1.0), clamp(soil / max(layerMean(LAYER_SOIL), vec3_splat(0.02)), 0.6, 1.4), closeness);
    vec3 albedo = mix(meadow, farm, smoothstep(0.3, 0.5, fields)) * plants + sand * arid + vec3(0.72, 0.74, 0.78) * ice;
    if (forest > 0.0) {
        vec3 canopy = layerColourGrad(p / 256.0, LAYER_WOODLAND, closeness, dpx / 256.0, dpy / 256.0)
                    * (0.92 + 0.30 * macro.b) * (0.88 + 0.24 * meso.a);
        albedo = mix(albedo, canopy, forest);
    }
    albedo *= 0.88 + 0.24 * mottle;

    // ---- Towns ---------------------------------------------------------------
    // The most built-up place here, and where the point lies on its streets,
    // from the vertices (earthTown).
    float built = v_params.z;
    vec2 townXY = v_surfacePos.xy;
    float townKind = v_surfacePos.z;
    float townSeed = v_params.w;
    if (built > 0.01) {
        float street;
        float verge;
        float housed = 1.0;
        vec2 lot;
        if (townKind > 0.5) {
            // A town's districts each turn its grid of streets (earthDistrict).
            vec2 district = floor(townXY / 768.0);
            float angle = (cellHash(district + vec2(71.0, 13.0)) - 0.5) * 0.9;
            float c = cos(angle);
            float s = sin(angle);
            vec2 turnedXY = vec2(townXY.x * c - townXY.y * s, townXY.x * s + townXY.y * c);
            lot = floor(turnedXY / LOT);
            vec2 inBlock = mod(lot, 4.0);
            vec2 across = abs(fract(turnedXY / LOT) - 0.5) * LOT;
            // A street runs down the middle of every fourth row of lots, with
            // verges either side.
            float streetX = step(2.5, inBlock.x) * (1.0 - smoothstep(4.5, 5.0 + footprint, across.x));
            float streetY = step(2.5, inBlock.y) * (1.0 - smoothstep(4.5, 5.0 + footprint, across.y));
            street = max(streetX, streetY);
            verge = max(step(2.5, inBlock.x), step(2.5, inBlock.y));
        } else {
            // A village's street, and the lane across it.
            float away = min(abs(townXY.x), abs(townXY.y));
            street = 1.0 - smoothstep(3.5, 4.0 + footprint, away);
            verge = 1.0 - smoothstep(5.0, 6.0 + footprint, away);
            lot = floor(townXY / LOT);
            housed = 1.0 - smoothstep(40.0, 52.0, away);
        }
        // Lots: gardens in the outskirts, yards and paving toward the middle;
        // from beyond where buildings are drawn, their roofs.
        float h = cellHash(lot + townSeed * 0.013);
        vec3 yard = mix(sward * (0.8 + 0.3 * h), vec3(0.21, 0.20, 0.18) * (0.8 + 0.4 * h), smoothstep(0.35, 0.95, built) * step(0.5, townKind) * (1.0 - verge));
        float roofs = smoothstep(u_earthChunk.z * 0.7, u_earthChunk.z, viewDistance) * step(h, built * 0.9) * (1.0 - verge) * housed;
        vec3 roofColour = h < 0.45 ? vec3(0.30, 0.115, 0.07) : h < 0.7 ? vec3(0.13, 0.135, 0.15) : vec3(0.32, 0.32, 0.31);
        yard = mix(yard, roofColour, roofs);
        vec3 town = mix(yard, vec3(0.085, 0.085, 0.09) * (0.9 + 0.2 * meso.r), street);
        float spread = smoothstep(0.02, 0.25, built);
        albedo = mix(albedo, town, spread);
        forest *= 1.0 - spread;
    }

    // ---- Mountains -----------------------------------------------------------
    // Steep ground is rock. Above the trees, alpine grass gives way to scree
    // and bare rock, and higher still snow lies wherever it can settle.
    float latitude = u_earthChunk.w;
    float treeline = 3700.0 - 2500.0 * smoothstep(22.0, 62.0, latitude);
    float rough = mottle - 0.5;
    float rock = smoothstep(0.24, 0.36, steep + 0.08 * rough);
    float scree = smoothstep(treeline + 250.0, treeline + 900.0, ground + 350.0 * rough) * (1.0 - ice);
    // Alpine meadow: brighter and yellower than the valleys below.
    albedo = mix(albedo, sward * vec3(1.18, 1.10, 0.80), smoothstep(treeline - 150.0, treeline + 250.0, ground) * plants * (1.0 - forest));
    if (scree + rock > 0.0) {
        // Grey limestone and granite, darker in the cracks, from the valley's rock.
        vec3 stone = layerColourGrad(p / 16.0, LAYER_ROCK, closeness, dpx / 16.0, dpy / 16.0);
        vec3 stoneFar = layerColourGrad(p / 128.0, LAYER_ROCK, closeness, dpx / 128.0, dpy / 128.0);
        stone = mix(stoneFar, stone, closeness) * vec3(0.80, 0.82, 0.86) * (0.62 + 0.55 * meso.b * macro.g + 0.2 * macro.a);
        albedo = mix(albedo, mix(stone, soil * 0.8, 0.3), scree * 0.75);
        albedo = mix(albedo, stone, rock);
    }
    // Snow lies above the line wherever it can settle, and in the gullies a little lower.
    float snowLine = treeline + 600.0;
    float snow = max(ice, smoothstep(snowLine, snowLine + 400.0, ground + 450.0 * rough + 300.0 * (meso.r - 0.5)));
    snow *= 1.0 - smoothstep(0.30, 0.50, steep - 0.25 * smoothstep(snowLine + 500.0, snowLine + 1300.0, ground));
    vec3 snowColour = vec3(0.86, 0.89, 0.93) * (0.94 + 0.08 * meso.r);
    albedo = mix(albedo, snowColour, snow);
    float roughness = mix(mix(0.92, 0.86, rock), 0.55, snow);

    // ---- Runways and airports ------------------------------------------------
    // Each airport's own ground first, then every runway over it.
    float runwayMask = u_earthCounts.y;
    for (int i = 0; i < 8; ++i) {
        if (runwayMask < 0.5) break;
        float listed = mod(runwayMask, 2.0);
        runwayMask = floor(runwayMask * 0.5);
        if (listed < 0.5) continue;
        vec4 a = u_earthRunways[2 * i];
        vec4 b = u_earthRunways[2 * i + 1];
        vec4 airport = u_earthAirports[2 * i];
        vec4 apron = u_earthAirports[2 * i + 1];
        if (apron.w < 0.5) continue;
        vec3 along = (b.xyz - a.xyz) / b.w;
        float t = dot(v_worldPos - a.xyz, along);
        if (t < -400.0 || t > b.w + 400.0) continue;
        vec3 offset = v_worldPos - a.xyz - along * t;
        // The airport's side of the runway (EarthAirportLayout).
        float sideways = dot(offset - up * dot(offset, up), normalize(cross(along, up))) * airport.x;
        float mid = b.w * 0.5 + apron.x;
        float taxiOffset = airport.y;
        float taxiHalf = airport.z;
        float apronNear = taxiOffset > 0.0 ? taxiOffset + taxiHalf : a.w + 50.0;
        float apronFar = apronNear + apron.z;
        float taxiway = 0.0;
        if (taxiOffset > 0.0) {
            taxiway = step(abs(sideways - taxiOffset), taxiHalf) * step(0.0, t) * step(t, b.w);
            // Links from the runway at each end and the middle.
            float links = min(min(abs(t - 30.0), abs(t - (b.w - 30.0))), abs(t - mid));
            taxiway = max(taxiway, step(links, taxiHalf) * step(a.w, sideways) * step(sideways, taxiOffset));
        } else {
            taxiway = step(abs(t - mid), 6.0) * step(a.w, sideways) * step(sideways, apronNear);
        }
        float apronArea = step(abs(t - mid), apron.y) * step(apronNear, sideways) * step(sideways, apronFar);
        if (taxiway + apronArea > 0.0) {
            vec3 tarmac = vec3_splat(0.09) * (0.85 + 0.3 * mottle);
            vec3 concrete = vec3(0.30, 0.30, 0.29) * (0.88 + 0.24 * meso.g);
            float centreLine = (1.0 - step(0.25, abs(sideways - taxiOffset))) * step(0.5, taxiway) * step(0.0, taxiOffset - 1.0);
            albedo = apronArea > 0.5 ? concrete : mix(tarmac, vec3(0.55, 0.40, 0.04), centreLine);
            roughness = 0.8;
        }
    }
    runwayMask = u_earthCounts.y;
    for (int i = 0; i < 8; ++i) {
        if (runwayMask < 0.5) break;
        float listed = mod(runwayMask, 2.0);
        runwayMask = floor(runwayMask * 0.5);
        if (listed < 0.5) continue;
        vec4 a = u_earthRunways[2 * i];
        vec4 b = u_earthRunways[2 * i + 1];
        vec3 along = (b.xyz - a.xyz) / b.w;
        float t = dot(v_worldPos - a.xyz, along);
        vec3 offset = v_worldPos - a.xyz - along * t;
        float across = length(offset - up * dot(offset, up));
        float beyond = max(-t, t - b.w);
        float paved = (1.0 - smoothstep(a.w - 0.5, a.w + 0.5, across)) * (1.0 - smoothstep(0.0, 1.0, beyond));
        if (paved <= 0.0) continue;
        vec3 tarmac = vec3_splat(0.075) * (0.85 + 0.3 * mottle);
        // A dashed centre line and the edge lines.
        float dash = step(fract(t / 60.0), 0.5) * (1.0 - smoothstep(0.35, 0.55, across));
        float edgeLine = smoothstep(a.w - 1.6, a.w - 1.2, across) * (1.0 - smoothstep(a.w - 0.6, a.w - 0.2, across));
        vec3 runway = mix(tarmac, vec3_splat(0.55), max(dash, edgeLine));
        albedo = mix(albedo, runway, paved);
        roughness = mix(roughness, 0.75, paved);
    }

    float nDotL = ofsSaturate(dot(n, u_sunDirection.xyz));
    float shadow = ofsSunShadow(v_worldPos, n, nDotL) * ofsCloudShadow(world);
    float sunUp = dot(up, u_sunDirection.xyz);
    vec3 sunLight = atmoSunIrradiance(max(ground, 0.0), sunUp) * shadow;
    vec3 color = ofsShadeSurface(n, v, albedo, 0.0, ofsSpecularAntialias(n, roughness), 1.0, sunLight, 1.0);

    // ---- Sea and lakes -------------------------------------------------------
    float waterDepth = water - ground;
    if (waterDepth > 0.0) {
        float time = u_solar.w;
        vec2 drift = u_wind.xz * 0.035 + vec2(0.31, 0.17);
        vec2 ripple = texture2DGrad(s_waterNormal, p / 16.0 + drift * time * 0.05, dpx / 16.0, dpy / 16.0).xy * 2.0 - 1.0;
        ripple += (texture2DGrad(s_waterNormal, p / 4.0 - drift.yx * time * 0.085, dpx / 4.0, dpy / 4.0).xy * 2.0 - 1.0) * 0.6;
        vec2 swell = texture2DGrad(s_waterNormal, p / 128.0 + drift * time * 0.012, dpx / 128.0, dpy / 128.0).xy * 2.0 - 1.0;
        float gust = 0.35 + 0.65 * texture2DGrad(s_noise, p / 1024.0 + drift * time * 0.01, dpx / 1024.0, dpy / 1024.0).r;
        float calm = exp(-viewDistance / 2600.0);
        // Waves tilt the surface about the local vertical.
        vec3 east = normalize(cross(up, vec3(0.0, 0.0, 1.0)));
        vec3 south = cross(east, up);
        vec2 tilt = (ripple * calm + swell * 0.35) * 0.16 * gust;
        vec3 wn = normalize(up + east * tilt.x + south * tilt.y);
        float nDotV = max(dot(wn, v), 1.0e-3);
        float fresnel = 0.02 + 0.98 * pow(ofsSaturate(1.0 - nDotV), 5.0);
        vec3 reflected = reflect(-v, wn);
        reflected.y = abs(reflected.y);
        vec3 mirror = atmoSkyRadiance(reflected);
        float waterShadow = ofsSunShadow(v_worldPos, up, ofsSaturate(sunUp)) * ofsCloudShadow(world);
        vec3 waterSun = atmoSunIrradiance(max(water, 0.0), sunUp) * waterShadow;
        vec3 h = normalize(v + u_sunDirection.xyz + vec3(0.0, 1.0e-5, 0.0));
        float alpha = mix(0.11, 0.022, calm) * (0.6 + 0.8 * gust);
        float sunDot = ofsSaturate(dot(wn, u_sunDirection.xyz));
        vec3 glitter = waterSun * (ofsDistributionGGX(ofsSaturate(dot(wn, h)), alpha)
                                   * ofsVisibilitySmith(nDotV, sunDot, alpha) * sunDot * fresnel);
        vec3 illumination = waterSun * ofsSaturate(sunUp) + ofsAmbientIrradiance(up);
        vec3 body = vec3(0.0030, 0.0125, 0.0165) * illumination * (1.0 - exp(-waterDepth * 0.35));
        vec3 bedColor = color * exp(-waterDepth * vec3(0.55, 0.16, 0.11) / max(nDotV, 0.25));
        vec3 surface = mix(body + bedColor, mirror, fresnel) + glitter;
        color = mix(color, surface, smoothstep(0.0, 0.35, waterDepth));
    }

    color = ofsApplyAerial(color, ofsScreenUv(gl_FragCoord), -v, viewDistance);
    gl_FragData[0] = vec4(color, 1.0);
    gl_FragData[1] = vec4(min(viewDistance / u_cameraForward.w, 65000.0), 0.0, 0.0, 1.0);
}
