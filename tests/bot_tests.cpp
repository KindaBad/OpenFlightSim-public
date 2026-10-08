#include "ofs/net/bot_ai.hpp"
#include "ofs/net/client.hpp"
#include "ofs/net/server.hpp"
#include "ofs/trim.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

using namespace ofs;
using namespace ofs::net;
namespace {
void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
Player &fixture(World &world, EntityId id) {
  return const_cast<Player &>(world.players().at(id));
}
void guidance() {
  Simulator sim(typhoonConfig());
  const auto trim = solveTrim(sim.config(), {1000,160,0,0,0});
  check(trim.converged, "bot trim");
  sim.setState(trim.state); sim.setControls(trim.controls);
  const auto gun = *aircraftDefinition(AircraftType::Typhoon).gun;
  State target = sim.state();
  const auto muzzle = target.pos_ned + target.att.rotate(gun.muzzle - loadedCg(sim.config(),target));
  target.pos_ned = muzzle + target.att.rotate({400,0,0});
  auto decision = flyBot(sim, &target, gun, 1);
  check(decision.firing && validControls(decision.controls), "aligned bot fires a valid burst");
  check(!flyBot(sim, &target, gun, 100).firing, "gun pauses between bursts");
  check(!flyBot(sim, &target, gun, 1, true).firing, "evasive break pauses firing");
  target.pos_ned = sim.state().pos_ned + Vec3{700,500,0};
  decision = flyBot(sim, &target, gun, 1);
  check(decision.controls.aileron_stick > 0 && !decision.firing,
        "bot banks toward a lateral opponent without firing off-axis");
  target.pos_ned = sim.state().pos_ned - Vec3{400,0,0};
  check(!flyBot(sim, &target, gun, 1).firing, "cannot fire through its tail");
  auto low = sim.state(); low.pos_ned.z = -100; sim.setState(low);
  decision = flyBot(sim, &target, gun, 1);
  check(decision.controls.elevator_stick > 0 && !decision.firing, "terrain recovery has priority");
  check(!flyBot(sim, nullptr, gun, 1).firing, "patrol never fires without an opponent");
}
void dogfight() {
  World world;
  const auto human = world.join(dogfightAircraftType(AircraftType::A320));
  const auto bot = world.joinBot();
  check(human && bot && world.botCount() == 1 && !world.joinBot(AircraftType::A320),
        "bounded armed bot roster");
  check((world.aircraft(bot).state.pos_ned-world.aircraft(human).state.pos_ned).norm()>600,
        "opponents spawn with room to react");
  unsigned enemyShots = 0, humanHits = 0;
  double minAgl = 1e9;
  for (unsigned i=0;i<120*90;++i) {
    world.step();
    const auto &p = world.players().at(bot);
    if (p.life.alive()) {
      check(finiteState(p.sim.state()), "bot flight stays finite");
      minAgl = std::min(minAgl,p.sim.instruments().agl);
    }
    for (const auto &event : world.combat().takeEvents()) {
      enemyShots += event.kind==CombatKind::Shot && event.owner==bot;
      humanHits += event.kind==CombatKind::Hit && event.target==human && event.owner==bot;
    }
    if(i%600==0) std::printf("dogfight t=%.0f range=%.0f botAlt=%.0f bank=%.1f shots=%u hits=%u missileLaunches=%llu pitch=%.2f humanAlt=%.0f\n",
        i/120.,(p.sim.state().pos_ned-world.players().at(human).sim.state().pos_ned).norm(),
        p.sim.instruments().alt_msl,p.sim.instruments().roll_deg,enemyShots,humanHits,
        (unsigned long long)world.missiles().stats().launches,p.sim.instruments().pitch_deg,
        world.players().at(human).sim.instruments().alt_msl);
  }
  check(enemyShots>0 && humanHits>0, "autonomous opponents pursue, fire and damage the player");
  check(world.missiles().stats().launches>0, "bots launch through real radar support and inventory checks");
  check(minAgl>50, "bot avoids terrain throughout the engagement");
  auto &opponent = fixture(world,bot);
  // A real lethal projectile verifies that AI lives use ordinary damage,
  // death, cooldown and respawn instead of a separate health implementation.
  auto shooter = opponent.sim.state();
  shooter.pos_ned -= shooter.att.rotate({10,0,0});
  GunConfig gun; gun.dispersion=0; gun.damage=10; gun.muzzle={};
  Life firing;
  const double health = opponent.life.health;
  check(world.combat().fire(world.tick()+1,human,shooter,firing,gun), "player damages a bot");
  for(unsigned i=0;i<5 && opponent.life.health==health;++i) world.step();
  world.step();
  check(opponent.life.health<health && opponent.life.alive() &&
        opponent.evadeUntil>world.tick() && !opponent.firing,
        "wounded bot breaks away and prepares to re-engage");
  // The first heavy round wrecks whatever it strikes; the next one, into the
  // wreckage, is lethal.
  for(unsigned shot=0;shot<3 && opponent.life.alive();++shot) {
    shooter = opponent.sim.state();
    shooter.pos_ned -= shooter.att.rotate({10,0,0});
    gun.damage=100;firing={};
    check(world.combat().fire(world.tick()+1,human,shooter,firing,gun), "lethal player shot");
    const double before=opponent.life.health;
    for(unsigned i=0;i<5 && opponent.life.health==before;++i) world.step();
  }
  check(!opponent.life.alive(), "player can destroy a bot");
  const auto generation = opponent.life.generation;
  const auto deadline = opponent.life.respawnTick;
  while(world.tick()<deadline) world.step();
  check(opponent.life.alive() && opponent.life.generation==generation+1 &&
        opponent.life.ammo==aircraftDefinition(opponent.type).gun->ammo,
        "bot respawns with a fresh life and ammunition");
  world.leave(human);
  for(unsigned i=0;i<240;++i) world.step();
  check(opponent.botTarget==0 && !opponent.firing, "bots patrol without humans");
  world.leave(bot);
  check(world.botCount()==0 && world.players().empty(), "bot removal cleans up roster");
  for(unsigned i=0;i<8;++i) check(world.joinBot()!=0,"fill bot roster");
  check(world.botCount()==8 && !world.joinBot(),"AI roster is capped at eight");
  for(unsigned i=0;i<120;++i) world.step();
  for(const auto &[id,p]:world.players()) {
    (void)id;
    check(p.botTarget==0 && !p.firing,"bots do not attack other bots");
  }
}
void connection() {
  ServerConfig config; config.bind="127.0.0.1"; config.port=0; config.bots=2;
  Server server(config);
  Client client("dogfight-test",dogfightAircraftType(AircraftType::A320));
  client.connect("127.0.0.1",server.port());
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);
  while(std::chrono::steady_clock::now()<deadline && client.remotes().size()<2) {
    server.poll(); server.step(); client.poll(tickSeconds);
    if(client.ready()) client.predict(client.prediction().simulator().controls());
    std::this_thread::sleep_for(std::chrono::milliseconds(8));
  }
  check(client.ready() && client.remotes().size()==2 && server.world().botCount()==2,
        "local host replicates both opponents to the ordinary client");
  client.disconnect(); server.shutdown();
  config.bots=9;
  bool rejected=false;
  try { Server invalid(config); } catch(const std::invalid_argument &) { rejected=true; }
  check(rejected,"server rejects excessive bot counts");
}
}
int main(int argc,char **argv) {
  try {
    const std::string mode=argc>1?argv[1]:"guidance";
    if(mode=="guidance") guidance();
    else if(mode=="dogfight") dogfight();
    else if(mode=="connection") connection();
    else throw std::runtime_error("unknown bot test");
    std::printf("PASS bots.%s\n",mode.c_str());
    return 0;
  } catch(const std::exception &e) {
    std::fprintf(stderr,"FAIL bots: %s\n",e.what()); return 1;
  }
}
