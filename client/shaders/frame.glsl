// Per-frame constants shared by every OpenFlightSim shader.
//
// One packed array instead of dozens of named uniforms: the renderer fills it
// once per frame from FrameConstants (client/src/renderer.hpp), and the layouts
// must stay in step. Positions are in render space (+X east, +Y up, +Z south),
// relative to the floating origin unless stated otherwise.

#ifndef OFS_FRAME_GLSL
#define OFS_FRAME_GLSL

uniform vec4 u_frame[24];

#define u_cameraPos      u_frame[0]   // xyz eye, w time in seconds
#define u_worldOrigin    u_frame[1]   // xyz origin offset (absolute = position + this), w exposure
#define u_sunDirection   u_frame[2]   // xyz unit vector toward the sun, w radiance scale of emissive materials
#define u_sunIrradiance  u_frame[3]   // rgb direct sun at the camera altitude, w cloud shadow strength
#define u_ambient0       u_frame[4]   // L1 irradiance: constant term
#define u_ambientX       u_frame[5]   //   gradient along +X
#define u_ambientY       u_frame[6]   //   gradient along +Y
#define u_ambientZ       u_frame[7]   //   gradient along +Z
#define u_viewport       u_frame[8]   // width, height, 1/width, 1/height of the scene target
#define u_atmoGeometry   u_frame[9]   // planet radius, atmosphere height, Rayleigh scale height, Mie scale height
#define u_rayleigh       u_frame[10]  // rgb scattering at sea level, w Mie anisotropy
#define u_mie            u_frame[11]  // x Mie scattering, y Mie extinction, z fog extinction, w fog scale height
#define u_ozone          u_frame[12]  // rgb absorption, w absolute eye altitude
#define u_groundAlbedo   u_frame[13]  // rgb mean terrain albedo, w aerial-perspective range
#define u_solar          u_frame[14]  // rgb irradiance above the atmosphere, w lake/water time
#define u_cloudLayer     u_frame[15]  // coverage, base altitude, thickness, enabled
#define u_cloudWeather   u_frame[16]  // xy wind displacement in metres, z extinction per metre, w weather-map 1/size
#define u_cloudShape     u_frame[17]  // x cirrus coverage, y cirrus altitude, z march range, w cloud-type bias
#define u_shadowParams   u_frame[18]  // x cascade count, y 1/tile size in texels, z depth bias, w strength
#define u_shadowTexel    u_frame[19]  // xyz world size of one texel per cascade, w blend band fraction
#define u_misc           u_frame[20]  // x frame jitter 0..1, y 1 when the framebuffer origin is bottom-left, z precipitation, w wetness
#define u_cameraForward  u_frame[21]  // xyz view direction, w scene range scale (metres per stored unit)
#define u_quality        u_frame[22]  // x terrain detail level, y water quality, z specular antialiasing, w terrain shadow steps
#define u_wind           u_frame[23]  // xyz wind in render space (m/s), w gust phase

// Screen position in [0,1]^2 of the fragment being shaded, in the orientation
// render-target textures use on this backend.
vec2 ofsScreenUv(vec4 fragCoord) { return fragCoord.xy * u_viewport.zw; }

// Converts between that orientation and normalised device coordinates.
vec2 ofsUvToNdc(vec2 uv)
{
    vec2 ndc = uv * 2.0 - 1.0;
    ndc.y = mix(-ndc.y, ndc.y, u_misc.y);
    return ndc;
}

vec2 ofsNdcToUv(vec2 ndc)
{
    return vec2(ndc.x * 0.5 + 0.5, mix(0.5 - ndc.y * 0.5, 0.5 + ndc.y * 0.5, u_misc.y));
}

#endif // OFS_FRAME_GLSL
