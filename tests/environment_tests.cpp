#include "scenario.hpp"
#include "ofs/terrain.hpp"
#include "ofs/aircraft_definition.hpp"
#include "effects.hpp"
#ifdef OFS_ENV_NETWORK
#include "ofs/net/world.hpp"
#endif
#include <cstdio>
#include <string>
using namespace ofs;
using scenario::check;

void surface() {
  for (double north:{-2300.,0.,2300.}) for(double east:{-600.,0.,600.})
    check(groundHeightNed(north,east)==0,"runway and approaches remain level");
  double maximumError=0;
  for(int ring=32;ring<kTerrainRings;ring+=3) for(int sector=0;sector<kTerrainSegments;sector+=7) {
    const auto a=terrainVertex(ring,sector),b=terrainVertex(ring,sector+1);
    const auto c=terrainVertex(ring+1,sector+1),d=terrainVertex(ring+1,sector);
    for(const auto& triangle:{std::array<Vec3,3>{a,b,c},std::array<Vec3,3>{a,c,d}}) {
      for(const auto weights:{Vec3{.17,.31,.52},Vec3{.8,.1,.1},Vec3{.01,.98,.01}}) {
        const auto p=triangle[0]*weights.x+triangle[1]*weights.y+triangle[2]*weights.z;
        const auto sample=sampleTerrain(p.x,p.y);
        maximumError=std::max(maximumError,std::abs(sample.heightNed-p.z));
        check(std::abs(sample.heightNed-p.z)<1e-7,"contact height matches rendered triangle interior");
        check(std::abs(sample.normalNed.norm()-1)<1e-10 && sample.normalNed.z<0,"outward unit contact normal");
        check(std::abs(sample.normalNed.dot(triangle[1]-triangle[0]))<1e-7,"normal perpendicular to actual face");
      }
    }
  }
  for(int ring=32;ring<96;ring+=3) {
    const auto p=terrainVertex(ring,0);
    check(std::abs(groundHeightNed(p.x-1e-5,p.y)-groundHeightNed(p.x+1e-5,p.y))<1e-4,"azimuth seam continuous");
  }
  check(groundHeightNed(6000,2000)<-10,"hills have solid elevation");
  std::printf("Terrain triangles: max interpolation error %.9g m\n",maximumError);
}

Simulator impactSimulator(AircraftType type,double closing,bool gear,bool hill=false) {
  auto cfg=aircraftDefinition(type).flight;
  cfg.wing_area=0;cfg.engines_configured=false;cfg.thrust_sl_static_each=0;
  cfg.fuel_flow_scale=0;cfg.control_law=FlightControlLaw::Direct;
  Simulator sim(cfg);State state;
  const double north=hill?6000:0,east=hill?2000:0;
  double bottom=0;
  if(gear) bottom=std::max({cfg.gear_nose.z,cfg.gear_main_l.z,cfg.gear_main_r.z});
  else for(auto p:cfg.belly_contacts)bottom=std::max(bottom,p.z);
  state.pos_ned={north,east,groundHeightNed(north,east)-bottom-.05};
  const auto normal=sampleTerrain(north,east).normalNed;
  state.vel_ned=-normal*closing;state.fcs_enabled=false;
  sim.setState(state);Controls controls;controls.gear01=gear?1:0;
  sim.setControls(controls);sim.primeActuators();return sim;
}

void crashes() {
  for(const auto& definition:aircraftDefinitions()) {
    auto soft=impactSimulator(definition.type,2,true);
    for(int i=0;i<240;++i)soft.step(1./240);
    check(airframeIntegrity(soft.state())>.999,"normal touchdown does not damage airframe");
    auto instant=impactSimulator(definition.type,35,false,true);
    for(int i=0;i<4;++i)instant.step(1./240);
    check(aircraftCrashed(instant.state()),"violent strike destroys aircraft within first contact");
    auto hard=impactSimulator(definition.type,35,false,true);
    const State start=hard.state();
    Simulator replay(hard.config());replay.setState(start);replay.setControls(hard.controls());
    bool reported=false;double maximumRate=0;
    for(int i=0;i<480;++i) {
      hard.step(1./240);replay.step(1./240);
      reported|=hard.groundImpact().damage>0;
      maximumRate=std::max(maximumRate,hard.state().omega_body.norm());
      check(scenario::finite(hard.state()),"hard sloped impact remains finite");
      check((hard.state().pos_ned-replay.state().pos_ned).norm()<1e-9 &&
            hard.state().surface_health==replay.state().surface_health,"collision damage deterministic under replay");
    }
    std::printf("Crash %s integrity %.4f max rate %.3f speed %.3f\n",definition.key.data(),airframeIntegrity(hard.state()),maximumRate,hard.state().vel_ned.norm());
    check(reported && aircraftCrashed(hard.state()),"severe ground strike destroys structure");
    check(hard.state().engine_health[0]==0 && hard.state().afterburner[0]==0,"wreck engines and reheat stop");
    check(maximumRate<100 && hard.instruments().agl>-5,"wreck cannot tunnel through hill or launch unbounded rotation");
    auto inverted=impactSimulator(definition.type,0,false);
    auto flipped=inverted.state();flipped.pos_ned.z=-5;flipped.att=quatFromEuler(kPi,0,0);
    inverted.setState(flipped);
    for(int i=0;i<720;++i) {inverted.step(1./240);check(scenario::finite(inverted.state()),"inverted impact remains finite");}
    check(inverted.instruments().agl>.1,"upper fuselage supports an inverted wreck above terrain");
  }
}

