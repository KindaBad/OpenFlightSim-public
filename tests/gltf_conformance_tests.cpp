#include "gltf.hpp"
#include "mesh.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace ofs::client;
void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(){try {
  std::vector<std::uint8_t> bytes(40);bytes[0]=2;
  const float position[3]{1,2,3};std::memcpy(bytes.data()+4,position,12);
  const std::string sparse=R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":40}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":1},{"buffer":0,"byteOffset":4,"byteLength":12}],"accessors":[{"componentType":5126,"count":3,"type":"VEC3","sparse":{"count":1,"indices":{"bufferView":0,"componentType":5121},"values":{"bufferView":1}}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
  const auto mesh=parseGltf(sparse,bytes);require(mesh.valid(),"Sparse-only partial accessor loads");
  require(!mesh.materials.empty()&&mesh.primitives[0].material<mesh.materials.size(),"Zero-material glTF has usable default material");
  const auto& v=mesh.primitives[0].vertices;require(v[0]==0&&v[1]==0&&v[2]==0&&v[12]==0&&v[24]==1&&v[25]==2&&v[26]==3,"Non-sparse vertices are zero base");
  std::string truncated=sparse;auto a=truncated.find("\"sparse\":");auto b=truncated.find("}],\"meshes\"",a);
  truncated.replace(a,b-a,"\"bufferView\":1");
  // View length 12 admits one VEC3, despite 40 bytes backing storage.
  bool failed=false;try{parseGltf(truncated,bytes);}catch(const std::runtime_error&){failed=true;}require(failed,"Declared view bounds reject second/third element");
  failed=false;try{parseGltf(R"({"asset":{"version":"2.0"},"extensionsRequired":["KHR_draco_mesh_compression"]})",{});}catch(const std::runtime_error&){failed=true;}require(failed,"Unsupported required extension fails");
  failed=false;try{parseGltf(R"({"asset":{"version":"2.0"},"materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0,"texCoord":1}}}]})",{});}catch(const std::runtime_error&){failed=true;}require(failed,"Unsupported UV set explicitly fails");
  auto tangentDocument=sparse;
  auto replace=[&](const char* before,const char* after){const auto pos=tangentDocument.find(before);require(pos!=std::string::npos,"Fixture replacement exists");tangentDocument.replace(pos,std::strlen(before),after);};
  replace("\"byteLength\":40","\"byteLength\":64");
  replace("\"byteLength\":12}]","\"byteLength\":12},{\"buffer\":0,\"byteOffset\":16,\"byteLength\":48}]");
  replace("}}}],\"meshes\"","}}},{\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC4\"}],\"meshes\"");
  replace("\"POSITION\":0","\"POSITION\":0,\"TANGENT\":1");
  bytes.resize(64);
  const float tangent[]{1,0,0,-1};for(unsigned vertex=0;vertex<3;++vertex)std::memcpy(bytes.data()+16+vertex*16,tangent,16);
  const Mesh authored=parseGltf(tangentDocument,bytes);
  require(authored.primitives[0].hasTangents&&authored.primitives[0].vertices[8]==1&&authored.primitives[0].vertices[11]==-1,"Authored tangents imported from glTF accessor");
  const auto gpu=buildGpuMesh(authored,nullptr,1);
  require(kMeshVertexFloats==12,"GPU layout retains tangent");
  for(std::size_t vertex=0;vertex<gpu.levels[0].vertices.size()/12;++vertex){const auto* out=gpu.levels[0].vertices.data()+vertex*12;require(out[8]==1&&out[11]==-1,"Authored tangent and handedness preserved through render mesh");}
  std::puts("PASS standards-reference glTF sparse/base/view/required-extension/UV/tangent checks");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL glTF conformance: %s\n",e.what());return 1;}}
