// Alpha-masked materials use the same base alpha/cutoff as the colour pass.
$input v_uv
#include <bgfx_shader.sh>
SAMPLER2D(s_baseColor, 1);
uniform vec4 u_baseColor;
uniform vec4 u_textureFlags;
uniform vec4 u_alphaSettings;
void main() {
  float alpha=u_baseColor.a*mix(1.0,texture2D(s_baseColor,v_uv).a,u_textureFlags.x);
  if(u_alphaSettings.x>.5 && alpha<u_alphaSettings.y)discard;
  gl_FragColor=vec4_splat(1.0);
}
