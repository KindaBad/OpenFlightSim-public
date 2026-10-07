#include "animation.hpp"
#include "effects.hpp"
#include "gltf.hpp"
#include "mesh.hpp"
#include "scenery.hpp"
#include "atmosphere_model.hpp"
#include "landscape.hpp"
#include "map.hpp"
#include "procedural.hpp"
#include "ofs/terrain.hpp"
#include "texture_mips.hpp"
#include "ofs/aircraft_definition.hpp"
#include "ofs/net/client.hpp"
#include "ofs/net/world.hpp"
#include "ofs/net/server.hpp"
#include "scenario.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <set>
#include <string>
#include <chrono>
#include <thread>

using namespace ofs;
using namespace ofs::client;
using namespace ofs::net;
using scenario::check;

void materials() {
  const std::vector<std::uint8_t> blackWhite{0,0,0,0,255,255,255,255};
  const auto colorMip=textureMipChain(blackWhite,2,1,TextureRole::Srgb);
  const auto dataMip=textureMipChain(blackWhite,2,1,TextureRole::Linear);
  check(colorMip.size()==12 && colorMip[8]==188 && colorMip[11]==128 && dataMip[8]==128,"mips use linear light for colour and linear alpha/data");
  const std::vector<std::uint8_t> odd{0,0,0,255,0,0,0,255,255,255,255,255};
  check(textureMipChain(odd,3,1,TextureRole::Linear)[12]==85,"odd mip sizes retain edge coverage");
  const auto normals=textureMipChain({255,128,128,255,128,128,255,255},2,1,TextureRole::Normal);
  const double nx=normals[8]/127.5-1,nz=normals[10]/127.5-1;
  check(std::abs(nx*nx+nz*nz-1)<.02,"filtered normal maps remain normalized");
  const char* start=R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],"buffers":[{"byteLength":36}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"}])";
  const float positions[]{0,0,0,1,0,0,0,1,0};std::vector<std::uint8_t> binary(sizeof positions);std::memcpy(binary.data(),positions,sizeof positions);
  for(const std::string& ending:{std::string("}"),std::string(R"(,"materials":[{"name":"__gltf_default","pbrMetallicRoughness":{"baseColorFactor":[1,0,0,1]}}]})")}) {
    const auto mesh=parseGltf(std::string(start)+ending,binary);check(mesh.valid() && mesh.primitives.size()==1,"implicit material glTF loads");
    const auto index=mesh.primitives[0].material;check(index<mesh.materials.size(),"default material index valid");
    check(mesh.materials[index].baseColor[1]==1 && mesh.materials[index].metallic==1 && mesh.materials[index].roughness==1,"implicit default material distinct from authored material");
    const float cells[]{.1f,.2f};auto gpu=buildGpuMesh(mesh,cells);check(gpu.levels[0].batches.size()==1 && gpu.levels[0].batches[0].material==index,"material-free GPU mesh construction");
  }
}
void definitions() {
  const auto& civil = aircraftDefinition(AircraftType::A320);
  const auto& fighter = aircraftDefinition(AircraftType::Typhoon);
  check(!civil.gun && fighter.gun && fighter.flight.mass < civil.flight.mass/3,
        "civilian capability and distinct fighter configuration");
  check(fighter.flight.engine_count==2 && civil.flight.engine_count==2, "engine configurations");
  for (const auto& definition : aircraftDefinitions()) {
    check(aircraftTypeFromName(definition.key)==definition.type,"type selection");
    check(validAircraftType(definition.type),"stable registered type");
    check(definition.visual.gearSeconds>1 && definition.visual.flapSeconds>1,"transition timing");
  }
  bool rejected=false;
  try { aircraftTypeFromName("../../custom.glb"); } catch(const std::invalid_argument&) { rejected=true; }
  check(rejected && !validAircraftType(static_cast<AircraftType>(255)),"invalid type selection");
  World world;
  check(!world.join(static_cast<AircraftType>(0)),"invalid spawn type");
  const auto civilId=world.join(AircraftType::A320), fighterId=world.join(AircraftType::Typhoon);
  check(world.aircraft(civilId).life.ammo==0 && world.aircraft(fighterId).life.ammo==fighter.gun->ammo,"capability ammo");
  for (unsigned tick=1; tick<=240; ++tick) {
    world.enqueueFire(civilId,{tick,world.tick()+1,0,0,true});
    world.step();
  }
  check(world.combat().stats().shots==0 && world.combat().projectiles().empty() &&
        world.players().at(civilId).fireInputs.empty() && world.aircraft(civilId).life.ammo==0,
        "server rejects A320 fire, no weapon queue or projectiles");
  check(world.enqueueFire(fighterId,{1,world.tick()+1,0,0,true}),"fighter fire admission");
  world.step();
  check(world.combat().stats().shots==1 && world.aircraft(fighterId).life.ammo==fighter.gun->ammo-1,"fighter authoritative gun");
  Message message; message.type=Type::Snapshot;
  message.aircrafts={world.aircraft(civilId),world.aircraft(fighterId)};
  Message decoded; std::string reason;
  check(decode(encode(message),decoded,reason) && decoded.aircrafts[0].type==AircraftType::A320 &&
        decoded.aircrafts[1].type==AircraftType::Typhoon,"mixed snapshot type roundtrip");
  message.type=Type::Hello; message.text="pilot"; message.aircraftType=AircraftType::Typhoon;
  check(decode(encode(message),decoded,reason) && decoded.aircraftType==AircraftType::Typhoon,"hello type roundtrip");
  auto invalid=encode(message); invalid.back()=99;
  check(!decode(invalid,decoded,reason),"wire invalid type rejection");
  message.type=Type::Snapshot; message.aircrafts[0].life.ammo=1;
  check(!decode(encode(message),decoded,reason),"civil ammo wire invariant");
  Prediction prediction;
  prediction.initialize(world.tick(),world.aircraft(fighterId),0);
  check(prediction.simulator().config().mass==fighter.flight.mass,"prediction uses fighter configuration");
  std::puts("capabilities: A320 attempted fire 240 ticks => 0 shots, fighter => 1 authoritative shot; IDs/codec/prediction PASS");
}

