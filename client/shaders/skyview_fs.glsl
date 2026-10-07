// Sky-view table (Hillaire 2020): radiance of the whole sky as seen from the
// camera's altitude, integrated once per frame at low resolution. The sky pass,
// glossy reflections and water all read it instead of marching per pixel.

$input v_uv
#include <bgfx_shader.sh>
#include "atmosphere_march.glsl"

void main()
{
    // Texel centres span the full parameter range; see atmoSkyUv.
    vec2 uv = (gl_FragCoord.xy - 0.5) / vec2(OFS_SKY_WIDTH - 1.0, OFS_SKY_HEIGHT - 1.0);
    float azimuth = clamp(uv.x, 0.0, 1.0) * OFS_PI;
    float t = clamp(uv.y, 0.0, 1.0) * 2.0 - 1.0;
    float elevation = t * abs(t) * 0.5 * OFS_PI;

    // A frame with the sun's azimuth along +X.
    float sunCos = u_sunDirection.y;
    float sunSin = sqrt(max(1.0 - sunCos * sunCos, 0.0));
    vec3 dir = vec3(cos(elevation) * cos(azimuth), sin(elevation), cos(elevation) * sin(azimuth));
    float mu = dir.y;
    float nu = dir.x * sunSin + dir.y * sunCos;

    float ground = u_atmoGeometry.x;
    float h0 = clamp(u_ozone.w, 1.0, u_atmoGeometry.y - 1.0);
    float r = ground + h0;
    float top = ground + u_atmoGeometry.y;
    float rho2 = h0 * (2.0 * ground + h0);
    float discriminant = r * r * mu * mu - rho2;
    bool hitsGround = mu < 0.0 && discriminant >= 0.0;
    float pathEnd = hitsGround ? -r * mu - sqrt(max(discriminant, 0.0))
                               : -r * mu + sqrt(max(r * r * mu * mu + (top - r) * (top + r), 0.0));

    vec2 phases = vec2(atmoPhaseRayleigh(nu), atmoPhaseMie(nu, u_rayleigh.w));
    vec3 radiance = vec3_splat(0.0);
    vec3 through = vec3_splat(1.0);
    for (int i = 0; i < 40; ++i) {
        // Quadratic spacing: most of the optical depth of a level ray is close by.
        float a = float(i) / 40.0;
        float b = float(i + 1) / 40.0;
        float along = pathEnd * (a * a + b * b) * 0.5;
        float stepLength = pathEnd * (b * b - a * a);
        float travelled = along * (along + 2.0 * r * mu);
        float altitude = max((travelled + rho2) / (sqrt(max(r * r + travelled, 0.0)) + ground), 0.0);
        float sunCosHere = clamp((r * sunCos + along * nu) / (ground + altitude), -1.0, 1.0);
        vec3 extinction = vec3_splat(0.0);
        vec3 source = atmoSource(altitude, sunCosHere, phases, extinction);
        vec3 stepThrough = exp(-extinction * stepLength);
        radiance += through * source * (vec3_splat(1.0) - stepThrough) / max(extinction, vec3_splat(1.0e-9));
        through *= stepThrough;
    }
    if (hitsGround) {
        // Lit terrain seen through the haze, for reflections that look downward.
        float sunCosGround = clamp((r * sunCos + pathEnd * nu) / ground, -1.0, 1.0);
        vec3 light = atmoSunIrradiance(0.0, sunCosGround) * max(sunCosGround, 0.0)
                   + atmoMultiScatter(0.0, sunCosGround) * u_solar.rgb * OFS_PI;
        radiance += through * u_groundAlbedo.rgb * light / OFS_PI;
    }
    gl_FragColor = vec4(radiance, 1.0);
}
