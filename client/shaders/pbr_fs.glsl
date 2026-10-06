// Forward PBR surface fragment shader: one directional sun, hemispheric
// ambient, a single-cascade shadow map and height/distance fog.

$input v_worldPos, v_normal, v_uv, v_surfacePos, v_tangent
#include <bgfx_shader.sh>
#include "common.glsl"

uniform vec4 u_worldOrigin;
uniform vec4 u_cameraPos;   // xyz used
uniform vec4 u_sunDirection;    // xyz: world, sun toward scene
uniform vec4 u_sunColor;
uniform vec4 u_skyAmbient;
uniform vec4 u_groundAmbient;

uniform vec4 u_baseColor;      // rgb + alpha
uniform vec4 u_metallicRoughness; // xy used
uniform vec4 u_emissive;     // rgb used
uniform vec4 u_doubleSided;   // x
SAMPLER2D(s_baseColor, 1);
SAMPLER2D(s_metallicRoughness, 2);
SAMPLER2D(s_emissive, 3);
SAMPLER2D(s_normal, 4);
SAMPLER2D(s_occlusion, 5);
uniform vec4 u_normalSettings;
uniform vec4 u_textureFlags;
uniform vec4 u_alphaSettings;

// Shadow map: a single depth-only directional pass. Sampling is a manual 3x3
// PCF over a comparison sampler, which is inexpensive and hides stair-stepping
// at this map size.
uniform mat4 u_shadowMatrix;
SAMPLER2DSHADOW(u_shadowMap, 0);
uniform vec4 u_shadowTexel;    // x
uniform vec4 u_shadowBias;     // x
uniform vec4 u_shadowStrength; // x

uniform vec4 u_fogColor;
uniform vec4 u_fogDensity;    // x
uniform vec4 u_fogHeightFalloff; // x
uniform vec4 u_fogGroundFade;  // x
uniform vec4 u_exposure;     // x

float surfaceNoise(vec2 p) {
    vec2 i=floor(p), f=fract(p);
    f=f*f*(3.0-2.0*f);
    vec4 h=fract(sin(vec4(dot(i,vec2(127.1,311.7)),dot(i+vec2(1,0),vec2(127.1,311.7)),
                         dot(i+vec2(0,1),vec2(127.1,311.7)),dot(i+1.0,vec2(127.1,311.7))))*43758.5453);
    return mix(mix(h.x,h.y,f.x),mix(h.z,h.w,f.x),f.y);
}

float sampleShadow(vec3 worldPos, float nDotL)
{
    const vec4 coord = mul(u_shadowMatrix, vec4(worldPos, 1.0));
    if (u_shadowStrength.x <= 0.0 || coord.w <= 0.0) {
        return 1.0;
    }
    const vec3 projected = coord.xyz / coord.w;
    // Outside the map the surface is unshadowed; clamping would smear the edge.
    if (any(lessThan(projected, vec3_splat(0.0))) || any(greaterThan(projected, vec3_splat(1.0)))) {
        return 1.0;
    }
    // Slope-scaled bias: grazing surfaces need much more depth offset.
    const float bias = u_shadowBias.x * (1.0 + (1.0 - nDotL) * 2.5);
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const vec2 offset = vec2(float(x), float(y)) * u_shadowTexel.x;
            sum += shadow2D(u_shadowMap, vec3(projected.xy + offset, projected.z - bias));
        }
    }
    const float visibility = sum / 9.0;
    return mix(1.0, visibility, u_shadowStrength.x);
}

