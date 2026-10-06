#pragma once
#include "texture_mips.hpp"
#include "gltf.hpp"
#include <string>
#include <tuple>
#include <map>
#include <stdexcept>
namespace ofs::client {
// Compression readiness: block costs can be estimated without claiming that
// compressed payload decoding/upload is implemented. KTX2 is a container;
// Basis requires transcoding to a supported GPU format before cost is known.
enum class TextureStorage { RGBA8, BC7, BC5, BC4 };
struct TextureAllocation {
  unsigned width{},height{},mips{};
  std::uint64_t bytes{};
};
inline TextureAllocation textureAllocation(unsigned w,unsigned h,bool mipmaps,unsigned cap,
                                            TextureStorage format=TextureStorage::RGBA8) {
  if(!w||!h||!cap)throw std::invalid_argument("Invalid texture dimensions/cap");
  while(w>cap||h>cap){w=std::max(1u,w/2);h=std::max(1u,h/2);}
  TextureAllocation result{w,h,0,0};
  do {
    ++result.mips;
    result.bytes+=format==TextureStorage::RGBA8?std::uint64_t(w)*h*4:
      std::uint64_t((w+3)/4)*((h+3)/4)*(format==TextureStorage::BC4?8:16);
    if(!mipmaps||(w==1&&h==1))break;
    w=std::max(1u,w/2);h=std::max(1u,h/2);
  }while(true);
  return result;
}
inline TextureRole textureRole(const Mesh& mesh,int image) {
  const auto uses=[&](auto slot){return std::any_of(mesh.materials.begin(),mesh.materials.end(),[&](const auto& m){
    const int index=slot(m);return index>=0&&index<int(mesh.textures.size())&&mesh.textures[index].image==image;
  });};
  return uses([](const auto& m){return m.normalTexture;})?TextureRole::Normal:
    uses([](const auto& m){return m.baseColorTexture;})||uses([](const auto& m){return m.emissiveTexture;})?TextureRole::Srgb:TextureRole::Linear;
}
struct TextureCost {
  std::string source;
  unsigned sourceWidth{},sourceHeight{};
  TextureAllocation allocation;
  TextureRole role{};
};
// Uses the renderer's sampler distinctions. Authored LODs reuse LOD0 handles.
inline std::vector<TextureCost> aircraftTextureCosts(const Mesh& mesh,unsigned cap) {
  std::map<std::tuple<int,int,int,bool,bool,bool,bool,TextureRole>,bool> seen;
  std::vector<TextureCost> costs;
  for(const auto& t:mesh.textures) {
    if(t.image<0||t.image>=int(mesh.images.size()))continue;
    const auto& i=mesh.images[t.image];if(i.rgba.empty())continue;
    const bool mip=t.minFilter>=9984&&t.minFilter<=9987;
    const auto role=textureRole(mesh,t.image);
    const auto wrap=[](int w){return w==33071||w==33648?w:10497;};
    const auto key=std::tuple{t.image,wrap(t.wrapS),wrap(t.wrapT),t.magFilter==9728,
      t.minFilter==9728||t.minFilter==9984||t.minFilter==9986,t.minFilter==9984||t.minFilter==9985,mip,role};
    if(seen.contains(key))continue;
    seen[key]=true;costs.push_back({i.source,i.width,i.height,textureAllocation(i.width,i.height,mip,cap),role});
  }
  return costs;
}
}