void effects() {
  using namespace ofs::client;
  EffectPool pool(192);CombatEffects fx(pool,EffectsQuality::High);
  Simulator::GroundImpact impact;impact.position={6000,2000,groundHeightNed(6000,2000)};
  impact.normal=sampleTerrain(6000,2000).normalNed;impact.velocity={80,0,25};
  impact.closingSpeed=25;impact.damage=.6;impact.bodyContact=true;
  fx.onGroundImpact(impact);
  check(pool.countOf(EffectKind::Dust)>0 && pool.countOf(EffectKind::Spark)>0,"terrain impact makes dust and sparks");
  auto wreck=impactSimulator(AircraftType::Typhoon,35,false);
  for(int i=0;i<300;++i)wreck.step(1./240);
  unsigned explosions=0;
  for(int i=0;i<600;++i) {
    fx.updateAircraft(wreck.state(),1./60,AircraftType::Typhoon,0);
    if(i==0) explosions=unsigned(pool.countOf(EffectKind::Explosion));
    if(i>120) check(pool.countOf(EffectKind::Explosion)==0,"crash fireball triggers once per life");
    pool.update(1./60);
    check(pool.size()<=pool.capacity(),"sustained wreck effects bounded");
    for(const auto& e:pool.effects()) if(e.kind==EffectKind::Debris || e.kind==EffectKind::Spark)
      check(e.position.z<=groundHeightNed(e.position.x,e.position.y)+.001,"debris collides with terrain");
  }
  check(explosions==1,"exactly one crash fireball");
  check(pool.countOf(EffectKind::Smoke)>0 && pool.countOf(EffectKind::Fire)>0,"wreck continues smoking and burning");
  pool.update(15);check(pool.size()==0,"crash particles expire after emission stops");
  fx.setQuality(EffectsQuality::Off);fx.onGroundImpact(impact);fx.onDestroyed({},{});
  check(pool.size()==0,"effects off has zero crash emissions");
}
#ifdef OFS_ENV_NETWORK
void multiplayer() {
  using namespace ofs::net;
  World world(false);const auto id=world.join(AircraftType::Typhoon);
  auto& player=const_cast<Player&>(world.players().at(id));
  auto entry=impactSimulator(AircraftType::Typhoon,35,false);
  player.sim.setState(entry.state());player.sim.setControls(entry.controls());
  bool destroyed=false;unsigned events=0;
  for(int i=0;i<120;++i) {
    world.step();for(const auto& event:world.combat().takeEvents()) {
      if(event.kind==CombatKind::Destroyed) {++events;destroyed=true;}
    }
  }
  check(destroyed && events==1 && !world.aircraft(id).life.alive(),"server emits one terrain destruction event");
  check(world.aircraft(id).life.deaths==1 && world.aircraft(id).life.kills==0,"terrain crash cannot award self kill");
  for(int i=0;i<600;++i)world.step();
  check(world.aircraft(id).life.alive() && world.aircraft(id).life.generation==1 &&
        airframeIntegrity(world.aircraft(id).state)==1,"server respawn restores structure");
}
#endif
int main(int argc,char** argv) {
  try {
    check(argc==2,"suite required");const std::string name=argv[1];
    if(name=="terrain")surface();else if(name=="crashes")crashes();else if(name=="effects")effects();
#ifdef OFS_ENV_NETWORK
    else if(name=="multiplayer")multiplayer();
#endif
    else throw std::invalid_argument("suite");
    return 0;
  } catch(const std::exception& e) {std::fprintf(stderr,"ENVIRONMENT FAIL: %s\n",e.what());return 1;}
}
