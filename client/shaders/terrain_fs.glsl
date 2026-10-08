// Terrain surface: land cover, lakes and the paved airfield.
//
// The mesh is the simulation's collision surface and is never displaced. All
// extra detail is shading: a per-pixel normal from the same analytic height
// function, material layers blended by slope, altitude and the baked
// land-cover map, lake water where a basin's level stands above the bed, and
// long shadows cast by the relief itself.
//
// The materials come at two scales. Cover layers are the country as it looks
// from the air, a few hundred metres to a tile: one is laid over each field,
// wood and stretch of open ground. Detail layers are the ground underfoot, a
// few metres to a tile, and only show close to.

$input v_worldPos, v_normal, v_uv, v_surfacePos
#include <bgfx_shader.sh>
#include "common.glsl"

SAMPLER2DARRAY(s_terrainAlbedo, 1);
SAMPLER2DARRAY(s_terrainNormal, 2);
SAMPLER2D(s_landMap, 3);
SAMPLER2D(s_lakeMap, 4);
SAMPLER2D(s_waterNormal, 5);
SAMPLER2D(s_heightMap, 12);
uniform vec4 u_terrainMap; // x metres per texel, y coarsest level, z base height, w height range
uniform vec4 u_surface;   // x kind: 0 land, 1 asphalt, 2 paint, 3 concrete; y decal layer
uniform vec4 u_baseColor; // tint for paved kinds

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
// Metres to a tile of each cover layer.
#define PASTURE_TILE 380.0
#define FIELD_TILE 240.0
#define WOOD_TILE 300.0
#define LAND_EXTENT 96000.0
#define FIELD_SIZE 430.0
#define LANE_SIZE 1700.0
#define FIELD_WANDER 55.0

// A layer read with the caller's own screen-space gradients, for coordinates
// that jump from one field to the next.
#if BGFX_SHADER_LANGUAGE_GLSL
#   define layerGrad(_coord, _dx, _dy) textureGrad(s_terrainAlbedo, _coord, _dx, _dy)
#else
#   define layerGrad(_coord, _dx, _dy) s_terrainAlbedo.m_texture.SampleGrad(s_terrainAlbedo.m_sampler, _coord, _dx, _dy)
#endif

// The shape of the ground (ofs::terrainElevation), baked by the landscape into
// a height map that covers the same square as the land cover. A coarser level
// of its chain is the same ground averaged over a wider footprint.
float terrainHeight(vec2 eastSouth, float level)
{
    return u_terrainMap.z + u_terrainMap.w * texture2DLod(s_heightMap, eastSouth / LAND_EXTENT + 0.5, level).r;
}

