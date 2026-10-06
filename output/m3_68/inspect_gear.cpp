#include "gltf.hpp"
#include "animation.hpp"
#include "mesh.hpp"
#include "ofs/aircraft_definition.hpp"
#include <cstdio>
int main(){using namespace ofs;using namespace ofs::client;auto d=aircraftDefinition(AircraftType::Su57);auto m=loadGltf(std::string(d.modelAsset));AircraftPose pose;State s;s.pos_ned.z=-2.3;Controls c;c.gear01=1;pose.update(s,c,d,0);std::vector<AssetMatrix>delta;evaluatePose(m.nodes,pose,delta);for(auto& p:m.primitives)if(p.name.find("tire")!=std::string::npos || p.name.find("wheel spin")!=std::string::npos){auto mat=m.materials[p.material];const auto& n=m.nodes.at(p.transformNode);const auto a=delta[p.transformNode];std::printf("GEAR %s node=%s channel=%s gain=%g axis=%g/%g/%g boundsY=%.3f/%.3f delta=%.3f/%.3f/%.3f diag=%.3f/%.3f/%.3f alpha=%d color=%g/%g/%g/%g double=%d\n",p.name.c_str(),n.name.c_str(),n.channel.c_str(),n.gain,n.axis[0],n.axis[1],n.axis[2],p.boundsMin[1],p.boundsMax[1],a[12],a[13],a[14],a[0],a[5],a[10],int(mat.alpha),mat.baseColor[0],mat.baseColor[1],mat.baseColor[2],mat.baseColor[3],mat.doubleSided);}}
