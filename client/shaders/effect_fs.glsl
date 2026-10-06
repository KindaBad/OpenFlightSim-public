// UV bands select vapor, emissive streaks/cores, smoke and turbulent fire.
$input v_color, v_uv, v_worldPos
#include <bgfx_shader.sh>
#include "common.glsl"
uniform vec4 u_cameraPos, u_worldOrigin, u_fogColor, u_fogDensity, u_fogHeightFalloff, u_fogGroundFade, u_sunColor;
void main() {
    float style=floor(v_uv.x*.5);
    vec2 uv=v_uv-vec2(style*2.0,0.0);
    vec2 p=uv*2.0-1.0;
    float r2=dot(p,p);
    vec3 world=v_worldPos+u_worldOrigin.xyz;
    float noise=weatherNoise(world.xz*1.7+world.y*.3)*.65
              +weatherNoise(world.xz*5.3+world.y*.7)*.35;
    float falloff=exp(-4.5*r2)*(1.0-smoothstep(.55,1.0,r2));
    vec3 radiance=v_color.rgb;
    if (style>.5 && style<1.5) {
        // Thin feathered veil, with gentle density variation instead of puffs.
        falloff=exp(-5.5*p.y*p.y-1.8*p.x*p.x)
               *(1.0-smoothstep(.35,1.0,abs(p.y)))
               *(1.0-smoothstep(.65,1.0,abs(p.x)))*(.65+noise*.35);
        radiance*=clamp(.25+length(u_sunColor.rgb)*.18,.25,1.0);
    } else if (style>1.5 && style<2.5) {
        // Narrow bright tube, tapered tail and a white-hot head.
        falloff=exp(-5.0*p.y*p.y)*(1.0-smoothstep(.5,1.0,abs(p.y)))
               *smoothstep(0.0,.25,uv.x)*(1.0-smoothstep(.93,1.0,uv.x));
        radiance*=7.0;
    } else if (style>2.5 && style<3.5) {
        radiance*=9.0;
        falloff=exp(-6.0*r2)*(1.0-smoothstep(.5,1.0,r2));
    } else if (style>3.5 && style<4.5) {
        // Lumpy, sun-lit density instead of smooth flat discs.
        falloff=exp(-2.0*r2)*(1.0-smoothstep(.25+noise*.4,1.0,r2))*(.55+noise*.65);
        float day=clamp(length(u_sunColor.rgb)*.15,.08,1.0);
        radiance*=mix(.42,1.25,noise)*day;
    } else if (style>4.5) {
        falloff=exp(-2.8*r2)*(1.0-smoothstep(.22+noise*.5,1.0,r2));
        radiance=mix(vec3(1.5,.13,.015),vec3(8.0,3.2,.6),clamp(noise*1.4-r2*.5,0.0,1.0))*v_color.rgb;
    }
    vec3 color=applyFog(radiance,length(u_cameraPos.xyz-v_worldPos),world.y,
        u_cameraPos.y+u_worldOrigin.y,u_fogColor.rgb,u_fogDensity.x,u_fogHeightFalloff.x,u_fogGroundFade.x);
    color=applyCloudAir(color,u_cameraPos.xyz+u_worldOrigin.xyz,world,u_sunColor.rgb);
    gl_FragColor=vec4(color,v_color.a*falloff);
}