void animations() {
  AircraftPose pose; State state; Controls controls;
  const auto& definition=aircraftDefinition(AircraftType::A320);
  pose.update(state,controls,definition,0);
  controls.gear01=0; controls.flap01=1; controls.spoiler01=1;
  controls.aileron_stick=.5; controls.elevator_stick=.5; controls.rudder_pedal=.5;
  const auto stateBefore=state;
  pose.update(state,controls,definition,.25);
  check(std::abs(pose.gear-.95)<1e-12 && std::abs(pose.flap-.0625)<1e-12,"gradual gear/flaps");
  check(pose.channel("aileron_L")<0 && pose.channel("aileron_R")>0 &&
        pose.channel("elevator")>0 && pose.channel("rudder")>0,"control surface signs");
  for(unsigned i=0;i<20;++i) pose.update(state,controls,definition,.25);
  check(pose.gear==0 && pose.flap==1 && pose.spoiler==1,"transition endpoints");
  controls.gear01=1;
  for(unsigned i=0;i<20;++i) pose.update(state,controls,definition,.25);
  check(std::abs(pose.gear-1)<1e-12,"gear extension endpoint");
  state.vel_ned={5,0,0}; state.n1[0]=.5; controls.steering=.3;
  pose.update(state,controls,definition,.02);
  check(pose.wheel!=0 && pose.noseWheel!=0 && pose.steering<0 && pose.fan[0]!=0,"wheels/steering/fans");
  check(stateBefore.pos_ned.norm()==state.pos_ned.norm() && controls.gear01==1,"visual never writes simulation");
  AircraftPose suspension;
  const auto& typhoon=aircraftDefinition(AircraftType::Typhoon);
  state.pos_ned.z=-typhoon.flight.gear_nose.z+.12;
  suspension.update(state,controls,typhoon,0);
  check(std::abs(suspension.channel("compression_nose")-.12)<1e-12 &&
        std::abs(suspension.channel("compression_L")-.12)<1e-12,"oleo keeps wheels on ground");
  state.pos_ned.z=-1000;
  suspension.update(state,controls,typhoon,.1);
  check(suspension.channel("compression_nose")==0 && suspension.channel("compression_R")==0,
        "airborne gear has no compression");
  std::puts("animations: control signs, smooth reversible gear/flaps/spoilers, wheels/steering/fans PASS");
  check(stableAircraftLod(26,145,0)==0 && stableAircraftLod(26,135,1)==1 &&
        stableAircraftLod(26,100,1)==0 && stableAircraftLod(26,160,0)==1 &&
        stableAircraftLod(26,1800,1)==2 && stableAircraftLod(26,1400,2)==2 &&
        stableAircraftLod(26,1100,2)==1,"LOD hysteresis and distant-aircraft reduction");
  State physical;physical.actuators_initialized=true;physical.elevator=.12;physical.aileron=.2;physical.rudder=-.3;physical.flap=.4;physical.spoiler=.6;
  physical.canard=.2;physical.elevon_l=.5;physical.elevon_r=-.1;
  AircraftPose allocated;
  allocated.update(physical,controls,typhoon,0);
  check(std::abs(allocated.channel("canard")+elevatorDeflection(physical.canard,typhoon.flight))<1e-12,"canard animation uses physical actuator");
  check(std::abs(allocated.channel("elevon_L")-clamp(elevatorDeflection(physical.elevon_l,typhoon.flight)-physical.flap*15*kDeg2Rad,-25*kDeg2Rad,25*kDeg2Rad))<1e-12,
    "trailing animation uses allocated physical actuator");

}

