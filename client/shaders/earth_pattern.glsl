// Shared by the round Earth's terrain shaders: the hash and noise of
// client/src/earth_pattern.hpp, and the towns the nearest of which the
// renderer lists. Keep them the same as there.

// The nearest towns and villages: centre and radius, then the east and south
// axes of their streets (east's .w the kind: 0 village, 1 town, 2 city).
uniform vec4 u_earthTowns[48];
// Which of the towns (x) and runways (y) the renderer lists reach this chunk, as bits.
uniform vec4 u_earthCounts;

// earthHash.
float cellHash(vec2 p)
{
    vec3 p3 = fract(vec3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// earthNoise.
float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = p - i;
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = cellHash(i);
    float b = cellHash(i + vec2(1.0, 0.0));
    float c = cellHash(i + vec2(0.0, 1.0));
    float d = cellHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// earthTownDensity.
float townDensity(float distance, float radius, vec2 pf)
{
    float ragged = radius * (0.78 + 0.44 * valueNoise(pf / 640.0 + vec2(3.0, 5.0)));
    return 1.0 - smoothstep(0.25 * ragged, ragged, distance);
}

// The most built-up place at a point: how built up (z), where the point lies
// in its streets (xy) and its kind (w).
vec4 earthTown(vec3 position, vec2 pf)
{
    vec4 best = vec4(0.0, 0.0, 0.0, 0.0);
    float townMask = u_earthCounts.x;
    for (int i = 0; i < 16; ++i) {
        if (townMask < 0.5) break;
        float listed = mod(townMask, 2.0);
        townMask = floor(townMask * 0.5);
        if (listed < 0.5) continue;
        vec4 c = u_earthTowns[3 * i];
        vec3 d = position - c.xyz;
        vec2 xy = vec2(dot(d, u_earthTowns[3 * i + 1].xyz), dot(d, u_earthTowns[3 * i + 2].xyz));
        float dist = length(xy);
        if (dist > c.w * 1.25) continue;
        float density = townDensity(dist, c.w, pf);
        if (density > best.z) best = vec4(xy, density, u_earthTowns[3 * i + 1].w);
    }
    return best;
}
