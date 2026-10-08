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
// Battle damage on an aircraft, in its body axes (x forward, y right, z down).
// Damage runs from 0 intact to 1 destroyed.
//   [0] left wing, right wing, tail, fuselage damage
//   [1] left engine, right engine damage, pattern seed, 1 when anything is
//       damaged, or 2 for a part that has broken away (see main)
//   [2] asset centre of gravity xyz, |y| of the wing stub that always remains
//   [3] wingtip |y|, aft and fore x of the outer wings, x the fins stand aft of
//   [4] fin base and top height, engine bay length and radius
//   [5] left engine bay aft end xyz
//   [6] right engine bay aft end xyz
uniform vec4 u_damage[7];
uniform mat4 u_ofsModel;

float damageHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float damageNoise(vec2 p)
{
    vec2 cell = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(damageHash(cell), damageHash(cell + vec2(1.0, 0.0)), f.x),
               mix(damageHash(cell + vec2(0.0, 1.0)), damageHash(cell + vec2(1.0, 1.0)), f.x), f.y);
}

// Shell holes scattered over a surface whose first axis points forward: x is 1
// inside a hole, y the soot around it, drawn out behind the hole by the
// airflow. `density` is the fraction of cells that have been hit.
vec2 damageHoles(vec2 p, float cellSize, float density, float seed)
{
    vec2 cell = floor(p / cellSize) + seed * 13.0;
    vec2 f = fract(p / cellSize) - 0.5;
    float hit = step(1.0 - density, damageHash(cell));
    vec2 q = f - (vec2(damageHash(cell + 17.0), damageHash(cell + 43.0)) - 0.5) * 0.5;
    float radius = 0.07 + 0.11 * damageHash(cell + 71.0);
    // An uneven rim reads as torn metal instead of a drilled circle.
    float ragged = 1.0 + 0.7 * (damageNoise(p * 11.0 / cellSize + seed) - 0.5);
    float core = length(q) / (radius * ragged);
    q.x *= q.x < 0.0 ? 0.3 : 1.0;
    float trail = length(q) / radius;
    return hit * vec2(1.0 - step(1.0, core), (1.0 - smoothstep(0.9, 2.6, trail)) * 0.85);
}

// Soot and heat around one engine bay: x darkening, y how deep inside a
// burnt-out jet pipe the surface is, z,w the outward direction from the bay
// axis in the body's y and z.
vec4 damageEngine(vec3 body, vec3 aft, float damage, float seed)
{
    float along = clamp(body.x - aft.x, -0.5, u_damage[4].z);
    vec3 fromAxis = body - (aft + vec3(along, 0.0, 0.0));
    float d = length(fromAxis);
    float reach = u_damage[4].w * (1.3 + 1.5 * damage);
    float streaks = 0.6 + 0.4 * damageNoise(vec2(body.x * 1.3, (body.y + body.z) * 7.0) + seed);
    float soot = damage * (1.0 - smoothstep(reach * 0.35, reach, d)) * streaks;
    float pipe = step(0.99, damage) * (1.0 - smoothstep(u_damage[4].w * 0.5, u_damage[4].w, d))
               * smoothstep(-0.1, 0.25, body.x - aft.x) * (1.0 - smoothstep(0.6, 2.2, body.x - aft.x));
    return vec4(soot, pipe, fromAxis.yz);
}

