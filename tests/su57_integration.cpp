#include "animation.hpp"
#include "gltf.hpp"
#include "mesh.hpp"
#include "ofs/net/client.hpp"
#include "ofs/net/server.hpp"
#include "scenario.hpp"
#include <cstdio>
#include <limits>
#include <set>
#include <thread>
#include <chrono>
using namespace ofs;using namespace ofs::client;using namespace ofs::net;using scenario::check;
void assets(const std::string& root) {
 const auto& d=aircraftDefinition(AircraftType::Su57);
 if(scenario::skipMissingAsset(root,d.modelAsset))return;
 auto m=loadGltf(root+'/'+std::string(d.modelAsset));
 check(m.valid(),"Su57 native GLB loads");
 std::printf("Su57 measured GLB dimensions %.6f %.6f %.6f; %llu triangles; %zu primitives\n",m.boundsMax[0]-m.boundsMin[0],m.boundsMax[2]-m.boundsMin[2],m.boundsMax[1]-m.boundsMin[1],(unsigned long long)m.triangleCount,m.primitives.size());
 check(std::abs(m.boundsMax[0]-m.boundsMin[0]-20.1)<.01 && std::abs(m.boundsMax[2]-m.boundsMin[2]-14.1)<.01 && std::abs(m.boundsMax[1]-m.boundsMin[1]-4.6)<.01,"metre scale length/span/gear height");
 std::set<std::string> channels;for(auto n:m.nodes)if(!n.channel.empty())channels.insert(n.channel);
 for(auto name:{"elevator","rudder","aileron_L","aileron_R","flap","slat","levcon","gear_fold","gear_door","steering","nose_wheel","wheel","compression_L","compression_R","compression_nose","vector_L","vector_R"})check(channels.contains(name),"complete useful rig channels");
 // One shared atlas: base colour, metallic/roughness and normal.
 check(m.images.size()>=3,"retained packed PBR maps");
 std::uint64_t previous=m.triangleCount;
 for(auto path:d.lodAssets) {
  auto lod=loadGltf(root+'/'+std::string(path));check(lod.valid() && lod.images.empty() && lod.triangleCount<previous,"LOD triangle reduction and shared texture table");
  for(const auto& p:lod.primitives){
   const auto& name=lod.materials.at(p.material).name;
   check(std::any_of(m.materials.begin(),m.materials.end(),[&](const auto& mat){return mat.name==name;}),"LOD material identity");
   if(p.transformNode>=0){const auto& node=lod.nodes.at(p.transformNode).name;check(std::any_of(m.nodes.begin(),m.nodes.end(),[&](const auto& n){return n.name==node;}),"LOD physical rig identity");}
  }
  previous=lod.triangleCount;
 }
 check(previous<8000,"cheap distant model");
 State st;Controls c;AircraftPose pose;pose.update(st,c,d,0);std::vector<AssetMatrix> rest,moved;evaluatePose(m.nodes,pose,rest);
 st.nozzle_angle[0]=.15;pose.update(st,c,d,0);evaluatePose(m.nodes,pose,moved);unsigned left=0,right=0;
 for(std::size_t i=0;i<m.nodes.size();++i) if(rest[i]!=moved[i]){left+=m.nodes[i].channel=="vector_L";right+=m.nodes[i].channel=="vector_R";}
 check(left==1 && right==0,"actual independent nozzle angle drives hierarchy");
 st.nozzle_angle[0]=0;st.pos_ned.z=-2.3;pose.update(st,c,d,0);evaluatePose(m.nodes,pose,moved);
 for(const auto& p:m.primitives)if(p.name.find("tire")!=std::string::npos){
  const auto delta=moved.at(p.transformNode);
  check(std::abs(delta[0]-1)<1e-5 && std::abs(delta[5]-1)<1e-5 && std::abs(delta[10]-1)<1e-5,"compression translates wheels without rotation");
  check(std::abs(delta[13]-.15)<1e-5,"wheel displacement equals physical oleo compression");
  check(std::abs(p.boundsMin[1]+delta[13]+2.3-d.visual.assetCg.y)<1e-5,"compressed tyre contact stays on ground plane");
 }

 for(const auto& p:m.primitives)if(p.name.find("tire")!=std::string::npos)
  std::printf("tire %s bounds %.4f/%.4f/%.4f -> %.4f/%.4f/%.4f material %u\n",p.name.c_str(),p.boundsMin[0],p.boundsMin[1],p.boundsMin[2],p.boundsMax[0],p.boundsMax[1],p.boundsMax[2],p.material);
}
void replay() {
 const auto& cfg=aircraftDefinition(AircraftType::Su57).flight;
 TrimRequest r;r.tas=180;r.altitude=3000;auto t=solveTrim(cfg,r);check(t.converged,"replay trim");
 Simulator a(cfg);a.setState(t.state);auto c=t.controls;c.maneuver_mode=true;c.elevator_stick=.4;c.aileron_stick=.2;c.throttle[0]=1;c.throttle[1]=.7;a.setControls(c);scenario::advance(a,90);
 Message m;m.type=Type::Snapshot;Aircraft rec;rec.id=1;rec.type=AircraftType::Su57;rec.state=a.state();rec.controls=c;m.aircrafts={rec};
 Message decoded;std::string reason;check(decode(encode(m),decoded,reason),"TV snapshot accepted");
 check(decoded.aircrafts[0].state.nozzle_angle[0]==a.state().nozzle_angle[0] && decoded.aircrafts[0].state.nozzle_angle[1]==a.state().nozzle_angle[1],"both nozzle actuator doubles preserved");
 Simulator exact(cfg);exact.setState(a.state());exact.setControls(c);Simulator packet(cfg);packet.setState(decoded.aircrafts[0].state);packet.setControls(decoded.aircrafts[0].controls);
 scenario::advance(a,1200);scenario::advance(exact,1200);scenario::advance(packet,1200);
 const auto nativeError=(a.state().pos_ned-exact.state().pos_ned).norm(),packetError=(a.state().pos_ned-packet.state().pos_ned).norm();
 std::printf("Su57 10s replay nativeError=%.9g packetError=%.9g vector=%.9g/%.9g\n",nativeError,packetError,a.state().nozzle_angle[0],a.state().nozzle_angle[1]);
 check(nativeError<1e-8 && packetError<.02,"physical TV replay remains deterministic and packet rounding bounded");
 for(double bad:{.4,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
  m.aircrafts[0].state.nozzle_angle[0]=bad;bool rejected=false;
  try{rejected=!decode(encode(m),decoded,reason);}catch(const std::invalid_argument&){rejected=true;}
  check(rejected,"invalid nozzle state rejected");
 }
}
void multiplayer() {
 ServerConfig config;config.bind="127.0.0.1";config.port=0;Server server(config);
 Client civil("A320",AircraftType::A320),typhoon("Typhoon",AircraftType::Typhoon),sr71("SR71",AircraftType::SR71),su57("Su57",AircraftType::Su57);
 const std::array<Client*,4> clients{&civil,&typhoon,&sr71,&su57};for(auto c:clients)c->connect("127.0.0.1",server.port());
 auto start=std::chrono::steady_clock::now(),next=start,last=start;double acc=0;
 auto period=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(tickSeconds));
 while(std::chrono::steady_clock::now()-start<std::chrono::seconds(8)) {
  auto now=std::chrono::steady_clock::now();const double dt=std::chrono::duration<double>(now-last).count();last=now;
  server.poll();while(now>=next){server.step();next+=period;}bool ready=true;for(auto c:clients){c->poll(dt);ready&=c->ready();}
  if(ready){acc+=dt;for(auto c:clients)c->setFiring(true);while(acc>=tickSeconds){for(auto c:clients){auto control=c->prediction().simulator().controls();if(c==&su57){control.throttle[0]=1;control.throttle[1]=.7;control.elevator_stick=.3;control.maneuver_mode=true;}c->predict(control);}acc-=tickSeconds;}}
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
 }
 for(auto c:clients)check(c->ready() && c->remotes().size()==3,"four live aircraft types");
 for(auto c:{&civil,&typhoon,&sr71}) {
  auto remote=c->remotes().at(su57.entity()).sampleAircraft(c->stats().renderTick);
  check(remote.type==AircraftType::Su57 && remote.state.afterburner[0]>.98 && remote.state.afterburner[1]<.001,"remote independent propulsion state");
  check(std::isfinite(remote.state.nozzle_angle[0]) && std::abs(remote.state.nozzle_angle[0])<=15*kDeg2Rad,"remote physical nozzle state");
 }
 check(server.world().aircraft(su57.entity()).controls.maneuver_mode,"live server receives maneuver mode");
 check(civil.life().ammo==0 && sr71.life().ammo==0 && su57.life().ammo<150,"server gun capabilities for four types");
 std::printf("Live four-type GNS 8s Su57 snapshots=%llu ammo=%u TV=%.6f/%.6f\n",(unsigned long long)su57.stats().snapshots,su57.life().ammo,server.world().aircraft(su57.entity()).state.nozzle_angle[0],server.world().aircraft(su57.entity()).state.nozzle_angle[1]);
 for(auto c:clients)c->disconnect();
}
int main(int argc,char** argv){try{check(argc>=2,"suite");std::string n=argv[1];if(n=="assets"){check(argc==3,"root");assets(argv[2]);}else if(n=="replay")replay();else if(n=="multiplayer")multiplayer();else throw std::invalid_argument("suite");return scenario::assetResult();}catch(const std::exception& e){std::fprintf(stderr,"SU57 INTEGRATION FAIL: %s\n",e.what());return 1;}}
