$input a_position, a_color0, a_texcoord0
$output v_color, v_uv, v_surfacePos
#include <bgfx_shader.sh>
#include "frame.glsl"
#include "flame.glsl"
uniform mat4 u_ofsModel;
uniform mat4 u_ofsViewProj;
uniform vec4 u_flame; // intensity, visual time, layer (diamonds/flame/sheath), seed
uniform vec4 u_effectParams; // x 1 for a rocket motor's shape and palette
void main()
{
    vec3 p=a_position;
    float t=clamp(a_texcoord0.y,0.0,1.0);
    float layer=u_flame.z;
    // How much glowing gas the eye looks through at this point of a shell.
    float depth=1.0;
    bool shell=false;
    if ((layer>2.5 && layer<3.5) || layer>4.5) {
        // The glow and the nozzle's hot throat already use their final coordinates.
    } else if (layer>3.5) {
        p.x*=2.0+5.0*u_flame.x;
        p.yz*=.46+.24*t+sin(t*19.0-u_flame.y*12.0+u_flame.w)*.035;
        p.y+=sin(t*21.0-u_flame.y*17.0+u_flame.w)*t*.055;
    } else if (u_effectParams.x>0.5) {
        // A rocket motor: a plain tapering cone.
        float pulse=sin(t*36.0-u_flame.y*22.0+u_flame.w)*.025;
        float radius=(1.0-pow(t,1.4))*(1.0+pulse);
        radius*=mix(.18,.44,layer*.5);
        radius*=.84+.16*u_flame.x;
        p.x*= (.45+3.7*u_flame.x)*(1.0-layer*.065);
        p.yz*=max(.008,radius);
        p.y+=sin(t*29.0-u_flame.y*34.0+u_flame.w)*t*t*.022;
        p.z+=cos(t*31.0-u_flame.y*29.0+u_flame.w)*t*t*.016;
    } else {
        // Reheat: a nearly parallel jet. The inner shell swells and pinches into
        // a chain of shock diamonds, the flame bulges a little around each one,
        // and the sheath spreads slowly as it mixes with the air.
        shell=true;
        float reach=flameLength(u_flame.x)*flameReach(layer);
        float along=t*reach;
        float cell=flameCell(along);
        float radius;
        if (layer<0.5) radius=(.09+.25*cell)*(1.0-.45*t);
        else if (layer<1.5) radius=(.41-.12*t)*(1.0+.05*(cell-.5)*(1.0-t));
        else radius=.45+.13*t;
        radius*=.86+.14*u_flame.x;
        radius*=1.0+sin(along*9.0-u_flame.y*31.0+u_flame.w)*.03*t;
        p.x*=reach;
        p.yz*=max(.008,radius);
        p.y+=sin(t*17.0-u_flame.y*23.0+u_flame.w)*t*t*.040;
        p.z+=cos(t*19.0-u_flame.y*19.0+u_flame.w)*t*t*.032;
    }
    v_uv=a_texcoord0;
    v_color=a_color0;
    vec4 world=mul(u_ofsModel,vec4(p,1.0));
    vec3 toEye=u_cameraPos.xyz-world.xyz;
    float eyeDistance=length(toEye);
    if (shell) {
        // A shell is only the skin of a volume of light. Weighting it by the
        // length of the sight line through that volume gives the plume a bright
        // middle and soft edges from the side, and a deep glow seen along it.
        toEye/=max(eyeDistance,1.0e-4);
        vec3 axis=mul(u_ofsModel,vec4(1.0,0.0,0.0,0.0)).xyz;
        axis/=max(length(axis),1.0e-6);
        vec3 radial=mul(u_ofsModel,vec4(0.0,a_position.y,a_position.z,0.0)).xyz;
        radial/=max(length(radial),1.0e-6);
        float alongView=dot(axis,toEye);
        depth=min(abs(dot(radial,toEye))/max(1.0-alongView*alongView,.10),2.6);
    }
    // x carries the eye distance for the refraction layer's range test.
    v_surfacePos=vec3(eyeDistance,depth,p.z);
    gl_Position=mul(u_ofsViewProj,world);
}