void main()
{
    vec3 geometric = normalize(v_normal);
    vec3 n = geometric;
    // Missing structure is cut away before any texture is read.
    float soot = 0.0;
    float pipeGlow = 0.0;
    vec2 pipeOutward = vec2_splat(0.0);
    bool damaged = u_damage[1].w > 0.5;
    if (damaged) {
        vec3 body = vec3(u_damage[2].x - v_surfacePos.x, u_damage[2].z - v_surfacePos.z, u_damage[2].y - v_surfacePos.y);
        float seed = u_damage[1].z;
        float side = step(0.0, body.y);
        float span = (abs(body.y) - u_damage[2].w) / max(u_damage[3].x - u_damage[2].w, 0.1);
        float height = (-body.z - u_damage[4].x) / max(u_damage[4].y - u_damage[4].x, 0.1);
        bool onWing = span > 0.0 && body.x > u_damage[3].y && body.x < u_damage[3].z;
        bool onFin = height > 0.0 && body.x < u_damage[3].w;
        // The break is ragged, and the same on the aircraft and on the piece
        // that left it, so the two fit where they parted.
        float wingBreak = (damageNoise(vec2(body.x * 2.3, seed + side * 9.0)) - 0.5) * 0.16;
        float finBreak = (damageNoise(vec2(body.x * 2.9, seed + 3.0)) - 0.5) * 0.2;
        if (u_damage[1].w > 1.5) {
            // A part that has broken away, drawn on its own: [0] is the part
            // (0 left wing, 1 right wing, 2 fin) and the inner and outer edge
            // of the slab that left. Everything else is cut.
            float part = u_damage[0].x;
            bool fin = part > 1.5;
            float along = fin ? height - finBreak : span - wingBreak;
            bool present = fin ? onFin : (onWing && abs(side - part) < 0.5);
            vec2 holes = fin ? damageHoles(body.xz, 0.45, 0.4, seed + 5.0) : damageHoles(body.xy, 0.5, 0.4, seed + side);
            if (!present || along <= u_damage[0].y || along > u_damage[0].z || (holes.x > 0.5 && gl_FrontFacing)) {
                discard;
            }
            soot = max(holes.y, 1.0 - smoothstep(0.0, 0.3, along - u_damage[0].y));
        } else {
            float wing = mix(u_damage[0].x, u_damage[0].y, side);
            if (wing > 0.0 && onWing) {
                // The outer panel goes first; a destroyed wing is a ragged stub.
                float remaining = mix(1.25, 0.08, smoothstep(0.4, 1.0, wing)) + wingBreak;
                vec2 holes = damageHoles(body.xy, 0.5, wing * 0.4, seed + side);
                // A hole is cut in the skin facing the viewer only, so the dark
                // inside of the wing shows through it instead of the sky beyond.
                if (span > remaining || (holes.x > 0.5 && gl_FrontFacing)) {
                    discard;
                }
                soot = max(soot, max(holes.y, smoothstep(remaining - 0.28, remaining, span) * step(0.4, wing)));
            }
            float tail = u_damage[0].z;
            if (tail > 0.0 && onFin) {
                float remaining = mix(1.25, 0.1, smoothstep(0.4, 1.0, tail)) + finBreak;
                vec2 holes = damageHoles(body.xz, 0.45, tail * 0.4, seed + 5.0);
                if (height > remaining || (holes.x > 0.5 && gl_FrontFacing)) {
                    discard;
                }
                soot = max(soot, max(holes.y, smoothstep(remaining - 0.3, remaining, height) * step(0.4, tail)));
            }
            vec4 left = damageEngine(body, u_damage[5].xyz, u_damage[1].x, seed);
            vec4 right = damageEngine(body, u_damage[6].xyz, u_damage[1].y, seed + 2.0);
            soot = max(soot, max(left.x, right.x));
            pipeGlow = max(left.y, right.y);
            pipeOutward = left.y > right.y ? left.zw : right.zw;
            // The fuselage is not hollowed out: its holes show as blackened pits.
            float fuselage = u_damage[0].w * step(abs(body.y), u_damage[2].w);
            if (fuselage > 0.0) {
                vec2 flank = damageHoles(body.xz, 0.6, fuselage * 0.3, seed + 11.0);
                vec2 back = damageHoles(body.xy, 0.6, fuselage * 0.3, seed + 17.0);
                soot = max(soot, max(max(flank.x, back.x), max(flank.y, back.y) * 0.8));
            }
        }
    }
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
    // A damaged airframe is drawn without culling, so the inside of a torn
    // skin is visible; it is shaded as bare, blackened structure below.
    bool interior = damaged && u_doubleSided.x < 0.5 && !gl_FrontFacing;
    if ((u_doubleSided.x > 0.5 || damaged) && !gl_FrontFacing) {
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
    if (interior) {
        soot = 1.0;
    }
    albedo = mix(albedo, vec3_splat(0.012), soot * 0.94);
    metallic *= 1.0 - soot;
    roughness = mix(roughness, 0.92, soot);

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
           * u_sunDirection.w * (1.0 - soot);
    if (pipeGlow > 0.0) {
        // Fire still burning inside a shot-out engine lights the wall of its
        // jet pipe: only surfaces that face the engine's axis, never the cowl.
        // Body y and z are asset -z and -y.
        vec3 outward = mul(u_ofsModel, vec4(0.0, -pipeOutward.y, -pipeOutward.x, 0.0)).xyz;
        float facing = ofsSaturate(-dot(geometric, normalize(outward + vec3(0.0, 1.0e-6, 0.0))) * 2.0);
        float flicker = 0.65 + 0.35 * sin(u_cameraPos.w * 19.0 + v_surfacePos.x * 9.0);
        color += vec3(2.6, 0.62, 0.07) * (pipeGlow * facing * flicker * u_sunDirection.w);
    }

    color = ofsApplyAerial(color, ofsScreenUv(gl_FragCoord), -v, viewDistance);

    // Linear scene radiance; exposure and the display transform are applied
    // once, after the whole frame has been composited.
    gl_FragData[0] = vec4(color, alpha);
    // Blended surfaces leave the opaque range behind them untouched.
    gl_FragData[1] = vec4(min(viewDistance / u_cameraForward.w, 65000.0), 0.0, 0.0, u_alphaSettings.z > 0.5 ? 0.0 : 1.0);
}