// Sine-free hash (Hoskins), stable across GPU vendors for integer-like input.
float cellHash(vec2 p)
{
    vec3 p3 = fract(vec3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// Farmland is a scatter of points, one to a square: the ground nearest each
// point is one field. Returns how far `q` (in squares) is from the edge of its
// field, with the field's own square and its neighbour's across that edge.
// Landscape::fieldPattern (client/src/landscape.cpp) is the same thing in C++,
// so the trees of a hedgerow stand on the line drawn here.
float fieldEdge(vec2 q, out vec2 own, out vec2 neighbour, out vec2 ownSite)
{
    vec2 home = floor(q);
    float nearest = 1.0e9;
    float second = 1.0e9;
    vec2 otherSite = vec2_splat(0.0);
    own = home;
    neighbour = home;
    ownSite = home;
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

// Field boundaries and lanes wander a little instead of running dead straight.
vec2 fieldWander(vec2 p)
{
    return p + FIELD_WANDER * vec2(sin(p.y / 340.0) + 0.5 * sin(p.x / 190.0 + 1.7) + 0.3 * sin(p.y / 95.0 + 4.1),
                                   sin(p.x / 410.0 + 0.6) + 0.5 * sin(p.y / 230.0 + 2.1) + 0.3 * sin(p.x / 83.0 + 0.9));
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

// What a layer's detail averages to, for ground too far off to show it.
vec4 layerDetailMean(float layer)
{
    vec4 texel = texture2DArrayLod(s_terrainNormal, vec3(0.5, 0.5, layer), 9.0);
    return vec4(0.0, 0.0, texel.z, texel.w);
}

// Long shadows from the relief: walk toward the sun over the height map and
// keep the tightest clearance, which gives a soft edge for free. Each step
// reads the map at about the width the shadow's edge has spread to by then.
float reliefShadow(vec2 p, float altitude, float pixelLevel)
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
        float level = clamp(max(log2(reach * 0.2 / u_terrainMap.x), pixelLevel), 0.0, u_terrainMap.y);
        clearance = min(clearance, (rayHeight - terrainHeight(p + heading * reach, level)) / reach);
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
        // Inside the mapped square the slope comes from the height map, read
        // at the scale a pixel covers; beyond it, from the mesh.
        vec2 fromEdge = LAND_EXTENT * 0.5 - abs(p);
        float mapped = smoothstep(200.0, 2500.0, min(fromEdge.x, fromEdge.y));
        float bed = altitude;
        float mapLevel = 0.0;
        if (mapped > 0.0) {
            float level = clamp(log2(max(footprint, 1.0e-3) / u_terrainMap.x), 0.0, u_terrainMap.y);
            float d = u_terrainMap.x * exp2(level);
            float east = terrainHeight(p + vec2(d, 0.0), level) - terrainHeight(p - vec2(d, 0.0), level);
            float south = terrainHeight(p + vec2(0.0, d), level) - terrainHeight(p - vec2(0.0, d), level);
            n = normalize(mix(n, normalize(vec3(-east, 2.0 * d, -south)), mapped));
            // Read no finer than the pixel is: far ground would otherwise
            // touch a different part of the map at every pixel.
            bed = mix(altitude, terrainHeight(p, level), mapped);
            mapLevel = level;
        }
        vec4 land = texture2D(s_landMap, p / LAND_EXTENT + 0.5);
        vec4 macro = texture2D(s_noise, p / 1730.0);
        vec4 meso = texture2D(s_noise, p / 171.0);
        float steep = 1.0 - n.y;

        float rock = smoothstep(0.13, 0.30, steep + (land.a - 0.5) * 0.20 + (meso.r - 0.5) * 0.12
                                 + smoothstep(1500.0, 2200.0, altitude) * 0.22);
        float snow = smoothstep(1620.0, 1930.0, altitude + (macro.g - 0.5) * 520.0 + (meso.b - 0.5) * 120.0)
                   * (1.0 - smoothstep(0.24, 0.50, steep));
        float forest;
        float farm = land.g;
        float dry = smoothstep(0.30, 0.72, macro.r * 0.55 + (1.0 - land.b) * 0.55 + smoothstep(700.0, 1500.0, altitude) * 0.35);

        // Cover: what the ground is, seen whole. Each is read at two sizes
        // and the larger takes over in drifts, so neither repeat shows.
        vec2 woodUv = turned(p, vec2(0.9211, 0.3894)) / WOOD_TILE;
        vec4 wood = texture2DArray(s_terrainAlbedo, vec3(woodUv, LAYER_WOODLAND));
        vec3 sward = mix(layerAlbedo(turned(p, vec2(0.7986, 0.6018)) / PASTURE_TILE, LAYER_PASTURE),
                         layerAlbedo(turned(p, vec2(0.2890, -0.9573)) / (PASTURE_TILE * 2.7), LAYER_PASTURE),
                         smoothstep(0.35, 0.65, macro.r));
        // Detail: the grain of the ground under the aircraft, laid over the
        // cover and gone within a kilometre.
        vec2 nearUv = p / 6.3;
        vec2 stoneUv = turned(p, vec2(0.9455, -0.3256)) / 143.0;
        float nearWeight = detailLevel > 0.5 ? exp(-viewDistance / 520.0) : 0.0;
        // Past a few kilometres a tile is smaller than a pixel and only its
        // repeat would show; hand over to the layer's mean colour and let the
        // land-cover noise carry the variation.
        float distant = smoothstep(1500.0, 7000.0, viewDistance);
        vec3 grain = vec3_splat(1.0);
        vec3 stone = mix(layerAlbedo(stoneUv, LAYER_ROCK), layerMean(LAYER_ROCK), distant * 0.6);
        if (nearWeight > 0.004) {
            grain = mix(grain, layerGrain(nearUv, LAYER_GRASS), nearWeight);
            stone *= mix(vec3_splat(1.0), layerGrain(p / 11.0, LAYER_ROCK), nearWeight);
        }
        vec3 soil = mix(layerBroad(p / 23.0, LAYER_SOIL, footprint * 512.0 / 23.0), layerMean(LAYER_SOIL), distant);
        // Painted crowns pass for trees from a height; lower down they are
        // softened toward the wood's own colour and the real trees stand on them.
        vec3 canopy = mix(wood.rgb, layerMean(LAYER_WOODLAND), 0.6 * exp(-viewDistance / 800.0))
                    * (0.92 + 0.44 * macro.b) * (0.88 + 0.24 * meso.a);
        // From among the trees the ground is the wood's floor: litter and
        // moss in the shade of the crowns, which are the trees themselves.
        float underfoot = exp(-viewDistance / 240.0);
        canopy = mix(canopy, mix(soil, sward, 0.35) * grain * 0.42, underfoot);
        // A wood ends at the crowns of its outermost trees, not along a contour.
        float crowns = wood.a - 0.45;
        forest = smoothstep(0.34, 0.62, land.r + (meso.g - 0.5) * 0.34 + crowns * 0.20) * (1.0 - rock) * (1.0 - snow);

        // Open ground: lush to dry with moisture and height, never one flat
        // green, with thorn and gorse in clumps where nothing grazes it down.
        vec3 meadow = sward * grain * mix(vec3(0.92, 1.00, 0.90), vec3(1.55, 1.12, 0.95), dry);
        meadow *= 0.84 + 0.34 * macro.a;
        meadow = mix(meadow, soil, smoothstep(0.62, 0.86, meso.a + dry * 0.2) * 0.45);
        float thicket = smoothstep(0.52, 0.78, texture2D(s_noise, p / 2300.0).a * 0.75 + texture2D(s_noise, p / 610.0).b * 0.35);
        float scrub = thicket * smoothstep(0.50, 0.64, wood.a * 0.55 + meso.r * 0.45)
                    * (1.0 - farm) * (1.0 - rock) * (1.0 - snow);

        // Farmland: irregular fields a few hundred metres across, most of them
        // grass and green crops, some cut for hay or under the plough, with
        // hedgerows and belts of trees between them, a wood here and there and
        // lanes winding through. A field is all farmed or not at all, so the
        // farmed land ends at a field's edge.
        if (farm > 0.004) {
            vec2 wandered = fieldWander(p);
            vec2 field;
            vec2 nextField;
            vec2 fieldSite;
            float fieldGap = fieldEdge(wandered / FIELD_SIZE, field, nextField, fieldSite) * FIELD_SIZE;
            vec2 block;
            vec2 nextBlock;
            vec2 blockSite;
            float laneGap = fieldEdge(wandered / LANE_SIZE + 37.5, block, nextBlock, blockSite) * LANE_SIZE;
            float farmed = step(0.42, texture2DLod(s_landMap, fieldSite * FIELD_SIZE / LAND_EXTENT + 0.5, 0.0).g);
            if (farmed > 0.5) {
                scrub = 0.0;
                // A lane cuts a field in two, and each side is its own field.
                vec2 plot = field + block * 7.31;
                float crop = cellHash(plot + 11.0);
                float tone = cellHash(plot * 1.7 + 3.0);
                // Each field is worked its own way: its cover lies along it.
                float lay = cellHash(plot + 23.0) * 3.14159265;
                vec2 along = vec2(cos(lay), sin(lay));
                float coverLayer = LAYER_PASTURE;
                float tile = PASTURE_TILE;
                vec3 tint = mix(vec3(0.80, 0.90, 0.92), vec3(1.16, 1.10, 0.96), tone);
                if (crop >= 0.44) {
                    tile = FIELD_TILE;
                    if (crop < 0.68) {
                        // A green crop, from blue-green to yellow-green.
                        coverLayer = LAYER_CROP;
                        tint = mix(vec3(0.74, 0.92, 1.05), vec3(1.25, 1.12, 0.85), tone);
                    } else if (crop < 0.77) {
                        // Beet or potatoes: darker, closed over.
                        coverLayer = LAYER_CROP;
                        tint = mix(vec3(0.52, 0.70, 0.80), vec3(0.66, 0.80, 0.78), tone);
                    } else if (crop < 0.89) {
                        coverLayer = LAYER_STUBBLE;
                        tint = mix(vec3(0.78, 0.80, 0.78), vec3(1.02, 1.00, 0.92), tone);
                    } else if (crop < 0.94) {
                        // Ripe corn: the crop's rows in the stubble's colour.
                        coverLayer = LAYER_CROP;
                        tint = mix(vec3(2.40, 1.25, 3.00), vec3(2.90, 1.45, 3.80), tone);
                    } else {
                        coverLayer = LAYER_PLOUGH;
                        tint = mix(vec3(0.85, 0.85, 0.85), vec3(1.25, 1.20, 1.15), tone);
                    }
                }
                vec2 coverUv = turned(p, along) / tile + vec2(crop, tone) * 7.0;
                vec3 cropColor = layerGrad(vec3(coverUv, coverLayer), turned(dpx, along) / tile, turned(dpy, along) / tile).rgb * tint;
                cropColor *= (0.90 + 0.22 * macro.a) * mix(vec3_splat(1.0), grain, step(coverLayer, LAYER_CROP + 0.5));
                // What grows on the boundary: a strip of rough grass, a hedge
                // or a belt of trees, its outline the crowns that make it up.
                float boundary = cellHash(field + nextField + 40.0);
                float reach = boundary < 0.75 ? 4.0 : 11.0;
                float hedge = step(0.30, boundary) * (1.0 - smoothstep(reach * 0.45, reach + footprint * 1.2, fieldGap - crowns * reach * 0.9));
                float margin = 1.0 - smoothstep(1.2, 3.2 + footprint * 1.2, fieldGap);
                cropColor = mix(cropColor, sward * vec3(1.15, 1.08, 0.95), margin * 0.7);
                // One field in a dozen was never cleared, or has gone back to wood.
                float wooded = step(cellHash(field + 61.0), 0.085) * smoothstep(-3.0, 5.0, fieldGap + crowns * 14.0);
                // Lanes: tarmac between grass verges, kept clear of the trees.
                float verge = 1.0 - smoothstep(4.5, 8.0 + footprint * 1.5, laneGap);
                float lane = 1.0 - smoothstep(2.4, 3.2 + footprint * 1.2, laneGap);
                cropColor = mix(cropColor, sward * vec3(1.20, 1.10, 0.95), verge * 0.8);
                cropColor = mix(cropColor, vec3(0.150, 0.146, 0.138) * (0.9 + 0.2 * meso.g), lane);
                forest = max(forest, max(hedge, wooded) * (1.0 - verge));
                meadow = cropColor;
            }
        }

        albedo = mix(meadow, canopy * 0.9, scrub);
        albedo = mix(albedo, canopy, forest);
        albedo = mix(albedo, stone * (0.80 + 0.40 * macro.a), rock);
        vec3 snowColor = vec3(0.86, 0.89, 0.93) * (0.94 + 0.08 * meso.r);
        albedo = mix(albedo, snowColor, snow);
        roughness = mix(mix(mix(0.92, 0.88, forest), 0.86, rock), 0.55, snow);

        // Surface relief is too fine to see from far off, where its five
        // lookups would be most of this shader's cost: it fades out and is
        // then not read at all.
        float reliefFade = 1.0 - smoothstep(6000.0, 10000.0, viewDistance);
        if (detailLevel > 0.5 && reliefFade > 0.0) {
            // Each material's relief is read only where that material shows
            // and is near enough to be made out; elsewhere its mean will do.
            vec4 detail = vec4(0.0, 0.0, 0.0, 1.0);
            if (nearWeight > 0.004) {
                vec4 grassDetail = layerDetail(nearUv, LAYER_GRASS);
                detail = vec4(grassDetail.xy * nearWeight, 0.0, mix(1.0, grassDetail.w, nearWeight));
            }
            float wooded = max(forest, scrub * 0.8);
            if (wooded > 0.004) {
                // Crowns stand proud of the gaps between them, and catch the sun.
                vec4 crownDetail = layerDetail(woodUv, LAYER_WOODLAND);
                detail = mix(detail, vec4(crownDetail.xy * 1.6 * (1.0 - exp(-viewDistance / 500.0)), 0.0, mix(crownDetail.w, 0.8, underfoot)), wooded);
            }
            if (rock > 0.004) {
                vec4 stoneFar = layerDetail(stoneUv, LAYER_ROCK);
                vec4 stone4 = vec4(stoneFar.xy * 1.4, 0.0, stoneFar.w);
                if (nearWeight > 0.004) {
                    vec4 stoneNear = layerDetail(p / 11.0, LAYER_ROCK);
                    stone4 = vec4(stone4.xy + stoneNear.xy * nearWeight, 0.0, stone4.w * mix(1.0, stoneNear.w, nearWeight));
                }
                detail = mix(detail, stone4, rock);
            }
            if (snow > 0.004) {
                vec2 drift = viewDistance < 3000.0 ? layerDetail(p / 14.0, LAYER_SNOW).xy : vec2_splat(0.0);
                detail = mix(detail, vec4(drift * 0.45, 0.0, 1.0), snow);
            }
            slopeDetail = detail.xy * reliefFade;
            occlusion = mix(1.0, detail.w, reliefFade);
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
        macroShadow = mix(1.0, reliefShadow(p, bed, mapLevel), mapped);
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
