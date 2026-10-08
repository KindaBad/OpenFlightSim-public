#include "scenario.hpp"
#include "ofs/terrain.hpp"
#include "ofs/aircraft_definition.hpp"
#include "effects.hpp"
#include "airfield.hpp"
#include "landscape.hpp"
#include "ofs/ground_service.hpp"
#ifdef OFS_ENV_NETWORK
#include "ofs/net/world.hpp"
#endif
#include <cmath>
#include <cstdio>
#include <limits>
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
// The airfield is laid out by one table; what is built from it has to leave
// the places aircraft use clear, and stay on the level ground under it.
void airfield() {
  using namespace ofs::client;
  const Airfield field=buildAirfield();
  check(!field.ground.runway.empty() && !field.ground.taxiways.empty() && !field.ground.concrete.empty() &&
        !field.ground.roads.empty() && !field.ground.whitePaint.empty() && !field.ground.yellowPaint.empty(),
        "every kind of paving and paint is laid");
  std::size_t vertices=0;
  for(const auto& part:field.parts) {
    check(!part.vertices.empty() && part.vertices.size()%3==0,"every material is used, in whole triangles");
    vertices+=part.vertices.size();
    for(const auto& v:part.vertices) {
      check(std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z),"structure vertices are finite");
      check(std::abs(v.nx*v.nx+v.ny*v.ny+v.nz*v.nz-1)<1e-3f,"structure normals are unit length");
      check(v.y>=-.01f && v.y<60,"structures stand on the ground and stay below 60 m");
      check(std::hypot(v.x,v.z)<3200,"structures stay on the level ground");
      check(v.x>kFenceWest-1 && v.x<kFenceEast+1 && v.z>kFenceNorth-1 && v.z<kFenceSouth+1,"structures stay inside the fence");
    }
  }
  check(vertices>20000 && vertices<400000,"the airfield is detailed but bounded");
  for(const auto* ground:{&field.ground.runway,&field.ground.taxiways,&field.ground.concrete,&field.ground.roads,
                          &field.ground.whitePaint,&field.ground.yellowPaint})
    for(const auto& v:*ground) {
      check(v.y>0 && v.y<.2f && v.ny>.99f,"paving and paint lie flat on the ground");
      check(std::hypot(v.x,v.z)<3250,"paving stays on the level ground");
      check(groundHeightNed(-v.z,v.x)==0,"the ground under paving is level");
    }
  // Nothing solid stands where an aircraft rolls, with a wingspan to spare.
  check(field.buildings.size()>=20 && field.movement.size()>=8,"buildings and movement areas are recorded");
  for(const auto& b:field.buildings) {
    check(b.x1>b.x0 && b.z1>b.z0,"a building has a footprint");
    check(airfieldUse(-(b.z0+b.z1)*.5,(b.x0+b.x1)*.5)!=AirfieldUse::Outside,"buildings are inside the fence");
    for(const auto& m:field.movement) {
      const float margin=std::string(m.name)=="runway"?60.f:std::string(m.name)=="apron"?0.f:8.f;
      const bool apart=b.x1<m.x0-margin || b.x0>m.x1+margin || b.z1<m.z0-margin || b.z0>m.z1+margin;
      check(apart,(std::string(b.name)+" stands clear of the "+m.name).c_str());
    }
    for(const auto& other:field.buildings)
      check(&other==&b || b.x1<=other.x0 || b.x0>=other.x1 || b.z1<=other.z0 || b.z0>=other.z1,"buildings do not overlap");
  }
  // What the ground is used for: the runway where a flight starts, grass beside
  // it, and open country beyond the fence.
  check(airfieldUse(0,0)==AirfieldUse::Paved && airfieldUse(1290,20)==AirfieldUse::Paved &&
        airfieldUse(-1290,-20)==AirfieldUse::Paved && airfieldUse(0,-105)==AirfieldUse::Paved,
        "runway and taxiway are paved");
  check(airfieldUse(0,60)==AirfieldUse::Grass && airfieldUse(1500,-300)==AirfieldUse::Grass,"grass inside the fence");
  check(airfieldUse(0,900)==AirfieldUse::Outside && airfieldUse(2500,-100)==AirfieldUse::Outside &&
        airfieldUse(0,462)==AirfieldUse::Paved,"open country beyond the fence, with a road through it");
  check(insideAirfieldClearway(0,60) && insideAirfieldClearway(1850,0) && insideAirfieldClearway(500,-600) &&
        !insideAirfieldClearway(0,900) && !insideAirfieldClearway(2500,600),"nothing grows on the airfield or under the approaches");
  // An aircraft that starts on the runway, on a stand or in a shelter's mouth stands on paving.
  for(const auto& [north,east]:{std::pair{0.,0.},{330.,-318.},{-700.,-436.},{-1270.,0.}})
    check(airfieldUse(north,east)==AirfieldUse::Paved,"parking places are paved");
  std::printf("airfield: %zu structure vertices, %zu buildings, %zu movement areas PASS\n",vertices,field.buildings.size(),field.movement.size());
}
// Standing on the ground long enough repairs and refuels an aircraft.
void service() {
  const auto& definition=aircraftDefinition(AircraftType::Typhoon);
  const auto& cfg=definition.flight;
  State parked;parked.pos_ned.z=-(cfg.gear_nose.z-.15);
  check(standingOnGround(cfg,parked) && !needsRepair(cfg,parked),"a sound parked aircraft stands and needs nothing");
  auto rolling=parked;rolling.vel_ned={serviceMaxSpeed+.5,0,0};
  check(!standingOnGround(cfg,rolling),"a rolling aircraft is not standing");
  rolling.vel_ned={serviceMaxSpeed-.5,0,0};
  check(standingOnGround(cfg,rolling),"a crawl counts as standing");
  auto hovering=parked;hovering.pos_ned.z=-40;
  check(!standingOnGround(cfg,hovering),"an aircraft in the air is not standing");
  auto hillside=parked;hillside.pos_ned={6000,2000,groundHeightNed(6000,2000)-2};
  check(standingOnGround(cfg,hillside),"height is measured from the ground it stands on");
  auto wreck=parked;wreck.surface_health.fill(0);
  check(!standingOnGround(cfg,wreck),"a wreck is not served");
  auto lost=parked;lost.pos_ned.x=std::numeric_limits<double>::quiet_NaN();
  check(!standingOnGround(cfg,lost),"a nonfinite state is not standing");
  // Every kind of wear is noticed, and all of it is put right.
  auto worn=parked;
  applyPartDamage(cfg,worn,DamagePart::LeftWing,60);
  applyPartDamage(cfg,worn,DamagePart::RightEngine,100);
  applyPartDamage(cfg,worn,DamagePart::Tail,40);
  applyPartDamage(cfg,worn,DamagePart::Fuselage,30);
  worn.surface_health[5]=.7;worn.fuel_mass=cfg.initial_fuel*.2;
  check(needsRepair(cfg,worn),"damage is noticed");
  for(const auto part:{DamagePart::LeftWing,DamagePart::RightEngine,DamagePart::Tail}) {
    auto one=parked;applyPartDamage(cfg,one,part,10);check(needsRepair(cfg,one),"each damaged part asks for repair");
  }
  auto dragging=parked;applyPartDamage(cfg,dragging,DamagePart::Fuselage,10);
  check(needsRepair(cfg,dragging),"a holed fuselage asks for repair");
  auto thirsty=parked;thirsty.fuel_mass=cfg.initial_fuel*.9;
  check(needsRepair(cfg,thirsty),"used fuel is noticed");
  thirsty.fuel_mass=cfg.initial_fuel*.99;
  check(!needsRepair(cfg,thirsty),"a full tank is left alone");
  const auto before=worn;
  repairAndRefuel(cfg,worn);
  check(!needsRepair(cfg,worn) && airframeIntegrity(worn)==1 && worn.engine_health[1]==1 && worn.fuel_mass==cfg.initial_fuel,
        "repair restores every part and fills the tanks");
  check(worn.pos_ned.z==before.pos_ned.z && worn.vel_ned.norm()==before.vel_ned.norm() && worn.time==before.time,
        "repair leaves the aircraft where it is");
  // The repaired aircraft flies like a new one.
  Simulator fresh(cfg),mended(cfg);
  auto airborne=parked;airborne.pos_ned.z=-3000;airborne.vel_ned={220,0,0};
  fresh.setState(airborne);
  auto damaged=airborne;applyPartDamage(cfg,damaged,DamagePart::LeftWing,60);repairAndRefuel(cfg,damaged);
  mended.setState(damaged);
  for(int i=0;i<240;++i){fresh.step(1./120);mended.step(1./120);}
  check((fresh.state().pos_ned-mended.state().pos_ned).norm()<1e-9,"a repaired aircraft flies as a new one does");
  std::puts("ground service: standing, wear, repair PASS");
}
#ifdef OFS_ENV_NETWORK
// A shared game does the same on the server: ten seconds standing, and the
// aircraft is whole, fuelled and armed, and everyone is told.
void turnround() {
  using namespace ofs::net;
  World world(false);
  const auto id=world.join(AircraftType::Typhoon);
  auto& player=const_cast<Player&>(world.players().at(id));
  const auto stock=std::uint8_t(weapons::decoyCapacity(AircraftType::Typhoon));
  const auto fullAmmo=player.life.ammo;
  check(stock>0 && player.weapons.flares==stock && player.weapons.chaff==stock,"an armed aircraft spawns with full dispensers");
  const auto serviced=[&](unsigned ticks) {
    unsigned events=0;
    for(unsigned i=0;i<ticks;++i) {
      world.step();
      for(const auto& event:world.combat().takeEvents()) if(event.kind==CombatKind::Serviced) {
        ++events;
        check(event.owner==id && event.target==id && event.health==100,"the turn-round names the aircraft");
      }
    }
    return events;
  };
  // Parked and whole: nothing to do, however long it stands.
  check(serviced(unsigned(World::serviceTicks())+240)==0 && player.standing>=World::serviceTicks(),
        "a sound aircraft is counted as standing but not served");
  // Shot up, out of ammunition and stores: served at once, having stood already.
  const auto wear=[&] {
    auto state=player.sim.state();
    applyPartDamage(player.sim.config(),state,DamagePart::RightWing,70);
    applyPartDamage(player.sim.config(),state,DamagePart::LeftEngine,100);
    state.fuel_mass*=.3;
    player.sim.setState(state);
    player.life.health=35;player.life.ammo=7;
    player.weapons.inventory.consume(0,WeaponType::Infrared);
    player.weapons.inventory.consume(2,WeaponType::ActiveRadar);
    player.weapons.inventory.selected=WeaponType::ActiveRadar;
    player.weapons.flares=2;player.weapons.chaff=0;
  };
  wear();
  check(serviced(2)==1,"an aircraft that has stood is served as soon as it needs it");
  const auto whole=[&] {
    const auto& state=player.sim.state();
    return player.life.health==100 && player.life.ammo==fullAmmo && player.weapons.flares==stock && player.weapons.chaff==stock &&
        player.weapons.inventory.remaining(WeaponType::Infrared)==2 && player.weapons.inventory.remaining(WeaponType::ActiveRadar)==2 &&
        !needsRepair(player.sim.config(),state) && state.engine_health[0]==1;
  };
  check(whole(),"served: whole, fuelled and armed");
  check(player.weapons.inventory.selected==WeaponType::ActiveRadar && player.standing<3,
        "the pilot's weapon selection is kept, and the wait starts again");
  check(player.sim.state().payload_mass>300,"the missiles' weight is back on the wings");
  // The wait starts again, and is the full ten seconds.
  wear();
  check(serviced(unsigned(World::serviceTicks())-2)==0 && !whole(),"the wait is ten seconds");
  check(serviced(4)==1 && whole(),"and then the work is done");
  // Moving off resets the wait.
  wear();
  serviced(600);
  auto state=player.sim.state();state.vel_ned={8,0,0};player.sim.setState(state);
  world.step();
  check(player.standing==0,"rolling away abandons the wait");
  // Nobody in the air is served.
  World sky;
  const auto flyer=sky.join(AircraftType::Typhoon);
  auto& airborne=const_cast<Player&>(sky.players().at(flyer));
  airborne.life.ammo=1;
  for(unsigned i=0;i<1500;++i) {
    sky.step();
    for(const auto& event:sky.combat().takeEvents()) check(event.kind!=CombatKind::Serviced,"an aircraft in flight is never served");
  }
  check(airborne.life.ammo==1 && airborne.standing==0,"flying is not standing");
  std::puts("turn-round: standing ten seconds, repaired, refuelled, rearmed, announced PASS");
}
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
    else if(name=="airfield")airfield();else if(name=="service")service();
#ifdef OFS_ENV_NETWORK
    else if(name=="multiplayer")multiplayer();else if(name=="turnround")turnround();
#endif
    else throw std::invalid_argument("suite");
    return 0;
  } catch(const std::exception& e) {std::fprintf(stderr,"ENVIRONMENT FAIL: %s\n",e.what());return 1;}
}
