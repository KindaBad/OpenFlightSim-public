#include "animation.hpp"
#include "effects.hpp"
#include "gltf.hpp"
#include "mesh.hpp"
#include "scenery.hpp"
#include "texture_mips.hpp"
#include "ofs/aircraft_definition.hpp"
#include "ofs/net/client.hpp"
#include "ofs/net/world.hpp"
#include "ofs/net/server.hpp"
#include "scenario.hpp"
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
  for(bool pine:{false,true}) {
    std::vector<SurfaceVertex> leaves,distant,wood;
    appendTree(leaves,distant,wood,{400,12,700},15,4,pine,31);
    check(!leaves.empty() && !wood.empty() && distant.size()<leaves.size(),"both species retain a cheaper distant silhouette");
    check(leaves.size()/3<350 && distant.size()/3<100,"bounded per-tree geometry budget");
    for(const auto* mesh:{&leaves,&distant,&wood}) for(std::size_t i=0;i<mesh->size();i+=3) {
      const auto& a=(*mesh)[i];const auto& b=(*mesh)[i+1];const auto& c=(*mesh)[i+2];
      const Vec3 p{a.x,a.y,a.z},q{b.x,b.y,b.z},r{c.x,c.y,c.z};
      const Vec3 n{a.nx+b.nx+c.nx,a.ny+b.ny+c.ny,a.nz+b.nz+c.nz};
      check(std::isfinite(p.norm2()) && std::isfinite(n.norm2()),"finite vegetation vertices/normals");
      check((q-p).cross(r-p).dot(n)>0,"foliage and branches face outward without degenerate triangles");
      check(a.y>=11.99 && a.y<29,"tree remains rooted with a bounded crown height");
    }
    std::vector<SurfaceVertex> repeat,farRepeat,woodRepeat;
    appendTree(repeat,farRepeat,woodRepeat,{400,12,700},15,4,pine,31);
    check(repeat.size()==leaves.size() && !std::memcmp(repeat.data(),leaves.data(),leaves.size()*sizeof(SurfaceVertex)),"deterministic vegetation generation");
  }
  check(std::abs(sceneryNoise(1.25,2.75)-sceneryNoise(4.25,7.75))>.01,"groves vary across the landscape");
  check(std::abs(sceneryNoise(1.99999,2.5)-sceneryNoise(2.00001,2.5))<.0001,"grove density continuous across patch boundaries");
  std::puts("PASS scenery: outward geometry, roots/crowns, deterministic species and bounded LODs");
}
int main(int argc,char** argv) {
  try {
    check(argc>=2,"expected suite"); const std::string suite=argv[1];
    if(suite=="scenery") scenery();
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
