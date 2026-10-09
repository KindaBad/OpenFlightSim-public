// The cloud of a nuclear burst, marched as a volume.
//
// The body of the cloud is described analytically, about the vertical through
// the burst: a vortex ring under a dome (the cap, which starts as the
// fireball), a flared column (the stem), a ring of dust running out along the
// ground (the base surge) and a collar of condensation round the stem. Each is
// broken into billows by the cloud noise volume, sampled in coordinates that
// move with the gas: turning over in the ring and climbing in the stem.
//
// Shading is that of smoke lit by the sun and sky, with a short march toward
// the sun for self-shadowing, plus the light the gas gives off while it is
// hot: the surface cools and darkens first, so the heat inside shows in the
// creases between billows. The same pass adds the light the fireball throws
// on the ground and into the air around it. The shape comes from
// client/src/nuclear_cloud.hpp, which fills u_nuke.

$input v_uv
#include <bgfx_shader.sh>
#include "common.glsl"

SAMPLER3D(s_cloudShape, 11);
SAMPLER3D(s_cloudDetail, 12);
SAMPLER2D(s_sceneRange, 13);
SAMPLER2D(s_cloudLayer, 14);
SAMPLER2D(s_cloudDepth, 15);
uniform mat4 u_ofsInvViewProj;
uniform vec4 u_nuke[8];

#define NUKE_GROUND   u_nuke[0].xyz // where the burst was, relative to the floating origin
#define NUKE_AGE      u_nuke[0].w
#define NUKE_HEIGHT   u_nuke[1].x   // of the ring's core above the ground
#define NUKE_RING     u_nuke[1].y   // radius of the ring's core
#define NUKE_TUBE     u_nuke[1].z   // radius of the ring's tube; of the fireball before there is a ring
#define NUKE_STEM     u_nuke[1].w   // radius of the stem
#define NUKE_SURGE    u_nuke[2].x   // radius the base surge has reached
#define NUKE_SURGE_H  u_nuke[2].y   // and how high it stands
#define NUKE_HEAT     u_nuke[2].z   // 1 is orange heat, above it white
#define NUKE_OPACITY  u_nuke[2].w
#define NUKE_STEPS    u_nuke[3].z
#define NUKE_CLOUDS   u_nuke[3].w   // 1 when the weather cloud layer can hide it
#define NUKE_CAP_TINT u_nuke[4].rgb
#define NUKE_ROLL     u_nuke[4].w   // how far the ring has turned over, radians
#define NUKE_DUST     u_nuke[5].rgb
#define NUKE_SCROLL   u_nuke[5].w   // how far the gas in the stem has climbed, metres
#define NUKE_GLOW     u_nuke[6].rgb // light of the fireball
#define NUKE_REACH    u_nuke[6].w   // and the distance it carries
#define NUKE_SKIRT_H  u_nuke[7].x
#define NUKE_SKIRT_R  u_nuke[7].y
#define NUKE_SKIRT    u_nuke[7].z
#define NUKE_SURGE_D  u_nuke[7].w   // density of the base surge

// How far noise can push a surface out, in units of the fields below.
#define NUKE_MARGIN 0.50