void particles() {
  EffectPool pool(32); Effect particle;
  particle.position={1,2,-3}; particle.velocity={10,0,0}; particle.lifetime=.5f; particle.gravity=9.81f;
  check(pool.spawn(particle),"spawn"); pool.update(.1);
  check(pool.effects()[0].age>.09 && pool.effects()[0].position.x>1.9 && pool.effects()[0].velocity.z>0,"age and motion persist without compaction");
  pool.update(.5); check(pool.size()==0,"lifetime cleanup");
  for(unsigned i=0;i<10000;++i) pool.spawn(particle);
  check(pool.size()==32 && pool.peakSize()==32 && pool.dropped()==9968,"bounded allocation and overflow");
  pool.update(1); check(pool.size()==0,"full pool expires");
  particle.lifetime=std::numeric_limits<float>::quiet_NaN();
  check(!pool.spawn(particle),"invalid particle rejected");
  particle.lifetime=1; particle.size=std::numeric_limits<float>::infinity();
  check(!pool.spawn(particle),"nonfinite particle size rejected");
  particle.size=1;particle.axis.x=std::numeric_limits<double>::quiet_NaN();
  check(!pool.spawn(particle),"nonfinite condensation orientation rejected");
  particle.kind=EffectKind::Vapor;particle.axis={};particle.stretch=2;
  check(!pool.spawn(particle),"stretched vapor needs a nonzero orientation");
  particle.axis={1,0,0};particle.billboard=false;particle.normal={1,0,0};
  check(!pool.spawn(particle),"wing vapor rejects a degenerate plane");
  EffectPool atmospheric(2048); CombatEffects effects(atmospheric,EffectsQuality::High);
  State plane; plane.vel_ned={150,0,0}; plane.n1[0]=plane.n1[1]=.8;
  for(unsigned i=0;i<120;++i) { effects.updateAircraft(plane,1./60,AircraftType::A320,0,1); atmospheric.update(1./60); }
  check(atmospheric.countOf(EffectKind::Smoke)==0 && atmospheric.countOf(EffectKind::Contrail)==0,
        "no normal airliner black smoke or ground contrails");
  plane.pos_ned.z=-8500;
  for(unsigned i=0;i<120;++i) { effects.updateAircraft(plane,1./60,AircraftType::A320,0,1); atmospheric.update(1./60); }
  check(atmospheric.countOf(EffectKind::Contrail)==0,"paused aircraft does not stack condensation at its nozzles");
  for(unsigned i=0;i<120;++i) { plane.pos_ned+=plane.vel_ned*(1./60); effects.updateAircraft(plane,1./60,AircraftType::A320,0,1); atmospheric.update(1./60); }
  check(atmospheric.countOf(EffectKind::Contrail)>20,"high altitude trails");
  const auto& trail=*std::find_if(atmospheric.effects().begin(),atmospheric.effects().end(),[](const auto& e){return e.kind==EffectKind::Contrail;});
  check(trail.velocity.norm()<1 && trail.billboard,"condensation remains in atmosphere");
  atmospheric.clear(); plane.pos_ned.z=-1500;
  effects.updateAircraft(plane,.01,AircraftType::Typhoon,1,3);
  plane.pos_ned.x+=15;
  effects.updateAircraft(plane,.1,AircraftType::Typhoon,1,3);
  check(atmospheric.countOf(EffectKind::Vapor)>0,"real high-load vapor mapping");
  // Use independent emitters and moving poses: vapor follows humid airflow,
  // with a broad Su-57 wing sheet as well as the two tip vortices.
  const auto vaporCount=[](double humidity,double load,double alpha,double speed,
                           double altitude,bool moving,bool enabled,EffectsQuality quality,Vec3 wind=Vec3{}) {
    EffectPool pool(512);CombatEffects effects(pool,quality);
    effects.setCondensation(enabled,humidity);effects.setEmissions(false,false);
    State s;s.pos_ned.z=-altitude;s.vel_ned={speed,0,0};
    s.att=quatFromEuler(0,alpha*kDeg2Rad,0);
    Weather weather;weather.wind_ned=wind;
    effects.updateAircraft(s,.01,AircraftType::Su57,0,load,100,weather);
    if(moving)s.pos_ned.x+=speed*.05;
    effects.updateAircraft(s,.05,AircraftType::Su57,0,load,100,weather);
    return pool.countOf(EffectKind::Vapor);
  };
  check(vaporCount(.85,4,20,180,1500,true,true,EffectsQuality::High)>2,
        "humid high-load Su57 wing sheets and vortices");
  check(vaporCount(.9,1,30,180,1500,true,true,EffectsQuality::Medium)>2,
        "high AoA condensation even without high G");
  check(vaporCount(.1,4,20,180,1500,true,true,EffectsQuality::High)==0 &&
        vaporCount(.85,1,3,180,1500,true,true,EffectsQuality::High)==0 &&
        vaporCount(.85,4,20,180,1500,false,true,EffectsQuality::High)==0 &&
        vaporCount(.85,4,20,20,1500,true,true,EffectsQuality::High)==0 &&
        vaporCount(.85,4,20,180,3,true,true,EffectsQuality::High)==0 &&
        vaporCount(.85,4,20,180,1500,true,false,EffectsQuality::High)==0 &&
        vaporCount(.85,4,20,180,1500,true,true,EffectsQuality::Low)==0 &&
        vaporCount(.85,4,20,180,1500,true,true,EffectsQuality::High,{180,0,0})==0,
        "dry/cruise/paused/slow/ground/disabled/low-quality/no-relative-flow gates");
  EffectPool fading(512);CombatEffects vapor(fading,EffectsQuality::High);
  State humid;humid.pos_ned.z=-1500;humid.vel_ned={180,0,0};
  vapor.updateAircraft(humid,.01,AircraftType::Su57,0,4);
  humid.pos_ned.x+=9;vapor.updateAircraft(humid,.05,AircraftType::Su57,0,4);
  check(fading.countOf(EffectKind::Vapor)>0,"vapor starts");
  const auto& definition=aircraftDefinition(AircraftType::Su57);
  const auto cg=loadedCg(definition.flight,humid);
  bool leftSheet=false,rightSheet=false,tipWisp=false;
  for(const auto& puff:fading.effects())if(puff.kind==EffectKind::Vapor) {
    check(puff.velocity.norm()<1 && puff.stretch>puff.size &&
          std::abs(puff.axis.norm()-1)<1e-6,"vapor is an oriented band advected by atmosphere");
    const auto body=humid.att.inverseRotate(puff.position-humid.pos_ned)+cg;
    if(std::abs(body.y)<5.8) {
      leftSheet|=body.y<0;rightSheet|=body.y>0;
      check(body.z<0 && puff.lifetime<.08,"wing veil hugs suction side and evaporates over the chord");
      check(std::abs(puff.axis.y)>.5 && puff.axis.x<0,"wing ridge follows sweep rather than camera orientation");
      check(!puff.billboard && std::abs(puff.normal.z)>.99 && puff.size<.4 &&
            (puff.tint>>24)<=42,"wing vapor stays thin, translucent and fixed to its plane");
    } else tipWisp|=puff.lifetime<.2 && std::abs(puff.axis.x)>.99;
  }
  check(leftSheet && rightSheet && tipWisp,"both wing ridges and narrow airflow-aligned tip wisps");
  humid.pos_ned.x+=9;vapor.updateAircraft(humid,.05,AircraftType::Su57,0,1);
  fading.update(.3);check(fading.countOf(EffectKind::Vapor)==0,"vapor evaporates after unloading");
  EffectPool banked(512);CombatEffects inverted(banked,EffectsQuality::Medium);
  inverted.setEmissions(false,false);
  humid.att=quatFromEuler(45*kDeg2Rad,0,0);Weather breeze;breeze.wind_ned={8,3,0};
  inverted.updateAircraft(humid,.01,AircraftType::Su57,0,-4,100,breeze);
  humid.pos_ned.x+=9;inverted.updateAircraft(humid,.05,AircraftType::Su57,0,-4,100,breeze);
  unsigned lowerBands=0;
  for(const auto& puff:banked.effects())if(puff.kind==EffectKind::Vapor) {
    check((puff.velocity-breeze.wind_ned).norm()<1e-6,"banked vapor advects with weather wind");
    const auto body=humid.att.inverseRotate(puff.position-humid.pos_ned)+cg;
    if(std::abs(body.y)<5.8) {
      const auto axis=humid.att.inverseRotate(puff.axis);
      check(body.z>0 && std::abs(axis.y)>.5 && axis.x<0,"negative-load vapor follows banked lower wing");
      ++lowerBands;
    }
  }
  check(lowerBands>0,"Medium quality retains banked wing condensation");
  const auto density=[](unsigned fps) {
    EffectPool pool(4096);CombatEffects emitter(pool,EffectsQuality::High);
    emitter.setCondensation(true,.85);emitter.setEmissions(false,false);
    State state;state.pos_ned.z=-1500;state.vel_ned={180,0,0};
    double opticalArea=0;
    for(unsigned frame=0;frame<fps;++frame) {
      const double dt=1./fps;
      pool.update(dt);state.pos_ned+=state.vel_ned*dt;
      emitter.updateAircraft(state,dt,AircraftType::Su57,0,4);
      if(frame>fps/4) for(const auto& puff:pool.effects())if(puff.kind==EffectKind::Vapor) {
        const double t=puff.age/puff.lifetime;
        opticalArea+=puff.size*puff.stretch*(puff.tint>>24)*(1-t*t)*clamp(t/.12,0.,1.);
      }
    }
    return opticalArea/fps;
  };
  const double sixty=density(60),oneTwenty=density(120),thirty=density(30);
  check(sixty>0 && std::abs(sixty-oneTwenty)/sixty<.25 &&
        std::abs(sixty-thirty)/sixty<.25,"vapor density stays similar across render frame rates");
  effects.onShot({}, {850,0,0},3,true); effects.onHit({},false); effects.onDestroyed({},{});
  check(atmospheric.countOf(EffectKind::MuzzleFlash)>0 && atmospheric.countOf(EffectKind::Explosion)>0,"combat effects");
  const auto& flash=*std::find_if(atmospheric.effects().begin(),atmospheric.effects().end(),[](const auto& e){return e.kind==EffectKind::Explosion;});
  check((flash.tint&255)>((flash.tint>>16)&255),"ABGR explosion tint is warm, not blue");
  atmospheric.update(10); check(atmospheric.size()==0,"all effects eventually retire");
  std::puts("particles: 10000 emissions bounded to 32; age/motion/expiry and atmospheric gates PASS");
}

