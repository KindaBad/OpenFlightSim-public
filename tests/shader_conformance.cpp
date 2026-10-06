// Small offscreen GPU fixtures exercise the actual shipped shaders, with pixel
// assertions independent of the aircraft assets and the renderer implementation.
#include "platform.hpp"
#include "ofs_shaders.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <vector>
using namespace ofs::client;
void require(bool v,const char* msg){if(!v)throw std::runtime_error(msg);}
template<std::size_t N,std::size_t M>
bgfx::ProgramHandle program(const std::uint8_t(&vs)[N],const std::uint8_t(&fs)[M]) {
  const auto v=bgfx::createShader(bgfx::copy(vs,N)),f=bgfx::createShader(bgfx::copy(fs,M));
  require(bgfx::isValid(v)&&bgfx::isValid(f),"Compiled shaders load");return bgfx::createProgram(v,f,true);
}
struct Vertex {float x,y,z,nx,ny,nz,u,v,tx,ty,tz,tw;};
int main(){bool initialized=false;try {
  Platform platform({64,64,"OpenFlightSim shader conformance"});SDL_HideWindow(platform.window());
  struct Shutdown {bool& active;~Shutdown(){if(active){bgfx::shutdown();active=false;}}} shutdown{initialized};
  bgfx::Init init;
#ifdef _WIN32
  init.type=bgfx::RendererType::Direct3D11;
#else
  init.type=bgfx::RendererType::OpenGL;
#endif
  init.swapChain=platform.nativeHandles();init.swapChain.width=64;init.swapChain.height=64;
  init.swapChain.formatColor=bgfx::TextureFormat::BGRA8;init.swapChain.formatDepthStencil=bgfx::TextureFormat::D24S8;
  init.fallback=false;require(bgfx::init(init),"GPU initialization");initialized=true;
#ifdef _WIN32
  auto shadow=program(shadow_vs_dx11,shadow_fs_dx11),pbr=program(pbr_vs_dx11,pbr_fs_dx11),flame=program(flame_vs_dx11,flame_fs_dx11);
#else
  auto shadow=program(shadow_vs_glsl,shadow_fs_glsl),pbr=program(pbr_vs_glsl,pbr_fs_glsl),flame=program(flame_vs_glsl,flame_fs_glsl);
#endif
  bgfx::VertexLayout layout;layout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
    .add(bgfx::Attrib::Normal,3,bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float)
    .add(bgfx::Attrib::Tangent,4,bgfx::AttribType::Float).end();
  const Vertex vertices[]={{-1,-1,0,0,0,1,0,0,1,0,0,1},{3,-1,0,0,0,1,2,0,1,0,0,1},{-1,3,0,0,0,1,0,2,1,0,0,1}};
  const auto vb=bgfx::createVertexBuffer(bgfx::copy(vertices,sizeof(vertices)),layout);
  const std::uint16_t front[]{0,1,2},back[]{0,2,1};
  const auto fi=bgfx::createIndexBuffer(bgfx::copy(front,sizeof(front))),bi=bgfx::createIndexBuffer(bgfx::copy(back,sizeof(back)));
  const auto framebuffer=bgfx::createFrameBuffer(64,64,bgfx::TextureFormat::RGBA8);
  const auto readback=bgfx::createTexture2D(64,64,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_READ_BACK|BGFX_TEXTURE_BLIT_DST);
  const std::uint8_t texels[]{255,255,255,0,255,255,255,255};
  const auto alphaTexture=bgfx::createTexture2D(2,1,false,1,bgfx::TextureFormat::RGBA8,
    BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT,bgfx::copy(texels,sizeof(texels)));
  const std::uint32_t white=0xffffffff;
  const auto whiteTexture=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(&white,4));
  const std::uint8_t tiltedNormal[]{204,128,230,255};
  const auto normalTexture=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(tiltedNormal,4));
  require(bgfx::isValid(framebuffer)&&bgfx::isValid(readback),"GPU readback capability");
  std::map<std::string,bgfx::UniformHandle> uniforms;
  const auto uniform=[&](const char* name,bgfx::UniformType::Enum type){auto [it,created]=uniforms.try_emplace(name);if(created)it->second=bgfx::createUniform(name,type);return it->second;};
  const auto vec=[&](const char* name,std::array<float,4> value){bgfx::setUniform(uniform(name,bgfx::UniformType::Vec4),value.data());};
  const float identity[]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  const float normal[]{1,0,0,0,1,0,0,0,1};
  const auto render=[&](bgfx::ProgramHandle shader,bool reverse,bool mask,float face,float cutoff=.5f,bool mapped=false) {
    bgfx::setViewRect(0,0,0,64,64);bgfx::setViewFrameBuffer(0,framebuffer);
    bgfx::setViewClear(0,BGFX_CLEAR_COLOR,0x000000ff);
    for(auto name:{"u_ofsModel","u_ofsViewProj","u_shadowMatrix"})bgfx::setUniform(uniform(name,bgfx::UniformType::Mat4),identity);
    bgfx::setUniform(uniform("u_normalMatrix",bgfx::UniformType::Mat3),normal);
    vec("u_baseColor",{1,1,1,1});vec("u_textureFlags",{mask?1.f:0.f,0,0,mapped?1.f:0.f});vec("u_alphaSettings",{mask?1.f:0.f,cutoff,0,0});
    vec("u_doubleSided",{1,0,0,0});vec("u_cameraPos",{mapped?face*1.2f:0,0,face*(mapped?1.6f:2.f),0});
    vec("u_sunDirection",{mapped?-face*.6f:0,0,-face*(mapped?.8f:1.f),0});vec("u_sunColor",{1,1,1,0});
    vec("u_skyAmbient",{0,0,0,0});vec("u_groundAmbient",{0,0,0,0});vec("u_metallicRoughness",{0,1,0,0});
    vec("u_emissive",{0,0,0,0});vec("u_normalSettings",{1,0,0,0});vec("u_shadowStrength",{0,0,0,0});vec("u_fogEnabled",{0,0,0,0});
    vec("u_cloudParams",{0,0,0,0});vec("u_weather",{0,0,10000,0});vec("u_worldOrigin",{0,0,0,0});
    vec("u_fogDensity",{0,0,0,0});vec("u_fogHeightFalloff",{1,0,0,0});vec("u_fogGroundFade",{1,0,0,0});vec("u_fogColor",{0,0,0,0});
    for(auto [slot,name]:std::array<std::pair<unsigned,const char*>,5>{{{1,"s_baseColor"},{2,"s_metallicRoughness"},{3,"s_emissive"},{4,"s_normal"},{5,"s_occlusion"}}})
      bgfx::setTexture(slot,uniform(name,bgfx::UniformType::Sampler),slot==1&&mask?alphaTexture:slot==4&&mapped?normalTexture:whiteTexture);
    bgfx::setVertexBuffer(0,vb);bgfx::setIndexBuffer(reverse?bi:fi);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_FRONT_CCW);
    bgfx::submit(0,shader);
    bgfx::blit(1,{.handle=readback},{.handle=bgfx::getTexture(framebuffer)});
    std::vector<std::uint8_t> pixels(64*64*4);
    const auto ready=bgfx::read({.handle=readback},pixels.data());require(ready!=UINT32_MAX,"Readback scheduled");
    while(bgfx::frame()<ready+1){}
    return pixels;
  };
  auto alpha=render(shadow,false,true,1);const auto pixel=[](const auto& data,unsigned x){return unsigned(data[(32*64+x)*4]);};
  require(pixel(alpha,16)<8&&pixel(alpha,48)>247,"Shadow mask discards transparent texel and keeps opaque texel");
  const auto factor=render(shadow,false,true,1,1.1f);require(pixel(factor,48)<8,"Shadow pass uses authored alpha cutoff");
  const auto faceFront=render(pbr,false,false,1),faceBack=render(pbr,true,false,-1);
  std::printf("GPU pixel diagnostics mask=%u/%u cutoff=%u front/back=%u/%u\n",pixel(alpha,16),pixel(alpha,48),pixel(factor,48),pixel(faceFront,32),pixel(faceBack,32));
  require(pixel(faceFront,32)>20&&pixel(faceBack,32)>20,"Double-sided backface receives light from underside");
  require(std::abs(int(pixel(faceFront,32))-int(pixel(faceBack,32)))<=3,"Front and back PBR normal response agrees");
  const auto mappedFront=render(pbr,false,false,1,.5f,true),mappedBack=render(pbr,true,false,-1,.5f,true);
  require(pixel(mappedFront,32)>20&&std::abs(int(pixel(mappedFront,32))-int(pixel(mappedBack,32)))<=3,"Tilted authored normal map reverses its entire backface basis");
  std::printf("PASS GPU tilted normal map front/back=%u/%u\n",pixel(mappedFront,32),pixel(mappedBack,32));
  std::printf("PASS GPU shader regression: alpha mask L/R=%u/%u cutoff=%u; double-sided front/back=%u/%u\n",pixel(alpha,16),pixel(alpha,48),pixel(factor,48),pixel(faceFront,32),pixel(faceBack,32));
  // Fractional powers at t just above one used to poison HDR radiance and
  // bloom with NaNs, producing black rectangles around the afterburner.
  const auto plumePixel=[&](float t) {
    bgfx::VertexLayout effectLayout;effectLayout.begin().add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,4,bgfx::AttribType::Float).add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float).end();
    const float data[]{-1,-1,0,1,1,1,1,.5f,t, 3,-1,0,1,1,1,1,.5f,t, -1,3,0,1,1,1,1,.5f,t};
    const auto buffer=bgfx::createVertexBuffer(bgfx::copy(data,sizeof(data)),effectLayout);
    const float bounded=std::clamp(t,0.f,1.f);
    const float radius=std::max(.008f,(1-std::pow(bounded,1.4f))*.18f);
    auto matrix=std::array<float,16>{-1.f/4.15f,0,0,0,0,1.f/radius,0,0,0,0,1,0,0,-std::sin(bounded*29)*bounded*bounded*.022f/radius,0,1};
    bgfx::setViewRect(0,0,0,64,64);bgfx::setViewFrameBuffer(0,framebuffer);
    bgfx::setViewClear(0,BGFX_CLEAR_COLOR,0x607080ff);
    bgfx::setUniform(uniform("u_ofsModel",bgfx::UniformType::Mat4),matrix.data());
    bgfx::setUniform(uniform("u_ofsViewProj",bgfx::UniformType::Mat4),identity);
    vec("u_flame",{1,0,0,0});bgfx::setVertexBuffer(0,buffer);
    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,BGFX_STATE_BLEND_ONE));
    bgfx::submit(0,flame);bgfx::blit(1,{.handle=readback},{.handle=bgfx::getTexture(framebuffer)});
    std::vector<std::uint8_t> pixels(64*64*4);const auto ready=bgfx::read({.handle=readback},pixels.data());
    require(ready!=UINT32_MAX,"Plume readback scheduled");while(bgfx::frame()<ready+1){}
    bgfx::destroy(buffer);return pixel(pixels,32);
  };
  const auto below=plumePixel(-.0001f),above=plumePixel(1.0001f),inside=plumePixel(.4f);
  std::printf("GPU plume boundaries before/after=%u/%u interior=%u\n",below,above,inside);
  require(std::abs(int(below)-96)<=1 && std::abs(int(above)-96)<=1,"Plume boundaries preserve background without NaN black squares");
  require(inside>100,"Plume interior still emits light");
  for(auto [name,handle]:uniforms)bgfx::destroy(handle);
  for(auto h:{alphaTexture,whiteTexture,normalTexture,readback})bgfx::destroy(h);
  bgfx::destroy(framebuffer);bgfx::destroy(vb);bgfx::destroy(fi);bgfx::destroy(bi);bgfx::destroy(shadow);bgfx::destroy(pbr);bgfx::destroy(flame);bgfx::shutdown();initialized=false;return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL GPU shader conformance: %s\n",e.what());if(initialized)bgfx::shutdown();return 1;}}
