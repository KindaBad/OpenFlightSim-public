#include "local_gun.hpp"
#include "effects.hpp"
#include <cstdio>
#include <stdexcept>
using namespace ofs;
using namespace ofs::client;
void check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
  try {
    State state; state.pos_ned={0,0,-1000}; state.vel_ned={140,30,0};
    LocalGun gun(AircraftType::Su57);
    const auto& definition=aircraftDefinition(AircraftType::Su57);
    const auto initial=gun.ammo();
    for (int i=0;i<120;++i) gun.step(state,1./120,true);
    auto events=gun.takeEvents();
    const auto shots=unsigned(std::ceil(120./std::ceil(7200/definition.gun->rpm)));
    check(events.size()==shots && gun.ammo()==initial-shots,"one second obeys configured tick cooldown and ammo");
    check((events.front().position-(state.pos_ned+state.att.rotate(definition.gun->muzzle-loadedCg(definition.flight,state)))).norm()<1e-9,"muzzle follows loaded CG");
    check(std::abs((events.front().velocity-state.vel_ned).norm()-definition.gun->muzzleVelocity)<1e-6,"shot inherits aircraft velocity");
    for (int i=0;i<10000;++i) { gun.step(state,1./120,true); gun.takeEvents(); }
    check(gun.ammo()==0 && !gun.ready() && gun.activeRounds()==0,"empty magazine stops firing and rounds expire");
    gun.reset(); check(gun.ammo()==initial && gun.ready() && gun.activeRounds()==0,"reset reloads and clears rounds");
    gun.step(state,0,true); check(gun.takeEvents().empty() && gun.ammo()==initial,"paused gun does not fire");
    LocalGun civil(AircraftType::A320); civil.step(state,1./120,true);
    check(civil.ammo()==0 && civil.takeEvents().empty(),"unarmed aircraft cannot fire");
    state.pos_ned={0,0,-30}; state.vel_ned={}; state.att=quatFromEuler(0,-30*kDeg2Rad,0);
    gun.step(state,1./120,true); check(gun.takeEvents().size()==1,"downward shot emitted");
    bool hit=false;
    for (int i=0;i<120;++i) { gun.step(state,1./120,false); for(const auto& event:gun.takeEvents()) if(!event.shot) {
      check(std::abs(event.position.z-groundHeightNed(event.position.x,event.position.y))<.001,"terrain impact position"); hit=true;
    } }
    check(hit && gun.activeRounds()==0,"terrain consumes solo projectile");
    check(std::abs(bulletTerrainFraction({0,0,-5},{10,0,5})-.5)<.00001,"flat ground sweep");
    check(!std::isfinite(bulletTerrainFraction({0,0,-1000},{10,0,-990})),"airborne sweep misses terrain");
    EffectPool pool(128); CombatEffects effects(pool,EffectsQuality::High);
    effects.onShot({0,0,-10},{100,0,150},3,true,42);
    pool.update(.1);
    check(pool.countOf(EffectKind::Tracer)==0 && pool.countOf(EffectKind::Impact)>0,"visual tracer terminates at terrain");
    pool.clear(); effects.onShot({0,0,-1000},{850,0,0},3,true,43);
    effects.onHit({20,0,-1000},false,43);
    check(pool.countOf(EffectKind::Tracer)==0 && pool.countOf(EffectKind::Spark)==12,"confirmed hits retire tracer and emit spark spray");
    std::puts("PASS solo guns: rate/ammo, muzzle/velocity, reset/pause, unarmed, terrain, tracer retirement");
  } catch(const std::exception& e) { std::fprintf(stderr,"GUN FAIL: %s\n",e.what()); return 1; }
}
