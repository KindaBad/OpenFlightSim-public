#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ofs::client {
enum class TextureRole { Linear, Srgb, Normal };
inline float srgbToLinear(float v) {
  return v<=.04045f?v/12.92f:std::pow((v+.055f)/1.055f,2.4f);
}
inline float linearToSrgb(float v) {
  return v<=.0031308f?v*12.92f:1.055f*std::pow(std::max(0.f,v),1.f/2.4f)-.055f;
}
// Input and output contain RGBA8; successive levels follow the base level.
// Colour is filtered in linear light, data maps linearly, normals on the unit sphere.
inline std::vector<std::uint8_t> textureMipChain(const std::vector<std::uint8_t>& base,
                                               unsigned w,unsigned h,TextureRole role) {
  auto pixels=base;
  if(!w || !h || base.size()!=std::size_t(w)*h*4) return {};
  std::size_t offset=0;
  while(w>1 || h>1) {
    const unsigned nw=std::max(1u,w/2),nh=std::max(1u,h/2);
    const auto next=pixels.size();pixels.resize(next+std::size_t(nw)*nh*4);
    for(unsigned y=0;y<nh;++y) for(unsigned x=0;x<nw;++x) {
      float value[4]{};
      // Area coverage also includes the last row/column of odd-sized maps.
      const unsigned x0=x*w/nw,x1=(x+1)*w/nw,y0=y*h/nh,y1=(y+1)*h/nh;
      const float weight=1.f/((x1-x0)*(y1-y0));
      for(unsigned sy=y0;sy<y1;++sy) for(unsigned sx=x0;sx<x1;++sx)
        for(unsigned c=0;c<4;++c) {
          float v=pixels[offset+4*(std::size_t(sy)*w+sx)+c]/255.f;
          if(role==TextureRole::Srgb && c<3)v=srgbToLinear(v);
          value[c]+=v*weight;
        }
      if(role==TextureRole::Normal) {
        float length=0;
        for(unsigned c=0;c<3;++c){value[c]=2*value[c]-1;length+=value[c]*value[c];}
        length=std::sqrt(length);
        for(unsigned c=0;c<3;++c)value[c]=.5f+.5f*(length>1e-6f?value[c]/length:(c==2?1.f:0.f));
      }
      for(unsigned c=0;c<4;++c) {
        if(role==TextureRole::Srgb && c<3)value[c]=linearToSrgb(value[c]);
        pixels[next+4*(std::size_t(y)*nw+x)+c]=std::uint8_t(std::clamp(value[c]*255.f+.5f,0.f,255.f));
      }
    }
    offset=next;w=nw;h=nh;
  }
  return pixels;
}
}
