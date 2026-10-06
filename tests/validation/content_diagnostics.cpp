#include "texture_cost.hpp"
#include "screenshot_pixels.hpp"
#include "ofs/aircraft_definition.hpp"
#include "scenario.hpp"
#include <filesystem>
#include <cstdio>
using namespace ofs::client;
int main(int argc,char** argv) {try {
  scenario::check(argc>=2,"suite name");
  if(std::string_view(argv[1])=="pixels") {
    std::vector<std::uint8_t> rgb(6);
    // Distinct red/blue pixels with non-opaque alpha and row padding.
    const std::vector<std::uint8_t> rgba{230,20,7,11,3,40,250,77,99,99};
    const std::vector<std::uint8_t> bgra{7,20,230,11,250,40,3,77,99,99};
    const std::vector<std::uint8_t> expected{230,20,7,3,40,250};
    for(auto order:{ScreenshotOrder::RGBA8,ScreenshotOrder::BGRA8}) {
      screenshotRgbRow(order==ScreenshotOrder::RGBA8?rgba:bgra,rgb,order);
      scenario::check(rgb==expected,"screenshot readback channel order");
    }
    scenario::check(textureAllocation(4096,4096,true,2048).bytes==22369620,"2K full RGBA8 chain independently summed");
    scenario::check(textureAllocation(1,1,true,2048,TextureStorage::BC7).bytes==16,"Compressed tail occupies one full block");
    scenario::check(textureAllocation(8,4,false,2048,TextureStorage::BC4).bytes==16,"BC4 eight-byte blocks");
    const std::vector<std::uint8_t> odd(7*3*4,128);
    scenario::check(textureMipChain(odd,7,3,TextureRole::Linear).size()==textureAllocation(7,3,true,2048).bytes,"Odd-sized mip cost equals upload length");
    bool failed=false;try{screenshotRgbRow({},rgb,ScreenshotOrder::RGBA8);}catch(const std::invalid_argument&){failed=true;}
    scenario::check(failed,"Truncated readback rejected");
  }else if(std::string_view(argv[1])=="textures") {
    scenario::check(argc==3,"asset root");std::vector<TextureCost> all;std::uint64_t total=0;
    for(const auto& d:ofs::aircraftDefinitions()) {
      if(scenario::skipMissingAsset(argv[2],d.modelAsset))continue;
      const auto path=(std::filesystem::path(argv[2])/d.modelAsset).string();const auto mesh=loadGltf(path);
      std::uint64_t bytes=0;
      for(auto cost:aircraftTextureCosts(mesh,2048)) {
        const auto a=cost.allocation;bytes+=a.bytes;
        std::printf("texture %.*s %s | %s source=%ux%u runtime=RGBA8 %ux%u mips=%u estimated_gpu_bytes=%llu\n",
          int(d.key.size()),d.key.data(),path.c_str(),cost.source.c_str(),cost.sourceWidth,cost.sourceHeight,
          a.width,a.height,a.mips,(unsigned long long)a.bytes);
        cost.source=std::string(d.key)+" | "+cost.source;all.push_back(cost);
      }
      total+=bytes;std::printf("aircraft %.*s estimated_gpu_bytes=%llu\n",int(d.key.size()),d.key.data(),(unsigned long long)bytes);
    }
    std::sort(all.begin(),all.end(),[](const auto& a,const auto& b){return a.allocation.bytes>b.allocation.bytes;});
    std::printf("total aircraft texture estimate (2K cap, LOD0 dedup): %llu bytes\n",(unsigned long long)total);
    for(std::size_t i=0;i<std::min(std::size_t(5),all.size());++i)std::printf("largest %s %llu bytes\n",all[i].source.c_str(),(unsigned long long)all[i].allocation.bytes);
  }else throw std::invalid_argument("unknown suite");
  return scenario::assetResult();
}catch(const std::exception& e){std::fprintf(stderr,"FAIL content diagnostics: %s\n",e.what());return 1;}}