void main()
{
    vec3 n = normalize(v_normal);
    // Authored tangent frame takes precedence; derivative frame is the fallback.
    if (u_textureFlags.w > .5) {
        vec3 t=vec3_splat(0.0); float hand=1.0;
        if(dot(v_tangent.xyz,v_tangent.xyz)>1e-10) {
            t=v_tangent.xyz; hand=v_tangent.w;
        } else {
            vec3 dx=dFdx(v_worldPos), dy=dFdy(v_worldPos);
            vec2 ux=dFdx(v_uv), uy=dFdy(v_uv);
            float determinant=ux.x*uy.y-ux.y*uy.x;
            if(abs(determinant)>1e-10) { t=(dx*uy.y-dy*ux.y)/determinant; hand=sign(determinant); }
        }
        t=t-n*dot(n,t);
        if(dot(t,t)>1e-10) {
            t=normalize(t); vec3 b=normalize(cross(n,t))*hand;
            vec3 mapped=texture2D(s_normal,v_uv).xyz*2.0-1.0;
            mapped.xy*=u_normalSettings.x;
            n=normalize(t*mapped.x+b*mapped.y+n*mapped.z);
        }
    }
    // Reverse the complete mapped normal: this is equivalent to reversing
    // all three TBN basis vectors for the back face, including tangential tilt.
    if (u_doubleSided.x > .5 && !gl_FrontFacing) n=-n;
    const vec3 v = normalize(u_cameraPos.xyz - v_worldPos);
    // The same thin canopy is visible from either side. Orient its normal
    // toward the viewer so an interior view does not become a grazing mirror.
    if (u_normalSettings.y > .001 && dot(n,v) < 0.0) n=-n;
    const vec3 l = normalize(-u_sunDirection.xyz);

    const vec4 mrTexel=texture2D(s_metallicRoughness,v_uv);
    const float metallic = clamp(u_metallicRoughness.x*mix(1.0,mrTexel.b,u_textureFlags.y), 0.0, 1.0);
    // A floor on roughness keeps degenerate materials from producing a mirror
    // that flickers between LOD levels.
    float roughness = clamp(u_metallicRoughness.y*mix(1.0,mrTexel.g,u_textureFlags.y), 0.045, 1.0);
    vec3 albedo = u_baseColor.rgb;
    const vec4 baseTexel=texture2D(s_baseColor,v_uv);
    albedo*=mix(vec3_splat(1.0),srgbToLinear(baseTexel.rgb),u_textureFlags.x);
    float alpha=u_baseColor.a*mix(1.0,baseTexel.a,u_textureFlags.x);
    if (u_alphaSettings.x>.5 && alpha<u_alphaSettings.y) discard;
    if (u_metallicRoughness.z > 0.5) {
        const vec2 p = v_surfacePos.xz;
        const float broad = surfaceNoise(p * .006) * .7 + surfaceNoise(p * .025) * .3;
        const float footprint = max(length(dFdx(p)), length(dFdy(p)));
        const float grain = (surfaceNoise(p * 12.0) - .5) * exp(-footprint * 3.0);
        if (u_metallicRoughness.z < 1.5) {
            const float moisture=surfaceNoise(p*.0011+vec2(37,11));
            const float meadow=surfaceNoise(p*.004+vec2(31,17));
            const float mottling=surfaceNoise(p*.11)*.55
                +mix(.5,surfaceNoise(p*.43),exp(-footprint*.5))*.30
                +mix(.5,surfaceNoise(p*1.7),exp(-footprint*2.0))*.15;
            albedo=mix(vec3(.047,.080,.021),vec3(.17,.205,.058),meadow);
            albedo=mix(albedo,vec3(.075,.14,.038),moisture*.35);
            float dirt=smoothstep(.58,.80,surfaceNoise(p*.035+vec2(16,7)))*.5;
            albedo=mix(albedo,vec3(.18,.13,.073),dirt*(1.0-moisture*.5));
            albedo*=.76+mottling*.50+grain*.15;
            // Sparse irregular farmland fades into natural meadows and hills.
            const vec2 warp=vec2(surfaceNoise(p*.0013),surfaceNoise(p*.0013+57.0))*170.0;
            const vec2 fieldPos=p+warp;
            const vec2 cell=floor(fieldPos/vec2(490,360));
            const float crop=fract(sin(dot(cell,vec2(127.1,311.7)))*43758.5453);
            const float fields=smoothstep(2200.0,4000.0,length(p))
                *(1.0-smoothstep(50.0,170.0,v_surfacePos.y))
                *(1.0-smoothstep(.40,.66,moisture));
            albedo=mix(albedo,mix(vec3(.075,.10,.031),vec3(.23,.185,.075),crop),fields*.36);
            float furrows=sin(fieldPos.x*.65+crop*12.0);
            albedo*=1.0+furrows*.07*fields*exp(-footprint*.6);
            const vec2 edge=min(fract(fieldPos/vec2(490,360)),1.0-fract(fieldPos/vec2(490,360)));
            const float hedge=1.0-smoothstep(.003,.013+footprint*.0008,min(edge.x,edge.y));
            albedo*=1.0-hedge*fields*.16;
            // Screen derivatives create filtered micro-relief without shifting
            // the shared terrain collision surface or shimmering from altitude.
            const vec2 dx=dFdx(p),dy=dFdy(p);
            const float determinant=dx.x*dy.y-dx.y*dy.x;
            const float height=mottling*.055*exp(-footprint*.7);
            if(abs(determinant)>1e-8) {
                const vec2 gradient=(dFdx(height)*vec2(dy.y,-dy.x)+dFdy(height)*vec2(-dx.y,dx.x))/determinant;
                n=normalize(n-vec3(gradient.x,0.0,gradient.y));
            }
            const float rock = smoothstep(.08,.38,1.0-n.y) + smoothstep(350.0,700.0,v_surfacePos.y)*.28;
            float strata=sin(v_surfacePos.y*.09+surfaceNoise(p*.02)*4.0);
            vec3 stone=mix(vec3(.13,.135,.12),vec3(.35,.31,.24),broad);
            stone*=1.0+strata*.10*exp(-footprint*.05);
            albedo=mix(albedo,stone,clamp(rock,0.0,.88));
            float snow=smoothstep(1450.0,1850.0,v_surfacePos.y+surfaceNoise(p*.008)*150.0)
                      *smoothstep(.45,.8,n.y);
            albedo=mix(albedo,vec3(.72,.78,.83),snow);
        } else if (u_metallicRoughness.z < 2.5) {
            albedo *= .82+broad*.30+grain*.10;
            // Asphalt seams and repaired patches, filtered by screen footprint.
            vec2 seam=min(fract(p/vec2(11,17)),1.0-fract(p/vec2(11,17)));
            float cracks=1.0-smoothstep(.001,.006+footprint*.005,min(seam.x,seam.y));
            albedo*=1.0-cracks*.20*exp(-footprint*.5);
            roughness=clamp(roughness+(broad-.5)*.15,.35,1.0);
            // Rubber wear follows the two main wheel tracks and fades at altitude.
            const float tracks = exp(-pow((abs(p.x)-3.8)/1.3,2.0));
            const float touchdown = exp(-pow((abs(p.y)-880.0)/170.0,2.0));
            albedo *= 1.0 - .38 * tracks * touchdown;
        } else if (u_metallicRoughness.z < 3.5) {
            const float variation=mix(.5,surfaceNoise(v_surfacePos.xz*1.8+v_surfacePos.y*.7),exp(-footprint*.8));
            // Per-tree seed avoids a uniform green coat; fine leaf clusters
            // darken recesses while the sky/sun illuminate the outer crown.
            const float treeTint=clamp(v_uv.x,0.0,1.0);
            albedo=mix(albedo*vec3(.66,.83,.74),albedo*vec3(1.18,1.10,.78),treeTint);
            albedo*=.58+variation*.70;
            albedo=mix(albedo,vec3(.13,.20,.042),smoothstep(.65,.9,variation)*.22);
        } else {
            albedo *= .72 + broad*.5 + grain*.1;
        }
    }

    const float nDotL = dot(n, l);
    const float shadow = sampleShadow(v_worldPos, max(nDotL, 0.0))
                       * cloudSunVisibility(v_worldPos+u_worldOrigin.xyz,l);

    // Direct term, gated by the shadow factor. Computed inline rather than
    // through shadeDirect() so the shadow can scale only the sun contribution.
    vec3 color;
    {
        const vec3 h = normalize(v + l);
        const float nDotV = max(dot(n, v), 1e-4);
        const float nDotH = max(dot(n, h), 0.0);
        const float vDotH = max(dot(v, h), 0.0);
        const vec3 f0 = mix(vec3_splat(0.04), albedo, metallic);
        const vec3 f = fresnelSchlick(vDotH, f0);
        const float d = distributionGGX(nDotH, roughness);
        const float g = geometrySmith(nDotV, max(nDotL, 0.0), roughness);
        const vec3 specular = (d * g * f) / (4.0 * nDotV * max(nDotL, 0.0) + 1e-4);
        const vec3 kd = (vec3_splat(1.0) - f) * (1.0 - metallic);
        const vec3 diffuse = kd * albedo / OFS_PI;
        color = (diffuse + specular) * u_sunColor.rgb * max(nDotL, 0.0) * shadow;
    }

    const float ao=mix(1.0,texture2D(s_occlusion,v_uv).r,u_normalSettings.z);
    const float nDotVEnv=max(dot(n,v),.001);
    const vec3 f0Env=mix(vec3_splat(.04),albedo,metallic);
    const vec3 fresnelEnv=fresnelSchlickRoughness(nDotVEnv,f0Env,roughness);
    // Analytic sky reflection with a roughness-dependent horizon, plus ground bounce.
    const vec3 reflection=reflect(-v,n);
    const float skyWeight=smoothstep(-.22-roughness*.4,.22+roughness*.4,reflection.y);
    vec3 environment=mix(u_groundAmbient.rgb,u_skyAmbient.rgb*2.4,skyWeight);
    const float sunLobe=pow(max(dot(reflection,l),0.0),mix(512.0,3.0,roughness*roughness));
    environment+=u_sunColor.rgb*sunLobe*(1.0-roughness)*.15*shadow;
    const vec3 diffuseAmbient=ambientHemispheric(n,albedo,0.0,u_skyAmbient.rgb,u_groundAmbient.rgb);
    color+=ao*diffuseAmbient*(vec3_splat(1.0)-fresnelEnv)*(1.0-metallic);
    color+=environment*fresnelEnv*mix(ao,1.0,1.0-roughness);
    if(u_metallicRoughness.z>2.5 && u_metallicRoughness.z<3.5) {
        // Light transmitted through foliage gives the sun-facing crown edges
        // a soft yellow-green rim, with a darker interior.
        const float backlight=pow(max(dot(v,-l),0.0),3.0);
        color+=albedo*u_sunColor.rgb*backlight*.16;
    }
    if (u_normalSettings.y>.001) {
        // Owned canopy glass gets the existing sky/ground environment, with
        // grazing-angle reflectance. No texture readback or legacy change.
        vec3 reflected=reflect(-v,n);
        float fresnel=.04+.96*pow(1.0-max(dot(n,v),0.0),5.0);
        vec3 environment=mix(u_groundAmbient.rgb,u_skyAmbient.rgb*2.5,
                             smoothstep(-.03,.18,reflected.y));
        environment+=u_sunColor.rgb*pow(max(dot(reflected,l),0.0),256.0)*.1;
        color+=environment*fresnel*u_normalSettings.y;
        alpha=mix(alpha,.94,fresnel*u_normalSettings.y);
    }
    color += u_emissive.rgb*mix(vec3_splat(1.0),srgbToLinear(texture2D(s_emissive,v_uv).rgb),u_textureFlags.z);

    const float viewDistance = length(u_cameraPos.xyz - v_worldPos);
    const float altitude = v_worldPos.y + u_worldOrigin.y;
    color = applyFog(color, viewDistance, altitude, u_cameraPos.y+u_worldOrigin.y, u_fogColor.rgb, u_fogDensity.x,
                     u_fogHeightFalloff.x, u_fogGroundFade.x);

    // Preserve linear radiance through transparency and effects; tone-map once
    // after the complete scene has been composited into the HDR framebuffer.
    gl_FragData[0] = vec4(color, alpha);
    gl_FragData[1] = vec4(min(viewDistance,u_weather.z),0.0,0.0,alpha);
}
