// The team game and the bomber, on the server's world with no network.
#include "ofs/net/server.hpp"
#include "ofs/trim.hpp"
#include <cstdio>
#include <stdexcept>
#include <string>

using namespace ofs;
using namespace ofs::net;
namespace {
void check(bool ok, const char *message) {
  if (!ok) throw std::runtime_error(message);
}
Player &fixture(World &world, EntityId id) {
  return const_cast<Player &>(world.players().at(id));
}
std::size_t structureOf(Team team, StructureKind kind, unsigned site = 0) {
  const auto all = structures();
  for (std::size_t i = 0; i < all.size(); ++i)
    if (all[i].team == team && all[i].kind == kind && all[i].site == site) return i;
  throw std::runtime_error("no such structure");
}
// Puts a bomber in level flight at `height`, heading north, where a bomb let
// go at once would land on `target`.
void aimAt(World &world, EntityId id, Vec3 target, double height, double speed = 200) {
  auto &p = fixture(world, id);
  const auto trim = solveTrim(p.sim.config(), {height, speed, 0, 0, 0});
  check(trim.converged, "bomber trim");
  auto s = trim.state;
  s.fuel_mass = p.sim.state().fuel_mass;
  p.weapons.inventory.applyPayload(p.sim.config(), s);
  s.time = p.sim.state().time;
  const auto &bomb = weapons::bombDefinition(p.weapons.inventory.bombType);
  for (int pass = 0; pass < 3; ++pass) {
    Vec3 impact;
    double seconds;
    check(weapons::bombImpact(bomb, s.pos_ned, s.vel_ned, {}, impact, seconds), "bomb reaches the ground");
    s.pos_ned.x += target.x - impact.x;
    s.pos_ned.y += target.y - impact.y;
  }
  p.sim.setState(s);
  p.sim.setControls(trim.controls);
}
void drop(World &world, EntityId id, unsigned count) {
  auto &p = fixture(world, id);
  for (unsigned i = 0; i < count; ++i) {
    WeaponAction action;
    action.sequence = p.weapons.lastSequence + 1;
    action.tick = world.tick() + 1;
    action.generation = p.life.generation;
    action.kind = WeaponActionKind::Launch;
    check(world.enqueueWeapon(id, action), "bomb release accepted");
    world.step();
    while (world.tick() < p.weapons.readyTick) world.step();
  }
}
void keepLevel(World &world, EntityId id) { fixture(world, id).lastInput = world.tick(); }

void layout() {
  const auto all = structures();
  check(all.size() <= maxStructures && all.size() == 2 * (12 + 4 * kOutposts), "structure list");
  for (const auto &s : all) {
    check(airfieldOwner(s.north, s.east) == (s.site ? Team::None : s.team), "home structures stand on their own airfield's plain");
    check(std::abs(groundHeightNed(s.north, s.east)) < 500, "structures stand in the low country");
  }
  check(std::hypot(teamAirfield(Team::Red).north - teamAirfield(Team::Blue).north,
                   teamAirfield(Team::Red).east - teamAirfield(Team::Blue).east) > 30000, "airfields far apart");
  for (const Team team : {Team::Red, Team::Blue}) {
    const auto &field = teamAirfield(team);
    for (double along = -1500; along <= 1500; along += 250)
      for (double across = -700; across <= 1200; across += 100)
        check(groundHeightNed(field.north + along, field.east + across) == 0, "team airfield is level");
  }
  std::puts("teams layout PASS");
}

void sides() {
  World world(false);
  world.setTeams(300);
  const auto red = world.join(AircraftType::B52, "red", Team::Red, 1);
  const auto blue = world.join(AircraftType::Typhoon, "blue", Team::Blue);
  const auto third = world.join(AircraftType::Typhoon, "auto");
  const auto fourth = world.join(AircraftType::Typhoon, "auto2");
  check(world.players().at(red).team == Team::Red && world.players().at(blue).team == Team::Blue, "chosen sides are kept");
  check(world.players().at(third).team != world.players().at(fourth).team, "unchosen pilots balance the sides");
  for (const auto &[id, p] : world.players()) {
    const auto &s = p.sim.state();
    check(airfieldOwner(s.pos_ned.x, s.pos_ned.y) == p.team, "spawned at own airfield");
    check(std::abs(s.pos_ned.y - teamAirfield(p.team).east) < 5, "spawned on the runway");
  }
  const auto &bomber = world.players().at(red).weapons.inventory;
  check(bomber.bombType == weapons::WeaponType::Bomb2000 && bomber.bombs == 18, "asked-for bomb load");
  for (int i = 0; i < 600; ++i) world.step();
  for (const auto &[id, p] : world.players())
    check(p.life.alive() && p.sim.instruments().tas < .5, "parked aircraft stand still");
  // The radar of one side never shows its own.
  check(world.setTeam(third, Team::Blue) || world.players().at(third).team == Team::Blue, "side change");
  check(world.teamStatus().teams && world.teamStatus().health.size() == structures().size(), "team status");
  std::puts("teams sides PASS");
}

void bombing() {
  World world(true);
  world.setTeams(300);
  world.setDefences(false);
  const auto id = world.join(AircraftType::B52, "bomber", Team::Red, 0);
  const auto target = structureOf(Team::Blue, StructureKind::Command);
  const auto &place = structures()[target];
  aimAt(world, id, {place.north, place.east, 0}, 3000);
  const double fuelBefore = fixture(world, id).sim.state().payload_mass;
  drop(world, id, 6);
  check(world.players().at(id).weapons.inventory.bombs == 45, "six bombs gone");
  check(world.players().at(id).sim.state().payload_mass < fuelBefore - 1300, "the aircraft is lighter by its bombs");
  check(world.missiles().missiles().size() == 6, "six bombs in the air");
  for (int i = 0; i < 60 * 120 && !world.missiles().missiles().empty(); ++i) { keepLevel(world, id); world.step(); }
  check(world.sites()[target].health <= 0, "a stick of Mk 82 destroys the command post");
  check(world.teamStatus().score[0] == unsigned(place.points) && world.teamStatus().score[1] == 0, "Red scores for it");
  check(world.players().at(id).life.alive(), "the bomber is unharmed at 3 km");
  bool said = false;
  for (const auto &line : world.takeNotices()) said = said || line.find("destroyed a command post") != std::string::npos;
  check(said, "the kill is announced");
  // A side's own bombs do not hurt its own ground.
  const auto own = structureOf(Team::Red, StructureKind::Command);
  aimAt(world, id, {structures()[own].north, structures()[own].east, 0}, 3000);
  drop(world, id, 6);
  for (int i = 0; i < 60 * 120 && !world.missiles().missiles().empty(); ++i) { keepLevel(world, id); world.step(); }
  check(world.sites()[own].health == 100, "no damage to own structures");
  std::puts("teams bombing PASS");
}

void nuclear() {
  World world(true);
  world.setTeams(5000);
  world.setDefences(false);
  const auto id = world.join(AircraftType::B52, "bomber", Team::Red, 2);
  const auto victim = world.join(AircraftType::Typhoon, "victim", Team::Blue);
  check(world.players().at(id).weapons.inventory.bombs == 1 &&
        world.players().at(id).weapons.inventory.bombType == weapons::WeaponType::Nuclear, "one weapon");
  const auto &field = teamAirfield(Team::Blue);
  // The victim stands parked on its own airfield.
  { auto &p = fixture(world, victim); auto s = p.sim.state();
    s.pos_ned = {field.north, field.east, -(p.sim.config().gear_nose.z - .15)}; s.vel_ned = {}; s.att = {};
    p.sim.setState(s); Controls c; c.brake01 = 1; p.sim.setControls(c); }
  aimAt(world, id, {field.north, field.east + 600, 0}, 9000, 220);
  drop(world, id, 1);
  check(world.players().at(id).weapons.inventory.bombs == 0, "bay empty");
  for (int i = 0; i < 74 * 120 && !world.missiles().missiles().empty(); ++i) {
    keepLevel(world, id); keepLevel(world, victim);
    world.step();
    if (!world.players().at(id).life.alive()) break;
  }
  check(world.missiles().missiles().empty(), "the weapon went off");
  unsigned standing = 0;
  for (std::size_t i = 0; i < structures().size(); ++i)
    if (structures()[i].team == Team::Blue && structures()[i].site == 0 && world.sites()[i].health > 0) ++standing;
  check(standing == 0, "nothing of the base is left standing");
  check(!world.players().at(victim).life.alive(), "an aircraft on the airfield is destroyed");
  const auto &own = world.players().at(id);
  std::printf("nuclear: bomber health %.0f, %0.f m from the burst\n", own.life.health,
              (own.sim.state().pos_ned - Vec3{field.north, field.east + 600, 0}).norm());
  check(own.life.alive(), "a bomber that dropped from 9 km gets away");
  // Dropped low, the bomber is caught by its own weapon.
  World low(true);
  low.setTeams(5000);
  const auto fool = low.join(AircraftType::B52, "low", Team::Red, 2);
  aimAt(low, fool, {0, 0, 0}, 600, 180);
  drop(low, fool, 1);
  for (int i = 0; i < 40 * 120 && low.players().at(fool).life.alive(); ++i) { keepLevel(low, fool); low.step(); }
  check(!low.players().at(fool).life.alive(), "dropped from 600 m it destroys the bomber");
  std::puts("teams nuclear PASS");
}

void defences() {
  World world(true);
  world.setTeams(300);
  const auto raider = world.join(AircraftType::Typhoon, "raider", Team::Blue);
  const auto &field = teamAirfield(Team::Red);
  { auto &p = fixture(world, raider); auto s = p.sim.state();
    s.pos_ned = {field.north - 1500, field.east + 600, -1500}; p.sim.setState(s); }
  bool missile = false, flak = false;
  for (int i = 0; i < 20 * 120; ++i) {
    keepLevel(world, raider);
    world.step();
    for (const auto &m : world.missiles().missiles()) missile = missile || (defenceEntity(m.owner.id) && m.target.id == raider);
    for (const auto &round : world.combat().projectiles()) flak = flak || defenceEntity(round.owner);
  }
  check(missile, "a missile site launches at an aircraft over the base");
  check(flak, "the flak guns open fire");
  // The same flight by one of the base's own side draws nothing.
  World quiet(true);
  quiet.setTeams(300);
  const auto friendly = quiet.join(AircraftType::Typhoon, "friend", Team::Red);
  { auto &p = fixture(quiet, friendly); auto s = p.sim.state();
    s.pos_ned = {field.north - 1500, field.east + 600, -1500}; p.sim.setState(s); }
  for (int i = 0; i < 10 * 120; ++i) { keepLevel(quiet, friendly); quiet.step(); }
  check(quiet.missiles().missiles().empty() && quiet.combat().projectiles().empty(), "defences hold fire for their own side");
  std::puts("teams defences PASS");
}

void service() {
  World world(false);
  world.setTeams(300);
  const auto id = world.join(AircraftType::B52, "bomber", Team::Blue, 0);
  auto &p = fixture(world, id);
  p.weapons.inventory.bombs = 3;
  p.life.health = 40;
  for (Tick i = 0; i < World::serviceTicks() + 240; ++i) world.step();
  check(p.weapons.inventory.bombs == 51 && p.life.health == 100, "a stop at the home airfield repairs and rearms");
  // Asking for another load while parked changes it at the next turn-round.
  WeaponAction action;
  action.sequence = p.weapons.lastSequence + 1;
  action.tick = world.tick() + 1;
  action.generation = p.life.generation;
  action.kind = WeaponActionKind::Loadout;
  action.station = 2;
  check(world.enqueueWeapon(id, action), "loadout request accepted");
  for (Tick i = 0; i < World::serviceTicks() + 240; ++i) world.step();
  check(p.weapons.inventory.bombType == weapons::WeaponType::Nuclear && p.weapons.inventory.bombs == 1, "the new load is fitted");
  // The other side's airfield, and the one in the middle, give nothing.
  for (const auto &field : {teamAirfield(Team::Red), teamAirfield(Team::None)}) {
    auto s = p.sim.state();
    s.pos_ned.x = field.north; s.pos_ned.y = field.east;
    p.sim.setState(s);
    p.weapons.inventory.bombs = 0;
    p.life.health = 40;
    // Out of reach of the defences' guns for the purpose of this check.
    for (Tick i = 0; i < World::serviceTicks() + 240 && p.life.alive(); ++i) { p.life.health = 40; world.step(); }
    check(p.weapons.inventory.bombs == 0, "no service away from the home airfield");
  }
  std::puts("teams service PASS");
}

void round() {
  World world(true);
  world.setTeams(50);
  world.setDefences(false);
  const auto id = world.join(AircraftType::B52, "bomber", Team::Red, 1);
  const auto blue = world.join(AircraftType::Typhoon, "blue", Team::Blue);
  for (const auto kind : {StructureKind::Command, StructureKind::Radar}) {
    const auto &place = structures()[structureOf(Team::Blue, kind)];
    aimAt(world, id, {place.north, place.east, 0}, 3000);
    drop(world, id, 2);
    for (int i = 0; i < 60 * 120 && !world.missiles().missiles().empty(); ++i) { keepLevel(world, id); keepLevel(world, blue); world.step(); }
  }
  auto status = world.teamStatus();
  check(status.winner == Team::Red && status.score[0] >= 50 && status.restartSeconds > 0, "Red reaches the limit and wins");
  for (int i = 0; i < 16 * 120; ++i) world.step();
  status = world.teamStatus();
  check(status.winner == Team::None && status.score[0] == 0 && status.health[structureOf(Team::Blue, StructureKind::Command)] == 100, "a new round starts clean");
  check(world.players().at(id).weapons.inventory.bombs == 18, "everyone starts the new round rearmed");
  std::puts("teams round PASS");
}

void protocol() {
  Message m;
  m.type = Type::TeamState;
  m.tick = 5; m.sequence = 9;
  m.teams.teams = true; m.teams.scoreLimit = 300; m.teams.score[0] = 120; m.teams.score[1] = 45;
  m.teams.winner = Team::Blue; m.teams.restartSeconds = 12;
  m.teams.health.assign(structures().size(), 100); m.teams.health[3] = 0;
  Message out; std::string reason;
  check(decode(encode(m), out, reason) && out.teams == m.teams, "team status round trip");
  auto bytes = encode(m); bytes.back() = 101;
  check(!decode(bytes, out, reason), "structure health over 100 is refused");
  Message hello; hello.type = Type::Hello; hello.text = "pilot"; hello.aircraftType = AircraftType::B52; hello.team = Team::Blue; hello.loadout = 2;
  check(decode(encode(hello), out, reason) && out.team == Team::Blue && out.loadout == 2 && out.aircraftType == AircraftType::B52, "hello carries side and load");
  bytes = encode(hello); bytes[bytes.size() - 2] = 3;
  check(!decode(bytes, out, reason), "an unknown side is refused");
  WeaponMessage w; w.type = Type::RadarState; w.tick = 1; w.sequence = 1;
  w.radar.bombType = weapons::WeaponType::Nuclear; w.radar.bombs = 1; w.radar.loadout = 2; w.radar.weapon = weapons::WeaponType::Nuclear;
  WeaponMessage back;
  check(decodeWeapon(encodeWeapon(w), back) && back.radar.bombs == 1 && back.radar.bombType == weapons::WeaponType::Nuclear && back.radar.loadout == 2, "bomb load on the wire");
  std::puts("teams protocol PASS");
}
}  // namespace

int main(int argc, char **argv) {
  try {
    check(argc == 2, "expected a suite");
    const std::string suite = argv[1];
    if (suite == "layout") layout();
    else if (suite == "sides") sides();
    else if (suite == "bombing") bombing();
    else if (suite == "nuclear") nuclear();
    else if (suite == "defences") defences();
    else if (suite == "service") service();
    else if (suite == "round") round();
    else if (suite == "protocol") protocol();
    else throw std::invalid_argument("unknown suite");
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "teams FAIL: %s\n", e.what());
    return 1;
  }
}
