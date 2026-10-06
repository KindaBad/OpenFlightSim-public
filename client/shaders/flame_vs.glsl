$input a_position, a_color0, a_texcoord0
$output v_color, v_uv, v_surfacePos
#include <bgfx_shader.sh>
uniform mat4 u_ofsModel;
uniform mat4 u_ofsViewProj;
uniform vec4 u_flame; // intensity, visual time, layer (core/middle/outer), seed
void main()
{
    vec3 p=a_position;
    float t=clamp(a_texcoord0.y,0.0,1.0);
    float layer=u_flame.z;
    if (layer>2.5 && layer<3.5) {
        // Corona geometry already uses its final coordinates.
    } else if (layer>3.5) {
        p.x*=2.0+5.0*u_flame.x;
        p.yz*=.46+.24*t+sin(t*19.0-u_flame.y*12.0+u_flame.w)*.035;
        p.y+=sin(t*21.0-u_flame.y*17.0+u_flame.w)*t*.055;
    } else {
        float pulse=sin(t*36.0-u_flame.y*22.0+u_flame.w)*.025;
        float radius=(1.0-pow(t,1.4))*(1.0+pulse);
        radius*=mix(.18,.44,layer*.5);
        radius*=.84+.16*u_flame.x;
        p.x*= (.45+3.7*u_flame.x)*(1.0-layer*.065);
        p.yz*=max(.008,radius);
        p.y+=sin(t*29.0-u_flame.y*34.0+u_flame.w)*t*t*.022;
        p.z+=cos(t*31.0-u_flame.y*29.0+u_flame.w)*t*t*.016;
    }
    v_uv=a_texcoord0;
    v_color=a_color0;
    v_surfacePos=p;
    gl_Position=mul(u_ofsViewProj,mul(u_ofsModel,vec4(p,1)));
}
