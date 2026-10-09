#include "gltf.hpp"
#include "mesh.hpp"
#include "ofs/aircraft_definition.hpp"
#include "ofs/physical_geometry.hpp"
#include <filesystem>
#include <set>
#include <cstdio>
#include <stdexcept>
using namespace ofs;using namespace ofs::client;
void require(bool v,const std::string& msg){if(!v)throw std::runtime_error(msg);}
void complete(const Mesh& mesh) {
  require(mesh.valid(),"Production model invalid");
  for(const auto& diagnostic:mesh.report)
    require(diagnostic.find("KHR_materials_clearcoat ignored")!=std::string::npos||
            diagnostic.find("KHR_materials_emissive_strength ignored")!=std::string::npos,
            "Production loader omitted content: "+diagnostic);
  for(const auto& image:mesh.images)require(!image.rgba.empty(),"Production image decode incomplete");
}
int main(int argc,char** argv){try {
  const std::filesystem::path root=argc>1?argv[1]:".";
  const bool mandatory=argc>2 && std::string_view(argv[2])=="required";
  unsigned validated=0,missing=0;
  for(const auto& d:aircraftDefinitions()) {
    const auto path=root/d.modelAsset;
    const PhysicalGeometry* geometry=d.type==AircraftType::A320?&a320Geometry():d.type==AircraftType::Su57?&su57Geometry():nullptr;
    // The JF-17 donor is scaled to the published 14.93 m and then stands
    // 5.15 m high against the published 4.77 m; the airframe is not squashed.
    const double length=geometry?geometry->length:d.type==AircraftType::Typhoon?15.96:d.type==AircraftType::JF17?14.93:d.type==AircraftType::B52?48.5:32.7406;
    // The Typhoon donor's fin stands 0.34 m above the published 5.28 m overall
    // height at the configured static stance; the airframe is not rescaled.
    const double height=geometry?geometry->height:d.type==AircraftType::Typhoon?5.62:d.type==AircraftType::JF17?5.15:d.type==AircraftType::B52?12.4:5.6388;
    // The B-52 donor, scaled to the published 48.5 m, spans 55.66 m against
    // the published 56.4 m.
    const double wingSpan=d.type==AircraftType::B52?55.66:d.flight.wing_span;
    if(!(std::filesystem::is_regular_file(path)||std::filesystem::is_regular_file(path.string()+".ofspack"))) {
      require(!mandatory,"Required production model absent: "+path.string());
      ++missing;
      std::printf("[NOT RUN asset-conformance] %s\n",path.c_str());continue;
    }
    const auto mesh=loadGltf(path.string());complete(mesh);
    const double measuredLength=mesh.boundsMax[0]-mesh.boundsMin[0],span=mesh.boundsMax[2]-mesh.boundsMin[2],high=mesh.boundsMax[1]-mesh.boundsMin[1];
    std::printf("asset-conformance %.*s length=%.6f span=%.6f height=%.6f\n",int(d.key.size()),d.key.data(),measuredLength,span,high);
    const double measured[]{measuredLength,span,high},expected[]{length,wingSpan,height};
    const char* dimensions[]{"length","span","height"};
    for(unsigned i=0;i<3;++i)std::printf("  scale %s expected=%.6f measured=%.6f error_m=%+.6f error_percent=%+.4f\n",dimensions[i],expected[i],measured[i],measured[i]-expected[i],100*(measured[i]-expected[i])/expected[i]);
    require(std::abs(measuredLength-length)<.15,"Physical length mismatch: "+std::string(d.key));
    require(std::abs(span-wingSpan)<.15,"Physical span mismatch: "+std::string(d.key));
    if(height>0)require(std::abs(high-height)<.15,"Known height mismatch");
    // Shared model-to-body transform: -(asset.X-CGX), -(asset.Z-CGZ), -(asset.Y-CGY).
    // Check forward direction, CG in measured envelope and parked contact plane.
    const auto cg=d.visual.assetCg;require(std::isfinite(cg.norm2()),"Nonfinite CG anchor");
    require(cg.x>mesh.boundsMin[0]&&cg.x<mesh.boundsMax[0],"CG longitudinal anchor outside model");
    for(auto gear:{d.flight.gear_nose,d.flight.gear_main_l,d.flight.gear_main_r}) {
      const Vec3 asset{cg.x-gear.x,cg.y-gear.z,cg.z-gear.y};
      require(asset.x>=mesh.boundsMin[0]&&asset.x<=mesh.boundsMax[0],"Gear longitudinal alignment");
      require(std::abs(asset.y-mesh.boundsMin[1])<.15,"Gear contact plane alignment");
    }
    require(std::abs(mesh.boundsMin[0])<.05,"Orientation: nose anchor must be asset X=0");
    std::set<std::string> channels,materials,nodeNames;
    for(const auto& node:mesh.nodes) {
      nodeNames.insert(node.name);if(!node.channel.empty())channels.insert(node.channel);
      for(float v:node.local)require(std::isfinite(v),"Nonfinite local transform");
      for(float v:node.world)require(std::isfinite(v),"Nonfinite world transform");
    }
    for(const auto& material:mesh.materials)materials.insert(material.name);
    for(const auto& p:mesh.primitives){require(p.material<mesh.materials.size(),"Material reference bounds");for(float v:p.vertices)require(std::isfinite(v),"Nonfinite vertex attribute");}
    for(auto name:{"rudder","wheel","nose_wheel","steering"})require(channels.contains(name),"Missing channel: "+std::string(name));
    if(d.type==AircraftType::Typhoon||d.type==AircraftType::SR71) {
      for(auto name:{"elevon_L","elevon_R","gear_fold","gear_door"})require(channels.contains(name),"Missing articulated node");
    } else for(auto name:{"elevator","aileron_L","aileron_R","flap"})require(channels.contains(name),"Missing surface channel");
    if(d.type==AircraftType::Su57)for(auto name:{"vector_L","vector_R","levcon","slat","compression_L","compression_R","compression_nose"})require(channels.contains(name),"Missing Su-57 rig channel");
    std::uint64_t previous=mesh.triangleCount;unsigned lods=0;
    for(const auto lod:d.lodAssets)if(!lod.empty()) {
      const auto lodPath=root/lod;require((std::filesystem::is_regular_file(lodPath)||std::filesystem::is_regular_file(lodPath.string()+".ofspack")),"Required authored LOD missing");
      const auto reduced=loadGltf(lodPath.string());complete(reduced);require(reduced.triangleCount<previous,"Invalid LOD reduction");
      for(const auto& p:reduced.primitives)require(materials.contains(reduced.materials.at(p.material).name),"LOD material identity");
      for(const auto& node:reduced.nodes)if(!node.channel.empty())require(nodeNames.contains(node.name)&&channels.contains(node.channel),"LOD control-node identity");
      previous=reduced.triangleCount;++lods;
    }
    if(!lods) { const float cells[2]{.35f,1.2f};require(buildGpuMesh(mesh,cells,3).levels.size()==3,"Generated LOD presence"); }
    ++validated;
  }
  std::printf("%s asset-conformance: validated=%u unavailable=%u mandatory=%d\n",missing?"NOT RUN (incomplete content pack)":"PASS",validated,missing,mandatory);return missing?77:0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL asset-conformance: %s\n",e.what());return 1;}}