// The four bodies as fields that are positive inside and fall by one over the
// body's own radius: x cap, y stem, z base surge, w condensation collar.
vec4 nukeFields(vec3 q, float r)
{
    vec2 tube = vec2(r - NUKE_RING, q.y - NUKE_HEIGHT);
    float torus = 1.0 - length(tube) / NUKE_TUBE;
    // A dome over the ring closes the head of the mushroom.
    float dome = 1.0 - length(vec2(r / (NUKE_RING + NUKE_TUBE * 0.55),
                                   (q.y - NUKE_HEIGHT - NUKE_TUBE * 0.18) / (NUKE_TUBE * 0.82)));
    float cap = max(torus, dome);

    // Flared where it leaves the ground and again where it enters the cap.
    float flare = 1.0 + 1.1 * exp(-max(q.y, 0.0) / (0.05 * NUKE_HEIGHT + 60.0))
                + 1.3 * smoothstep(0.55, 1.0, q.y / NUKE_HEIGHT);
    float stem = min(1.0 - r / (NUKE_STEM * flare),
                     min((NUKE_HEIGHT - q.y) / NUKE_TUBE + 0.5, q.y / 80.0 + 1.0));

    float ring = 1.0 - length(vec2((r - NUKE_SURGE) / (2.6 * NUKE_SURGE_H), q.y / NUKE_SURGE_H));
    // A thin floor of dust left inside the ring.
    float floorDust = min(min(1.0 - q.y / (0.45 * NUKE_SURGE_H), (NUKE_SURGE - r) / NUKE_SURGE_H), 0.30);
    float surge = NUKE_SURGE_D > 0.0 ? max(ring, floorDust) : -9.0;

    float skirt = -9.0;
    if (NUKE_SKIRT > 0.0) {
        // A shallow bell hanging round the stem.
        float out_ = r - NUKE_SKIRT_R;
        skirt = 1.0 - length(vec2(out_ / (0.70 * NUKE_SKIRT_R),
                                  (q.y - NUKE_SKIRT_H + 0.45 * out_) / (0.13 * NUKE_SKIRT_R + 25.0)));
    }
    return vec4(cap, stem, surge, skirt);
}

// A lower bound on the distance to anything that could have density.
float nukeClearance(vec4 f)
{
    vec4 gap = max(-f - vec4_splat(NUKE_MARGIN), vec4_splat(0.0));
    return min(min(gap.x * NUKE_TUBE * 0.8, gap.y * NUKE_STEM * 0.5),
               min(gap.z * NUKE_SURGE_H * 0.9, gap.w * NUKE_SKIRT_R * 0.09 + (f.w < -8.0 ? 1.0e6 : 0.0)));
}

// Noise coordinates that belong to the cap: they rise and spread with it, and
// the billows well up through them as the ring turns over. (Turning the
// coordinates themselves about the ring's core winds the billows into rings
// wherever the turn is not the same on both sides of them.)
vec3 nukeCapCoord(vec3 q, float r)
{
    return vec3(q.x, q.y - NUKE_HEIGHT - NUKE_ROLL * NUKE_TUBE * 0.16, q.z) / NUKE_TUBE;
}
vec3 nukeStemCoord(vec3 q)
{
    return vec3(q.x, (q.y - NUKE_SCROLL) * 0.62, q.z) / NUKE_STEM;
}

// Density from one octave of noise in the densest body: enough for shadows.
float nukeShade(vec3 q)
{
    float r = length(q.xz);
    vec4 f = nukeFields(q, r);
    float best = max(max(f.x, f.y), f.z);
    if (best < -NUKE_MARGIN) {
        return 0.0;
    }
    float n;
    float gain = 1.0;
    if (f.x >= f.y && f.x >= f.z) {
        n = texture3DLod(s_cloudShape, nukeCapCoord(q, r) * 0.31 + vec3_splat(0.31), 2.5).r;
    } else if (f.y >= f.z) {
        n = texture3DLod(s_cloudShape, nukeStemCoord(q) * 0.30 + vec3_splat(0.67), 2.5).r;
    } else {
        n = texture3DLod(s_cloudShape, q / (NUKE_SURGE_H * 6.5), 2.5).r;
        gain = NUKE_SURGE_D;
    }
    return ofsSaturate((best + (n - 0.55) * 1.0) * 5.0) * gain;
}

