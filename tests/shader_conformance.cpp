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
  auto display=program(fullscreen_vs_dx11,post_fs_dx11);
#else
  auto shadow=program(shadow_vs_glsl,shadow_fs_glsl),pbr=program(pbr_vs_glsl,pbr_fs_glsl),flame=program(flame_vs_glsl,flame_fs_glsl);
  auto display=program(fullscreen_vs_glsl,post_fs_glsl);
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
  // No haze: the aerial-perspective atlas contributes nothing in these fixtures.
  const std::uint32_t black=0xff000000;
  const auto blackTexture=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(&black,4));
  const std::uint8_t tiltedNormal[]{204,128,230,255};
  const auto normalTexture=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,0,bgfx::copy(tiltedNormal,4));
  require(bgfx::isValid(framebuffer)&&bgfx::isValid(readback),"GPU readback capability");
  std::map<std::string,bgfx::UniformHandle> uniforms;
  const auto uniform=[&](const char* name,bgfx::UniformType::Enum type){auto [it,created]=uniforms.try_emplace(name);if(created)it->second=bgfx::createUniform(name,type);return it->second;};
  const auto vec=[&](const char* name,std::array<float,4> value){bgfx::setUniform(uniform(name,bgfx::UniformType::Vec4),value.data());};
  // The packed per-frame constants, indexed as in client/shaders/frame.glsl.
  using Frame=std::array<std::array<float,4>,24>;
  const auto frameUniform=bgfx::createUniform("u_frame",bgfx::UniformType::Vec4,24);
  const auto shadowMatrices=bgfx::createUniform("u_shadowMatrix",bgfx::UniformType::Mat4,3);
  const auto setFrame=[&](const Frame& frame){bgfx::setUniform(frameUniform,frame.data(),24);};
  const auto baseFrame=[&]{
    Frame frame{};
    frame[2]={0,0,1,1};            // toward the sun; unit emissive radiance
    frame[8]={64,64,1.f/64,1.f/64}; // viewport
    frame[9]={6360000,100000,8000,1200};
    frame[11]={0,0,0,150};         // no aerosol or fog, but a finite fog scale height
    frame[14]={2,2,2,0};           // solar irradiance; the disc sits on the horizon, half visible
    frame[13]={0,0,0,1000};        // aerial range
    frame[20]={0,bgfx::getCaps()->originBottomLeft?1.f:0.f,0,0};
    frame[21]={0,0,-1,4};          // metres per stored range unit
    return frame;
  };
  const float identity[]{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
  const float normal[]{1,0,0,0,1,0,0,0,1};
  const auto render=[&](bgfx::ProgramHandle shader,bool reverse,bool mask,float face,float cutoff=.5f,bool mapped=false) {
    bgfx::setViewRect(0,0,0,64,64);bgfx::setViewFrameBuffer(0,framebuffer);
    bgfx::setViewClear(0,BGFX_CLEAR_COLOR,0x000000ff);
    for(auto name:{"u_ofsModel","u_ofsViewProj","u_lightViewProj"})bgfx::setUniform(uniform(name,bgfx::UniformType::Mat4),identity);
    float cascades[48];for(unsigned i=0;i<48;++i)cascades[i]=identity[i%16];
    bgfx::setUniform(shadowMatrices,cascades,3);
    bgfx::setUniform(uniform("u_normalMatrix",bgfx::UniformType::Mat3),normal);
    vec("u_baseColor",{1,1,1,1});vec("u_textureFlags",{mask?1.f:0.f,0,0,mapped?1.f:0.f});vec("u_alphaSettings",{mask?1.f:0.f,cutoff,0,0});
    vec("u_doubleSided",{1,0,0,0});vec("u_metallicRoughness",{0,1,0,0});
    vec("u_emissive",{0,0,0,0});vec("u_normalSettings",{1,0,0,0});
    // Sun and eye on the lit side of the plane; no sky, shadows, clouds or haze.
    Frame frame=baseFrame();
    frame[0]={mapped?face*1.2f:0,0,face*(mapped?1.6f:2.f),0};
    frame[2]={mapped?face*.6f:0,0,face*(mapped?.8f:1.f),1};
    setFrame(frame);
    for(auto [slot,name]:std::array<std::pair<unsigned,const char*>,5>{{{1,"s_baseColor"},{2,"s_metallicRoughness"},{3,"s_emissive"},{4,"s_normal"},{5,"s_occlusion"}}})
      bgfx::setTexture(slot,uniform(name,bgfx::UniformType::Sampler),slot==1&&mask?alphaTexture:slot==4&&mapped?normalTexture:whiteTexture);
    // A clear path to the sun (white transmittance) and black sky, haze and noise lookups.
    bgfx::setTexture(6,uniform("s_transmittance",bgfx::UniformType::Sampler),whiteTexture);
    for(auto [slot,name]:std::array<std::pair<unsigned,const char*>,4>{{{7,"s_skyView"},{8,"s_aerial"},{9,"s_weatherMap"},{10,"s_noise"}}})
      bgfx::setTexture(slot,uniform(name,bgfx::UniformType::Sampler),blackTexture);
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
    // Undo the vertex shader's shaping of the shock-diamond shell at full
    // reheat (flame_vs.glsl and flame.glsl), so the triangle covers the target.
    const float bounded=std::clamp(t,0.f,1.f);
    const float reach=(.8f+5.6f)*.80f,along=bounded*reach;
    const float phase=(along-.36f)/.72f,cell=std::abs((phase-std::floor(phase))*2-1);
    const float radius=std::max(.008f,(.09f+.25f*cell)*(1-.45f*bounded)*(1+std::sin(along*9)*.03f*bounded));
    auto matrix=std::array<float,16>{-1.f/reach,0,0,0,0,1.f/radius,0,0,0,0,1,0,0,-std::sin(bounded*17)*bounded*bounded*.040f/radius,0,1};
    bgfx::setViewRect(0,0,0,64,64);bgfx::setViewFrameBuffer(0,framebuffer);
    bgfx::setViewClear(0,BGFX_CLEAR_COLOR,0x607080ff);
    bgfx::setUniform(uniform("u_ofsModel",bgfx::UniformType::Mat4),matrix.data());
    bgfx::setUniform(uniform("u_ofsViewProj",bgfx::UniformType::Mat4),identity);
    // Seen square-on from far off, the whole shell faces the eye.
    Frame frame=baseFrame();frame[0]={0,500,0,0};
    setFrame(frame);vec("u_flame",{1,0,0,0});vec("u_effectParams",{0,0,64,64});bgfx::setVertexBuffer(0,buffer);
    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_SRC_ALPHA,BGFX_STATE_BLEND_ONE));
    bgfx::submit(0,flame);bgfx::blit(1,{.handle=readback},{.handle=bgfx::getTexture(framebuffer)});
    std::vector<std::uint8_t> pixels(64*64*4);const auto ready=bgfx::read({.handle=readback},pixels.data());
    require(ready!=UINT32_MAX,"Plume readback scheduled");while(bgfx::frame()<ready+1){}
    bgfx::destroy(buffer);return pixel(pixels,32);
  };
  // 0.211 of the way along is the heart of the third shock diamond.
  const auto below=plumePixel(-.0001f),above=plumePixel(1.0001f),inside=plumePixel(.211f);
  std::printf("GPU plume boundaries before/after=%u/%u interior=%u\n",below,above,inside);
  require(std::abs(int(below)-96)<=1 && std::abs(int(above)-96)<=1,"Plume boundaries preserve background without NaN black squares");
  require(inside>100,"Plume interior still emits light");
  // The display transform is the one place scene radiance becomes pixels, so
  // its anchors are pinned: black stays black, middle grey stays middle grey
  // and the top of the exposure range reaches white without clipping early.
  const auto displayed=[&](float radiance) {
    const float texel[]{radiance,radiance,radiance,1};
    const auto scene=bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA32F,
      BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT,bgfx::copy(texel,sizeof(texel)));
    bgfx::setViewRect(0,0,0,64,64);bgfx::setViewFrameBuffer(0,framebuffer);
    bgfx::setViewClear(0,BGFX_CLEAR_COLOR,0xff00ffff);
    setFrame(baseFrame());vec("u_postSettings",{1,0,0,1});vec("u_postStep",{1,0,0,0});
    bgfx::setTexture(0,uniform("s_scene",bgfx::UniformType::Sampler),scene);
    bgfx::setTexture(1,uniform("s_bloom",bgfx::UniformType::Sampler),blackTexture);
    bgfx::setTexture(2,uniform("s_distortion",bgfx::UniformType::Sampler),blackTexture);
    bgfx::setVertexBuffer(0,vb);bgfx::setIndexBuffer(fi);bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A);
    bgfx::submit(0,display);bgfx::blit(1,{.handle=readback},{.handle=bgfx::getTexture(framebuffer)});
    std::vector<std::uint8_t> pixels(64*64*4);const auto ready=bgfx::read({.handle=readback},pixels.data());
    require(ready!=UINT32_MAX,"Display readback scheduled");while(bgfx::frame()<ready+1){}
    bgfx::destroy(scene);
    // Green carries no white-balance gain, so it reports the tone curve alone.
    return unsigned(pixels[(32*64+32)*4+1]);
  };
  const auto dark=displayed(0),grey=displayed(.18f),bright=displayed(.18f*std::exp2(4.7f)),stop=displayed(.36f);
  std::printf("GPU display transform black/grey/+1 stop/white=%u/%u/%u/%u\n",dark,grey,stop,bright);
  require(dark<=2,"Display transform keeps black");
  require(std::abs(int(grey)-118)<=4,"Display transform maps 18% grey to its sRGB value");
  require(stop>grey+25&&stop<grey+60,"Display transform keeps about unit contrast around middle grey");
  require(bright>=253,"Display transform reaches white at the top of its range");
  bgfx::destroy(frameUniform);bgfx::destroy(shadowMatrices);
  for(auto [name,handle]:uniforms)bgfx::destroy(handle);
  for(auto h:{alphaTexture,whiteTexture,blackTexture,normalTexture,readback})bgfx::destroy(h);
  bgfx::destroy(framebuffer);bgfx::destroy(vb);bgfx::destroy(fi);bgfx::destroy(bi);bgfx::destroy(shadow);bgfx::destroy(pbr);bgfx::destroy(flame);bgfx::destroy(display);bgfx::shutdown();initialized=false;return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL GPU shader conformance: %s\n",e.what());if(initialized)bgfx::shutdown();return 1;}}
