// Aerial-perspective atlas: light scattered into the line of sight between the
// camera and 32 depths across the view frustum. Surfaces look it up by screen
// position and range, which gives every distant object physically derived haze
// that matches the sky behind it.

$input v_uv
#include <bgfx_shader.sh>
#include "atmosphere_march.glsl"

void main()
{
    vec2 pixel = gl_FragCoord.xy;
    vec2 tile = floor(pixel / OFS_AERIAL_TILE);
    float slice = tile.x + tile.y * 8.0;
    vec2 local = pixel - tile * OFS_AERIAL_TILE;
    vec2 uv = (local - 0.5) / (OFS_AERIAL_TILE - 1.0);
    vec3 ray = atmoViewRay(uv);

    float fraction = (slice + 1.0) / OFS_AERIAL_SLICES;
    float pathEnd = u_groundAlbedo.w * fraction * fraction;
    float eye = max(u_ozone.w, 0.5);
    // The flat world's ground bounds the path for downward rays.
    if (ray.y < -1.0e-5) pathEnd = min(pathEnd, eye / -ray.y);

    float sunCos = u_sunDirection.y;
    float nu = dot(ray, u_sunDirection.xyz);
    vec2 phases = vec2(atmoPhaseRayleigh(nu), atmoPhaseMie(nu, u_rayleigh.w));
    vec3 radiance = vec3_splat(0.0);
    vec3 through = vec3_splat(1.0);
    for (int i = 0; i < 20; ++i) {
        float a = float(i) / 20.0;
        float b = float(i + 1) / 20.0;
        float along = pathEnd * (a * a + b * b) * 0.5;
        float stepLength = pathEnd * (b * b - a * a);
        float altitude = max(eye + ray.y * along, 0.0);
        vec3 extinction = vec3_splat(0.0);
        vec3 source = atmoSource(altitude, sunCos, phases, extinction);
        vec3 stepThrough = exp(-extinction * stepLength);
        radiance += through * source * (vec3_splat(1.0) - stepThrough) / max(extinction, vec3_splat(1.0e-9));
        through *= stepThrough;
    }
    gl_FragColor = vec4(radiance, dot(through, vec3_splat(1.0 / 3.0)));
}