// Density at q, the colour of what is there and how hot it is.
float nukeDensity(vec3 q, float r, vec4 f, out vec3 albedo, out float temperature, out float depth)
{
    float density = 0.0;
    albedo = NUKE_CAP_TINT;
    temperature = 0.0;
    depth = 0.0;
    if (f.x > -NUKE_MARGIN) {
        vec3 p = nukeCapCoord(q, r);
        float n = texture3DLod(s_cloudShape, p * 0.31 + vec3_splat(0.31), 2.0).r * 0.70
                + texture3DLod(s_cloudShape, p * 0.93 + vec3_splat(0.83), 1.0).r * 0.30;
        float fine = texture3DLod(s_cloudDetail, p * 2.3, 0.0).r;
        float inside = f.x + (n - 0.55) * 1.0 - fine * 0.12;
        density = ofsSaturate(inside * 5.0);
        depth = inside;
        // Hot through the body, cooler toward the skin and in the lumps that
        // have been thrown furthest out.
        // The billows crust over with soot as they cool; the creases between
        // them stay open on the fire.
        float crust = 0.10 + 0.90 * ofsSaturate((NUKE_HEAT - 0.55) * 1.5);
        temperature = NUKE_HEAT * mix(crust, 1.0, max(smoothstep(0.10, 0.70, inside), smoothstep(0.56, 0.36, n)));
    }
    if (f.y > -NUKE_MARGIN) {
        vec3 p = nukeStemCoord(q);
        float n = texture3DLod(s_cloudShape, p * 0.30 + vec3_splat(0.67), 1.5).r * 0.66
                + texture3DLod(s_cloudShape, p * 0.86 + vec3_splat(0.19), 1.0).r * 0.34;
        float inside = f.y + (n - 0.55) * 1.25;
        float stem = ofsSaturate(inside * 4.5);
        if (stem > density) {
            // The stem is dust below and the cap's own smoke where it enters it.
            albedo = mix(NUKE_DUST, NUKE_CAP_TINT, smoothstep(0.55, 0.95, q.y / NUKE_HEIGHT));
            // Drawn up through the middle, the heat of the cap reaches a way down it.
            temperature = NUKE_HEAT * NUKE_HEAT * smoothstep(0.05, 0.6, inside)
                        * (0.25 + 0.75 * smoothstep(0.25, 1.0, q.y / NUKE_HEIGHT)) * (0.7 + 0.5 * (1.0 - n));
            density = stem;
            depth = inside;
        }
    }
    if (f.z > -NUKE_MARGIN) {
        vec3 p = q / (NUKE_SURGE_H * 6.5);
        float n = texture3DLod(s_cloudShape, p + vec3(0.0, NUKE_AGE * 0.004, 0.0), 2.0).r * 0.65
                + texture3DLod(s_cloudShape, p * 3.1 + vec3_splat(0.47), 1.0).r * 0.35;
        float inside = f.z + (n - 0.55) * 0.80;
        float surge = ofsSaturate(inside * 5.0) * NUKE_SURGE_D;
        if (surge > density) {
            albedo = NUKE_DUST * 1.12;
            temperature = 0.0;
            density = surge;
            depth = inside;
        }
    }
    if (f.w > -NUKE_MARGIN) {
        float n = texture3DLod(s_cloudShape, q / (NUKE_SKIRT_R * 1.6) + vec3_splat(0.11), 2.0).r;
        float inside = f.w + (n - 0.55) * 1.0 - 0.15;
        float skirt = ofsSaturate(inside * 4.0) * NUKE_SKIRT;
        if (skirt > density) {
            // Water, not smoke.
            albedo = vec3_splat(0.93);
            temperature = 0.0;
            density = skirt;
            depth = inside * 0.3;
        }
    }
    return density * NUKE_OPACITY;
}

// Light given off by gas at `temperature`: red, through orange and yellow, to white.
vec3 nukeFire(float temperature)
{
    float t = max(temperature, 0.0);
    vec3 hue = mix(vec3(1.0, 0.30, 0.05), vec3(1.0, 0.62, 0.24), smoothstep(0.25, 0.95, t));
    hue = mix(hue, vec3(1.0, 0.93, 0.80), smoothstep(0.9, 1.35, t));
    return hue * (t * t * t * 3.4);
}

// Where a ray meets a vertical cylinder standing on y = low: enter and leave.
vec2 nukeCylinder(vec3 ro, vec3 rd, float radius, float low, float high)
{
    float a = dot(rd.xz, rd.xz);
    float b = dot(ro.xz, rd.xz);
    float c = dot(ro.xz, ro.xz) - radius * radius;
    vec2 span = vec2(-1.0e9, 1.0e9);
    if (a > 1.0e-8) {
        float disc = b * b - a * c;
        if (disc < 0.0) {
            return vec2(1.0, -1.0);
        }
        float root = sqrt(disc);
        span = vec2((-b - root) / a, (-b + root) / a);
    } else if (c > 0.0) {
        return vec2(1.0, -1.0);
    }
    if (abs(rd.y) > 1.0e-6) {
        float t0 = (low - ro.y) / rd.y;
        float t1 = (high - ro.y) / rd.y;
        span = vec2(max(span.x, min(t0, t1)), min(span.y, max(t0, t1)));
    } else if (ro.y < low || ro.y > high) {
        return vec2(1.0, -1.0);
    }
    return span;
}