void hierarchy(const std::string& root) {
  unsigned inspected=0;
  for(const auto& definition:aircraftDefinitions()) {
    if(scenario::skipMissingAsset(root,definition.modelAsset)) continue;
    ++inspected;
    const auto mesh=loadGltf(root+"/"+std::string(definition.modelAsset));
    check(mesh.valid() && !mesh.nodes.empty(),"real aircraft hierarchy loaded");
    std::set<std::string> channels;
    unsigned parents=0;
    for(std::size_t i=0;i<mesh.nodes.size();++i) {
      const auto& node=mesh.nodes[i];
      if(!node.channel.empty()) channels.insert(node.channel);
      for(int child:node.children) {check(mesh.nodes.at(child).parent==static_cast<int>(i),"parent child identity"); ++parents;}
    }
    check(parents>10,"hierarchy not flattened");
    if (definition.type==AircraftType::Typhoon) {
      for(const char* name:{"canard","elevon_L","elevon_R","rudder","spoiler","gear_fold","gear_door","wheel","nose_wheel","steering","nozzle_L","nozzle_R","compression_nose","compression_L","compression_R"})
        check(channels.contains(name),"Typhoon articulated channel present");
      // One shared atlas: base colour, metallic/roughness and normal.
      check(mesh.images.size()>=3 && !mesh.textures.empty(),"Typhoon embedded atlas maps");
      check(std::count_if(mesh.materials.begin(),mesh.materials.end(),[](const auto&m){return m.normalTexture>=0;})>=1,"Typhoon normal map decoded");
    } else if(definition.type==AircraftType::SR71) {
      for(const char* name:{"elevon_L","elevon_R","rudder","gear_fold","gear_door","wheel","nose_wheel","steering","inlet_L","inlet_R","nozzle_L","nozzle_R"})
        check(channels.contains(name),"SR71 delta and inlet articulated channels");
      check(!channels.contains("flap") && !channels.contains("canard"),"SR71 has no conventional flap or canard rig");
    } else if(definition.type==AircraftType::Su57) {
      for(const char* name:{"aileron_L","aileron_R","elevator","rudder","flap","slat","levcon","gear_fold","gear_door","wheel","nose_wheel","steering","vector_L","vector_R"})
        check(channels.contains(name),"Su57 articulated channel present");
    } else for(const char* name:{"aileron_L","aileron_R","elevator","rudder","flap","gear","wheel","nose_wheel","steering"})
      check(channels.contains(name),"required articulated channel present");
    if(definition.type==AircraftType::A320) check(channels.contains("spoiler") && channels.contains("fan_L") && channels.contains("fan_R"),"A320 spoiler and engine rigs");
    AircraftPose pose; State state; Controls controls;
    pose.update(state,controls,definition,0);
    std::vector<AssetMatrix> rest,moved;
    evaluatePose(mesh.nodes,pose,rest);
    controls.elevator_stick=.7; controls.aileron_stick=.7; controls.rudder_pedal=.7;
    pose.update(state,controls,definition,.1); evaluatePose(mesh.nodes,pose,moved);
    unsigned changed=0;
    for(const auto& primitive:mesh.primitives) {
      check(primitive.node>=0 && mesh.nodes[primitive.node].mesh>=0,"primitive mesh/node reference");
      if(primitive.transformNode>=0 && rest[primitive.transformNode]!=moved[primitive.transformNode]) ++changed;
    }
    check(changed>=5,"independent surface transforms change");
    const float cells[]{.15f,.45f}; const auto gpu=buildGpuMesh(mesh,cells);
    std::set<std::pair<int,unsigned>> pairs;
    for(const auto& batch:gpu.levels[0].batches)
      check(pairs.emplace(batch.transformNode,batch.material).second,"material/rig cache unique batching");
    std::printf("%s hierarchy nodes=%zu primitives=%zu articulatedChannels=%zu batches=%zu tris=%llu/%llu/%llu images=%zu\n",
      definition.key.data(),mesh.nodes.size(),mesh.primitives.size(),channels.size(),pairs.size(),
      (unsigned long long)gpu.levels[0].triangleCount,(unsigned long long)gpu.levels[1].triangleCount,
      (unsigned long long)gpu.levels[2].triangleCount,mesh.images.size());
  }
  std::printf("hierarchy: %u of %zu aircraft models inspected\n",inspected,aircraftDefinitions().size());
}
void mixed() {
  ServerConfig config; config.bind="127.0.0.1"; config.port=0;
  Server server(config);
  Client civil("civil",AircraftType::A320), fighter("fighter",AircraftType::Typhoon);
  civil.connect("127.0.0.1",server.port()); fighter.connect("127.0.0.1",server.port());
  auto start=std::chrono::steady_clock::now(), next=start, last=start;
  double accumulator=0; bool connected=false;
  const auto period=std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(tickSeconds));
  while(std::chrono::steady_clock::now()-start<std::chrono::seconds(5)) {
    const auto now=std::chrono::steady_clock::now(); const double dt=std::chrono::duration<double>(now-last).count(); last=now;
    server.poll(); while(now>=next) {server.step(); next+=period;}
    civil.poll(dt); fighter.poll(dt);
    if(civil.ready() && fighter.ready()) {
      connected=true; accumulator+=dt; civil.setFiring(true); fighter.setFiring(true);
      while(accumulator>=tickSeconds) {
        civil.predict(civil.prediction().simulator().controls());
        fighter.predict(fighter.prediction().simulator().controls()); accumulator-=tickSeconds;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  check(connected && civil.ready() && fighter.ready(),"mixed live sessions");
  check(civil.remotes().size()==1 && fighter.remotes().size()==1,"mixed remote membership");
  check(civil.remotes().begin()->second.sampleAircraft(civil.stats().renderTick).type==AircraftType::Typhoon &&
        fighter.remotes().begin()->second.sampleAircraft(fighter.stats().renderTick).type==AircraftType::A320,
        "mixed live remote model identity");
  check(civil.life().ammo==0 && fighter.life().ammo<aircraftDefinition(AircraftType::Typhoon).gun->ammo && server.world().combat().stats().shots>20,
        "civil fire ignored while fighter fires in same session");
  for(const auto& round:server.world().combat().projectiles()) check(round.owner==fighter.entity(),"only fighter owns projectiles");
  std::printf("mixed GNS 5s: civil/fighter snapshots=%llu/%llu authoritative shots=%llu civilAmmo=%u fighterAmmo=%u\n",
    (unsigned long long)civil.stats().snapshots,(unsigned long long)fighter.stats().snapshots,
    (unsigned long long)server.world().combat().stats().shots,civil.life().ammo,fighter.life().ammo);
  civil.disconnect(); fighter.disconnect();
  for(unsigned i=0;i<300 && !server.world().players().empty();++i) {server.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  check(server.world().players().empty() && server.world().combat().projectiles().empty(),"mixed session cleanup");
}
void scenery() {
  for(bool conifer:{false,true}) {
    const auto full=unitTree(conifer,false),distant=unitTree(conifer,true);
    check(!full.empty() && distant.size()<full.size()/2,"both species keep a much cheaper distant mesh");
    check(full.size()/3<450 && distant.size()/3<80,"bounded per-tree geometry budget");
    bool wood=false,foliage=false;
    for(const auto* mesh:{&full,&distant}) for(std::size_t i=0;i<mesh->size();i+=3) {
      const auto& a=(*mesh)[i];const auto& b=(*mesh)[i+1];const auto& c=(*mesh)[i+2];
      const Vec3 p{a.x,a.y,a.z},q{b.x,b.y,b.z},r{c.x,c.y,c.z};
      const Vec3 n{a.nx+b.nx+c.nx,a.ny+b.ny+c.ny,a.nz+b.nz+c.nz};
      check(std::isfinite(p.norm2()) && std::isfinite(n.norm2()),"finite vegetation vertices/normals");
      check((q-p).cross(r-p).dot(n)>0,"foliage and branches face outward without degenerate triangles");
      // Instances scale this mesh by the tree's height and crown spread.
      check(a.y>=-1e-6 && a.y<=1.02 && std::hypot(a.x,a.z)<.40,"unit tree is rooted at the origin and one unit tall");
      check((a.u==0 || a.u==1) && a.u==b.u && a.u==c.u && std::abs(a.v-a.y)<1e-6,"vertices carry the wood/foliage flag and their height");
      (a.u>.5f?foliage:wood)=true;
    }
    check(wood && foliage,"each tree has a trunk and a crown");
    const auto repeat=unitTree(conifer,false);
    check(repeat.size()==full.size() && !std::memcmp(repeat.data(),full.data(),full.size()*sizeof(SurfaceVertex)),"deterministic vegetation generation");
  }
  std::puts("PASS scenery: outward geometry, unit scale, deterministic species and bounded LODs");
}

void atmosphere() {
  const AtmosphereModel clear(AtmosphereParameters::fromWeather(70,0,150));
  const auto sunAt=[](float degrees){const float e=degrees*float(kDeg2Rad);return std::array<float,3>{std::cos(e),std::sin(e),0};};
  // Koschmieder: contrast falls to 2 % at the stated visual range, so the
  // optical depth of that path at sea level is ln(50).
  for(float visibility:{5.f,20.f,70.f,150.f}) {
    const AtmosphereModel air(AtmosphereParameters::fromWeather(visibility,0,150));
    check(std::abs(air.medium(0).extinction.g*visibility*1000-3.912)<.05,"aerosol load reproduces the requested visual range");
  }
  // Direct sunlight: about 100 klx under a high sun, redder and dimmer as it sets.
  const Rgb noon=clear.sunIrradiance(0,std::sin(60*float(kDeg2Rad)));
  const Rgb low=clear.sunIrradiance(0,std::sin(4*float(kDeg2Rad)));
  check(noon.luminance()>8.5f && noon.luminance()<12.f,"clear high sun delivers about 100 klx at sea level");
  check(low.luminance()<noon.luminance()*.45f && low.r/low.b>noon.r/noon.b*2,"a low sun is dimmer and redder");
  check(clear.sunIrradiance(0,-.05f).luminance()==0,"no direct sun below the horizon");
  check(clear.sunIrradiance(10000,std::sin(4*float(kDeg2Rad))).luminance()>low.luminance()*1.5f,"sunlight is stronger above the haze layer");
  // Transmittance falls with path length and rises with altitude.
  float previous=2;
  for(float cosine:{1.f,.7f,.4f,.15f,.03f}) {
    const float t=clear.transmittanceToSpace(0,cosine).g;
    check(t>0 && t<previous,"transmittance decreases toward the horizon");previous=t;
  }
  check(clear.transmittanceToSpace(8000,.2f).b>clear.transmittanceToSpace(0,.2f).b,"thinner air above transmits more");
  check(clear.transmittanceToSpace(0,-.2f).luminance()==0,"the planet blocks rays below the horizon");
  // Sky: blue overhead, brighter and paler at the horizon, a few thousand cd/m2.
  const auto sun=sunAt(45);
  const Rgb zenith=clear.skyRadiance(2,{0,1,0},sun),horizon=clear.skyRadiance(2,{-.9998f,.02f,0},sun);
  check(zenith.b>zenith.r*2 && zenith.luminance()>.1f && zenith.luminance()<.6f,"zenith is blue at a few thousand cd/m2");
  check(horizon.luminance()>zenith.luminance()*1.5f && horizon.b/horizon.r<zenith.b/zenith.r,"the horizon is brighter and paler than the zenith");
  check(clear.skyRadiance(12000,{0,1,0},sun).luminance()<zenith.luminance()*.5f,"the sky darkens with altitude");
  const AtmosphereLighting lighting=clear.lighting(2,sun);
  check(lighting.skyIrradianceUp.luminance()>.8f && lighting.skyIrradianceUp.luminance()<2.5f,"clear sky supplies roughly 8-25 klx of diffuse light");
  check(lighting.skyIrradianceUp.b>lighting.skyIrradianceUp.r,"skylight is blue");
  // The L1 ambient reproduces the hemisphere integral: more light from above than below.
  const float up=lighting.ambientConstant.g+lighting.ambientY.g,down=lighting.ambientConstant.g-lighting.ambientY.g;
  check(up>down && down>0 && std::abs(up-lighting.skyIrradianceUp.g)<lighting.skyIrradianceUp.g*.35f,"ambient irradiance matches the sky above and a dimmer ground below");
  // Fog and haze only ever remove direct light.
  const AtmosphereModel foggy(AtmosphereParameters::fromWeather(70,.6f,150));
  check(foggy.sunIrradiance(0,sun[1]).luminance()<clear.sunIrradiance(0,sun[1]).luminance()*.6f,"ground fog attenuates the sun at the surface");
  check(std::abs(foggy.sunIrradiance(3000,sun[1]).luminance()-clear.sunIrradiance(3000,sun[1]).luminance())<.05f,"fog is confined to its shallow layer");
  for(const auto* table:{&clear.transmittanceTable(),&clear.multiScatterTable()}) for(float value:*table)
    check(std::isfinite(value) && value>=0,"atmosphere tables are finite and non-negative");
  // Exposure: brighter scenes meter down, dusk meters up, both within limits.
  const float day=exposureFromIrradiance(lighting.meteredIrradiance,0);
  const float dusk=exposureFromIrradiance(clear.lighting(2,sunAt(-3)).meteredIrradiance,0);
  check(day>.2f && day<.7f && dusk>day*8 && dusk<=36.f,"exposure adapts from daylight to dusk within its limits");
  check(std::abs(exposureFromIrradiance(lighting.meteredIrradiance,1)/day-2)<1e-4f,"one stop of compensation doubles the exposure");
  std::printf("PASS atmosphere: sun %.0f klx, sky %.0f klx, zenith %.0f cd/m2, exposure day/dusk %.2f/%.1f\n",
    noon.luminance()*10,lighting.skyIrradianceUp.luminance()*10,zenith.luminance()*10000,day,dusk);
}

void proceduralTextures() {
  using namespace procedural;
  // Every primitive repeats exactly at its period, or tiles would show seams.
  for(float offset:{.13f,.5f,.87f}) {
    check(std::abs(perlin(offset,2.3f,1.7f,8,5)-perlin(offset+8,2.3f,1.7f,8,5))<1e-4f,"gradient noise repeats at its period");
    check(std::abs(perlinFbm(1.1f,offset,3.3f,4,4,9)-perlinFbm(1.1f,offset+4,3.3f,4,4,9))<1e-4f,"fractal noise repeats at its period");
    const Cell a=worley(offset,1.2f,2.4f,6,3),b=worley(offset+6,1.2f,2.4f,6,3);
    check(std::abs(a.f1-b.f1)<1e-4f && a.id==b.id && a.f1<=a.f2,"cellular noise repeats at its period");
  }
  const auto shape=cloudShapeVolume(32),again=cloudShapeVolume(32);
  check(shape==again,"cloud volumes are deterministic");
  check(*std::min_element(shape.begin(),shape.end())==0 && *std::max_element(shape.begin(),shape.end())==255,"cloud shape uses the full density range");
  check(cloudDetailVolume(16).size()==16u*16*16,"detail volume has the requested size");
  // Coverage is equalised: a threshold selects that fraction of the sky.
  const auto weather=weatherMap(128);
  for(int threshold:{64,128,191}) {
    std::size_t above=0;
    for(std::size_t i=0;i<weather.size();i+=4) above+=weather[i]>=threshold;
    check(std::abs(double(above)/(weather.size()/4)-(1-threshold/255.))<.02,"weather-map coverage is uniform in rank");
  }
  const auto layers=terrainLayers(64);
  check(layers.size==64 && layers.albedoHeight.size()==kTerrainLayerCount && layers.normalRoughness.size()==kTerrainLayerCount,"one albedo and one detail image per terrain layer");
  const auto mean=[&](int layer,int channel){double sum=0;const auto& d=layers.albedoHeight[layer];for(std::size_t i=channel;i<d.size();i+=4)sum+=d[i];return sum/(d.size()/4);};
  check(mean(kLayerGrass,1)>mean(kLayerGrass,0) && mean(kLayerGrass,1)>mean(kLayerGrass,2),"grass is green");
  check(mean(kLayerSnow,1)>200 && mean(kLayerAsphalt,1)<90 && mean(kLayerForest,1)<mean(kLayerGrass,1),"snow is bright, asphalt dark and forest darker than grass");
  for(int layer=0;layer<kTerrainLayerCount;++layer) for(std::size_t i=0;i<layers.normalRoughness[layer].size();i+=4) {
    // The two stored components of a unit normal that points out of the surface.
    const double x=layers.normalRoughness[layer][i]/127.5-1,y=layers.normalRoughness[layer][i+1]/127.5-1;
    check(x*x+y*y<1.02,"terrain detail normals are unit vectors in the upper hemisphere");
  }
  const auto water=waterNormalTile(64);
  check(water.size()==64u*64*4 && noiseTile(64).size()==64u*64*4,"water and noise tiles have the requested size");
  std::puts("PASS procedural: tiling noise, deterministic volumes, equalised weather and plausible material layers");
}

void landscape() {
  const Landscape land(256,256);
  check(land.landTexels().size()==256u*256*4 && land.lakeTexels().size()==256u*256,"land-cover maps have the requested size");
  // Lakes: only in closed basins, never on the airfield, always above their bed.
  check(land.lakeCount()>5 && land.lakeAreaKm2()>5 && land.lakeAreaKm2()<400,"a plausible number and area of lakes");
  check(land.lakeSurface(0,0)==Landscape::kNoLake && !land.underWater(0,0) && !land.underWater(1200,0),"the airfield drains and stays dry");
  unsigned wet=0;
  for(double north=-40000;north<=40000;north+=400) for(double east=-40000;east<=40000;east+=400) {
    const float level=land.lakeSurface(north,east);
    if(land.underWater(north,east)) {++wet;check(terrainElevation(north,east)<level && level-terrainElevation(north,east)<60,"water stands above its bed, at a bounded depth");}
    check(land.forestDensity(north,east)>=0 && land.forestDensity(north,east)<=1,"forest density is a fraction");
  }
  check(wet>10,"lakes are found by sampling");
  check(land.forestDensity(0,0)==0 && land.farmland(0,0)==0,"nothing grows or is farmed on the runway");
  check(insideAirfieldClearway(0,0) && insideAirfieldClearway(300,-200) && !insideAirfieldClearway(0,3000),"the paved footprint is kept clear");
  // Navigation map: the picture shows the lakes, and frames place ground points on it.
  {
    const MapImage map=buildMapImage(land,128);
    check(map.size==128 && map.rgba.size()==128u*128*4,"the map picture has the requested size");
    unsigned water=0;
    for(int row=0;row<128;++row) for(int column=0;column<128;++column) {
      const double cell=2.*Landscape::kExtent/128,north=Landscape::kExtent-(row+.5)*cell,east=(column+.5)*cell-Landscape::kExtent;
      const std::uint8_t* texel=&map.rgba[(std::size_t(row)*128+column)*4];
      check(texel[3]==255,"the map picture is opaque");
      if(land.underWater(north,east)) {++water;check(texel[2]>texel[0]+40,"lakes are painted blue");}
      else check(texel[2]<=texel[0]+25,"dry land is not painted as water");
    }
    check(water>0,"the map picture shows lakes");
    const MapFrame frame{100,50,200,1000,-2000,5000};
    float x,y;
    check(frame.project(1000,-2000,x,y) && std::abs(x-200)<1e-3f && std::abs(y-150)<1e-3f,"the frame centre is the ground centre");
    check(frame.project(6000,3000,x,y) && std::abs(x-300)<1e-3f && std::abs(y-50)<1e-3f,"north is up and east is right");
    check(!frame.project(1000,3100,x,y) && x>300,"points beyond the edge are reported outside");
    float l,t,r,b,u0,v0,u1,v1;
    check(frame.picture(l,t,r,b,u0,v0,u1,v1) && l==100 && t==50 && std::abs(r-300)<1e-3f && std::abs(b-250)<1e-3f,"a frame inside the picture is filled");
    check(std::abs((u0+u1)*.5f-(-2000+Landscape::kExtent)/(2*Landscape::kExtent))<1e-5f &&
          std::abs((v0+v1)*.5f-(Landscape::kExtent-1000)/(2*Landscape::kExtent))<1e-5f,"texture coordinates follow the ground");
    const MapFrame edge{0,0,100,0,Landscape::kExtent,10000};
    check(edge.picture(l,t,r,b,u0,v0,u1,v1) && std::abs(r-50)<1e-3f && u1==1,"a frame over the edge shows only the picture's part");
    check(!MapFrame{0,0,100,0,Landscape::kExtent*2,10000}.picture(l,t,r,b,u0,v0,u1,v1),"a frame beyond the picture shows none");
    check(minimapHalfSpan(0)==6000 && minimapHalfSpan(300)>minimapHalfSpan(100) && minimapHalfSpan(5000)==30000,"the minimap opens out with speed");
  }
  // Trees: deterministic, on the ground, out of the water and off the pavement.
  std::size_t total=0,conifers=0;
  for(int cz=-9;cz<9;++cz) for(int cx=-9;cx<9;++cx) {
    std::vector<TreeInstance> trees,repeat;
    land.treesInChunk(cx,cz,400,trees);land.treesInChunk(cx,cz,400,repeat);
    check(trees.size()==repeat.size() && (trees.empty() || !std::memcmp(trees.data(),repeat.data(),trees.size()*sizeof(TreeInstance))),"tree placement is deterministic");
    for(const TreeInstance& tree:trees) {
      const double north=-tree.south,east=tree.east;
      check(east>=cx*1000. && east<(cx+1)*1000. && tree.south>=cz*1000. && tree.south<(cz+1)*1000.,"trees stay inside their chunk");
      check(std::abs(tree.up+groundHeightNed(north,east))<.01,"trees stand on the collision surface");
      check(!insideAirfieldClearway(north,east) && !land.underWater(north,east) && tree.up<1750,"no trees on pavement, in lakes or above the treeline");
      check(tree.height>4 && tree.height<28 && tree.spread>.7f && tree.spread<1.3f,"tree dimensions are in metres and bounded");
      conifers+=tree.conifer>.5f;
    }
    total+=trees.size();
  }
  check(total>5000 && conifers>total/20 && conifers<total,"a mixed forest of both species");
  std::printf("PASS landscape: %d lakes over %.0f km2, %zu trees in 324 km2 (%zu conifers)\n",land.lakeCount(),land.lakeAreaKm2(),total,conifers);
}
int main(int argc,char** argv) {
  try {
    check(argc>=2,"expected suite"); const std::string suite=argv[1];
    if(suite=="scenery") scenery();
    else if(suite=="atmosphere") atmosphere();
    else if(suite=="procedural") proceduralTextures();
    else if(suite=="landscape") landscape();
    else if(suite=="materials") materials();
    else if(suite=="definitions") definitions();
    else if(suite=="animation") animations();
    else if(suite=="particles") particles();
    else if(suite=="hierarchy") {check(argc==3,"expected asset root"); hierarchy(argv[2]);}
    else if(suite=="mixed") mixed();
    else throw std::invalid_argument("unknown suite");
    return scenario::assetResult();
  } catch(const std::exception& e) {std::fprintf(stderr,"VISUAL FAIL: %s\n",e.what()); return 1;}
}
