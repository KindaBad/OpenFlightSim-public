#include "ofs/net/server.hpp"
#include "ofs/net/client.hpp"
#include "gltf.hpp"
#include "mesh.hpp"
#include "animation.hpp"
#include "scenario.hpp"
#include <chrono>
#include <cstdio>
#include <set>
#include <thread>
using namespace ofs;using namespace ofs::net;using namespace ofs::client;using scenario::check;
void registry() {
  check(aircraftDefinitions().size()>=3 && aircraftTypeFromName("typhoon")==AircraftType::Typhoon,"third aircraft selection");
  const auto& d=aircraftDefinition(AircraftType::Typhoon);
  check(d.gun && d.gun->ammo==150 && d.gun->rpm==1700 && d.flight.engine_count==2,"twin fighter capabilities");
  check(d.flight.mass!=su57Config().mass && d.flight.afterburner_thrust_each>d.flight.thrust_sl_static_each,"independent configuration");
  for(const auto& b:d.collision)check(b.radius>0 && b.center.norm()+b.radius<24,"specific lightweight hitboxes inside broad phase");
  World world;
  for(unsigned i=0;i<maxPlayers;++i)check(world.join(aircraftDefinitions()[i%aircraftDefinitions().size()].type)!=0,"registry-wide spawn capacity");
  check(!world.join(AircraftType::Typhoon),"capacity rejection");
  auto snap=world.snapshot();Message decoded;std::string reason;
  check(decode(encode(snap),decoded,reason) && decoded.aircrafts.size()==maxPlayers,"64 mixed snapshot");
  for(std::size_t i=0;i<decoded.aircrafts.size();++i)check(decoded.aircrafts[i].type==snap.aircrafts[i].type,"all IDs survive roundtrip");
  World single;const auto id=single.join(AircraftType::Typhoon);
  auto controls=single.aircraft(id).controls;controls.throttle[0]=1;controls.throttle[1]=.85;
  for(unsigned i=1;i<=600;++i){check(single.enqueue(id,{{i,single.tick()+1,controls}}),"engine control admission");single.step();}
  const auto a=single.aircraft(id);check(a.state.afterburner[0]>.99 && a.state.afterburner[1]==0,"server independent engine state");
  snap=single.snapshot();check(decode(encode(snap),decoded,reason) && decoded.aircrafts[0].state.afterburner[0]>.99,"minimal reheat scalar replicated");
  snap.aircrafts[0].state.afterburner[0]=1.01;check(!decode(encode(snap),decoded,reason),"invalid afterburner level rejected");
  check(single.enqueueFire(id,{1,single.tick()+1,0,0,true}),"Typhoon fire admitted");single.step();
  check(single.aircraft(id).life.ammo==149 && single.combat().projectiles().size()==1,"definition gun authoritative ammo");
  const auto&p=single.combat().projectiles().front();check(p.damage==34 && p.lifetime==d.gun->lifetime,"per-round gun characteristics");
  Prediction prediction;prediction.initialize(single.tick(),single.aircraft(id),0);
  check(prediction.simulator().config().afterburner_thrust_each==90000,"prediction selects Typhoon thrust");
  std::puts("Typhoon registry/64 spawns, protocol, engine authority, prediction and gun PASS");
}
void assets(const std::string&root) {
  const auto& d=aircraftDefinition(AircraftType::Typhoon);
  if(scenario::skipMissingAsset(root,d.modelAsset))return;
  auto base=loadGltf(root+"/"+std::string(d.modelAsset));
  check(base.valid(),"Typhoon GLB valid");const auto gpu=buildGpuMesh(base,nullptr,1);auto previous=gpu.levels[0].triangleCount;
  std::set<std::string> names;for(const auto&m:base.materials)names.insert(m.name);
  check(std::count_if(base.materials.begin(),base.materials.end(),[](const auto&m){return m.environmentReflection>0;})==1,"canopy reflection opted in");
  for(const auto path:d.lodAssets){auto reduced=loadGltf(root+"/"+std::string(path));check(reduced.valid() && reduced.images.empty(),"geometry-only authored LOD");
    for(const auto&m:reduced.materials)check(names.contains(m.name),"shared material identity");
    for(const auto&p:reduced.primitives)if(p.transformNode>=0){auto name=reduced.nodes[p.transformNode].name;check(std::any_of(base.nodes.begin(),base.nodes.end(),[&](const auto&n){return n.name==name;}),"shared rig identity");}
    auto built=buildGpuMesh(reduced,nullptr,1);check(built.levels[0].triangleCount<previous,"strict LOD reduction");previous=built.levels[0].triangleCount;
  }
  check(previous<10000,"cheap distant silhouette");
  AircraftPose pose;State state;Controls controls;pose.update(state,controls,d,0);std::vector<AssetMatrix> off,on;
  evaluatePose(base.nodes,pose,off);state.afterburner[0]=1;pose.update(state,controls,d,1);evaluatePose(base.nodes,pose,on);
  unsigned left=0,right=0;for(std::size_t i=0;i<base.nodes.size();++i){if(base.nodes[i].channel=="nozzle_L" && off[i]!=on[i])++left;if(base.nodes[i].channel=="nozzle_R" && off[i]!=on[i])++right;}
  check(left==12 && right==0,"twelve articulated petals per engine react independently");
  check(stableAircraftLod(d.visual.radius,3000,2,4)==3,"fourth LOD selection");
  std::printf("Typhoon authored LOD0=%llu distant=%llu; textures shared; independent petals PASS\n",(unsigned long long)gpu.levels[0].triangleCount,(unsigned long long)previous);
}
void multiplayer() {
  ServerConfig config;config.bind="127.0.0.1";config.port=0;Server server(config);
  Client civil("A320",AircraftType::A320),su57("Su57",AircraftType::Su57),typhoon("Typhoon",AircraftType::Typhoon);
  for(auto*c:{&civil,&su57,&typhoon})c->connect("127.0.0.1",server.port());
  auto start=std::chrono::steady_clock::now(),next=start,last=start;double accumulator=0;
  auto period=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(tickSeconds));
  while(std::chrono::steady_clock::now()-start<std::chrono::seconds(6)){
    auto now=std::chrono::steady_clock::now();double dt=std::chrono::duration<double>(now-last).count();last=now;
    server.poll();while(now>=next){server.step();next+=period;}
    for(auto*c:{&civil,&su57,&typhoon})c->poll(dt);
    if(civil.ready()&&su57.ready()&&typhoon.ready()){
      accumulator+=dt;for(auto*c:{&civil,&su57,&typhoon})c->setFiring(true);
      while(accumulator>=tickSeconds){civil.predict(civil.prediction().simulator().controls());su57.predict(su57.prediction().simulator().controls());
        auto c=typhoon.prediction().simulator().controls();c.throttle[0]=1;c.throttle[1]=.85;typhoon.predict(c);accumulator-=tickSeconds;}
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  for(auto*c:{&civil,&su57,&typhoon})check(c->ready() && c->remotes().size()==2,"three live aircraft clients");
  for(auto*c:{&civil,&su57}){auto a=c->remotes().at(typhoon.entity()).sampleAircraft(c->stats().renderTick);
    check(a.type==AircraftType::Typhoon && a.state.afterburner[0]>.98 && a.state.afterburner[1]<.001,"remote independent afterburner state");}
  check(civil.life().ammo==0 && su57.life().ammo<150 && typhoon.life().ammo<150,"mixed authoritative gun capabilities");
  std::printf("Mixed live GNS 6s snapshots=%llu/%llu/%llu shots=%llu TyphoonAB=%.6f/%.6f\n",(unsigned long long)civil.stats().snapshots,(unsigned long long)su57.stats().snapshots,(unsigned long long)typhoon.stats().snapshots,(unsigned long long)server.world().combat().stats().shots,server.world().aircraft(typhoon.entity()).state.afterburner[0],server.world().aircraft(typhoon.entity()).state.afterburner[1]);
  for(auto*c:{&civil,&su57,&typhoon})c->disconnect();
}
int main(int argc,char**argv){try{check(argc>=2,"expected suite");std::string s=argv[1];if(s=="registry")registry();else if(s=="assets"){check(argc==3,"expected root");assets(argv[2]);}else if(s=="multiplayer")multiplayer();else throw std::invalid_argument("unknown suite");return scenario::assetResult();}catch(const std::exception&e){std::fprintf(stderr,"TYPHOON INTEGRATION FAIL: %s\n",e.what());return 1;}}