void main()
{
    vec2 ndc = ofsUvToNdc(v_uv);
    vec4 farPoint = mul(u_ofsInvViewProj, vec4(ndc, 1.0, 1.0));
    vec4 nearPoint = mul(u_ofsInvViewProj, vec4(ndc, 0.0, 1.0));
    vec3 ray = normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
    vec3 ro = u_cameraPos.xyz - NUKE_GROUND;
    vec3 sun = u_sunDirection.xyz;
    float sceneRange = texture2DLod(s_sceneRange, v_uv, 0.0).r * u_cameraForward.w;

    vec2 span = nukeCylinder(ro, ray, u_nuke[3].x, -60.0, u_nuke[3].y);
    float enter = max(span.x, 0.0);
    float leave = min(span.y, sceneRange);

    float groundAltitude = NUKE_GROUND.y + u_worldOrigin.y;
    vec3 sunLight = atmoSunIrradiance(groundAltitude + NUKE_HEIGHT * 0.7, sun.y);
    vec3 skyLight = ofsAmbientIrradiance(vec3(0.0, 1.0, 0.0));
    vec3 groundLight = ofsAmbientIrradiance(vec3(0.0, -1.0, 0.0));
    float cosTheta = dot(ray, sun);
    float phase = 0.80 + 0.9 * pow(ofsSaturate(cosTheta), 5.0) + 0.15 * ofsSaturate(-cosTheta);
    // Strides are a fraction of the body the ray is in, fine enough to find
    // its skin; an opaque body ends the march within a dozen of them, so the
    // count is spent on the edges.
    float fineStep = min(NUKE_TUBE, NUKE_STEM * 1.5) * 0.07;

    vec3 radiance = vec3_splat(0.0);
    float transmittance = 1.0;
    float meanRange = 0.0;
    float weight = 0.0;
    float previous = 0.0;
    if (leave > enter) {
        float t = enter + NUKE_TUBE * 0.07 * ofsDither(gl_FragCoord.xy);
        for (int i = 0; i < 96; ++i) {
            if (float(i) >= NUKE_STEPS || t >= leave || transmittance < 0.015) {
                break;
            }
            vec3 q = ro + ray * t;
            float r = length(q.xz);
            vec4 f = nukeFields(q, r);
            float clear = nukeClearance(f);
            float scale = NUKE_TUBE;
            if (f.y > f.x && f.y > -NUKE_MARGIN) {
                scale = NUKE_STEM * 1.5;
            }
            if (f.z > max(f.x, f.y) && f.z > -NUKE_MARGIN) {
                scale = NUKE_SURGE_H * 2.0;
            }
            if (f.w > max(max(f.x, f.y), f.z) && f.w > -NUKE_MARGIN) {
                scale = NUKE_SKIRT_R * 0.3;
            }
            scale = min(scale, NUKE_TUBE);
            float extinction = 9.0 / scale;
            // Longer strides the further the ray has come, to reach the far side.
            float stride = scale * 0.07 * (1.0 + float(i) * 0.05) * (1.0 + t * 0.00004);
            if (clear > stride) {
                // Not to the same surface for every pixel, or the strides
                // that follow would cut the cloud into shells.
                t += max(clear - stride * ofsDither(gl_FragCoord.xy + vec2(float(i) * 7.0, 3.0)), stride * 0.5);
                previous = 0.0;
                continue;
            }
            vec3 albedo;
            float temperature;
            float depth;
            float density = nukeDensity(q, r, f, albedo, temperature, depth);
            if (density <= 0.002) {
                stride *= 0.45;
            } else {
                // Sunlight left after the cloud between here and the sun.
                float tau = nukeShade(q + sun * (NUKE_TUBE * 0.10)) * 0.20
                          + nukeShade(q + sun * (NUKE_TUBE * 0.36)) * 0.42;
                // The cap shades the stem and the ground under it.
                if (sun.y > 0.02 && q.y < NUKE_HEIGHT - NUKE_TUBE * 0.4) {
                    vec3 under = q + sun * ((NUKE_HEIGHT - q.y) / sun.y);
                    tau += 0.5 * ofsSaturate((1.0 - length(under.xz) / (NUKE_RING + NUKE_TUBE)) * 4.0) * NUKE_OPACITY;
                }
                tau *= 9.0;
                // Light scattered many times over still gets through thick smoke.
                float sunReach = max(exp(-tau), 0.22 * exp(-0.22 * tau));
                float open = ofsSaturate(1.0 - depth * 1.6);
                vec3 lit = (sunLight * (sunReach * phase)
                            + mix(groundLight, skyLight, ofsSaturate(0.5 + (q.y - NUKE_HEIGHT * 0.5) / NUKE_TUBE))
                              * (0.35 + 0.85 * open)) * (2.6 / (4.0 * OFS_PI));
                // The fire inside lights the smoke around it from within.
                lit += NUKE_GLOW * (0.005 * ofsSaturate(1.0 - length(q - vec3(0.0, NUKE_HEIGHT, 0.0)) / (NUKE_RING + NUKE_TUBE * 2.5)));
                vec3 source = albedo * lit + nukeFire(temperature) * (11.0 * u_sunDirection.w);
                // The mean of this sample and the last, so the skin of the
                // cloud is not cut into slices one stride thick.
                float sigma = 0.5 * (density + previous) * extinction;
                // Shorter strides through the skin, where the eye stops.
                stride *= density < 0.7 ? 0.45 : 1.0;
                float through = exp(-sigma * stride);
                float seen = transmittance * (1.0 - through);
                radiance += source * seen;
                meanRange += t * seen;
                weight += seen;
                transmittance *= through;
            }
            previous = density;
            t += stride;
        }
    }

    float alpha = 1.0 - transmittance;
    vec3 color = vec3_splat(0.0);
    if (weight > 1.0e-5) {
        meanRange /= weight;
        vec3 hazed = ofsApplyAerial(radiance / max(alpha, 1.0e-4), v_uv, ray, meanRange) * alpha;
        float shown = 1.0;
        if (NUKE_CLOUDS > 0.5) {
            // The cloud layer is drawn coarser than the screen: taken from
            // four places round the pixel, its edge is not a row of blocks.
            shown = 0.0;
            for (int tap = 0; tap < 4; ++tap) {
                vec2 offset = vec2(float(tap - 2 * (tap / 2)) - 0.5, float(tap / 2) - 0.5) * u_viewport.zw * 6.0;
                float cover = texture2DLod(s_cloudLayer, v_uv + offset, 0.0).a;
                float cloudRange = texture2DLod(s_cloudDepth, v_uv + offset, 0.0).r * u_cameraForward.w;
                shown += 0.25 * (1.0 - cover * smoothstep(0.7, 1.15, meanRange / max(cloudRange, 1.0)));
            }
        }
        color = hazed * shown;
        alpha *= shown;
    }

    // The light of the fireball: on whatever the ray ends on, and scattered in
    // the air round the fireball itself.
    float glow = max(NUKE_GLOW.r, max(NUKE_GLOW.g, NUKE_GLOW.b));
    if (glow > 0.0) {
        vec3 centre = vec3(0.0, NUKE_HEIGHT, 0.0);
        vec3 added = vec3_splat(0.0);
        if (sceneRange < 150000.0) {
            vec3 hit = ro + ray * sceneRange - centre;
            float d2 = dot(hit, hit) / (NUKE_REACH * NUKE_REACH);
            added += NUKE_GLOW * (0.16 / ((1.0 + d2) * (1.0 + d2)));
        }
        float along = clamp(dot(centre - ro, ray), 0.0, sceneRange);
        float miss = length(ro + ray * along - centre);
        added += NUKE_GLOW * (0.55 * exp(-miss / (NUKE_REACH * 0.16)) + 0.06 * exp(-miss / (NUKE_REACH * 0.7)))
               * ofsSaturate(along / max(NUKE_TUBE, 1.0));
        color += added * transmittance;
    }
    gl_FragColor = vec4(color, alpha);
}
